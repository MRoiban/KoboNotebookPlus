#!/usr/bin/env python3
"""Focused host tests for verify-globals-budget.py."""

from __future__ import annotations

import importlib.util
import pathlib
import subprocess
import sys
import unittest


SCRIPT = pathlib.Path(__file__).with_name("verify-globals-budget.py")


def load_verifier():
    spec = importlib.util.spec_from_file_location("verify_globals_budget", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot import %s" % SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


VERIFIER = load_verifier()


class GlobalsBudgetParserTests(unittest.TestCase):
    def declarations(self, source: str):
        return VERIFIER.analyze_source(source)

    def names(self, source: str, category: str):
        return {
            declaration.name
            for declaration in self.declarations(source)
            if declaration.category == category
        }

    def test_classifies_constness_functions_mutex_and_aggregate(self) -> None:
        source = r'''
static int mutableValue = 0;
static int const immutableValue = 1;
static char const immutableCharacters[] = "static { not syntax }";
static char const* mutablePointer = nullptr;
static char const* const immutablePointer = nullptr;
static void* mutableTable[kWords];
static QMutex allowedMutex;
static Result (*callback)(int, int) = nullptr;
static struct nh_info info = {
    "name", nullptr, nullptr, nullptr, 30,
};
static std::map<void*, std::string> const kMapDestructorParityAnchor;
static qint64 const byteLimit = qint64(8) * 1024;

static bool helper(int value) {
    static int functionLocalState = 1;
    static unsigned char const signature[] = { 1, 2, 3 };
    return value == functionLocalState + signature[0];
}

__attribute__((constructor(101))) static void traceLibraryLoad() {
}
'''
        self.assertEqual(
            self.names(source, "mutable"),
            {"mutableValue", "mutablePointer", "mutableTable", "callback", "info"},
        )
        self.assertEqual(
            self.names(source, "immutable"),
            {
                "immutableValue",
                "immutableCharacters",
                "immutablePointer",
                "kMapDestructorParityAnchor",
                "byteLimit",
            },
        )
        self.assertEqual(self.names(source, "mutex"), {"allowedMutex"})
        self.assertEqual(self.names(source, "function"), {"helper", "traceLibraryLoad"})

    def test_ignores_comments_literals_preprocessor_and_function_locals(self) -> None:
        source = r'''
// static int commentState;
/* static int blockCommentState; */
#define DECLARE_STATIC static int macroState
static char const text[] = R"tag(static int rawStringState { };)tag";
static char quote = '}';
void externalFunction() {
    static int localState;
}
'''
        declarations = self.declarations(source)
        self.assertEqual(
            {(item.name, item.category) for item in declarations},
            {("text", "immutable"), ("quote", "mutable")},
        )

    def test_rejects_multiple_declarators_in_one_statement(self) -> None:
        with self.assertRaisesRegex(
            VERIFIER.VerificationError, "multiple namespace-scope declarators"
        ):
            self.declarations("static int first = 0, second = 0;\n")

    def test_framework_invocation_only_counts_at_fragment_depth_zero(self) -> None:
        source = r'''
NickelHook(initialize, &info, nullptr, nullptr, nullptr)
void function() {
    NickelHook(notAFrameworkDescriptor)
}
// NickelHook(comment)
'''
        self.assertEqual(
            VERIFIER.top_level_invocation_count(source, "NickelHook"), 1
        )

    def test_mutable_allowlist_is_name_based_not_only_a_count(self) -> None:
        declarations = (
            VERIFIER.Declaration(
                pathlib.Path("fixture.cc.inc"),
                1,
                "gPluginState",
                "mutable",
                "static PluginState* gPluginState;",
            ),
            VERIFIER.Declaration(
                pathlib.Path("fixture.cc.inc"),
                2,
                "replacementGlobal",
                "mutable",
                "static int replacementGlobal;",
            ),
        )
        errors = VERIFIER.validation_errors(
            VERIFIER.Audit(declarations, ("NickelHook",))
        )
        self.assertTrue(
            any(
                "expected {gPluginState, info}" in error
                and "replacementGlobal" in error
                for error in errors
            ),
            errors,
        )


class RepositoryGlobalsBudgetTests(unittest.TestCase):
    def test_repository_inventory_matches_phase_two_budget(self) -> None:
        audit = VERIFIER.audit_plugin(VERIFIER.DEFAULT_SOURCE_ROOT)
        self.assertEqual(VERIFIER.validation_errors(audit), [])
        self.assertEqual(audit.names("mutable"), {"gPluginState", "info"})
        self.assertEqual(audit.count("mutex"), 0)
        self.assertEqual(audit.count("immutable"), 297)
        self.assertEqual(audit.count("function"), 146)
        self.assertEqual(audit.framework_globals, ("NickelHook",))

    def test_cli_reports_verified_inventory(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT)],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("mutable namespace statics: 2 (gPluginState, info)", result.stdout)
        self.assertIn("immutable namespace statics: 297", result.stdout)
        self.assertIn("framework global descriptors: 1 (NickelHook)", result.stdout)
        self.assertIn("Globals budget verified", result.stdout)


if __name__ == "__main__":
    unittest.main()
