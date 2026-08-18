#!/usr/bin/env python3
"""Regression tests for collect-device-stability.py."""

from __future__ import annotations

import hashlib
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("collect-device-stability.py")


def tree_fingerprint(root: Path) -> list[tuple[object, ...]]:
    """Capture device content/metadata while deliberately ignoring access time."""

    result: list[tuple[object, ...]] = []
    for path in sorted(root.rglob("*")):
        metadata = path.lstat()
        relative = str(path.relative_to(root))
        digest = ""
        if stat.S_ISREG(metadata.st_mode):
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
        result.append(
            (
                relative,
                stat.S_IFMT(metadata.st_mode),
                stat.S_IMODE(metadata.st_mode),
                metadata.st_size,
                metadata.st_mtime_ns,
                digest,
            )
        )
    return result


class DeviceStabilityCollectorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="kobo-stability-collector-test-"
        )
        self.base = Path(self.temporary.name)
        self.volume = self.base / "KOBOeReader"
        self.templates = (
            self.volume
            / ".kobo"
            / "iink"
            / "custom-notebook-templates"
            / "templates"
        )
        self.templates.mkdir(parents=True)
        self.current = self.templates / "plugin-status.txt"
        self.previous = self.templates / "plugin-status.previous.txt"
        self.previous.write_text(
            "\n".join(
                (
                    "plugin library loaded",
                    "NickelHook init entered",
                    "NickelHook init complete; waiting for natural libiinknote load",
                    "delayed hook initialization complete",
                )
            )
            + "\n",
            encoding="utf-8",
        )
        self.write_healthy_current()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_healthy_current(self) -> None:
        self.current.write_text(
            "\n".join(
                (
                    "plugin library loaded",
                    "NickelHook init entered",
                    "NickelHook init complete; waiting for natural libiinknote load",
                    "delayed hook initialization complete",
                    "notebook-gesture: undo dispatch begin",
                    "notebook-gesture: undo dispatch complete ms=12",
                    (
                        "perf: notebook-open total-ms=100 stock-ms=88 "
                        "plugin-ms=12 observers-ms=3 eraser-ms=1 "
                        "eraser-queued=no layer-context-ms=2 "
                        "layer-route-ms=6 layer=base-fast"
                    ),
                )
            )
            + "\n",
            encoding="utf-8",
        )

    def run_collector(
        self,
        *extra: str,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(self.volume),
                *extra,
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def test_healthy_collection_copies_logs_without_mutating_volume(self) -> None:
        before = tree_fingerprint(self.volume)
        snapshot = self.base / "host-snapshot"
        result = self.run_collector("--output", str(snapshot))
        after = tree_fingerprint(self.volume)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(before, after)
        self.assertEqual(
            (snapshot / "plugin-status.txt").read_bytes(),
            self.current.read_bytes(),
        )
        self.assertEqual(
            (snapshot / "plugin-status.previous.txt").read_bytes(),
            self.previous.read_bytes(),
        )
        self.assertIn("Notebook performance summary:", result.stdout)
        self.assertIn("Layer artifact verification:", result.stdout)
        self.assertIn("Generated asset-cache dry-run:", result.stdout)
        self.assertIn("RESULT: PASS", result.stdout)
        self.assertIn("mounted device was not written", result.stdout)

    def test_pending_installer_payload_is_a_failure(self) -> None:
        (self.volume / ".kobo" / "KoboRoot.tgz").write_bytes(b"pending")
        result = self.run_collector()
        self.assertEqual(result.returncode, 1)
        self.assertIn("installer payload is still pending", result.stdout)
        self.assertIn("cannot verify successful extraction", result.stdout)

    def test_installer_appledouble_is_reported_but_not_pending(self) -> None:
        (self.volume / ".kobo" / "._KoboRoot.tgz").write_bytes(b"metadata")
        (self.volume / ".kobo" / "._KoboRoot.tgz.installing").write_bytes(
            b"metadata"
        )
        result = self.run_collector()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("AppleDouble metadata remains", result.stdout)
        self.assertNotIn("installer payload is still pending", result.stdout)

    def test_unmatched_gesture_begin_is_a_failure(self) -> None:
        self.current.write_text(
            "\n".join(
                (
                    "plugin library loaded",
                    "NickelHook init entered",
                    "NickelHook init complete; waiting for natural libiinknote load",
                    "delayed hook initialization complete",
                    "notebook-gesture: redo dispatch begin",
                )
            )
            + "\n",
            encoding="utf-8",
        )
        result = self.run_collector()
        self.assertEqual(result.returncode, 1)
        self.assertIn("gesture dispatch begin(s) without completion", result.stdout)

    def test_missing_current_log_and_fatal_previous_indicator_fail(self) -> None:
        self.current.unlink()
        with self.previous.open("a", encoding="utf-8") as handle:
            handle.write("fatal crash signal 11\n")
        result = self.run_collector()
        self.assertEqual(result.returncode, 1)
        self.assertIn("current plugin status log was not found", result.stdout)
        self.assertIn("explicit crash/fatal indicator", result.stdout)

    def test_root_level_nickelhook_fatal_log_is_collected_and_fails(self) -> None:
        fatal = self.volume / "CustomNotebookTemplates-early-init.log"
        fatal.write_text("fatal: constructor aborted\n", encoding="utf-8")
        snapshot = self.base / "host-snapshot"
        result = self.run_collector("--output", str(snapshot))
        self.assertEqual(result.returncode, 1)
        self.assertIn("root-level NickelHook fatal-init evidence", result.stdout)
        self.assertEqual(
            (snapshot / fatal.name).read_bytes(),
            fatal.read_bytes(),
        )

    def test_snapshot_directory_inside_mount_is_refused(self) -> None:
        snapshot = self.volume / "host-snapshot"
        result = self.run_collector("--snapshot-dir", str(snapshot))
        self.assertEqual(result.returncode, 2)
        self.assertFalse(snapshot.exists())
        self.assertIn("must be outside", result.stderr)

    def test_existing_snapshot_logs_are_not_overwritten(self) -> None:
        snapshot = self.base / "host-snapshot"
        snapshot.mkdir()
        destination = snapshot / "plugin-status.txt"
        destination.write_text("preserve me\n", encoding="utf-8")
        result = self.run_collector("--output", str(snapshot))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            destination.read_text(encoding="utf-8"),
            "preserve me\n",
        )
        self.assertIn("refusing to overwrite", result.stdout)


if __name__ == "__main__":
    unittest.main()
