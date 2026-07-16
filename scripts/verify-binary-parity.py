#!/usr/bin/env python3
"""Gate intentional ABI parity between two ARM plugin shared libraries.

Addresses and symbol sizes are deliberately not compared: both can move during a
source-only refactor without changing the loader-visible ABI.  Symbol binding/type
and versioned names are compared, as is DT_NEEDED order.  Section-size changes are
printed to make code/data growth visible, but are informational.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
from typing import Counter, Iterable, Sequence, Tuple


Symbol = Tuple[str, str]
SymbolCounts = Counter[Symbol]


class VerificationError(RuntimeError):
    """An input, tool, or child verifier could not be used safely."""


def executable(candidate: str | None) -> str | None:
    if not candidate:
        return None
    expanded = os.path.expanduser(candidate)
    if os.sep in expanded:
        path = pathlib.Path(expanded)
        if path.is_file() and os.access(str(path), os.X_OK):
            return str(path.resolve())
        return None
    return shutil.which(expanded)


def xcrun_tool(name: str) -> str | None:
    try:
        result = subprocess.run(
            ["xcrun", "-f", name],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except FileNotFoundError:
        return None
    if result.returncode != 0:
        return None
    return executable(result.stdout.strip())


def find_tool(
    name: str,
    explicit: str | None = None,
    tool_prefix: str | None = None,
) -> str:
    """Find a binutils/LLVM tool, preferring configured cross tools."""
    if explicit:
        resolved = executable(explicit)
        if not resolved:
            raise VerificationError(f"configured {name} is not executable: {explicit}")
        return resolved

    configured = os.environ.get(name.upper())
    if configured:
        resolved = executable(configured)
        if not resolved:
            raise VerificationError(
                f"${name.upper()} is not executable: {configured}"
            )
        return resolved

    prefixes: list[str] = []
    for prefix in (
        tool_prefix,
        os.environ.get("CROSS_COMPILE"),
        os.environ.get("TOOL_PREFIX"),
        "arm-nickel-linux-gnueabihf-",
    ):
        if prefix and prefix not in prefixes:
            prefixes.append(prefix)
    for prefix in prefixes:
        resolved = executable(prefix + name)
        if resolved:
            return resolved
    if tool_prefix:
        raise VerificationError(
            f"{tool_prefix + name} was not found for --tool-prefix {tool_prefix}"
        )

    for candidate in (f"llvm-{name}", name):
        resolved = executable(candidate)
        if resolved:
            return resolved
        resolved = xcrun_tool(candidate)
        if resolved:
            return resolved
    raise VerificationError(
        f"could not find {name}; pass --{name}, set ${name.upper()}, "
        "or pass --tool-prefix/CROSS_COMPILE"
    )


def run_tool(command: Sequence[str]) -> str:
    try:
        result = subprocess.run(
            list(command),
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        raise VerificationError(f"could not run {command[0]}: {exc}") from exc
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic"
        raise VerificationError(
            f"{' '.join(command)} failed with exit {result.returncode}: {detail}"
        )
    return result.stdout


def validate_arm_shared_object(path: pathlib.Path) -> None:
    try:
        header = path.read_bytes()[:20]
    except OSError as exc:
        raise VerificationError(f"cannot read {path}: {exc}") from exc
    if len(header) < 20 or header[:4] != b"\x7fELF":
        raise VerificationError(f"not an ELF file: {path}")
    if header[4] != 1 or header[5] != 1:
        raise VerificationError(f"not a 32-bit little-endian ELF: {path}")
    elf_type, machine = struct.unpack_from("<HH", header, 16)
    if elf_type != 3:
        raise VerificationError(f"not an ELF shared object (ET_DYN): {path}")
    if machine != 40:
        raise VerificationError(f"not an ARM ELF (e_machine={machine}): {path}")


NM_LINE = re.compile(
    r"^\s*(?:[0-9a-fA-F]+\s+)?(?P<kind>[A-Za-z?])\s+(?P<name>\S+)\s*$"
)


def parse_nm(output: str) -> SymbolCounts:
    symbols: SymbolCounts = collections.Counter()
    for line in output.splitlines():
        match = NM_LINE.match(line)
        if match:
            symbols[(match.group("kind"), match.group("name"))] += 1
    return symbols


def dynamic_symbols(nm: str, path: pathlib.Path, defined: bool) -> SymbolCounts:
    mode = "--defined-only" if defined else "--undefined-only"
    return parse_nm(run_tool([nm, "-D", mode, str(path)]))


NEEDED_LINE = re.compile(r"^\s*NEEDED\s+(?P<name>\S+)\s*$")
SECTION_LINE = re.compile(
    r"^\s*\d+\s+(?P<name>\S+)\s+(?P<size>[0-9a-fA-F]+)(?:\s|$)"
)


def parse_needed(output: str) -> list[str]:
    result: list[str] = []
    for line in output.splitlines():
        match = NEEDED_LINE.match(line)
        if match:
            result.append(match.group("name"))
    return result


def parse_sections(output: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in output.splitlines():
        match = SECTION_LINE.match(line)
        if not match:
            continue
        name = match.group("name")
        if name in result:
            raise VerificationError(f"duplicate section name in objdump output: {name}")
        result[name] = int(match.group("size"), 16)
    if not result:
        raise VerificationError("objdump returned no parseable section headers")
    return result


def needed_libraries(objdump: str, path: pathlib.Path) -> list[str]:
    return parse_needed(run_tool([objdump, "-p", str(path)]))


def section_sizes(objdump: str, path: pathlib.Path) -> dict[str, int]:
    return parse_sections(run_tool([objdump, "-h", str(path)]))


def symbol_label(symbol: Symbol) -> str:
    kind, name = symbol
    return f"{kind} {name}"


def expanded_difference(left: SymbolCounts, right: SymbolCounts) -> Iterable[Symbol]:
    for symbol in sorted((left - right).elements(), key=lambda item: (item[1], item[0])):
        yield symbol


def report_symbol_difference(
    title: str,
    reference: SymbolCounts,
    candidate: SymbolCounts,
) -> bool:
    if reference == candidate:
        print(f"PASS {title}: {sum(reference.values())} entries")
        return True
    print(f"FAIL {title}", file=sys.stderr)
    for symbol in expanded_difference(reference, candidate):
        print(f"  reference only: {symbol_label(symbol)}", file=sys.stderr)
    for symbol in expanded_difference(candidate, reference):
        print(f"  candidate only: {symbol_label(symbol)}", file=sys.stderr)
    return False


def report_needed_difference(reference: list[str], candidate: list[str]) -> bool:
    if reference == candidate:
        print(f"PASS DT_NEEDED: {len(reference)} entries (including order)")
        return True
    print("FAIL DT_NEEDED differs (order is significant)", file=sys.stderr)
    width = max(len(reference), len(candidate))
    for index in range(width):
        old = reference[index] if index < len(reference) else "<missing>"
        new = candidate[index] if index < len(candidate) else "<missing>"
        marker = " " if old == new else "!"
        print(f"  {marker} [{index}] {old} -> {new}", file=sys.stderr)
    return False


def report_section_drift(reference: dict[str, int], candidate: dict[str, int]) -> None:
    names = sorted(set(reference) | set(candidate))
    drift = [name for name in names if reference.get(name) != candidate.get(name)]
    if not drift:
        print(f"INFO section sizes: no drift across {len(names)} sections")
        return
    print("INFO section-size drift (informational only):")
    for name in drift:
        old = reference.get(name)
        new = candidate.get(name)
        old_label = "<missing>" if old is None else f"0x{old:x} ({old})"
        new_label = "<missing>" if new is None else f"0x{new:x} ({new})"
        delta = ""
        if old is not None and new is not None:
            delta = f"; delta {new - old:+d}"
        print(f"  {name}: {old_label} -> {new_label}{delta}")


def run_preview_verifier(
    script: pathlib.Path,
    objdump: str,
    libraries: Sequence[pathlib.Path],
) -> bool:
    ok = True
    for label, library in zip(("reference", "candidate"), libraries):
        command = [
            sys.executable,
            str(script),
            str(library),
            "--objdump",
            objdump,
        ]
        result = subprocess.run(
            command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        output = (result.stdout + result.stderr).strip()
        if result.returncode == 0:
            print(f"PASS pinned plugin ABI ({label}): {output}")
        else:
            ok = False
            print(f"FAIL pinned plugin ABI ({label})", file=sys.stderr)
            for line in output.splitlines() or ["verifier returned no diagnostic"]:
                print(f"  {line}", file=sys.stderr)
    return ok


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare the loader-visible ABI of a reference and candidate ARM plugin."
        ),
        epilog=(
            "The adjacent verify-layer-preview-abi.py is run on both files by "
            "default. Firmware pins remain covered by verify-layer-abi.py, which "
            "should be run separately in the full refactor gate."
        ),
    )
    parser.add_argument("reference", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--tool-prefix", help="cross-tool prefix, including trailing -")
    parser.add_argument("--nm", help="path/name of nm or llvm-nm")
    parser.add_argument("--objdump", help="path/name of objdump or llvm-objdump")
    parser.add_argument(
        "--skip-pinned-plugin-abi",
        action="store_true",
        help="skip verify-layer-preview-abi.py (primarily for verifier tests)",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        for path in (args.reference, args.candidate):
            if not path.is_file():
                raise VerificationError(f"shared library not found: {path}")
            validate_arm_shared_object(path)

        nm = find_tool("nm", args.nm, args.tool_prefix)
        objdump = find_tool("objdump", args.objdump, args.tool_prefix)
        print(f"Tools: nm={nm}; objdump={objdump}")
        print(f"Reference: {args.reference} (sha256 {sha256(args.reference)})")
        print(f"Candidate: {args.candidate} (sha256 {sha256(args.candidate)})")

        reference_exports = dynamic_symbols(nm, args.reference, True)
        candidate_exports = dynamic_symbols(nm, args.candidate, True)
        reference_imports = dynamic_symbols(nm, args.reference, False)
        candidate_imports = dynamic_symbols(nm, args.candidate, False)
        reference_needed = needed_libraries(objdump, args.reference)
        candidate_needed = needed_libraries(objdump, args.candidate)
        reference_sections = section_sizes(objdump, args.reference)
        candidate_sections = section_sizes(objdump, args.candidate)

        checks = [
            report_symbol_difference(
                "dynamic defined exports", reference_exports, candidate_exports
            ),
            report_symbol_difference(
                "dynamic undefined imports", reference_imports, candidate_imports
            ),
            report_needed_difference(reference_needed, candidate_needed),
        ]
        report_section_drift(reference_sections, candidate_sections)

        if not args.skip_pinned_plugin_abi:
            verifier = pathlib.Path(__file__).with_name("verify-layer-preview-abi.py")
            if not verifier.is_file():
                raise VerificationError(f"pinned plugin ABI verifier not found: {verifier}")
            checks.append(
                run_preview_verifier(
                    verifier, objdump, (args.reference, args.candidate)
                )
            )
    except VerificationError as exc:
        print(f"Binary parity verification ERROR: {exc}", file=sys.stderr)
        return 2

    if not all(checks):
        print("Binary parity verification FAILED", file=sys.stderr)
        return 1
    print("Binary parity verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
