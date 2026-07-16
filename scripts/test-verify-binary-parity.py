#!/usr/bin/env python3
"""Focused host tests for verify-binary-parity.py."""

from __future__ import annotations

import importlib.util
import os
import pathlib
import stat
import struct
import subprocess
import sys
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).with_name("verify-binary-parity.py")


def load_verifier():
    spec = importlib.util.spec_from_file_location("verify_binary_parity", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


VERIFIER = load_verifier()


def write_arm_shared_object(path: pathlib.Path) -> None:
    header = bytearray(52)
    header[:4] = b"\x7fELF"
    header[4] = 1  # ELFCLASS32
    header[5] = 1  # ELFDATA2LSB
    header[6] = 1  # EV_CURRENT
    struct.pack_into("<HHI", header, 16, 3, 40, 1)  # ET_DYN, EM_ARM
    path.write_bytes(header)


FAKE_NM = """#!/usr/bin/env python3
import os
import pathlib
import sys

candidate = pathlib.Path(sys.argv[-1]).name.startswith("candidate")
if "--defined-only" in sys.argv:
    print("00000100 T plugin_entry")
    print("00000104 W stable_weak_export")
    if candidate and os.environ.get("FAKE_EXPORT_DRIFT"):
        print("00000108 T accidental_export")
elif "--undefined-only" in sys.argv:
    print("         U dlopen@GLIBC_2.4")
    print("         w _ITM_registerTMCloneTable")
"""


FAKE_OBJDUMP = """#!/usr/bin/env python3
import os
import pathlib
import sys

candidate = pathlib.Path(sys.argv[-1]).name.startswith("candidate")
if "-p" in sys.argv:
    libraries = ["libQt5Core.so.5", "libc.so.6"]
    if candidate and os.environ.get("FAKE_NEEDED_DRIFT"):
        libraries.reverse()
    for library in libraries:
        print("  NEEDED       " + library)
elif "-h" in sys.argv:
    text_size = "00000020" if candidate else "00000010"
    print("Sections:")
    print("Idx Name          Size      VMA       Type")
    print("  0 .text         " + text_size + " 00000100 TEXT")
    print("  1 .data         00000008 00000200 DATA")
else:
    sys.exit(2)
"""


class BinaryParityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.reference = self.root / "reference.so"
        self.candidate = self.root / "candidate.so"
        write_arm_shared_object(self.reference)
        write_arm_shared_object(self.candidate)
        self.nm = self.write_tool("fake-nm", FAKE_NM)
        self.objdump = self.write_tool("fake-objdump", FAKE_OBJDUMP)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_tool(self, name: str, content: str) -> pathlib.Path:
        path = self.root / name
        path.write_text(content, encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def run_verifier(self, **environment: str) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env.update(environment)
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(self.reference),
                str(self.candidate),
                "--nm",
                str(self.nm),
                "--objdump",
                str(self.objdump),
                "--skip-pinned-plugin-abi",
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )

    def test_section_size_drift_is_informational(self) -> None:
        result = self.run_verifier()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(".text: 0x10 (16) -> 0x20 (32); delta +16", result.stdout)
        self.assertIn("Binary parity verified", result.stdout)

    def test_added_dynamic_export_fails(self) -> None:
        result = self.run_verifier(FAKE_EXPORT_DRIFT="1")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("candidate only: T accidental_export", result.stderr)
        self.assertIn("Binary parity verification FAILED", result.stderr)

    def test_dt_needed_order_change_fails(self) -> None:
        result = self.run_verifier(FAKE_NEEDED_DRIFT="1")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("DT_NEEDED differs (order is significant)", result.stderr)

    def test_nm_parser_preserves_binding_and_versions(self) -> None:
        parsed = VERIFIER.parse_nm(
            "00000100 T exported\n         U imported@GLIBC_2.4\n"
            "         w optional_import\n"
        )
        self.assertEqual(parsed[("T", "exported")], 1)
        self.assertEqual(parsed[("U", "imported@GLIBC_2.4")], 1)
        self.assertEqual(parsed[("w", "optional_import")], 1)

    def test_non_arm_input_is_rejected(self) -> None:
        data = bytearray(self.reference.read_bytes())
        struct.pack_into("<H", data, 18, 62)  # EM_X86_64
        self.reference.write_bytes(data)
        result = self.run_verifier()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("not an ARM ELF", result.stderr)


if __name__ == "__main__":
    unittest.main()
