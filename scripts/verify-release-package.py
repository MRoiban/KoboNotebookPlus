#!/usr/bin/env python3
"""Validate a KoboRoot installer against its locally built plugin.

The release archive is intentionally tiny: one plugin and the exact stock
libiinknote image for firmware 4.38.23697. Any extra member, link, duplicate,
path traversal, architecture mismatch, or byte drift fails closed.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import tarfile


PLUGIN_MEMBER = "usr/local/Kobo/imageformats/libcustomnotebooktemplates.so"
STOCK_MEMBER = "usr/local/Kobo/libiinknote.so"
EXPECTED_MEMBERS = frozenset((PLUGIN_MEMBER, STOCK_MEMBER))
EXPECTED_STOCK_SHA256 = (
    "f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalized_member(name: str) -> str:
    while name.startswith("./"):
        name = name[2:]
    path = pathlib.PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not name:
        raise ValueError(f"unsafe archive member: {name!r}")
    return str(path)


def validate_arm_shared_object(data: bytes) -> None:
    if len(data) < 20 or data[:4] != b"\x7fELF":
        raise ValueError("packaged plugin is not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise ValueError("packaged plugin is not 32-bit little-endian ELF")
    elf_type, machine = struct.unpack_from("<HH", data, 16)
    if elf_type != 3 or machine != 40:
        raise ValueError(
            f"packaged plugin is not an ARM shared object "
            f"(type={elf_type}, machine={machine})"
        )


def validate_package(archive: pathlib.Path, built_plugin: pathlib.Path) -> dict[str, str]:
    built = built_plugin.read_bytes()
    validate_arm_shared_object(built)

    payloads: dict[str, bytes] = {}
    with tarfile.open(archive, "r:gz") as bundle:
        for member in bundle.getmembers():
            name = normalized_member(member.name)
            if not member.isfile():
                raise ValueError(f"archive member is not a regular file: {name}")
            if name in payloads:
                raise ValueError(f"duplicate archive member: {name}")
            stream = bundle.extractfile(member)
            if stream is None:
                raise ValueError(f"archive member cannot be read: {name}")
            payloads[name] = stream.read()

    names = frozenset(payloads)
    if names != EXPECTED_MEMBERS:
        missing = sorted(EXPECTED_MEMBERS - names)
        extra = sorted(names - EXPECTED_MEMBERS)
        raise ValueError(f"archive contents differ; missing={missing}, extra={extra}")

    packaged_plugin = payloads[PLUGIN_MEMBER]
    if packaged_plugin != built:
        raise ValueError("packaged plugin is not byte-identical to build output")
    validate_arm_shared_object(packaged_plugin)

    stock_digest = sha256(payloads[STOCK_MEMBER])
    if stock_digest != EXPECTED_STOCK_SHA256:
        raise ValueError(
            f"stock libiinknote SHA-256 is {stock_digest}, "
            f"expected {EXPECTED_STOCK_SHA256}"
        )

    return {
        "archive": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "plugin": sha256(packaged_plugin),
        "stock": stock_digest,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=pathlib.Path)
    parser.add_argument("built_plugin", type=pathlib.Path)
    args = parser.parse_args()
    try:
        digests = validate_package(args.archive, args.built_plugin)
    except (OSError, tarfile.TarError, ValueError) as exc:
        print(f"Release package verification FAILED: {exc}")
        return 1

    print("Release package verified: exactly two regular files, ELF32 ARM plugin")
    print(f"KoboRoot.tgz SHA-256: {digests['archive']}")
    print(f"Plugin SHA-256: {digests['plugin']}")
    print(f"Stock libiinknote.so SHA-256: {digests['stock']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
