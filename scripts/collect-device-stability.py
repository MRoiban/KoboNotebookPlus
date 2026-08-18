#!/usr/bin/env python3
"""Collect read-only post-test evidence from a mounted Kobo filesystem.

The Kobo USB filesystem does not expose /usr/local/Kobo, so this tool does not
claim to verify the installed plugin binary.  It checks the evidence which is
available on /mnt/onboard, optionally copies bounded status logs to a host-only
snapshot directory, and runs the repository's read-only validators.
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass, field
import hashlib
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
from typing import Iterable


SCRIPT_ROOT = Path(__file__).resolve().parent
SUPPORT_ROOT = Path(".kobo/iink/custom-notebook-templates")
MAX_LOG_BYTES = 16 * 1024 * 1024

STARTUP_MARKER = "plugin library loaded"
READY_MARKER = "delayed hook initialization complete"
INIT_MARKER = "NickelHook init entered"
WAIT_MARKER = "NickelHook init complete; waiting for natural libiinknote load"
GESTURE_BEGIN = re.compile(
    r"notebook-gesture: (undo|redo) dispatch begin"
)
GESTURE_COMPLETE = re.compile(
    r"notebook-gesture: (undo|redo) dispatch complete ms=(\d+)"
)
CRASH_INDICATOR = re.compile(
    r"\b(?:"
    r"crash(?:ed|ing)?|fatal|segfault|segmentation fault|abort(?:ed)?|"
    r"assert(?:ion)?|terminate(?:d)?|uncaught|backtrace|stack trace|"
    r"signal (?:6|11)"
    r")\b",
    re.IGNORECASE,
)
ERROR_INDICATOR = re.compile(
    r"\b(?:error|failed|failure|invalid|mismatch|unavailable)\b|\bthrew\b",
    re.IGNORECASE,
)


@dataclass
class Findings:
    issues: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def issue(self, message: str) -> None:
        self.issues.append(message)
        print(f"FAIL: {message}")

    def warning(self, message: str) -> None:
        self.warnings.append(message)
        print(f"WARNING: {message}")

    @staticmethod
    def passed(message: str) -> None:
        print(f"PASS: {message}")

    @staticmethod
    def info(message: str) -> None:
        print(f"INFO: {message}")


@dataclass
class LogEvidence:
    label: str
    source: Path
    text: str
    analysis_path: Path
    sha256: str


def is_within(path: Path, parent: Path) -> bool:
    """Return whether path is parent itself or one of its descendants."""

    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def is_regular_nonsymlink(path: Path) -> bool:
    try:
        metadata = path.lstat()
    except OSError:
        return False
    return stat.S_ISREG(metadata.st_mode) and not path.is_symlink()


def unique_paths(paths: Iterable[Path]) -> list[Path]:
    result: list[Path] = []
    seen: set[str] = set()
    for path in paths:
        key = os.path.normcase(str(path))
        if key not in seen:
            seen.add(key)
            result.append(path)
    return result


def find_status_logs(root: Path, name: str) -> list[Path]:
    """Find a status log in only the plugin's bounded support trees."""

    support_roots = (
        root / SUPPORT_ROOT,
        root / ".kobo" / "custom",
    )
    direct = [
        support_roots[0] / "templates" / name,
        support_roots[0] / name,
        support_roots[1] / "templates" / name,
        support_roots[1] / name,
    ]
    discovered: list[Path] = []
    for support in support_roots:
        if support.is_dir():
            try:
                discovered.extend(sorted(support.rglob(name)))
            except OSError:
                # The direct candidates can still produce a useful diagnostic.
                pass
    return [
        path
        for path in unique_paths(direct + discovered)
        if path.exists()
    ]


def read_bounded_log(
    label: str,
    path: Path,
    findings: Findings,
) -> tuple[str, str] | None:
    if not is_regular_nonsymlink(path):
        findings.issue(f"{label} status log is not a regular file: {path}")
        return None
    try:
        size = path.stat().st_size
    except OSError as exc:
        findings.issue(f"cannot stat {label} status log: {exc}")
        return None
    if size > MAX_LOG_BYTES:
        findings.issue(
            f"{label} status log exceeds the {MAX_LOG_BYTES}-byte safety "
            f"limit ({size} bytes): {path}"
        )
        return None
    try:
        contents = path.read_bytes()
    except OSError as exc:
        findings.issue(f"cannot read {label} status log: {exc}")
        return None
    return (
        contents.decode("utf-8", errors="replace"),
        hashlib.sha256(contents).hexdigest(),
    )


def copy_log_to_host(
    label: str,
    source: Path,
    output: Path,
    findings: Findings,
) -> Path | None:
    destination = output / (
        "plugin-status.previous.txt"
        if label == "previous"
        else "plugin-status.txt"
    )
    if destination.exists():
        findings.issue(
            f"snapshot destination already exists; refusing to overwrite: "
            f"{destination}"
        )
        return None
    try:
        shutil.copyfile(source, destination)
    except OSError as exc:
        findings.issue(f"could not copy {label} status log to host: {exc}")
        return None
    Findings.passed(f"copied {label} status log to host: {destination}")
    return destination


def report_indicator_lines(
    label: str,
    category: str,
    matches: list[tuple[int, str]],
) -> None:
    if not matches:
        return
    print(f"  {label} {category} ({len(matches)}):")
    for line_number, line in matches[:12]:
        compact = line.strip().replace("\t", " ")
        if len(compact) > 240:
            compact = compact[:237] + "..."
        print(f"    line {line_number}: {compact}")
    if len(matches) > 12:
        print(f"    ... {len(matches) - 12} more")


def analyze_log(
    label: str,
    text: str,
    findings: Findings,
    require_ready: bool,
) -> None:
    lines = text.splitlines()
    startup = any(STARTUP_MARKER in line for line in lines)
    initialized = any(INIT_MARKER in line for line in lines)
    waiting = any(WAIT_MARKER in line for line in lines)
    ready = any(READY_MARKER in line for line in lines)

    if startup:
        Findings.passed(f"{label} session has the plugin-load marker")
    elif require_ready:
        findings.issue(f"{label} session lacks the plugin-load marker")
    else:
        findings.warning(f"{label} session lacks the plugin-load marker")

    if initialized:
        Findings.passed(f"{label} session entered NickelHook initialization")
    elif require_ready:
        findings.issue(f"{label} session lacks the NickelHook init marker")
    else:
        findings.warning(f"{label} session lacks the NickelHook init marker")

    if waiting:
        Findings.passed(
            f"{label} session reached the natural-libiinknote wait milestone"
        )
    elif require_ready:
        findings.issue(
            f"{label} session lacks the natural-libiinknote wait marker"
        )
    else:
        findings.warning(
            f"{label} session lacks the natural-libiinknote wait marker"
        )

    if ready:
        Findings.passed(f"{label} session completed delayed hook initialization")
    elif require_ready:
        findings.issue(f"{label} session lacks the delayed-hook readiness marker")
    else:
        findings.warning(
            f"{label} session lacks the delayed-hook readiness marker"
        )

    pending: deque[tuple[str, int]] = deque()
    completed = {"undo": [], "redo": []}
    ordering_errors: list[str] = []
    crash_lines: list[tuple[int, str]] = []
    error_lines: list[tuple[int, str]] = []

    for line_number, line in enumerate(lines, start=1):
        began = GESTURE_BEGIN.search(line)
        if began:
            pending.append((began.group(1), line_number))

        complete = GESTURE_COMPLETE.search(line)
        if complete:
            kind = complete.group(1)
            completed[kind].append(int(complete.group(2)))
            if not pending:
                ordering_errors.append(
                    f"{kind} completion at line {line_number} has no begin"
                )
            elif pending[0][0] == kind:
                pending.popleft()
            else:
                expected, begin_line = pending.popleft()
                ordering_errors.append(
                    f"{kind} completion at line {line_number} follows "
                    f"{expected} begin at line {begin_line}"
                )

        if CRASH_INDICATOR.search(line):
            crash_lines.append((line_number, line))
        elif ERROR_INDICATOR.search(line):
            error_lines.append((line_number, line))

    if pending:
        detail = ", ".join(
            f"{kind} at line {line_number}"
            for kind, line_number in pending
        )
        findings.issue(
            f"{label} session has {len(pending)} gesture dispatch begin(s) "
            f"without completion: {detail}"
        )
    if ordering_errors:
        findings.issue(
            f"{label} session has {len(ordering_errors)} gesture ordering "
            "error(s): " + "; ".join(ordering_errors[:4])
        )
    if not pending and not ordering_errors:
        Findings.passed(
            f"{label} gesture dispatches are balanced "
            f"(undo={len(completed['undo'])}, redo={len(completed['redo'])})"
        )

    if crash_lines:
        findings.issue(
            f"{label} session contains {len(crash_lines)} explicit "
            "crash/fatal indicator(s)"
        )
    if error_lines:
        findings.warning(
            f"{label} session contains {len(error_lines)} nonfatal "
            "error/fallback indicator(s)"
        )
    report_indicator_lines(label, "crash/fatal indicators", crash_lines)
    report_indicator_lines(label, "error/fallback indicators", error_lines)


def print_subprocess_output(label: str, output: str, error: str) -> None:
    print(f"{label}:")
    if output.strip():
        for line in output.rstrip().splitlines():
            print(f"  {line}")
    if error.strip():
        for line in error.rstrip().splitlines():
            print(f"  stderr: {line}")
    if not output.strip() and not error.strip():
        print("  (no output)")


def run_helper(
    label: str,
    command: list[str],
    findings: Findings,
    timeout: int = 120,
) -> subprocess.CompletedProcess[str] | None:
    try:
        result = subprocess.run(
            command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        findings.issue(f"{label} could not run: {exc}")
        return None

    print_subprocess_output(label, result.stdout, result.stderr)
    if result.returncode:
        findings.issue(f"{label} exited with status {result.returncode}")
    else:
        Findings.passed(f"{label} completed")
    return result


def check_install_payload(root: Path, findings: Findings) -> None:
    update_root = root / ".kobo"
    staging = sorted(update_root.glob("*.installing"))
    pending = unique_paths(
        [
            path
            for path in (
                update_root / "KoboRoot.tgz",
                *staging,
            )
            if path.exists() and not path.name.startswith("._")
        ]
    )
    if pending:
        findings.issue(
            "installer payload is still pending on the onboard filesystem: "
            + ", ".join(str(path) for path in pending)
        )
    else:
        Findings.passed(
            "no KoboRoot.tgz or *.installing payload remains in .kobo "
            "(the updater consumed/attempted it, or it was absent)"
        )
    appledouble = unique_paths(
        [
            path
            for path in (
                update_root / "._KoboRoot.tgz",
                *staging,
            )
            if path.exists() and path.name.startswith("._")
        ]
    )
    if appledouble:
        findings.warning(
            "installer AppleDouble metadata remains (not an install "
            "payload): " + ", ".join(str(path) for path in appledouble)
        )
    Findings.info(
        "USB exposes only the onboard filesystem; this check cannot verify "
        "successful extraction or the installed /usr/local/Kobo plugin "
        "binary/hash because Kobo removes the payload even after some updater "
        "failures"
    )


def check_nickelhook_fatal_logs(
    root: Path,
    output: Path | None,
    findings: Findings,
) -> None:
    """Report NickelHook's root-level early-initialization failure dumps."""

    candidates = unique_paths(
        [
            path
            for pattern in (
                "CustomNotebookTemplates*.log",
                "NickelHook*.log",
            )
            for path in sorted(root.glob(pattern))
            if path.exists()
        ]
    )
    if not candidates:
        Findings.passed("no root-level NickelHook fatal-init log was found")
        return

    for index, source in enumerate(candidates, start=1):
        label = f"NickelHook fatal-init log {source.name}"
        result = read_bounded_log(label, source, findings)
        if result is None:
            continue
        text, digest = result
        findings.issue(
            f"root-level NickelHook fatal-init evidence exists: {source} "
            f"(sha256={digest})"
        )
        lines = [
            (line_number, line)
            for line_number, line in enumerate(text.splitlines(), start=1)
            if line.strip()
        ]
        report_indicator_lines(
            label,
            "contents",
            lines,
        )
        if output is None:
            continue
        destination = output / source.name
        if destination.exists():
            findings.issue(
                "snapshot destination already exists; refusing to overwrite: "
                f"{destination}"
            )
            continue
        try:
            shutil.copyfile(source, destination)
        except OSError as exc:
            findings.issue(f"could not copy {label} to host: {exc}")
        else:
            Findings.passed(
                f"copied {label} to host as evidence #{index}: {destination}"
            )


def collect(root: Path, output: Path | None) -> int:
    findings = Findings()
    print("Kobo post-test stability validation (device reads only)")
    print(f"Mounted root: {root}")
    if output is not None:
        print(f"Host snapshot: {output}")

    check_install_payload(root, findings)

    if output is not None:
        try:
            output.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            findings.issue(f"cannot create host snapshot directory: {exc}")
            output = None

    check_nickelhook_fatal_logs(root, output, findings)

    logs: list[LogEvidence] = []
    for label, name in (
        ("previous", "plugin-status.previous.txt"),
        ("current", "plugin-status.txt"),
    ):
        candidates = find_status_logs(root, name)
        if not candidates:
            if label == "current":
                findings.issue("current plugin status log was not found")
            else:
                findings.warning("previous plugin status log was not found")
            continue
        source = candidates[0]
        if len(candidates) > 1:
            findings.warning(
                f"multiple {label} status logs found; using {source}"
            )
        Findings.info(f"{label} status log: {source}")
        result = read_bounded_log(label, source, findings)
        if result is None:
            continue
        text, digest = result
        Findings.info(f"{label} status log sha256={digest}")
        analysis_path = source
        if output is not None:
            copied = copy_log_to_host(
                label, source, output, findings
            )
            if copied is not None:
                analysis_path = copied
        logs.append(
            LogEvidence(
                label=label,
                source=source,
                text=text,
                analysis_path=analysis_path,
                sha256=digest,
            )
        )
        analyze_log(
            label,
            text,
            findings,
            require_ready=(label == "current"),
        )

    summarizer = SCRIPT_ROOT / "summarize-notebook-performance.py"
    if logs and summarizer.is_file():
        # Keep the integration deliberately minimal: positional log paths are
        # the summarizer's stable interface, while its human/JSON output and
        # optional threshold flags may evolve independently.
        run_helper(
            "Notebook performance summary",
            [
                sys.executable,
                str(summarizer),
                *[str(log.analysis_path) for log in logs],
            ],
            findings,
        )
    elif not logs:
        findings.warning("performance summary skipped because no logs were readable")
    else:
        findings.warning(f"performance summarizer is unavailable: {summarizer}")

    layer_verifier = SCRIPT_ROOT / "verify-layer-artifacts.py"
    if layer_verifier.is_file():
        run_helper(
            "Layer artifact verification",
            [
                sys.executable,
                str(layer_verifier),
                "--allow-empty",
                str(root),
            ],
            findings,
        )
    else:
        findings.warning(f"layer verifier is unavailable: {layer_verifier}")

    cache_seeder = SCRIPT_ROOT / "seed-generated-asset-cache.py"
    support = root / SUPPORT_ROOT
    if cache_seeder.is_file() and support.is_dir():
        result = run_helper(
            "Generated asset-cache dry-run",
            [sys.executable, str(cache_seeder), str(root)],
            findings,
        )
        if result is not None and result.returncode == 0:
            would_write = re.search(r"\bwould-write=(\d+)\b", result.stdout)
            if would_write and int(would_write.group(1)) > 0:
                findings.warning(
                    f"asset-cache dry-run reported {would_write.group(1)} "
                    "would-write stamp(s); FAT/Qt versus host-local mtime "
                    "representation can cause this report even when all "
                    "fingerprints match, and the collector left them unchanged"
                )
    elif not support.is_dir():
        findings.warning(
            f"asset-cache dry-run skipped; support directory not found: {support}"
        )
    else:
        findings.warning(f"asset-cache seeder is unavailable: {cache_seeder}")

    if findings.issues:
        print(
            f"RESULT: FAIL ({len(findings.issues)} issue(s), "
            f"{len(findings.warnings)} warning(s)); mounted device was not written"
        )
        return 1
    print(
        f"RESULT: PASS ({len(findings.warnings)} warning(s)); "
        "mounted device was not written"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Read-only post-test validation for a mounted Kobo. Optionally "
            "copy plugin status logs to a host snapshot directory."
        )
    )
    parser.add_argument("root", type=Path, help="mounted Kobo filesystem root")
    parser.add_argument(
        "--output",
        "--snapshot-dir",
        dest="output",
        type=Path,
        help=(
            "host-only directory for copied current/previous status logs "
            "(existing log files are never overwritten)"
        ),
    )
    arguments = parser.parse_args()

    root = arguments.root.expanduser().resolve()
    if not root.is_dir():
        print(f"ERROR: mounted root is not a directory: {root}", file=sys.stderr)
        return 2
    if not (root / ".kobo").is_dir():
        print(
            f"ERROR: mounted root does not contain a .kobo directory: {root}",
            file=sys.stderr,
        )
        return 2

    output = None
    if arguments.output is not None:
        output = arguments.output.expanduser().resolve()
        if is_within(output, root):
            print(
                "ERROR: snapshot output must be outside the mounted Kobo "
                f"filesystem: {output}",
                file=sys.stderr,
            )
            return 2
        if output.exists() and not output.is_dir():
            print(
                f"ERROR: snapshot output is not a directory: {output}",
                file=sys.stderr,
            )
            return 2

    return collect(root, output)


if __name__ == "__main__":
    raise SystemExit(main())
