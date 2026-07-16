#!/usr/bin/env python3
"""Convert IsometricGrid into an external custom-template slot.

This patch is specific to Kobo firmware 4.38.23697. It changes only three
same-length strings in libiinknote.so, so the ELF size and every section/file
offset remain unchanged.

The serialized background identifier remains ``IsometricGrid``. On the Kobo,
place the full-size paper and menu icon at:

    /mnt/onboard/.kobo/custom/template_custom1_condor.png
    /mnt/onboard/.kobo/custom/template_icon.png

BackgroundWidget inserts ``_condor`` before ``.png`` at runtime.
"""

from __future__ import annotations

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path


EXPECTED_SOURCE_SHA256 = (
    "f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea"
)


@dataclass(frozen=True)
class Replacement:
    purpose: str
    old: bytes
    new: bytes


REPLACEMENTS = (
    Replacement(
        "background base path",
        b":/images/mynotes/isometric_grid_background.png",
        b"/mnt/onboard/.kobo/custom/template_custom1.png",
    ),
    Replacement(
        "menu icon path",
        b":/images/mynotes/isometric_grid_bg_icon.png",
        b"/mnt/onboard/.kobo/custom/template_icon.png",
    ),
    Replacement(
        "menu label",
        b"Isometric Grid",
        b"Custom Paper 1",
    ),
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="matching-firmware libiinknote.so")
    parser.add_argument("output", type=Path, help="patched library to create")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = args.source.read_bytes()
    source_hash = sha256(source)
    if source_hash != EXPECTED_SOURCE_SHA256:
        raise SystemExit(
            "refusing to patch an unexpected libiinknote.so\n"
            f"expected: {EXPECTED_SOURCE_SHA256}\n"
            f"actual:   {source_hash}"
        )

    patched = bytearray(source)
    for replacement in REPLACEMENTS:
        if len(replacement.old) != len(replacement.new):
            raise SystemExit(
                f"{replacement.purpose}: replacement length changed "
                f"({len(replacement.old)} != {len(replacement.new)})"
            )
        count = source.count(replacement.old)
        if count != 1:
            raise SystemExit(
                f"{replacement.purpose}: expected one source occurrence, found {count}"
            )
        offset = source.index(replacement.old)
        patched[offset : offset + len(replacement.old)] = replacement.new
        print(
            f"{replacement.purpose}: offset=0x{offset:x} "
            f"length={len(replacement.old)}"
        )

    if len(patched) != len(source):
        raise SystemExit("internal error: patched ELF size changed")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(patched)
    print(f"source_sha256={source_hash}")
    print(f"patched_sha256={sha256(patched)}")
    print(f"size={len(patched)}")
    print(f"output={args.output}")
    print("device_template=/mnt/onboard/.kobo/custom/template_custom1_condor.png")
    print("device_icon=/mnt/onboard/.kobo/custom/template_icon.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
