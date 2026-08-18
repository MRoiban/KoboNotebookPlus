#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import pathlib
import re
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PLUGIN_ROOT = ROOT / "mods" / "custom-notebook-templates"
LAUNCHER = PLUGIN_ROOT / "iink-launcher.sh"
RUNTIME = PLUGIN_ROOT / "src" / "plugin_runtime.cc"
MAKEFILE = PLUGIN_ROOT / "Makefile"
RELEASE_VERIFIER = ROOT / "scripts" / "verify-release-package.py"


class ExportContractTests(unittest.TestCase):
    @staticmethod
    def marker_paths() -> tuple[str, str]:
        runtime = RUNTIME.read_text()
        launcher = LAUNCHER.read_text()
        runtime_match = re.search(
            r'kExportQualityPath\[\]\s*=\s*\n?\s*"([^"]+)"',
            runtime,
        )
        launcher_match = re.search(
            r"^\s*marker=(\S+)\s*$",
            launcher,
            re.MULTILINE,
        )
        if runtime_match is None or launcher_match is None:
            raise AssertionError("quality marker path declaration is missing")
        return runtime_match.group(1), launcher_match.group(1)

    def test_quality_marker_path_matches_launcher(self) -> None:
        runtime_path, launcher_path = self.marker_paths()
        self.assertEqual(runtime_path, launcher_path)
        self.assertNotIn("/.kobo/custom/", launcher_path)

    def test_fast_marker_selects_fast_exporter_and_is_consumed(self) -> None:
        _, marker_path = self.marker_paths()
        with tempfile.TemporaryDirectory(
            prefix="kobo-export-launcher-test-"
        ) as directory:
            root = pathlib.Path(directory)
            kobo = root / "Kobo"
            kobo.mkdir()
            marker = root / "export-quality"
            launcher = root / "iink-launcher.sh"
            source = LAUNCHER.read_text()
            source = source.replace("/usr/local/Kobo", str(kobo))
            source = source.replace(marker_path, str(marker))
            launcher.write_text(source)
            launcher.chmod(0o755)

            for name in ("iink", "iink-export-best", "iink-export-fast"):
                executable = kobo / name
                executable.write_text(
                    "#!/bin/sh\n"
                    f"printf '%s\\n' '{name}'\n"
                )
                executable.chmod(0o755)

            marker.write_text("fast\n")
            result = subprocess.run(
                [str(launcher), "exporter"],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.stdout, "iink-export-fast\n")
            self.assertFalse(marker.exists())

    def test_launcher_is_shell_valid(self) -> None:
        subprocess.run(["sh", "-n", str(LAUNCHER)], check=True)
        self.assertTrue(LAUNCHER.stat().st_mode & 0o111)

    def test_release_uses_versioned_launcher_source(self) -> None:
        makefile = MAKEFILE.read_text()
        destination = "iink-launcher.sh:/usr/local/Kobo/iink-launcher.sh"
        self.assertIn(destination, makefile)
        self.assertNotIn("package/" + destination, makefile)

        verifier = RELEASE_VERIFIER.read_text()
        digest_match = re.search(
            r'EXPECTED_LAUNCHER_SHA256\s*=\s*\(\s*"([0-9a-f]{64})"',
            verifier,
        )
        self.assertIsNotNone(digest_match)
        self.assertEqual(
            hashlib.sha256(LAUNCHER.read_bytes()).hexdigest(),
            digest_match.group(1),
        )


if __name__ == "__main__":
    unittest.main()
