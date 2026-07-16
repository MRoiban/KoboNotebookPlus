#!/usr/bin/env python3
"""Verify the Phase 2 namespace-scope storage budget.

The old ``grep '^static '`` metric mixed functions, immutable constants, and
mutable storage.  This verifier performs a deliberately small, fail-closed
lexical analysis of the umbrella translation unit's ``.cc.inc`` fragments.
It does not need Qt headers or a host C++ compiler.

Only declarations at fragment depth zero are considered.  The umbrella
includes those fragments directly inside its anonymous namespace, so depth
zero in a fragment is namespace scope in the effective translation unit.
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys
from typing import Iterable, Sequence


DEFAULT_SOURCE_ROOT = (
    pathlib.Path(__file__).resolve().parents[1]
    / "mods"
    / "custom-notebook-templates"
    / "src"
)
UMBRELLA_NAME = "customnotebooktemplates.cc"

EXPECTED_MUTABLE = frozenset(("gPluginState", "info"))
EXPECTED_MUTEXES = frozenset()
EXPECTED_IMMUTABLE_COUNT = 294
EXPECTED_FUNCTION_COUNT = 132
EXPECTED_FRAMEWORK_GLOBALS = ("NickelHook",)

IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
INCLUDE_FRAGMENT = re.compile(
    r'^\s*#\s*include\s+"(?P<path>[^"]+\.cc\.inc)"\s*$', re.MULTILINE
)
MUTEX_TYPES = frozenset(("QMutex",))


class VerificationError(RuntimeError):
    """The source could not be audited unambiguously."""


@dataclasses.dataclass(frozen=True)
class Declaration:
    path: pathlib.Path
    line: int
    name: str
    category: str
    text: str


@dataclasses.dataclass(frozen=True)
class Audit:
    declarations: tuple[Declaration, ...]
    framework_globals: tuple[str, ...]

    def names(self, category: str) -> frozenset[str]:
        return frozenset(
            declaration.name
            for declaration in self.declarations
            if declaration.category == category
        )

    def count(self, category: str) -> int:
        return sum(
            declaration.category == category
            for declaration in self.declarations
        )


def _blank_range(output: list[str], start: int, end: int) -> None:
    for index in range(start, end):
        if output[index] != "\n":
            output[index] = " "


def mask_cpp(source: str) -> str:
    """Blank comments and literals while preserving offsets and newlines."""
    output = list(source)
    index = 0
    length = len(source)
    while index < length:
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            if end < 0:
                end = length
            _blank_range(output, index, end)
            index = end
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            if end < 0:
                raise VerificationError("unterminated block comment")
            end += 2
            _blank_range(output, index, end)
            index = end
            continue

        # C++ raw string literal.  Prefixes such as u8R still arrive here at R.
        if source.startswith('R"', index):
            delimiter_end = source.find("(", index + 2, index + 19)
            if delimiter_end < 0:
                raise VerificationError("malformed raw string literal")
            delimiter = source[index + 2 : delimiter_end]
            terminator = ")" + delimiter + '"'
            end = source.find(terminator, delimiter_end + 1)
            if end < 0:
                raise VerificationError("unterminated raw string literal")
            end += len(terminator)
            _blank_range(output, index, end)
            index = end
            continue

        quote = source[index]
        if quote not in ('"', "'"):
            index += 1
            continue
        end = index + 1
        while end < length:
            if source[end] == "\\":
                end += 2
                continue
            if source[end] == quote:
                end += 1
                break
            end += 1
        else:
            raise VerificationError("unterminated quoted literal")
        _blank_range(output, index, end)
        index = end

    # Preprocessor directives are not declarations.  Mask continuations too.
    masked = "".join(output)
    lines = masked.splitlines(keepends=True)
    continuation = False
    for line_index, line in enumerate(lines):
        directive = continuation or re.match(r"^\s*#", line) is not None
        if not directive:
            continue
        continuation = line.rstrip("\n").rstrip().endswith("\\")
        lines[line_index] = "".join("\n" if char == "\n" else " " for char in line)
    return "".join(lines)


def _word_at(source: str, index: int, word: str) -> bool:
    if not source.startswith(word, index):
        return False
    before = source[index - 1] if index else ""
    after_index = index + len(word)
    after = source[after_index] if after_index < len(source) else ""
    return not (before.isalnum() or before == "_") and not (
        after.isalnum() or after == "_"
    )


def _top_level_function_name(head: str) -> tuple[str, int] | None:
    """Return a free-function declarator name from a declaration head."""
    paren_depth = 0
    square_depth = 0
    candidates: list[tuple[str, int]] = []
    index = 0
    while index < len(head):
        char = head[index]
        if char == "[":
            square_depth += 1
        elif char == "]":
            square_depth -= 1
        elif char == "(" and paren_depth == 0 and square_depth == 0:
            prefix = head[:index]
            match = re.search(r"([A-Za-z_]\w*)\s*$", prefix)
            if match and match.group(1) not in {
                "alignof",
                "decltype",
                "noexcept",
                "sizeof",
                "__attribute__",
            }:
                candidates.append((match.group(1), match.start(1)))
            paren_depth += 1
        elif char == "(":
            paren_depth += 1
        elif char == ")":
            paren_depth -= 1
        index += 1
    if paren_depth or square_depth:
        raise VerificationError("unbalanced function declarator")
    return candidates[-1] if candidates else None


def _is_function_declarator(head: str) -> bool:
    # ``Return (*callback)(Args)`` is storage, despite its parameter list.
    if re.search(r"\(\s*\*\s*(?:const\s+)?[A-Za-z_]\w*\s*\)\s*\(", head):
        return False
    return _top_level_function_name(head) is not None


def _scan_static_declaration(
    masked: str, source: str, start: int, path: pathlib.Path
) -> tuple[Declaration, int]:
    index = start + len("static")
    paren_depth = 0
    square_depth = 0
    initializer_braces = 0
    saw_top_level_equal = False
    head_end: int | None = None

    while index < len(masked):
        char = masked[index]
        if char == "(":
            paren_depth += 1
        elif char == ")":
            paren_depth -= 1
        elif char == "[":
            square_depth += 1
        elif char == "]":
            square_depth -= 1
        elif char == "=" and not (paren_depth or square_depth or initializer_braces):
            saw_top_level_equal = True
            if head_end is None:
                head_end = index
        elif char == "{" and not (paren_depth or square_depth):
            if initializer_braces:
                initializer_braces += 1
            else:
                candidate_head = masked[start:index]
                if not saw_top_level_equal and _is_function_declarator(candidate_head):
                    name_data = _top_level_function_name(candidate_head)
                    if name_data is None:
                        raise VerificationError("function name could not be parsed")
                    line = source.count("\n", 0, start) + 1
                    return (
                        Declaration(
                            path,
                            line,
                            name_data[0],
                            "function",
                            source[start:index].strip(),
                        ),
                        index,
                    )
                initializer_braces = 1
                if head_end is None:
                    head_end = index
        elif char == "}" and initializer_braces:
            initializer_braces -= 1
        elif char == ";" and not (paren_depth or square_depth or initializer_braces):
            end = index + 1
            if _top_level_commas(masked[start:index]):
                raise VerificationError(
                    "multiple namespace-scope declarators in one statement are unsupported"
                )
            head = masked[start : head_end if head_end is not None else index]
            if _is_function_declarator(head):
                name_data = _top_level_function_name(head)
                if name_data is None:
                    raise VerificationError("function name could not be parsed")
                category = "function"
                name = name_data[0]
            else:
                name, name_offset = _object_name(head)
                category = _object_category(head, name_offset)
            line = source.count("\n", 0, start) + 1
            return Declaration(path, line, name, category, source[start:end].strip()), end
        if paren_depth < 0 or square_depth < 0:
            raise VerificationError("unbalanced declaration delimiters")
        index += 1
    raise VerificationError("unterminated static declaration")


def _top_level_commas(head: str) -> list[int]:
    paren = square = braces = angle = 0
    commas: list[int] = []
    for index, char in enumerate(head):
        if char == "(":
            paren += 1
        elif char == ")":
            paren -= 1
        elif char == "[":
            square += 1
        elif char == "]":
            square -= 1
        elif char == "{":
            braces += 1
        elif char == "}":
            braces -= 1
        elif char == "<" and not (paren or square or braces):
            angle += 1
        elif char == ">" and angle and not (paren or square or braces):
            angle -= 1
        elif char == "," and not (paren or square or braces or angle):
            commas.append(index)
    return commas


def _object_name(head: str) -> tuple[str, int]:
    if _top_level_commas(head):
        raise VerificationError(
            "multiple namespace-scope declarators in one statement are unsupported"
        )

    function_pointer = re.search(
        r"\(\s*\*\s*(?:const\s+)?(?P<name>[A-Za-z_]\w*)\s*\)\s*\(", head
    )
    if function_pointer:
        return function_pointer.group("name"), function_pointer.start("name")

    paren_depth = 0
    for index, char in enumerate(head):
        if char == "(":
            paren_depth += 1
        elif char == ")":
            paren_depth -= 1
        elif char == "[" and paren_depth == 0:
            match = re.search(r"([A-Za-z_]\w*)\s*$", head[:index])
            if not match:
                raise VerificationError("array declarator name could not be parsed")
            return match.group(1), match.start(1)

    identifiers = list(IDENTIFIER.finditer(head))
    if not identifiers:
        raise VerificationError("object declarator name could not be parsed")
    match = identifiers[-1]
    return match.group(), match.start()


def _top_level_type_tokens(prefix: str) -> list[str]:
    """Return identifiers and pointer/reference marks outside template args."""
    tokens: list[str] = []
    angle_depth = 0
    index = 0
    while index < len(prefix):
        char = prefix[index]
        if char == "<":
            angle_depth += 1
            index += 1
            continue
        if char == ">" and angle_depth:
            angle_depth -= 1
            index += 1
            continue
        match = IDENTIFIER.match(prefix, index)
        if match:
            if angle_depth == 0:
                tokens.append(match.group())
            index = match.end()
            continue
        if angle_depth == 0 and char in "*&":
            tokens.append(char)
        index += 1
    return tokens


def _object_category(head: str, name_offset: int) -> str:
    prefix = head[:name_offset]
    all_type_names = set(IDENTIFIER.findall(prefix))
    if all_type_names & MUTEX_TYPES:
        return "mutex"

    tokens = _top_level_type_tokens(prefix)
    if "constexpr" in tokens:
        return "immutable"

    pointer_positions = [index for index, token in enumerate(tokens) if token == "*"]
    if pointer_positions:
        # ``T const* p`` has a mutable pointer; ``T* const p`` does not.
        return (
            "immutable"
            if "const" in tokens[pointer_positions[-1] + 1 :]
            else "mutable"
        )
    if "&" in tokens:
        # Conservatively treat a reference to mutable storage as mutable.
        return "mutable"
    return "immutable" if "const" in tokens else "mutable"


def analyze_source(source: str, path: pathlib.Path | None = None) -> tuple[Declaration, ...]:
    """Classify fragment-depth-zero static declarations."""
    source_path = path or pathlib.Path("<memory>")
    try:
        masked = mask_cpp(source)
        declarations: list[Declaration] = []
        brace_depth = 0
        index = 0
        while index < len(masked):
            char = masked[index]
            if char == "{":
                brace_depth += 1
                index += 1
                continue
            if char == "}":
                brace_depth -= 1
                if brace_depth < 0:
                    raise VerificationError("unbalanced source braces")
                index += 1
                continue
            if brace_depth == 0 and _word_at(masked, index, "static"):
                declaration, index = _scan_static_declaration(
                    masked, source, index, source_path
                )
                declarations.append(declaration)
                continue
            index += 1
        if brace_depth:
            raise VerificationError("unbalanced source braces")
        return tuple(declarations)
    except VerificationError as exc:
        raise VerificationError(f"{source_path}: {exc}") from exc


def top_level_invocation_count(source: str, name: str) -> int:
    masked = mask_cpp(source)
    brace_depth = 0
    count = 0
    index = 0
    while index < len(masked):
        if masked[index] == "{":
            brace_depth += 1
        elif masked[index] == "}":
            brace_depth -= 1
        elif brace_depth == 0 and _word_at(masked, index, name):
            after = index + len(name)
            while after < len(masked) and masked[after].isspace():
                after += 1
            if after < len(masked) and masked[after] == "(":
                count += 1
        index += 1
    return count


def fragment_paths(source_root: pathlib.Path) -> tuple[pathlib.Path, ...]:
    umbrella = source_root / UMBRELLA_NAME
    try:
        contents = umbrella.read_text(encoding="utf-8")
    except OSError as exc:
        raise VerificationError(f"cannot read umbrella {umbrella}: {exc}") from exc
    paths: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for match in INCLUDE_FRAGMENT.finditer(contents):
        path = (source_root / match.group("path")).resolve()
        if path in seen:
            raise VerificationError(f"duplicate fragment include: {path}")
        if not path.is_file():
            raise VerificationError(f"included fragment does not exist: {path}")
        seen.add(path)
        paths.append(path)
    if not paths:
        raise VerificationError(f"umbrella includes no .cc.inc fragments: {umbrella}")
    return tuple(paths)


def audit_plugin(source_root: pathlib.Path) -> Audit:
    declarations: list[Declaration] = []
    framework_globals: list[str] = []
    for path in fragment_paths(source_root):
        try:
            source = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise VerificationError(f"cannot read {path}: {exc}") from exc
        declarations.extend(analyze_source(source, path))
        for framework_name in EXPECTED_FRAMEWORK_GLOBALS:
            framework_globals.extend(
                framework_name
                for _ in range(top_level_invocation_count(source, framework_name))
            )
    return Audit(tuple(declarations), tuple(framework_globals))


def _format_names(names: Iterable[str]) -> str:
    values = sorted(names)
    return ", ".join(values) if values else "none"


def validation_errors(audit: Audit) -> list[str]:
    errors: list[str] = []
    mutable = audit.names("mutable")
    mutexes = audit.names("mutex")
    if mutable != EXPECTED_MUTABLE:
        errors.append(
            "mutable statics differ: expected {%s}; found {%s}"
            % (_format_names(EXPECTED_MUTABLE), _format_names(mutable))
        )
    if mutexes != EXPECTED_MUTEXES:
        errors.append(
            "mutex statics differ: expected {%s}; found {%s}"
            % (_format_names(EXPECTED_MUTEXES), _format_names(mutexes))
        )
    immutable_count = audit.count("immutable")
    if immutable_count != EXPECTED_IMMUTABLE_COUNT:
        errors.append(
            "immutable static count differs: expected %d; found %d"
            % (EXPECTED_IMMUTABLE_COUNT, immutable_count)
        )
    function_count = audit.count("function")
    if function_count != EXPECTED_FUNCTION_COUNT:
        errors.append(
            "static function count differs: expected %d; found %d"
            % (EXPECTED_FUNCTION_COUNT, function_count)
        )
    if audit.framework_globals != EXPECTED_FRAMEWORK_GLOBALS:
        errors.append(
            "framework globals differ: expected [%s]; found [%s]"
            % (
                _format_names(EXPECTED_FRAMEWORK_GLOBALS),
                _format_names(audit.framework_globals),
            )
        )
    return errors


def print_audit(audit: Audit) -> None:
    print(
        "INFO mutable namespace statics: %d (%s)"
        % (audit.count("mutable"), _format_names(audit.names("mutable")))
    )
    print(
        "INFO mutex namespace statics: %d (%s)"
        % (audit.count("mutex"), _format_names(audit.names("mutex")))
    )
    print("INFO immutable namespace statics: %d" % audit.count("immutable"))
    print("INFO static functions: %d" % audit.count("function"))
    print(
        "INFO framework global descriptors: %d (%s)"
        % (len(audit.framework_globals), _format_names(audit.framework_globals))
    )


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=pathlib.Path,
        default=DEFAULT_SOURCE_ROOT,
        help="plugin src directory (default: %(default)s)",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        audit = audit_plugin(arguments.source_root.resolve())
    except VerificationError as exc:
        print("Globals budget verification ERROR: %s" % exc, file=sys.stderr)
        return 2
    print_audit(audit)
    errors = validation_errors(audit)
    if errors:
        for error in errors:
            print("FAIL %s" % error, file=sys.stderr)
        print("Globals budget verification FAILED", file=sys.stderr)
        return 1
    print("Globals budget verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
