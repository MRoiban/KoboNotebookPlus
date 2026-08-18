#!/usr/bin/env python3

import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("seed-generated-asset-cache.py")
SPEC = importlib.util.spec_from_file_location("asset_cache_seed", SCRIPT)
assert SPEC and SPEC.loader
SEEDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SEEDER)


def fake_png(width: int, height: int, payload: bytes) -> bytes:
    return (
        SEEDER.PNG_SIGNATURE
        + struct.pack(">I", 13)
        + b"IHDR"
        + struct.pack(">II", width, height)
        + b"\x08\x06\x00\x00\x00"
        + b"\x00\x00\x00\x00"
        + payload
        + SEEDER.PNG_IEND
    )


class AssetCacheSeederTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="kobo-asset-cache-test-"
        )
        self.volume = Path(self.temporary.name)
        self.templates = (
            self.volume / SEEDER.SUPPORT_RELATIVE / "templates"
        )
        self.covers = self.volume / SEEDER.SUPPORT_RELATIVE / "covers"
        self.templates.mkdir(parents=True)
        self.covers.mkdir(parents=True)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_asset(
        self, directory: Path, name: str, contents: bytes
    ) -> Path:
        path = directory / name
        path.write_bytes(contents)
        os.utime(path, (1785000000, 1785000000))
        return path

    def test_dry_run_then_atomic_seed_and_warm_hit(self) -> None:
        cover = fake_png(1404, 1872, b"cover" * 2000)
        icon = fake_png(280, 280, b"icon" * 1000)
        self.write_asset(self.covers, "Cloth.png", cover)
        self.write_asset(self.covers, "Cloth_condor.png", cover)
        self.write_asset(self.covers, "Cloth-icon.png", icon)

        self.assertEqual(
            SEEDER.seed_volume(self.volume, write=False)[:2], (0, 2)
        )
        self.assertFalse(
            (self.covers / "Cloth_condor.png.cnt-cache").exists()
        )
        self.assertEqual(
            SEEDER.seed_volume(self.volume, write=True)[:2], (0, 2)
        )
        self.assertEqual(
            SEEDER.seed_volume(self.volume, write=False)[:2], (2, 0)
        )
        sidecar = self.covers / "._Cloth_condor.png.cnt-cache"
        sidecar.write_bytes(b"host metadata")
        self.assertEqual(
            SEEDER.seed_volume(self.volume, write=True)[:2], (2, 0)
        )
        self.assertFalse(sidecar.exists())

    def test_changed_source_sample_invalidates_both_outputs(self) -> None:
        source = self.write_asset(
            self.covers,
            "Paper.png",
            fake_png(1404, 1872, b"same" * 2000),
        )
        self.write_asset(
            self.covers,
            "Paper_condor.png",
            source.read_bytes(),
        )
        self.write_asset(
            self.covers,
            "Paper-icon.png",
            fake_png(280, 280, b"icon" * 1000),
        )
        SEEDER.seed_volume(self.volume, write=True)
        changed = bytearray(source.read_bytes())
        changed[len(changed) // 2] ^= 1
        source.write_bytes(changed)
        os.utime(source, (1785000000, 1785000000))
        # The copy stamp is no longer eligible because the full cover equality
        # check fails; the picker stamp still detects the sampled source change.
        fresh, would_write, _ = SEEDER.seed_volume(
            self.volume, write=False
        )
        self.assertEqual((fresh, would_write), (0, 1))

    def test_template_overlay_uses_overlay_conversion_version(self) -> None:
        self.write_asset(
            self.templates,
            "Notes.png",
            fake_png(1404, 1872, b"source" * 1000),
        )
        self.write_asset(
            self.templates,
            "Notes_condor.png",
            fake_png(1404, 1872, b"overlay" * 1000),
        )
        self.write_asset(
            self.templates,
            "Notes-icon.png",
            fake_png(280, 280, b"icon" * 1000),
        )
        SEEDER.seed_volume(self.volume, write=True)
        stamp = (
            self.templates / "Notes_condor.png.cnt-cache"
        ).read_text(encoding="ascii")
        self.assertIn("conversion=template-overlay-v2\n", stamp)
        self.assertIn("source-identity=", stamp)

    def test_invalid_or_appledouble_png_is_ignored(self) -> None:
        self.write_asset(
            self.covers,
            "._Hidden.png",
            fake_png(1404, 1872, b"hidden"),
        )
        self.write_asset(self.covers, "Broken.png", b"not a png")
        self.assertEqual(
            SEEDER.seed_volume(self.volume, write=False)[:2], (0, 0)
        )


if __name__ == "__main__":
    unittest.main()
