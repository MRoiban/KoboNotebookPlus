#!/usr/bin/env python3
"""Build pinned Best/Fast MyScript exporter executables for Kobo 4.38.23697."""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import struct
import tempfile


EXPECTED_SHA256 = "98bb2032f47f75bd1c3796f3cda2f016edc43a57435007035d6b5b8657dd7bb1"
EXPECTED_SIZE = 140_148
VIEW_TO_FILE_BIAS = 0x10000

# The exporter is invoked synchronously on its Qt event loop. Shorten only the
# successful-completion idle timer so the next request can select a new quality
# variant promptly. The request-entry timer remains 120 seconds: its timeout
# calls exit(0), so shortening it could terminate a long or nested-loop render
# before QPdfWriter finalizes the destination.
COMMON_PATCHES = {
    0x2061A: ("4df2c041", "40f2e831"),
    0x20620: ("c0f20101", "c0f20001"),
}

# Half-resolution rendering: 2480x3508 -> 1240x1754 for PDF, and
# 2480x3307 -> 1240x1654 for ZIP/PNG/JPEG. The PDF destination QSize at
# 0x1fe00/0x1fe04 deliberately remains 2480x3508: QPainter uses it to scale
# the smaller raster back over the normal A4 page. The three 2480.0f scale
# literals must change with the raster width or MyScript would render at the
# wrong zoom.
FAST_PATCHES = {
    0x1FCD0: ("40f6b452", "40f2da62"),  # movw r2, #1754
    0x1FCD4: ("4ff41b61", "40f2d841"),  # movw r1, #1240
    0x1FFC8: ("40f6b452", "40f2da62"),
    0x1FFD0: ("4ff41b61", "40f2d841"),
    0x20040: ("00001b45", "00009b44"),  # 2480.0f -> 1240.0f
    0x20266: ("40f6eb42", "40f27662"),  # movw r2, #1654
    0x2026A: ("4ff41b61", "40f2d841"),
    0x202CC: ("00001b45", "00009b44"),
    0x204A0: ("40f6eb42", "40f27662"),
    0x204A8: ("4ff41b61", "40f2d841"),
    0x2080E: ("40f6eb42", "40f27662"),
    0x20812: ("4ff41b61", "40f2d841"),
    0x2092A: ("40f6eb42", "40f27662"),
    0x20932: ("4ff41b61", "40f2d841"),
    0x20BB4: ("00001b45", "00009b44"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_elf(data: bytes) -> None:
    if len(data) != EXPECTED_SIZE:
        raise ValueError(f"unexpected iink size {len(data)}, expected {EXPECTED_SIZE}")
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("iink is not a 32-bit little-endian ELF")
    elf_type, machine = struct.unpack_from("<HH", data, 16)
    if elf_type != 2 or machine != 40:
        raise ValueError(
            f"iink is not an ARM executable (type={elf_type}, machine={machine})"
        )


def apply_patches(
    stock: bytes,
    groups: tuple[dict[int, tuple[str, str]], ...],
) -> bytes:
    patched = bytearray(stock)
    seen: set[int] = set()
    for group in groups:
        for address, (expected_hex, replacement_hex) in group.items():
            if address in seen:
                raise ValueError(f"duplicate patch address 0x{address:x}")
            seen.add(address)
            expected = bytes.fromhex(expected_hex)
            replacement = bytes.fromhex(replacement_hex)
            if len(expected) != len(replacement):
                raise ValueError(f"size-changing patch at 0x{address:x}")
            offset = address - VIEW_TO_FILE_BIAS
            actual = bytes(patched[offset : offset + len(expected)])
            if actual != expected:
                raise ValueError(
                    f"patch precondition failed at 0x{address:x}: "
                    f"found {actual.hex()}, expected {expected.hex()}"
                )
            patched[offset : offset + len(expected)] = replacement
    return bytes(patched)


def write_executable(path: pathlib.Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, 0o755)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("best", type=pathlib.Path)
    parser.add_argument("fast", type=pathlib.Path)
    args = parser.parse_args()

    stock = args.source.read_bytes()
    validate_elf(stock)
    digest = sha256(stock)
    if digest != EXPECTED_SHA256:
        raise SystemExit(
            f"refusing unexpected iink SHA-256 {digest}; expected {EXPECTED_SHA256}"
        )

    best = apply_patches(stock, (COMMON_PATCHES,))
    fast = apply_patches(stock, (COMMON_PATCHES, FAST_PATCHES))
    validate_elf(best)
    validate_elf(fast)
    if len(best) != len(stock) or len(fast) != len(stock):
        raise SystemExit("internal error: exporter patch changed ELF size")

    write_executable(args.best, best)
    write_executable(args.fast, fast)
    print(f"stock_sha256={digest}")
    print(f"best_sha256={sha256(best)}")
    print(f"fast_sha256={sha256(fast)}")
    print(f"size={len(stock)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
