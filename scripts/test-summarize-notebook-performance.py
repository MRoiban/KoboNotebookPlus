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



def edit_record(total=100, op="page-move-later", result="ok", extra=""):
    return f"perf: notebook-edit op={op} result={result} total-ms={total} {extra}".strip()


class EditAnalyzerTests(unittest.TestCase):
    def analysis(self, lines):
        return ANALYZER.analyze_sources([("fixture", lines)])

    def test_repeat_phases_sum_per_record_and_keep_counts_order(self):
        sample = ANALYZER.parse_notebook_edit(edit_record(extra=
            "save-ms=10 engine-ms=1 save-ms=20 page-count=12"))
        self.assertEqual(list(sample.phases), ["save", "engine"])
        self.assertEqual(sample.phases["save"], 30)
        self.assertEqual(sample.phase_counts["save"], 2)
        self.assertEqual(sample.counters, {"page-count": 12})
        report = self.analysis([edit_record(extra="save-ms=10 save-ms=20"),
                                edit_record(200, extra="save-ms=50"),
                                edit_record(999, result="failed")]).to_dict()["notebook_edit"]
        group = report["groups"]["page-move-later"]
        self.assertEqual(group["total_ms"], {"n": 2, "median": 150., "p10": 100.,
                                            "p90": 200., "min": 100., "max": 200.})
        phase = group["phases_ms"]["save"]
        self.assertEqual(phase["median"], 40)
        self.assertEqual(phase["occurrences"], 3)
        self.assertAlmostEqual(phase["share_median_total_pct"], 100*40/150)
        self.assertEqual(report["failed"], 1)

    def test_variants_and_settle_are_separate_groups(self):
        lines = [edit_record(), edit_record(extra='variant=stock'),
                 edit_record(20, extra='variant=position'),
                 edit_record(50, op='page-move-later-settle',
                             extra='variant=position paint-ms=30.5 paints=2 capped=0')]
        analysis = self.analysis(lines)
        groups = ANALYZER.edit_report(analysis)['groups']
        self.assertEqual(groups['page-move-later']['n'], 2)
        self.assertEqual(groups['page-move-later@position']['n'], 1)
        settle = groups['page-move-later-settle@position']
        self.assertEqual(settle['variant'], 'position')
        self.assertEqual(settle['phases_ms']['paint']['median'], 30.5)
        for extra in ('variant=Bad', 'variant=x@y', 'variant=', 'variant=stock variant=position'):
            with self.subTest(extra=extra), self.assertRaises(ANALYZER.MalformedPerformanceRecord):
                ANALYZER.parse_notebook_edit(edit_record(extra=extra))

    def test_strict_malformed_edits(self):
        invalid = ["", "op=x", "op=X result=ok total-ms=1", "op=x result=no total-ms=1",
                   "op=x result=ok total-ms=nan", "op=x result=ok total-ms=-1",
                   "op=x result=ok total-ms=1 total-ms=2", "op=x op=y result=ok total-ms=1",
                   "op=x result=ok total-ms=1 save-ms=inf", "op=x result=ok total-ms=1 page-count=1.2",
                   "op=x result=ok total-ms=1 garbage", "op=x result=ok total-ms=1 a=1 a=2"]
        for payload in invalid:
            with self.subTest(payload=payload), self.assertRaises(ANALYZER.MalformedPerformanceRecord):
                ANALYZER.parse_notebook_edit("perf: notebook-edit " + payload)
        analysis = self.analysis(["perf: notebook-edit op=x"])
        self.assertEqual(analysis.malformed_edits[0].location.line, 1)
        self.assertIn("missing field", ANALYZER.format_text(analysis))
        self.assertTrue(ANALYZER.validation_failures(analysis))
        self.assertIsNone(ANALYZER.parse_notebook_edit("unrelated"))

    def test_optional_phases_and_zero_total(self):
        group = self.analysis([edit_record(0, extra="save-ms=0"), edit_record(0)]).to_dict()["notebook_edit"]["groups"]["page-move-later"]
        self.assertIsNone(group["phases_ms"]["save"]["share_median_total_pct"])
        self.assertEqual(group["phases_ms"]["save"]["n"], 1)

    def test_bootstrap_fixed_seed_significance_and_missing(self):
        before = self.analysis([edit_record(100)]*5)
        after = self.analysis([edit_record(80)]*5)
        comparison = ANALYZER.compare_edits(before, after)
        self.assertEqual(comparison, ANALYZER.compare_edits(before, after))
        row = comparison["page-move-later"]
        self.assertEqual(row["ci95_ms"], [-20., -20.])
        self.assertEqual(row["delta_pct"], -20)
        self.assertTrue(row["significant"])
        self.assertFalse(ANALYZER.compare_edits(before, before)["page-move-later"]["significant"])
        self.assertEqual(ANALYZER.compare_edits(before, self.analysis([]))["page-move-later"]["status"], "missing candidate")

    def test_compare_cli_files_and_directories(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "baseline").mkdir()
            (root / "baseline" / "plugin-status.previous.txt").write_text(edit_record(100))
            candidate = root / "trace.txt"
            candidate.write_text(edit_record(50))
            result = subprocess.run([sys.executable, str(SCRIPT), "--json", "--compare",
                                     str(root / "baseline"), str(candidate)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["comparison"]["page-move-later"]["after_ms"], 50)
            candidate.write_text("perf: notebook-edit op=x")
            result = subprocess.run([sys.executable, str(SCRIPT), str(candidate)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("missing field", result.stdout)

if __name__ == "__main__":
    unittest.main()
