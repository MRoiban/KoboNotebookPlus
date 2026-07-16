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


class PromotedTranslationUnitParserTests(unittest.TestCase):
    def declarations(self, source: str):
        return VERIFIER.analyze_translation_unit_source(source)

    def inventory(self, source: str):
        return {
            (declaration.name, declaration.category)
            for declaration in self.declarations(source)
        }

    def test_catches_namespace_objects_and_ignores_function_locals(self) -> None:
        source = r'''
int globalLeak = 0;
namespace named {
int namedLeak;
}
namespace {
int anonymousLeak = 0;
QMutex lock;
char const kOk[] = "x";
bool helper() {
    static int lazy = 1;
    return lazy != 0;
}
}
'''
        self.assertEqual(
            self.inventory(source),
            {
                ("globalLeak", "mutable"),
                ("namedLeak", "mutable"),
                ("anonymousLeak", "mutable"),
                ("lock", "mutex"),
                ("kOk", "immutable"),
            },
        )

    def test_ignores_types_members_functions_and_declaration_only_extern(self) -> None:
        source = r'''
extern int declarationOnly;
struct ForwardDeclaration;
class Example {
    int member;
    static int classState;
};
static void helper() {}
int Example::classState = 0;
'''
        self.assertEqual(
            self.inventory(source),
            {("classState", "mutable")},
        )

    def test_catches_elaborated_struct_objects_and_braced_initializers(self) -> None:
        source = r'''
struct Info aggregate = { 1, 2 };
struct Info anotherAggregate;
int const kValues[] = { 1, 2, 3 };
auto const kCallback = [] { return 1; };
'''
        self.assertEqual(
            self.inventory(source),
            {
                ("aggregate", "mutable"),
                ("anotherAggregate", "mutable"),
                ("kValues", "immutable"),
                ("kCallback", "immutable"),
            },
        )

    def test_classifies_pointers_and_function_pointer_storage(self) -> None:
        source = r'''
char const* mutablePointer = nullptr;
char const* const immutablePointer = nullptr;
Result (*callback)(int) = nullptr;
'''
        self.assertEqual(
            self.inventory(source),
            {
                ("mutablePointer", "mutable"),
                ("immutablePointer", "immutable"),
                ("callback", "mutable"),
            },
        )

    def test_rejects_multiple_namespace_declarators(self) -> None:
        with self.assertRaisesRegex(
            VERIFIER.VerificationError,
            "multiple namespace-scope declarators",
        ):
            self.declarations("int first = 0, second = 0;\n")

    def test_direct_initialized_objects_fail_closed(self) -> None:
        for declaration in (
            "QMutex lock(QMutex::Recursive);",
            "int leak(1);",
            "std::atomic<bool> ready(false);",
        ):
            with self.subTest(declaration=declaration):
                with self.assertRaisesRegex(
                    VERIFIER.VerificationError,
                    "ambiguous parenthesized namespace declaration",
                ):
                    self.declarations("namespace { %s }\n" % declaration)

    def test_ignores_template_using_aliases(self) -> None:
        self.assertEqual(
            self.declarations(
                "template<class T, class U> using Pair = QMap<T, U>;\n"
            ),
            (),
        )

    def test_promoted_validation_rejects_mutable_and_mutex_with_locations(self) -> None:
        audit = VERIFIER.Audit(
            self.declarations("namespace { int leak; QMutex lock; }\n"),
            (),
        )
        errors = VERIFIER.promoted_validation_errors(audit)
        self.assertEqual(len(errors), 2)
        self.assertTrue(any("mutable" in error and "leak" in error for error in errors))
        self.assertTrue(any("mutex" in error and "lock" in error for error in errors))


class RepositoryGlobalsBudgetTests(unittest.TestCase):
    def test_repository_inventory_matches_phase_two_budget(self) -> None:
        audit = VERIFIER.audit_plugin(VERIFIER.DEFAULT_SOURCE_ROOT)
        self.assertEqual(VERIFIER.validation_errors(audit), [])
        self.assertEqual(audit.names("mutable"), {"gPluginState", "info"})
        self.assertEqual(audit.count("mutex"), 0)
        self.assertEqual(audit.count("immutable"), 285)
        self.assertEqual(audit.count("function"), 125)
        self.assertEqual(audit.framework_globals, ("NickelHook",))

        promoted = VERIFIER.audit_promoted_sources(VERIFIER.DEFAULT_SOURCE_ROOT)
        self.assertEqual(VERIFIER.promoted_validation_errors(promoted), [])
        self.assertEqual(promoted.count("mutable"), 0)
        self.assertEqual(promoted.count("mutex"), 0)
        self.assertEqual(promoted.count("immutable"), 22)
        self.assertEqual(
            {
                (declaration.path.name, declaration.name)
                for declaration in promoted.declarations
                if declaration.category == "immutable"
            },
            {
                ("fs_util.cc", "kTemplateRoot"),
                ("fs_util.cc", "kCondorSuffix"),
                ("fs_util.cc", "kMaximumAutomaticPngSize"),
                ("fs_util.cc", "kBackgroundWidth"),
                ("fs_util.cc", "kBackgroundHeight"),
                ("fs_util.cc", "kPickerIconSize"),
                ("settings.cc", "kTrace"),
                ("settings.cc", "kTemplateRoot"),
                ("settings.cc", "kEraserSizeSettings"),
                ("templates.cc", "kManifest"),
                ("templates.cc", "kTemplateRoot"),
                ("templates.cc", "kCondorSuffix"),
                ("templates.cc", "kMaximumCustomTemplates"),
                ("templates.cc", "kBackgroundWidth"),
                ("templates.cc", "kBackgroundHeight"),
                ("templates.cc", "kBackgroundOptionsVma"),
                ("templates.cc", "kRendererMapVma"),
                ("templates.cc", "kExpectedBuiltinMapSize"),
                ("covers.cc", "kCoverRoot"),
                ("covers.cc", "kMaximumCustomCovers"),
                ("covers.cc", "kBackgroundWidth"),
                ("covers.cc", "kBackgroundHeight"),
            },
        )

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
        self.assertIn("immutable namespace statics: 285", result.stdout)
        self.assertIn("framework global descriptors: 1 (NickelHook)", result.stdout)
        self.assertIn(
            "promoted-TU mutable namespace objects: 0 (none)",
            result.stdout,
        )
        self.assertIn(
            "promoted-TU mutex namespace objects: 0 (none)",
            result.stdout,
        )
        self.assertIn(
            "promoted-TU immutable namespace objects: 22",
            result.stdout,
        )
        self.assertIn("Globals budget verified", result.stdout)


if __name__ == "__main__":
    unittest.main()
