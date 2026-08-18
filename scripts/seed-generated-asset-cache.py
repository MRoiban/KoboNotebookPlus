#!/usr/bin/env python3

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import struct
import tempfile
from typing import Iterable, Optional, Tuple


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_IEND = b"\x00\x00\x00\x00IEND\xaeB`\x82"
MAXIMUM_PNG_SIZE = 32 * 1024 * 1024
SUPPORT_RELATIVE = Path(".kobo/iink/custom-notebook-templates")


def remove_appledouble_sidecar(path: Path) -> None:
    sidecar = path.with_name("._" + path.name)
    try:
        sidecar.unlink()
    except FileNotFoundError:
        pass


def png_dimensions(path: Path) -> Optional[Tuple[int, int]]:
    try:
        size = path.stat().st_size
        if size < 33 or size > MAXIMUM_PNG_SIZE:
            return None
        with path.open("rb") as handle:
            header = handle.read(24)
            handle.seek(-len(PNG_IEND), os.SEEK_END)
            footer = handle.read(len(PNG_IEND))
    except OSError:
        return None
    if (
        not header.startswith(PNG_SIGNATURE)
        or header[12:16] != b"IHDR"
        or footer != PNG_IEND
    ):
        return None
    return struct.unpack(">II", header[16:24])


def sampled_fingerprint(path: Path) -> str:
    size = path.stat().st_size
    if size < 1:
        raise ValueError(f"empty asset: {path}")
    chunk_size = 4096
    offsets = (
        0,
        max(0, size // 2 - chunk_size // 2),
        max(0, size - chunk_size),
    )
    digest = hashlib.sha256()
    previous = None
    with path.open("rb") as handle:
        for offset in offsets:
            if offset == previous:
                continue
            previous = offset
            expected = min(chunk_size, size - offset)
            handle.seek(offset)
            block = handle.read(chunk_size)
            if len(block) != expected:
                raise OSError(f"short sampled read: {path}")
            digest.update(str(offset).encode("ascii"))
            digest.update(b":")
            digest.update(block)
    return digest.hexdigest()


def full_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            block = handle.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def virtual_source_path(volume: Path, source: Path) -> str:
    relative = source.relative_to(volume)
    return str(PurePosixPath("/mnt/onboard") / PurePosixPath(relative.as_posix()))


def stamp_contents(
    volume: Path,
    source: Path,
    destination: Path,
    conversion: str,
) -> bytes:
    source_stat = source.stat()
    destination_stat = destination.stat()
    identity = hashlib.sha256(
        virtual_source_path(volume, source).encode("utf-8")
    ).hexdigest()
    return (
        "cnt-generated-png-cache-v1\n"
        f"conversion={conversion}\n"
        f"source-identity={identity}\n"
        f"source-size={source_stat.st_size}\n"
        f"source-mtime-ms={source_stat.st_mtime_ns // 1_000_000}\n"
        f"source-sample={sampled_fingerprint(source)}\n"
        f"destination-size={destination_stat.st_size}\n"
        f"destination-sample={sampled_fingerprint(destination)}\n"
    ).encode("ascii")


def automatic_sources(directory: Path) -> Iterable[Path]:
    if not directory.is_dir():
        return ()
    result = []
    for path in sorted(directory.iterdir(), key=lambda item: item.name.lower()):
        lower = path.name.lower()
        if (
            path.is_file()
            and not path.name.startswith("._")
            and lower.endswith(".png")
            and not lower.endswith("_condor.png")
            and not lower.endswith("-icon.png")
            and not lower.endswith("_icon.png")
            and 8 <= path.stat().st_size <= MAXIMUM_PNG_SIZE
        ):
            result.append(path)
    return result


def candidate_outputs(volume: Path):
    support = volume / SUPPORT_RELATIVE
    for category in ("templates", "covers"):
        directory = support / category
        for source in automatic_sources(directory):
            icon = source.with_name(source.stem + "-icon.png")
            if png_dimensions(source) != (1404, 1872):
                continue
            if png_dimensions(icon) == (280, 280):
                yield source, icon, "picker-280-v1"

            condor = source.with_name(source.stem + "_condor.png")
            if png_dimensions(condor) != (1404, 1872):
                continue
            if category == "covers":
                # Cover renderer copies must be exact. Do the full comparison
                # once on the host before bypassing the device's equivalent
                # validation pass.
                if (
                    source.stat().st_size != condor.stat().st_size
                    or full_sha256(source) != full_sha256(condor)
                ):
                    continue
                yield source, condor, "condor-copy-v1"
            else:
                # Template overlays may deliberately differ from opaque paper.
                # Valid dimensions/envelope plus the existing generated output
                # are sufficient; the version bump still forces regeneration
                # if the conversion algorithm changes.
                yield source, condor, "template-overlay-v2"


def write_atomic(path: Path, contents: bytes) -> None:
    temporary_name = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=path.parent,
            prefix=path.name + ".",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary_name = temporary.name
            temporary.write(contents)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_name, path)
    finally:
        if temporary_name:
            temporary_path = Path(temporary_name)
            if temporary_path.exists():
                temporary_path.unlink()
            remove_appledouble_sidecar(temporary_path)
    remove_appledouble_sidecar(path)


def seed_volume(volume: Path, write: bool) -> Tuple[int, int, int]:
    volume = volume.absolute()
    support = volume / SUPPORT_RELATIVE
    if not support.is_dir():
        raise ValueError(f"support directory not found: {support}")

    fresh = 0
    changed = 0
    total_bytes = 0
    for source, destination, conversion in candidate_outputs(volume):
        contents = stamp_contents(
            volume, source, destination, conversion
        )
        stamp = Path(str(destination) + ".cnt-cache")
        try:
            existing = stamp.read_bytes()
        except FileNotFoundError:
            existing = b""
        if existing == contents:
            fresh += 1
            if write:
                remove_appledouble_sidecar(stamp)
            continue
        changed += 1
        total_bytes += len(contents)
        if write:
            write_atomic(stamp, contents)
    return fresh, changed, total_bytes


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Seed validated cover/template cache stamps on a mounted Kobo. "
            "Dry-run is the default."
        )
    )
    parser.add_argument("volume", type=Path)
    parser.add_argument(
        "--write",
        action="store_true",
        help="atomically write the validated .cnt-cache stamps",
    )
    arguments = parser.parse_args()
    fresh, changed, total_bytes = seed_volume(
        arguments.volume, arguments.write
    )
    action = "written" if arguments.write else "would-write"
    print(
        f"asset-cache fresh={fresh} {action}={changed} "
        f"bytes={total_bytes}"
    )


if __name__ == "__main__":
    main()
