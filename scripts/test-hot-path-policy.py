#!/usr/bin/env python3

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "mods" / "custom-notebook-templates" / "src"


def text(name: str) -> str:
    return (SOURCE / name).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"function signature not found: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"function body not found: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function body: {signature}")


class HotPathPolicyTests(unittest.TestCase):
    def test_trace_has_no_per_line_flash_barrier(self) -> None:
        source = text("settings.cc")
        body = function_body(
            source, "void trace(char const* message, bool truncate)"
        )
        self.assertEqual(body.count("fsync(fd)"), 1)
        self.assertIn("if (truncate)\n        fsync(fd);", body)
        self.assertEqual(body.count("write("), 1)
        self.assertIn("rename(kTrace, kPreviousTrace)", source)
        load = function_body(source, "static void traceLibraryLoad()")
        self.assertLess(
            load.index('char const suffix[] = ".failsafe"'),
            load.index("rename(kTrace, kPreviousTrace)"),
        )
        self.assertIn("active session trace preserved", load)

    def test_input_callbacks_do_not_reenable_verbose_trace_budgets(self) -> None:
        source = text("layers_eraser.cc")
        positive_budget = re.compile(
            r"(?:diagramPenTraceBudget|diagramEraserObserverTraceBudget|"
            r"eraserTraceBudget)\s*=\s*[1-9]"
        )
        self.assertIsNone(positive_budget.search(source))
        pen_down = function_body(
            source, "static bool layerGuardedDiagramPenDown("
        )
        self.assertIn(
            "if (diagnosticsEnabled)\n"
            "            after = restrictedToolLayer(dependencies, pen);",
            pen_down,
        )
        eraser_update = function_body(
            source,
            "static uint32_t layerAwareDiagramEraserUpdateSelection(",
        )
        self.assertIn(
            "bool const diagnosticsEnabled =",
            eraser_update,
        )
        eraser_release = function_body(
            source,
            "static int32_t layerAwareDiagramEraserEraseSelection(",
        )
        self.assertLess(
            eraser_release.index(
                "diagramEraserObserverTraceBudget <= 0"
            ),
            eraser_release.index("int committedBeforeEmpty"),
        )

    def test_delayed_gesture_is_epoch_guarded_and_bounded(self) -> None:
        header = text("passive_input_router.h")
        source = text("passive_input_router.cc")
        notebook = text("notebook_gestures.cc")
        self.assertIn("void rebindTarget(QWidget* target);", header)
        self.assertIn("std::uint64_t targetEpoch;", header)
        self.assertIn("std::uint64_t inputEpoch;", header)
        self.assertIn("kActionSettleDelayMs = 50", source)
        self.assertIn("startTimer(kActionSettleDelayMs)", source)
        self.assertIn("pending.targetEpoch == targetEpoch_", source)
        self.assertIn("pending.inputEpoch == inputEpoch_", source)
        self.assertIn("touchSequenceOpen_ || tabletContactOpen_", source)
        self.assertIn(
            "actionDispatching_ || actionSettling_ || "
            "actionPending_",
            source,
        )
        self.assertNotIn("std::deque", header)
        self.assertIn("PendingAction pendingAction_;", header)
        self.assertIn("bool actionPending_;", header)
        self.assertIn("std::vector<TouchPoint> pointBuffer_;", header)
        self.assertIn("PointBufferLease const pointLease", source)
        self.assertIn(
            "QList<QTouchEvent::TouchPoint> const& points",
            source,
        )
        self.assertIn("frame.points.emplace_back();", source)
        self.assertNotIn(
            "type == QEvent::TabletMove",
            source,
        )
        self.assertIn(
            "if (actionTimerId_ == 0)\n"
            "            clearQueuedAction();",
            source,
        )
        begin = source.index("if (event->type() == QEvent::TouchBegin)")
        self.assertLess(
            source.index("clearQueuedAction();", begin),
            source.index("touchSequenceOpen_ = true;", begin),
        )
        self.assertIn("rebindTarget(notebook);", notebook)
        self.assertIn("notePadEditor(notebook)", notebook)
        self.assertIn("notePadEditorControl(notebook)", notebook)
        self.assertNotIn("downThreeFingerSwipe_", notebook)
        self.assertNotIn("HeldInputChord", notebook)

    def test_generated_assets_short_circuit_before_heavy_work(self) -> None:
        util = text("fs_util.cc")
        templates = text("templates.cc")
        picker = function_body(
            util,
            "bool createPickerIcon("
            "QString const& sourcePath, QString* iconPath)",
        )
        condor = function_body(
            util,
            "bool syncCondorVariant("
            "QString const& sourcePath, QString* condorPath)",
        )
        overlay = function_body(
            templates,
            "bool syncTemplateOverlay("
            "QString const& sourcePath, QString* condorPath)",
        )
        self.assertLess(
            picker.index("generatedPngIsFresh("), picker.index("QImage page;")
        )
        self.assertLess(
            condor.index("generatedPngIsFresh("),
            condor.index("filesEqual("),
        )
        self.assertLess(
            overlay.index("generatedPngIsFresh("),
            overlay.index("QImage page;"),
        )
        self.assertIn("source-identity=", util)
        self.assertIn("source-sample=", util)
        self.assertIn("destination-sample=", util)
        self.assertIn('!name.startsWith(QLatin1String("._"))', util)

    def test_export_naming_avoids_stream_and_string_runtime_surface(
        self,
    ) -> None:
        header = text("export_naming.h")
        source = text("export_naming.cc")
        export_range = text("export_range.cc")
        self.assertNotIn("#include <string>", header)
        self.assertNotIn("#include <sstream>", source)
        self.assertNotIn("std::string", source)
        self.assertNotIn("std::ostringstream", source)
        self.assertNotIn("std::string utf8String", export_range)
        self.assertIn("struct FileName", header)
        self.assertIn("char bytes[241];", header)

    def test_layer_open_and_tool_change_use_safe_fast_paths(self) -> None:
        state = text("layers_state.cc")
        hooks = text("hooks.cc")
        menu = text("layers_menu.cc")
        load = function_body(state, "bool loadLayerContext(")
        self.assertLess(
            load.index("AllowBaseOnlyOpenFastPath"),
            load.index("reconcileLayerStateWithNativeLayout("),
        )
        render = function_body(hooks, "void _cnt_render_volume_hook(")
        self.assertLess(
            render.index("invalidateLiveLayerContext(layerState())"),
            render.index("renderVolumeOriginal(widget, volume)"),
        )
        self.assertIn("AllowBaseOnlyOpenFastPath", render)
        self.assertIn("perf: notebook-open", hooks)

        tool_theme = function_body(hooks, "void _cnt_set_tool_theme_hook(")
        self.assertLess(
            tool_theme.index("loadCachedRoutingContext("),
            tool_theme.index("loadLayerContext("),
        )
        self.assertIn("if (cacheHit && baseOnly)", tool_theme)
        self.assertNotIn(
            "applyConfiguredEraserSizeForWidget", tool_theme
        )
        operation = function_body(menu, "void runLayerOperation(")
        self.assertLess(
            operation.index("invalidateLiveLayerContext("),
            operation.index("performLayerOperation("),
        )
        active_tool = function_body(
            hooks, "void _cnt_set_active_tool_hook("
        )
        self.assertLess(
            active_tool.index("setActiveToolOriginal(widget, tool)"),
            active_tool.index("renderVolumeDepth > 0"),
        )
        self.assertLess(
            active_tool.index("renderVolumeDepth > 0"),
            active_tool.index("afterActiveTool("),
        )
        self.assertNotIn("toolIdentity(", text("layers_eraser.cc"))
        self.assertNotIn("pointerIdentity(", text("layers_eraser.cc"))
        popup = function_body(menu, "void showPopup(")
        self.assertLess(
            popup.index("synchronizeSavedActiveLayer("),
            popup.index("rememberLiveLayerContext("),
        )

    def test_eraser_replays_skip_already_correct_or_inactive_tools(self) -> None:
        eraser = text("layers_eraser.cc")
        menu = text("eraser_menu.cc")
        apply_one = function_body(
            eraser, "static bool applyConfigurationToExactEraser("
        )
        self.assertLess(
            apply_one.index(
                "radiusAlreadyApplied && policyAlreadyApplied"
            ),
            apply_one.index("restrictedToolLayer(dependencies, tool)"),
        )
        queue = function_body(menu, "bool queueActiveEraserReplay(")
        self.assertLess(
            queue.index("capturedPolicy < 0"),
            queue.index("new QTimer(widget)"),
        )
        self.assertLess(
            queue.index("capturedGeneration"),
            queue.index("new QTimer(widget)"),
        )
        self.assertIn(
            "notePadEditor(object)\n"
            "                        == capturedEditor",
            queue,
        )
        self.assertIn(
            "notePadEditorControl(object)\n"
            "                        == capturedEditorControl",
            queue,
        )
        self.assertIn(
            "activeEraserPolicy(object) == capturedPolicy",
            queue,
        )
        activation = function_body(menu, "void afterActiveTool(")
        self.assertIn(
            "if (!engineUpdated) {\n"
            "            queueActiveEraserReplay(",
            activation,
        )

        hooks = text("hooks.cc")
        render = function_body(hooks, "void _cnt_render_volume_hook(")
        self.assertLess(
            render.index("invalidateActiveEraserReplay("),
            render.index("renderVolumeOriginal(widget, volume)"),
        )
        theme = function_body(hooks, "void _cnt_set_tool_theme_hook(")
        self.assertLess(
            theme.index("invalidateActiveEraserReplay(object)"),
            theme.index("queueActiveEraserReplay("),
        )

    def test_failsafe_is_disarmed_synchronously_after_successful_init(
        self,
    ) -> None:
        entry = text("plugin_entry.cc")
        descriptor = entry[
            entry.index("static struct nh_info info = {") :
            entry.index("NickelHook(initialize")
        ]
        self.assertIn(
            "a later native\n"
            "    // notebook crash can no longer make the plugin disappear",
            descriptor,
        )
        self.assertRegex(descriptor, r"\n\s*0,\n};")

        nickelhook = (
            ROOT / "third_party" / "NickelHook" / "nh.c"
        ).read_text(encoding="utf-8")
        destroy_definition = nickelhook[
            nickelhook.rindex(
                "void nh_failsafe_destroy(nh_failsafe_t *fs, int delay)"
            ) :
        ]
        destroy = function_body(
            destroy_definition,
            "void nh_failsafe_destroy(nh_failsafe_t *fs, int delay)",
        )
        self.assertLess(
            destroy.index("if (delay <= 0)"),
            destroy.index("pthread_create("),
        )
        self.assertIn("_nh_failsafe_destroy(fs);", destroy)

    def test_home_tiles_skip_obvious_non_notebooks_and_hit_logging(self) -> None:
        cache = text("cover_cache.cc")
        services = text("notebook_hook_services.cc")
        resolver = function_body(
            cache,
            "QString notebookPathFromContentId(QString const& contentId)",
        )
        self.assertLess(
            resolver.index("fileName().contains"),
            resolver.index("QFileInfo(path).isFile()"),
        )
        cached = function_body(cache, "bool cachedNotebookCoverType(")
        self.assertNotIn("cover-cache: hit", cached)
        grid = function_body(
            services, "void augmentNotebookGridCover("
        )
        self.assertNotIn("live notebook card replaced", grid)
        self.assertIn(
            "grid augmentation threw; stock card preserved",
            grid,
        )

    def test_cover_composition_reuses_clean_cache_and_success_logs_are_bounded(
        self,
    ) -> None:
        cache = text("cover_cache.cc")
        templates = text("templates.cc")
        covers = text("covers.cc")
        compose = function_body(
            cache, "QImage composeCoverWithRenderedInk("
        )
        self.assertIn(
            "QImage composed = cleanCustomCoverImage(state, type);",
            compose,
        )
        self.assertNotIn("QImage composed(cover->backgroundBase)", compose)
        self.assertNotIn("PNG template loaded", templates)
        self.assertNotIn(
            "automatically loaded notebook template '%s'",
            templates,
        )
        self.assertNotIn("PNG cover loaded", covers)
        self.assertNotIn(
            "automatically loaded notebook cover '%s'",
            covers,
        )

    def test_sleep_cache_releases_hidden_frame_without_base_filter_call(
        self,
    ) -> None:
        sleep = text("notebook_sleep.cc")
        watcher = function_body(
            sleep, "bool NotebookSleepRefreshReceiver::eventFilter("
        )
        self.assertIn("case QEvent::Hide:", watcher)
        self.assertIn("invalidateCurrentPageCache(state);", watcher)
        self.assertNotIn("QObject::eventFilter(", watcher)
        self.assertEqual(watcher.count("return false;"), 1)


if __name__ == "__main__":
    unittest.main()
