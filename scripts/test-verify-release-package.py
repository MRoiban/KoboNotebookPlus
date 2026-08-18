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


def arm_executable() -> bytes:
    data = bytearray(20)
    data[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<HH", data, 16, 2, 40)
    return bytes(data)


def write_bundle(path: pathlib.Path, files: dict[str, bytes]) -> None:
    with tarfile.open(path, "w:gz") as bundle:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o755
            bundle.addfile(info, io.BytesIO(data))


class ReleasePackageTests(unittest.TestCase):
    def test_accepts_exact_five_file_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            plugin = root / "plugin.so"
            archive = root / "KoboRoot.tgz"
            plugin.write_bytes(arm_elf())
            stock = b"stock"
            best = arm_executable() + b"best"
            fast = arm_executable() + b"fast"
            launcher = b"#!/bin/sh\nexit 0\n"
            originals = (
                VERIFIER.EXPECTED_STOCK_SHA256,
                VERIFIER.EXPECTED_BEST_EXPORTER_SHA256,
                VERIFIER.EXPECTED_FAST_EXPORTER_SHA256,
                VERIFIER.EXPECTED_LAUNCHER_SHA256,
            )
            VERIFIER.EXPECTED_STOCK_SHA256 = VERIFIER.sha256(stock)
            VERIFIER.EXPECTED_BEST_EXPORTER_SHA256 = VERIFIER.sha256(best)
            VERIFIER.EXPECTED_FAST_EXPORTER_SHA256 = VERIFIER.sha256(fast)
            VERIFIER.EXPECTED_LAUNCHER_SHA256 = VERIFIER.sha256(launcher)
            try:
                write_bundle(
                    archive,
                    {
                        "./" + VERIFIER.PLUGIN_MEMBER: plugin.read_bytes(),
                        "./" + VERIFIER.STOCK_MEMBER: stock,
                        "./" + VERIFIER.BEST_EXPORTER_MEMBER: best,
                        "./" + VERIFIER.FAST_EXPORTER_MEMBER: fast,
                        "./" + VERIFIER.LAUNCHER_MEMBER: launcher,
                    },
                )
                result = VERIFIER.validate_package(archive, plugin)
            finally:
                (
                    VERIFIER.EXPECTED_STOCK_SHA256,
                    VERIFIER.EXPECTED_BEST_EXPORTER_SHA256,
                    VERIFIER.EXPECTED_FAST_EXPORTER_SHA256,
                    VERIFIER.EXPECTED_LAUNCHER_SHA256,
                ) = originals
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
                    VERIFIER.BEST_EXPORTER_MEMBER: arm_executable(),
                    VERIFIER.FAST_EXPORTER_MEMBER: arm_executable(),
                    VERIFIER.LAUNCHER_MEMBER: b"#!/bin/sh\n",
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
