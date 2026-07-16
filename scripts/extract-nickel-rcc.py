#!/usr/bin/env python3
"""List or extract files from RCC bundles embedded in Kobo's nickel binary.

The default addresses are for Kobo firmware 4.38.23697. Find the corresponding
qRegisterResourceData arguments in qInitResources_resources when using another
firmware build, then override the VMA arguments below.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


COMPRESSED = 0x01
DIRECTORY = 0x02


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


@dataclass(frozen=True)
class Resource:
    path: str
    flags: int
    locale: int
    data_offset: int


class ResourceTable:
    def __init__(self, blob: bytes, tree: int, names: int, data: int):
        self.blob = blob
        self.tree = tree
        self.names = names
        self.data = data

    def _name(self, offset: int) -> str:
        start = self.names + offset
        length = u16(self.blob, start)
        raw = self.blob[start + 6 : start + 6 + length * 2]
        return raw.decode("utf-16-be")

    def _entry(self, index: int) -> tuple[int, int, int, int]:
        start = self.tree + index * 14
        return (
            u32(self.blob, start),
            u16(self.blob, start + 4),
            u32(self.blob, start + 6),
            u32(self.blob, start + 10),
        )

    def resources(self) -> list[Resource]:
        result: list[Resource] = []
        visiting: set[int] = set()

        def walk(index: int, parents: tuple[str, ...]) -> None:
            if index in visiting:
                raise ValueError(f"resource-tree cycle at entry {index}")
            visiting.add(index)
            name_offset, flags, first, second = self._entry(index)
            parts = parents + ((self._name(name_offset),) if index else ())
            if flags & DIRECTORY:
                child_count, child_index = first, second
                for child in range(child_index, child_index + child_count):
                    walk(child, parts)
            else:
                result.append(Resource("/".join(parts), flags, first, second))
            visiting.remove(index)

        walk(0, ())
        return result

    def contents(self, resource: Resource) -> bytes:
        start = self.data + resource.data_offset
        stored_size = self.stored_size(resource)
        payload = self.blob[start + 4 : start + 4 + stored_size]
        if len(payload) != stored_size:
            raise ValueError(f"truncated data for {resource.path}")
        if not resource.flags & COMPRESSED:
            return payload

        if len(payload) < 4:
            raise ValueError(f"truncated compressed header for {resource.path}")
        expected_size = u32(payload, 0)
        result = zlib.decompress(payload[4:])
        if len(result) != expected_size:
            raise ValueError(
                f"size mismatch for {resource.path}: "
                f"expected {expected_size}, got {len(result)}"
            )
        return result

    def stored_size(self, resource: Resource) -> int:
        return u32(self.blob, self.data + resource.data_offset)


def embedded_table(args: argparse.Namespace, nickel: bytes) -> ResourceTable:
    return ResourceTable(
        nickel,
        args.tree_vma - args.vma_bias,
        args.names_vma - args.vma_bias,
        args.data_vma - args.vma_bias,
    )


def rcc_table(rcc: bytes) -> ResourceTable:
    if len(rcc) < 20 or rcc[:4] != b"qres":
        raise ValueError("selected resource is not a binary RCC bundle")
    _, version, tree, data, names = struct.unpack_from(">4sIIII", rcc, 0)
    if version != 1:
        raise ValueError(f"unsupported RCC version {version}; expected version 1")
    return ResourceTable(rcc, tree, names, data)


def safe_destination(root: Path, resource_path: str) -> Path:
    relative = PurePosixPath(resource_path)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"unsafe resource path: {resource_path}")
    return root.joinpath(*relative.parts)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nickel", type=Path, help="path to the extracted nickel ELF")
    parser.add_argument("--bundle", help="embedded RCC bundle to inspect")
    parser.add_argument("--prefix", default="", help="only paths with this prefix")
    parser.add_argument("--contains", default="", help="only paths containing this text")
    parser.add_argument("--output", type=Path, help="extract matches under this directory")
    parser.add_argument("--details", action="store_true", help="show flags and byte sizes")
    parser.add_argument("--replace", help="exact resource path to replace")
    parser.add_argument("--replacement", type=Path, help="replacement file")
    parser.add_argument("--patched-nickel", type=Path, help="write a size-preserving patched ELF")
    parser.add_argument("--list-bundles", action="store_true", help="list embedded RCC names")
    parser.add_argument("--vma-bias", type=lambda x: int(x, 0), default=0x10000)
    parser.add_argument("--data-vma", type=lambda x: int(x, 0), default=0x28B60)
    parser.add_argument("--names-vma", type=lambda x: int(x, 0), default=0x15041A8)
    parser.add_argument("--tree-vma", type=lambda x: int(x, 0), default=0x1504828)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    nickel = args.nickel.read_bytes()
    outer = embedded_table(args, nickel)
    bundles = [item for item in outer.resources() if item.path.endswith(".rcc")]

    if args.list_bundles:
        for item in sorted(bundles, key=lambda item: item.path):
            print(item.path)
        return 0

    if not args.bundle:
        print("error: --bundle is required unless --list-bundles is used", file=sys.stderr)
        return 2

    matches = [item for item in bundles if item.path == args.bundle]
    if len(matches) != 1:
        print(f"error: expected one bundle named {args.bundle!r}, found {len(matches)}", file=sys.stderr)
        return 2

    table = rcc_table(outer.contents(matches[0]))

    replacement_args = (args.replace, args.replacement, args.patched_nickel)
    if any(replacement_args):
        if not all(replacement_args):
            print(
                "error: --replace, --replacement, and --patched-nickel are all required",
                file=sys.stderr,
            )
            return 2
        targets = [item for item in table.resources() if item.path == args.replace]
        if len(targets) != 1:
            print(
                f"error: expected one resource named {args.replace!r}, found {len(targets)}",
                file=sys.stderr,
            )
            return 2
        if matches[0].flags & COMPRESSED:
            print("error: replacing a compressed outer RCC is unsupported", file=sys.stderr)
            return 2

        target = targets[0]
        replacement = args.replacement.read_bytes()
        if target.flags & COMPRESSED:
            encoded = struct.pack(">I", len(replacement)) + zlib.compress(replacement, 9)
        else:
            encoded = replacement
        capacity = table.stored_size(target)
        if len(encoded) > capacity:
            print(
                f"error: encoded replacement is {len(encoded)} bytes; slot holds {capacity}",
                file=sys.stderr,
            )
            return 2

        outer_rcc_start = outer.data + matches[0].data_offset + 4
        target_start = outer_rcc_start + table.data + target.data_offset
        patched = bytearray(nickel)
        struct.pack_into(">I", patched, target_start, len(encoded))
        payload_start = target_start + 4
        patched[payload_start : payload_start + capacity] = encoded.ljust(capacity, b"\0")
        args.patched_nickel.parent.mkdir(parents=True, exist_ok=True)
        args.patched_nickel.write_bytes(patched)
        print(f"resource={target.path}")
        print(f"slot={capacity} encoded={len(encoded)} padding={capacity - len(encoded)}")
        print(f"source_sha256={hashlib.sha256(nickel).hexdigest()}")
        print(f"patched_sha256={hashlib.sha256(patched).hexdigest()}")
        print(f"output={args.patched_nickel}")
        return 0

    selected = [
        item
        for item in table.resources()
        if item.path.startswith(args.prefix) and args.contains in item.path
    ]

    if args.output is None:
        for item in selected:
            if args.details:
                print(
                    f"{item.path}\tflags=0x{item.flags:x}"
                    f"\tstored={table.stored_size(item)}"
                    f"\textracted={len(table.contents(item))}"
                )
            else:
                print(item.path)
        return 0

    for item in selected:
        destination = safe_destination(args.output, item.path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(table.contents(item))
        print(destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
