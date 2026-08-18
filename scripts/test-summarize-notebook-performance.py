#!/usr/bin/env python3
"""Regression tests for summarize-notebook-performance.py."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("summarize-notebook-performance.py")
SPEC = importlib.util.spec_from_file_location(
    "summarize_notebook_performance", SCRIPT
)
assert SPEC and SPEC.loader
ANALYZER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ANALYZER
SPEC.loader.exec_module(ANALYZER)


def open_record(
    *,
    total: int,
    stock: int,
    plugin: int,
    layer: str = "base-fast",
) -> str:
    return (
        "perf: notebook-open "
        f"total-ms={total} stock-ms={stock} plugin-ms={plugin} "
        "observers-ms=2 eraser-ms=1 eraser-queued=no "
        f"layer-context-ms=3 layer-route-ms=4 layer={layer}"
    )


STARTUP = (
    "plugin library loaded\n"
    "NickelHook init entered\n"
    "NickelHook init complete; waiting for natural libiinknote load\n"
    "delayed hook initialization complete\n"
)


class PerformanceAnalyzerTests(unittest.TestCase):
    def test_structured_open_metrics_and_startup_markers(self) -> None:
        lines = (
            STARTUP
            + open_record(total=20, stock=12, plugin=8)
            + "\n"
            + open_record(
                total=40, stock=15, plugin=25, layer="routed"
            )
        ).splitlines()
        analysis = ANALYZER.analyze_sources([("current.log", lines)])
        report = analysis.to_dict()

        self.assertEqual(report["startup"]["detected_sessions"], 1)
        self.assertEqual(report["startup"]["complete_files"], 1)
        self.assertEqual(report["notebook_open"]["samples"], 2)
        self.assertEqual(
            report["notebook_open"]["layers"],
            {"base-fast": 1, "routed": 1},
        )
        self.assertEqual(
            report["notebook_open"]["metrics_ms"]["total"],
            {"count": 2, "p50": 30.0, "p95": 40.0, "max": 40},
        )

    def test_malformed_perf_records_are_counted_with_locations(self) -> None:
        analysis = ANALYZER.analyze_sources(
            [
                (
                    "broken.log",
                    [
                        "perf: notebook-open total-ms=not-a-number",
                        open_record(total=9, stock=8, plugin=1),
                    ],
                )
            ]
        )
        report = analysis.to_dict()["notebook_open"]

        self.assertEqual(report["samples"], 1)
        self.assertEqual(report["malformed"], 1)
        self.assertEqual(
            report["malformed_records"][0]["path"], "broken.log"
        )
        self.assertEqual(report["malformed_records"][0]["line"], 1)

    def test_gesture_pairing_is_ordered_and_stops_at_file_boundary(
        self,
    ) -> None:
        analysis = ANALYZER.analyze_sources(
            [
                (
                    "previous.log",
                    [
                        "notebook-gesture: undo dispatch begin",
                        "notebook-gesture: redo dispatch begin",
                        "notebook-gesture: redo dispatch complete ms=7",
                        "notebook-gesture: undo dispatch complete ms=5",
                        "notebook-gesture: redo dispatch complete ms=9",
                        "notebook-gesture: undo dispatch begin",
                    ],
                ),
                (
                    "current.log",
                    [
                        "notebook-gesture: undo dispatch complete ms=11",
                    ],
                ),
            ]
        )
        gestures = analysis.to_dict()["gestures"]

        self.assertEqual(gestures["begins"], {"undo": 2, "redo": 1})
        self.assertEqual(gestures["completions"], {"undo": 2, "redo": 2})
        self.assertEqual(gestures["matched_total"], 0)
        self.assertEqual(len(gestures["mismatches"]), 2)
        self.assertEqual(len(gestures["unmatched_begins"]), 1)
        self.assertEqual(len(gestures["unmatched_completions"]), 2)
        self.assertEqual(
            gestures["durations_ms"]["redo"],
            {"count": 2, "p50": 8.0, "p95": 9.0, "max": 9},
        )

    def test_cli_json_and_validation_thresholds(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="kobo-performance-test-"
        ) as temporary:
            log = Path(temporary) / "plugin-status.txt"
            log.write_text(
                STARTUP
                + open_record(total=15, stock=13, plugin=2)
                + "\nnotebook-gesture: undo dispatch begin"
                + "\nnotebook-gesture: undo dispatch complete ms=6\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--json",
                    "--require-samples",
                    "1",
                    "--require-gestures",
                    "1",
                    "--fail-on-unmatched",
                    str(log),
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["notebook_open"]["samples"], 1)
        self.assertEqual(report["gestures"]["matched_total"], 1)
        self.assertEqual(report["gestures"]["durations_ms"]["undo"]["p50"], 6.0)

    def test_cli_fails_requirements_and_pairing_anomalies(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="kobo-performance-test-"
        ) as temporary:
            log = Path(temporary) / "plugin-status.txt"
            log.write_text(
                "notebook-gesture: undo dispatch begin\n"
                "notebook-gesture: redo dispatch complete ms=3\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--require-samples",
                    "1",
                    "--require-gestures",
                    "1",
                    "--fail-on-unmatched",
                    str(log),
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

        self.assertEqual(result.returncode, 1)
        self.assertIn("notebook-open samples 0 < required 1", result.stderr)
        self.assertIn("matched gestures 0 < required 1", result.stderr)
        self.assertIn("gesture pairing anomalies 1", result.stderr)
        self.assertIn("mismatches=1", result.stdout)

    def test_default_text_keeps_latency_and_gesture_summary(self) -> None:
        analysis = ANALYZER.analyze_sources(
            [
                (
                    "fixture.log",
                    [
                        open_record(total=12, stock=10, plugin=2),
                        "notebook-gesture: redo dispatch begin",
                        "notebook-gesture: redo dispatch complete ms=4",
                    ],
                )
            ]
        )
        text = ANALYZER.format_text(analysis)

        self.assertIn("notebook-open samples=1 malformed=0", text)
        self.assertIn("total-ms p50=12.0 p95=12.0 max=12", text)
        self.assertIn("gestures undo=0 redo=1", text)
        self.assertIn("redo-gesture-ms p50=4.0 p95=4.0 max=4", text)


if __name__ == "__main__":
    unittest.main()
