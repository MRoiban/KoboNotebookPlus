#!/usr/bin/env python3
"""Validate layer sidecars and preview PNGs copied from a Kobo filesystem."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
import sys
import zlib


LAYER_ID = re.compile(r"^cnt\.layer\.[a-z0-9.-]+$")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_STATE_BYTES = 64 * 1024
MAX_PREVIEW_BYTES = 8 * 1024 * 1024
MAX_DIMENSION = 4096
MAX_CUSTOM_LAYERS = 15


def digest(*values: str) -> str:
    return hashlib.sha256("\n".join(values).encode("utf-8")).hexdigest()


def mounted_notebook(root: pathlib.Path, onboard_path: str) -> pathlib.Path | None:
    prefix = "/mnt/onboard/"
    if not onboard_path.startswith(prefix):
        return None
    return root / onboard_path[len(prefix) :]


def png_dimensions(path: pathlib.Path) -> tuple[int, int]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("invalid PNG signature")
    offset = len(PNG_SIGNATURE)
    dimensions: tuple[int, int] | None = None
    saw_idat = False
    saw_iend = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("truncated PNG chunk")
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        end = offset + 12 + length
        if end > len(data):
            raise ValueError("truncated PNG chunk data")
        payload = data[offset + 8 : offset + 8 + length]
        expected_crc = struct.unpack(">I", data[offset + 8 + length : end])[0]
        actual_crc = zlib.crc32(chunk_type + payload) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise ValueError(f"invalid {chunk_type.decode('ascii', 'replace')} CRC")
        if chunk_type == b"IHDR":
            if offset != len(PNG_SIGNATURE) or length != 13:
                raise ValueError("invalid IHDR")
            dimensions = struct.unpack(">II", payload[:8])
        elif chunk_type == b"IDAT":
            saw_idat = True
        elif chunk_type == b"IEND":
            if length != 0:
                raise ValueError("invalid IEND")
            saw_iend = True
            if end != len(data):
                raise ValueError("trailing bytes after IEND")
            break
        offset = end
    if dimensions is None or not saw_idat or not saw_iend:
        raise ValueError("PNG is missing IHDR, IDAT, or IEND")
    return dimensions


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Validate .kobo/custom/layers metadata and preview cache after a "
            "device test. ROOT is the mounted Kobo onboard filesystem."
        )
    )
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument(
        "--allow-empty",
        action="store_true",
        help="succeed when the device has no layer sidecars yet",
    )
    args = parser.parse_args()

    root = args.root.resolve()
    layer_root = root / ".kobo" / "custom" / "layers"
    if not layer_root.is_dir():
        if args.allow_empty:
            print(f"No layer directory at {layer_root}")
            return 0
        parser.error(f"layer directory not found: {layer_root}")

    errors: list[str] = []
    warnings: list[str] = []
    expected_previews: dict[str, tuple[pathlib.Path | None, str]] = {}
    state_count = 0
    custom_count = 0

    for path in sorted(layer_root.glob("*.json")):
        state_count += 1
        if path.stat().st_size > MAX_STATE_BYTES:
            errors.append(f"{path.name}: state exceeds {MAX_STATE_BYTES} bytes")
            continue
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            errors.append(f"{path.name}: unreadable JSON ({exc})")
            continue
        if not isinstance(value, dict) or value.get("version") != 1:
            errors.append(f"{path.name}: unsupported state schema")
            continue

        notebook = value.get("notebook")
        part = value.get("part")
        active = value.get("active")
        layers = value.get("layers")
        if not all(isinstance(item, str) and item for item in (notebook, part, active)):
            errors.append(f"{path.name}: notebook, part, and active must be strings")
            continue
        if not isinstance(layers, list) or len(layers) > MAX_CUSTOM_LAYERS:
            errors.append(f"{path.name}: invalid custom-layer collection")
            continue

        expected_state_name = digest(notebook, part) + ".json"
        if path.name != expected_state_name:
            errors.append(
                f"{path.name}: filename does not match notebook/part identity "
                f"({expected_state_name})"
            )

        notebook_path = mounted_notebook(root, notebook)
        if notebook_path is None:
            errors.append(f"{path.name}: notebook is outside /mnt/onboard")
        elif not notebook_path.is_file():
            warnings.append(f"{path.name}: notebook is absent from this mount")

        ids: list[str] = []
        for index, item in enumerate(layers):
            if not isinstance(item, dict):
                errors.append(f"{path.name}: layer {index} is not an object")
                continue
            layer_id = item.get("id")
            name = item.get("name")
            if (
                not isinstance(layer_id, str)
                or len(layer_id) > 96
                or not LAYER_ID.fullmatch(layer_id)
            ):
                errors.append(f"{path.name}: layer {index} has an invalid ID")
                continue
            if layer_id in ids:
                errors.append(f"{path.name}: duplicate layer ID {layer_id}")
                continue
            if not isinstance(name, str) or not 1 <= len(name.strip()) <= 64:
                errors.append(f"{path.name}: layer {index} has an invalid name")
            ids.append(layer_id)
            custom_count += 1

        if active.startswith("cnt.layer.") and active not in ids:
            errors.append(f"{path.name}: active custom layer is absent from rows")

        for layer_id in [active if active not in ids else ""] + ids:
            if not layer_id:
                continue
            preview_name = digest(notebook, part, layer_id) + ".png"
            expected_previews[preview_name] = (notebook_path, layer_id)
        # The base ID may differ from the active ID, so infer it from any
        # preview that matches this state below instead of guessing its SDK name.

    preview_root = layer_root / "previews"
    preview_count = 0
    fresh_count = 0
    if preview_root.is_dir():
        for path in sorted(preview_root.glob("*.png")):
            preview_count += 1
            size = path.stat().st_size
            if size <= 0 or size > MAX_PREVIEW_BYTES:
                errors.append(f"{path.name}: preview size is invalid ({size} bytes)")
                continue
            try:
                width, height = png_dimensions(path)
            except (OSError, ValueError, struct.error) as exc:
                errors.append(f"{path.name}: {exc}")
                continue
            if not (0 < width <= MAX_DIMENSION and 0 < height <= MAX_DIMENSION):
                errors.append(f"{path.name}: invalid dimensions {width}x{height}")
                continue
            source = expected_previews.get(path.name)
            if source is None:
                warnings.append(f"{path.name}: orphaned or base-layer preview")
                continue
            notebook_path, _ = source
            if notebook_path and notebook_path.is_file():
                if path.stat().st_mtime < notebook_path.stat().st_mtime:
                    warnings.append(f"{path.name}: preview is stale")
                else:
                    fresh_count += 1

    if state_count == 0 and not args.allow_empty:
        errors.append("no layer sidecars found")

    print(
        f"Layer artifacts: {state_count} state file(s), {custom_count} custom "
        f"layer(s), {preview_count} preview(s), {fresh_count} fresh matched preview(s)"
    )
    for warning in warnings:
        print(f"WARNING: {warning}")
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
