#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import io
import pathlib
import struct
import tarfile
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).with_name("verify-release-package.py")
SPEC = importlib.util.spec_from_file_location("verify_release_package", SCRIPT)
assert SPEC and SPEC.loader
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)


def arm_elf() -> bytes:
    data = bytearray(20)
    data[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<HH", data, 16, 3, 40)
    return bytes(data)


def write_bundle(path: pathlib.Path, files: dict[str, bytes]) -> None:
    with tarfile.open(path, "w:gz") as bundle:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            bundle.addfile(info, io.BytesIO(data))


class ReleasePackageTests(unittest.TestCase):
    def test_accepts_exact_two_file_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            plugin = root / "plugin.so"
            archive = root / "KoboRoot.tgz"
            plugin.write_bytes(arm_elf())
            stock = b"stock"
            original = VERIFIER.EXPECTED_STOCK_SHA256
            VERIFIER.EXPECTED_STOCK_SHA256 = VERIFIER.sha256(stock)
            try:
                write_bundle(
                    archive,
                    {
                        "./" + VERIFIER.PLUGIN_MEMBER: plugin.read_bytes(),
                        "./" + VERIFIER.STOCK_MEMBER: stock,
                    },
                )
                result = VERIFIER.validate_package(archive, plugin)
            finally:
                VERIFIER.EXPECTED_STOCK_SHA256 = original
            self.assertEqual(result["plugin"], VERIFIER.sha256(plugin.read_bytes()))

    def test_rejects_extra_member(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            plugin = root / "plugin.so"
            archive = root / "KoboRoot.tgz"
            plugin.write_bytes(arm_elf())
            write_bundle(
                archive,
                {
                    VERIFIER.PLUGIN_MEMBER: plugin.read_bytes(),
                    VERIFIER.STOCK_MEMBER: b"stock",
                    "unexpected": b"no",
                },
            )
            with self.assertRaisesRegex(ValueError, "archive contents differ"):
                VERIFIER.validate_package(archive, plugin)

    def test_rejects_non_arm_plugin(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            plugin = root / "plugin.so"
            plugin.write_bytes(b"not an elf")
            with self.assertRaisesRegex(ValueError, "not an ELF"):
                VERIFIER.validate_package(root / "missing.tgz", plugin)


if __name__ == "__main__":
    unittest.main()
