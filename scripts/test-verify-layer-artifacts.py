#!/usr/bin/env python3
"""Regression tests for verify-layer-artifacts.py."""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


SCRIPT = pathlib.Path(__file__).with_name("verify-layer-artifacts.py")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def digest(*values: str) -> str:
    return hashlib.sha256("\n".join(values).encode("utf-8")).hexdigest()


def chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = zlib.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


def one_pixel_png() -> bytes:
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)
    rgba_scanline = b"\x00\x00\x00\x00\xff"
    return (
        PNG_SIGNATURE
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(rgba_scanline))
        + chunk(b"IEND", b"")
    )


class ArtifactVerifierTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.layers = self.root / ".kobo" / "custom" / "layers"
        (self.layers / "previews").mkdir(parents=True)
        (self.root / "sample.nebo").write_bytes(b"notebook fixture")
        self.notebook = "/mnt/onboard/sample.nebo"
        self.part = "fixture-part"
        self.layer_id = "cnt.layer.01234567-89ab-cdef"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_state(self, active: str | None = None) -> pathlib.Path:
        state = {
            "version": 1,
            "notebook": self.notebook,
            "part": self.part,
            "active": active or self.layer_id,
            "layers": [{"id": self.layer_id, "name": "Layer 2"}],
        }
        path = self.layers / f"{digest(self.notebook, self.part)}.json"
        path.write_text(json.dumps(state), encoding="utf-8")
        return path

    def write_preview(self, content: bytes | None = None) -> pathlib.Path:
        path = self.layers / "previews" / (
            digest(self.notebook, self.part, self.layer_id) + ".png"
        )
        path.write_bytes(content if content is not None else one_pixel_png())
        return path

    def run_verifier(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT), str(self.root)],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def test_valid_state_and_preview(self) -> None:
        self.write_state()
        self.write_preview()
        result = self.run_verifier()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("1 fresh matched preview(s)", result.stdout)

    def test_missing_active_custom_row_is_rejected(self) -> None:
        self.write_state(active="cnt.layer.missing")
        result = self.run_verifier()
        self.assertEqual(result.returncode, 1)
        self.assertIn("active custom layer is absent", result.stderr)

    def test_corrupt_preview_crc_is_rejected(self) -> None:
        self.write_state()
        content = bytearray(one_pixel_png())
        content[-5] ^= 0x01
        self.write_preview(bytes(content))
        result = self.run_verifier()
        self.assertEqual(result.returncode, 1)
        self.assertIn("CRC", result.stderr)


if __name__ == "__main__":
    unittest.main()
