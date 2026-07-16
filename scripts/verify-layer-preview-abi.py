#!/usr/bin/env python3
"""Verify the compiled ARM hard-float layer-preview listener trampoline."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys


SYMBOL_FRAGMENT = "LayerPreviewRendererListener::exportImage("


def find_objdump(explicit: str | None) -> str:
    candidates = [
        explicit,
        os.environ.get("LLVM_OBJDUMP"),
        shutil.which("llvm-objdump"),
        "/Library/Developer/CommandLineTools/usr/bin/llvm-objdump",
    ]
    try:
        xcrun = subprocess.run(
            ["xcrun", "-f", "llvm-objdump"],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        ).stdout.strip()
    except FileNotFoundError:
        xcrun = ""
    candidates.append(xcrun)
    for candidate in candidates:
        if candidate and pathlib.Path(candidate).is_file():
            return candidate
    raise RuntimeError("llvm-objdump not found; pass --objdump")


def function_body(disassembly: str) -> str:
    lines = disassembly.splitlines()
    start = None
    for index, line in enumerate(lines):
        if SYMBOL_FRAGMENT in line and re.match(r"^[0-9a-fA-F]+ <", line):
            start = index
            break
    if start is None:
        raise RuntimeError("preview listener function was not found")
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if re.match(r"^[0-9a-fA-F]+ <", lines[index]):
            end = index
            break
    return "\n".join(lines[start:end])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "plugin",
        nargs="?",
        type=pathlib.Path,
        default=pathlib.Path(
            "mods/custom-notebook-templates/libcustomnotebooktemplates.so"
        ),
    )
    parser.add_argument("--objdump")
    args = parser.parse_args()
    if not args.plugin.is_file():
        parser.error(f"plugin not found: {args.plugin}")

    try:
        objdump = find_objdump(args.objdump)
        output = subprocess.run(
            [objdump, "-d", "--demangle", str(args.plugin)],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout
        body = function_body(output)
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"Layer preview ABI verification FAILED: {exc}", file=sys.stderr)
        return 1

    failures: list[str] = []
    for register in range(4):
        if not re.search(rf"\bvstr\s+s{register}\b", body):
            failures.append(f"s{register} is not saved")
        if not re.search(rf"\bvldr\s+s{register}\b", body):
            failures.append(f"s{register} is not restored")
    if not re.search(r"\bblx\s+r\d+\b", body):
        failures.append("delegate is not called indirectly")

    forwarded = False
    loads = re.findall(r"\bldr(?:\.w)?\s+(r\d+),\s*\[sp,\s*#[^\]]+\]", body)
    for register in loads:
        if re.search(rf"\bstr(?:\.w)?\s+{register},\s*\[sp(?:,\s*#(?:0x)?0)?\]", body):
            forwarded = True
            break
    if not forwarded:
        failures.append("stack output-path argument is not forwarded")

    if failures:
        print("Layer preview ABI verification FAILED", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(
        "Layer preview ABI verified: s0-s3 and the stack path are preserved "
        "across the listener proxy"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
