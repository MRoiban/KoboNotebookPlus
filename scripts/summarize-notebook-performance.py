#!/usr/bin/env python3
"""Analyze Kobo notebook-open and passive-gesture trace records.

The parser is intentionally independent from the plugin so it can validate
both a mounted device's current/previous logs and copied recovery snapshots.
Each input file is treated as a separate process session for gesture pairing;
an unfinished action at the end of one log can never be paired with an action
from another log.
"""

import argparse
from collections import deque
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import statistics
import sys
from typing import Iterable, Sequence


OPEN_PREFIX = "perf: notebook-open "
OPEN_INTEGER_FIELDS = (
    ("total-ms", "total"),
    ("stock-ms", "stock"),
    ("plugin-ms", "plugin"),
    ("observers-ms", "observers"),
    ("eraser-ms", "eraser"),
    ("layer-context-ms", "layer_context"),
    ("layer-route-ms", "layer_route"),
)
STARTUP_MARKERS = (
    ("library_loaded", "plugin library loaded"),
    ("init_entered", "NickelHook init entered"),
    (
        "init_waiting",
        "NickelHook init complete; waiting for natural libiinknote load",
    ),
    ("hooks_complete", "delayed hook initialization complete"),
)
GESTURE_BEGIN = re.compile(
    r"notebook-gesture: (undo|redo) dispatch begin\b"
)
GESTURE_COMPLETE = re.compile(
    r"notebook-gesture: (undo|redo) dispatch complete ms=(\d+)\b"
)
GESTURE_KINDS = ("undo", "redo")


class MalformedPerformanceRecord(ValueError):
    """A notebook-open trace marker was present but its payload was invalid."""


@dataclass(frozen=True)
class NotebookOpenSample:
    total: int
    stock: int
    plugin: int
    observers: int
    eraser: int
    eraser_queued: bool
    layer_context: int
    layer_route: int
    layer: str


@dataclass(frozen=True)
class EventLocation:
    path: str
    line: int

    def to_dict(self) -> dict[str, object]:
        return {"path": self.path, "line": self.line}


@dataclass(frozen=True)
class GestureBegin:
    kind: str
    location: EventLocation

    def to_dict(self) -> dict[str, object]:
        return {
            "kind": self.kind,
            **self.location.to_dict(),
        }


@dataclass(frozen=True)
class GestureCompletion:
    kind: str
    duration_ms: int
    location: EventLocation

    def to_dict(self) -> dict[str, object]:
        return {
            "kind": self.kind,
            "duration_ms": self.duration_ms,
            **self.location.to_dict(),
        }


@dataclass(frozen=True)
class GestureMismatch:
    begin: GestureBegin
    completion: GestureCompletion

    def to_dict(self) -> dict[str, object]:
        return {
            "expected": self.begin.kind,
            "actual": self.completion.kind,
            "begin": self.begin.location.to_dict(),
            "completion": self.completion.location.to_dict(),
            "duration_ms": self.completion.duration_ms,
        }


@dataclass(frozen=True)
class MalformedRecord:
    location: EventLocation
    reason: str

    def to_dict(self) -> dict[str, object]:
        return {**self.location.to_dict(), "reason": self.reason}


@dataclass
class LogSummary:
    path: str
    line_count: int = 0
    startup_markers: dict[str, int] = field(
        default_factory=lambda: {
            key: 0 for key, _marker in STARTUP_MARKERS
        }
    )

    @property
    def detected_sessions(self) -> int:
        return self.startup_markers["library_loaded"]

    @property
    def has_startup_markers(self) -> bool:
        return any(self.startup_markers.values())

    @property
    def startup_complete(self) -> bool:
        return all(self.startup_markers.values())

    @property
    def missing_startup_markers(self) -> list[str]:
        return [
            key for key, count in self.startup_markers.items() if count == 0
        ]

    def to_dict(self) -> dict[str, object]:
        return {
            "path": self.path,
            "line_count": self.line_count,
            "detected_sessions": self.detected_sessions,
            "startup_markers": dict(self.startup_markers),
            "startup_complete": self.startup_complete,
            "missing_startup_markers": self.missing_startup_markers,
        }


@dataclass(frozen=True)
class MetricSummary:
    count: int
    p50: float
    p95: float
    maximum: int

    def to_dict(self) -> dict[str, object]:
        return {
            "count": self.count,
            "p50": self.p50,
            "p95": self.p95,
            "max": self.maximum,
        }


@dataclass
class Analysis:
    logs: list[LogSummary] = field(default_factory=list)
    notebook_opens: list[NotebookOpenSample] = field(default_factory=list)
    malformed_performance: list[MalformedRecord] = field(
        default_factory=list
    )
    gesture_begins: dict[str, int] = field(
        default_factory=lambda: {kind: 0 for kind in GESTURE_KINDS}
    )
    gesture_completions: dict[str, list[int]] = field(
        default_factory=lambda: {kind: [] for kind in GESTURE_KINDS}
    )
    gesture_matches: dict[str, int] = field(
        default_factory=lambda: {kind: 0 for kind in GESTURE_KINDS}
    )
    gesture_mismatches: list[GestureMismatch] = field(default_factory=list)
    unmatched_gesture_begins: list[GestureBegin] = field(
        default_factory=list
    )
    unmatched_gesture_completions: list[GestureCompletion] = field(
        default_factory=list
    )

    @property
    def notebook_open_count(self) -> int:
        return len(self.notebook_opens)

    @property
    def matched_gesture_count(self) -> int:
        return sum(self.gesture_matches.values())

    @property
    def gesture_anomaly_count(self) -> int:
        return (
            len(self.gesture_mismatches)
            + len(self.unmatched_gesture_begins)
            + len(self.unmatched_gesture_completions)
        )

    def metric_values(self, name: str) -> list[int]:
        return [
            int(getattr(sample, name)) for sample in self.notebook_opens
        ]

    def to_dict(self) -> dict[str, object]:
        metrics = {
            name: summarize_metric(self.metric_values(name)).to_dict()
            for _trace_name, name in OPEN_INTEGER_FIELDS
            if self.notebook_opens
        }
        layers: dict[str, int] = {}
        eraser_queued = {"yes": 0, "no": 0}
        for sample in self.notebook_opens:
            layers[sample.layer] = layers.get(sample.layer, 0) + 1
            eraser_queued["yes" if sample.eraser_queued else "no"] += 1

        files_with_startup = [
            log for log in self.logs if log.has_startup_markers
        ]
        durations = {
            kind: (
                summarize_metric(self.gesture_completions[kind]).to_dict()
                if self.gesture_completions[kind]
                else None
            )
            for kind in GESTURE_KINDS
        }
        return {
            "logs": [log.to_dict() for log in self.logs],
            "startup": {
                "detected_sessions": sum(
                    log.detected_sessions for log in self.logs
                ),
                "files_with_markers": len(files_with_startup),
                "complete_files": sum(
                    log.startup_complete for log in files_with_startup
                ),
                "incomplete_files": [
                    log.path
                    for log in files_with_startup
                    if not log.startup_complete
                ],
            },
            "notebook_open": {
                "samples": self.notebook_open_count,
                "malformed": len(self.malformed_performance),
                "malformed_records": [
                    record.to_dict()
                    for record in self.malformed_performance
                ],
                "layers": layers,
                "eraser_queued": eraser_queued,
                "metrics_ms": metrics,
            },
            "gestures": {
                "begins": dict(self.gesture_begins),
                "completions": {
                    kind: len(self.gesture_completions[kind])
                    for kind in GESTURE_KINDS
                },
                "matched": dict(self.gesture_matches),
                "matched_total": self.matched_gesture_count,
                "mismatches": [
                    mismatch.to_dict()
                    for mismatch in self.gesture_mismatches
                ],
                "unmatched_begins": [
                    begin.to_dict()
                    for begin in self.unmatched_gesture_begins
                ],
                "unmatched_completions": [
                    completion.to_dict()
                    for completion in self.unmatched_gesture_completions
                ],
                "durations_ms": durations,
            },
        }


def percentile(values: Sequence[int], ratio: float) -> float:
    """Return the nearest-rank percentile used by the original analyzer."""

    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(
        len(ordered) - 1,
        int((len(ordered) - 1) * ratio + 0.5),
    )
    return float(ordered[index])


def summarize_metric(values: Sequence[int]) -> MetricSummary:
    if not values:
        raise ValueError("cannot summarize an empty metric")
    return MetricSummary(
        count=len(values),
        p50=float(statistics.median(values)),
        p95=percentile(values, 0.95),
        maximum=max(values),
    )


def parse_notebook_open(line: str) -> NotebookOpenSample | None:
    """Parse one notebook-open record, or return None for an unrelated line."""

    marker = line.find(OPEN_PREFIX)
    if marker < 0:
        return None
    payload = line[marker + len(OPEN_PREFIX) :].strip()
    if not payload:
        raise MalformedPerformanceRecord("missing fields")

    fields: dict[str, str] = {}
    for token in payload.split():
        if "=" not in token:
            raise MalformedPerformanceRecord(
                f"field has no '=' separator: {token}"
            )
        key, value = token.split("=", 1)
        if not key or not value:
            raise MalformedPerformanceRecord(f"invalid field: {token}")
        if key in fields:
            raise MalformedPerformanceRecord(f"duplicate field: {key}")
        fields[key] = value

    required = [
        trace_name for trace_name, _name in OPEN_INTEGER_FIELDS
    ] + ["eraser-queued", "layer"]
    missing = [key for key in required if key not in fields]
    if missing:
        raise MalformedPerformanceRecord(
            "missing field(s): " + ", ".join(missing)
        )

    integers: dict[str, int] = {}
    for trace_name, name in OPEN_INTEGER_FIELDS:
        value = fields[trace_name]
        if not value.isdecimal():
            raise MalformedPerformanceRecord(
                f"{trace_name} is not a non-negative integer"
            )
        integers[name] = int(value)

    queued_value = fields["eraser-queued"]
    if queued_value not in ("yes", "no"):
        raise MalformedPerformanceRecord(
            "eraser-queued must be 'yes' or 'no'"
        )

    return NotebookOpenSample(
        total=integers["total"],
        stock=integers["stock"],
        plugin=integers["plugin"],
        observers=integers["observers"],
        eraser=integers["eraser"],
        eraser_queued=queued_value == "yes",
        layer_context=integers["layer_context"],
        layer_route=integers["layer_route"],
        layer=fields["layer"],
    )


def analyze_sources(
    sources: Iterable[tuple[str, Iterable[str]]],
) -> Analysis:
    """Analyze named line iterables without reading the filesystem."""

    analysis = Analysis()
    for path, lines in sources:
        log = LogSummary(path=path)
        pending: deque[GestureBegin] = deque()
        for line_number, raw_line in enumerate(lines, start=1):
            log.line_count = line_number
            line = raw_line.rstrip("\r\n")
            location = EventLocation(path, line_number)

            for key, marker in STARTUP_MARKERS:
                if marker in line:
                    log.startup_markers[key] += 1

            try:
                sample = parse_notebook_open(line)
            except MalformedPerformanceRecord as error:
                analysis.malformed_performance.append(
                    MalformedRecord(location, str(error))
                )
            else:
                if sample is not None:
                    analysis.notebook_opens.append(sample)

            began = GESTURE_BEGIN.search(line)
            if began:
                kind = began.group(1)
                analysis.gesture_begins[kind] += 1
                pending.append(GestureBegin(kind, location))

            completed = GESTURE_COMPLETE.search(line)
            if completed:
                completion = GestureCompletion(
                    kind=completed.group(1),
                    duration_ms=int(completed.group(2)),
                    location=location,
                )
                analysis.gesture_completions[completion.kind].append(
                    completion.duration_ms
                )
                if not pending:
                    analysis.unmatched_gesture_completions.append(
                        completion
                    )
                else:
                    begin = pending.popleft()
                    if begin.kind == completion.kind:
                        analysis.gesture_matches[completion.kind] += 1
                    else:
                        analysis.gesture_mismatches.append(
                            GestureMismatch(begin, completion)
                        )

        # Never carry an in-flight gesture across process-log boundaries.
        analysis.unmatched_gesture_begins.extend(pending)
        analysis.logs.append(log)
    return analysis


def analyze_logs(paths: Sequence[Path]) -> Analysis:
    sources: list[tuple[str, list[str]]] = []
    for path in paths:
        sources.append(
            (
                str(path),
                path.read_text(
                    encoding="utf-8", errors="replace"
                ).splitlines(),
            )
        )
    return analyze_sources(sources)


def _format_locations(
    records: Sequence[MalformedRecord],
    limit: int = 5,
) -> str:
    locations = [
        f"{record.location.path}:{record.location.line}"
        for record in records[:limit]
    ]
    if len(records) > limit:
        locations.append(f"+{len(records) - limit} more")
    return ",".join(locations)


def format_text(analysis: Analysis) -> str:
    """Format a compact, human-readable validation report."""

    lines: list[str] = []
    startup_files = [
        log for log in analysis.logs if log.has_startup_markers
    ]
    lines.append(
        "logs "
        f"files={len(analysis.logs)} "
        f"lines={sum(log.line_count for log in analysis.logs)} "
        f"sessions={sum(log.detected_sessions for log in analysis.logs)} "
        "startup-complete-files="
        f"{sum(log.startup_complete for log in startup_files)} "
        "startup-incomplete-files="
        f"{sum(not log.startup_complete for log in startup_files)}"
    )
    for log in analysis.logs:
        markers = log.startup_markers
        missing = (
            ",".join(log.missing_startup_markers)
            if log.missing_startup_markers
            else "none"
        )
        lines.append(
            "startup "
            f"file={log.path} "
            f"library-loaded={markers['library_loaded']} "
            f"init-entered={markers['init_entered']} "
            f"init-waiting={markers['init_waiting']} "
            f"hooks-complete={markers['hooks_complete']} "
            f"missing={missing}"
        )

    layer_results: dict[str, int] = {}
    queued_yes = 0
    for sample in analysis.notebook_opens:
        layer_results[sample.layer] = (
            layer_results.get(sample.layer, 0) + 1
        )
        queued_yes += sample.eraser_queued
    lines.append(
        "notebook-open "
        f"samples={analysis.notebook_open_count} "
        f"malformed={len(analysis.malformed_performance)} "
        f"layers={json.dumps(layer_results, sort_keys=True)}"
    )
    for _trace_name, name in OPEN_INTEGER_FIELDS:
        values = analysis.metric_values(name)
        if not values:
            continue
        summary = summarize_metric(values)
        lines.append(
            f"{name}-ms p50={summary.p50:.1f} "
            f"p95={summary.p95:.1f} "
            f"max={summary.maximum}"
        )
    if analysis.notebook_opens:
        lines.append(
            "eraser-queued "
            f"yes={queued_yes} "
            f"no={analysis.notebook_open_count - queued_yes}"
        )
    if analysis.malformed_performance:
        lines.append(
            "malformed-perf "
            f"locations={_format_locations(analysis.malformed_performance)}"
        )

    lines.append(
        "gestures "
        f"undo={len(analysis.gesture_completions['undo'])} "
        f"redo={len(analysis.gesture_completions['redo'])} "
        f"begins-undo={analysis.gesture_begins['undo']} "
        f"begins-redo={analysis.gesture_begins['redo']} "
        f"matched={analysis.matched_gesture_count} "
        f"mismatches={len(analysis.gesture_mismatches)} "
        f"unmatched-begin={len(analysis.unmatched_gesture_begins)} "
        "unmatched-complete="
        f"{len(analysis.unmatched_gesture_completions)}"
    )
    for kind in GESTURE_KINDS:
        values = analysis.gesture_completions[kind]
        if not values:
            continue
        summary = summarize_metric(values)
        lines.append(
            f"{kind}-gesture-ms p50={summary.p50:.1f} "
            f"p95={summary.p95:.1f} "
            f"max={summary.maximum}"
        )
    return "\n".join(lines)


def validation_failures(
    analysis: Analysis,
    *,
    require_samples: int = 0,
    require_gestures: int = 0,
    fail_on_unmatched: bool = False,
) -> list[str]:
    failures: list[str] = []
    if analysis.notebook_open_count < require_samples:
        failures.append(
            "notebook-open samples "
            f"{analysis.notebook_open_count} < required {require_samples}"
        )
    if analysis.matched_gesture_count < require_gestures:
        failures.append(
            "matched gestures "
            f"{analysis.matched_gesture_count} < required {require_gestures}"
        )
    if fail_on_unmatched and analysis.gesture_anomaly_count:
        failures.append(
            "gesture pairing anomalies "
            f"{analysis.gesture_anomaly_count} "
            "(mismatched or unmatched begin/complete)"
        )
    return failures


def non_negative_integer(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Summarize Kobo notebook performance and gesture logs."
    )
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument(
        "--json",
        action="store_true",
        dest="json_output",
        help="emit the complete machine-readable report as JSON",
    )
    parser.add_argument(
        "--require-samples",
        type=non_negative_integer,
        default=0,
        metavar="N",
        help="fail unless at least N valid notebook-open samples exist",
    )
    parser.add_argument(
        "--require-gestures",
        type=non_negative_integer,
        default=0,
        metavar="N",
        help="fail unless at least N gestures pair in order by action",
    )
    parser.add_argument(
        "--fail-on-unmatched",
        action="store_true",
        help=(
            "fail on any out-of-order or unmatched gesture begin/complete"
        ),
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_argument_parser()
    arguments = parser.parse_args(argv)
    try:
        analysis = analyze_logs(arguments.logs)
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    if arguments.json_output:
        print(json.dumps(analysis.to_dict(), indent=2, sort_keys=True))
    else:
        print(format_text(analysis))

    failures = validation_failures(
        analysis,
        require_samples=arguments.require_samples,
        require_gestures=arguments.require_gestures,
        fail_on_unmatched=arguments.fail_on_unmatched,
    )
    for failure in failures:
        print(f"validation failed: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
