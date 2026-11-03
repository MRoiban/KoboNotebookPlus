#!/usr/bin/env python3
"""Host policy checks for the shipped refresh policy and opt-in diagnostics. No device access."""
import importlib.util
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('hot_path_policy', HERE / 'test-hot-path-policy.py')
POLICY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(POLICY)
text, body = POLICY.text, POLICY.function_body


class RefreshExperimentPolicyTests(unittest.TestCase):
    def test_missing_invalid_or_unmounted_file_keeps_optimized_defaults(self):
        trace = text('perf_trace.cc')
        reader = body(trace, 'Experiment readExperiment(')
        for check in ('if (!mounts.open(', 'if (!mounted)', 'if (!file.open(',
                      'bytes.size() > 256', 'file.error() != QFile::NoError',
                      'if (pair.size() != 2)', 'if (seen & bit)'):
            self.assertIn(check, reader)
        self.assertIn('fields.at(2) == "vfat"', reader)
        self.assertLess(reader.index('if (!mounted)'), reader.index('QFile file('))
        self.assertEqual(reader.count('file.read(257)'), 1)
        self.assertIn('Experiment defaults;', reader)
        self.assertIn('return defaults;', reader)
        self.assertNotIn('return stock;', reader)
        header = text('perf_trace.h')
        for default in ('Variant move = Position;', 'Variant duplicate = SetPart;',
                        'Variant layerActivate = None;', 'Variant layerAdd = None;',
                        'bool settle = false;'):
            self.assertIn(default, header)
        self.assertIn('Experiment result;', reader)
        for key, destination in (('move', 'move'), ('dup', 'duplicate'),
                                 ('layer-activate', 'layerActivate')):
            self.assertIn('pair.at(0) == "' + key + '"', reader)
            self.assertIn('destination = &result.' + destination + ';', reader)
        self.assertIn('if (value == "stock")', reader)
        self.assertIn('*destination = Stock;', reader)

    def test_stock_is_explicit_or_missing_position_pin_with_distinct_trace(self):
        selector = body(text('perf_trace.cc'), 'Variant experimentVariant(')
        self.assertIn('Experiment const& experiment = scope ? scope->experiment_ : defaults;', selector)
        for key, destination in (('move', 'move'), ('dup', 'duplicate'),
                                 ('layer-activate', 'layerActivate')):
            self.assertIn('!std::strcmp(key, "' + key + '")', selector)
            self.assertIn('result = experiment.' + destination + ';', selector)
        self.assertIn('bool const fallback = (result == Position || result == SetPart)', selector)
        self.assertIn('&& !firmware.widgetPositionUpdated;', selector)
        self.assertIn('scope->variant_ = fallback ? "stock-fallback" : variantName(result);', selector)
        self.assertIn('if (fallback)\n        return Stock;', selector)
        self.assertEqual(selector.count('return Stock;'), 1)
        self.assertNotIn('readExperiment(', selector)
        self.assertNotIn('result == None', selector)

    def test_optional_position_pin_does_not_gate_stock_feature_resolution(self):
        resolver = text('firmware_resolver.cc')
        group = resolver[resolver.index('bool const positionResolved'):resolver.index('bool const noteSymbols')]
        self.assertIn('if (!positionResolved)', group)
        self.assertIn('firmwareApi().widgetPositionUpdated = nullptr;', group)
        self.assertIn('stock path retained for pages', group)
        self.assertNotIn('return false', group)

    def test_scroll_variant_and_unused_pin_are_removed(self):
        for name in ('perf_trace.h', 'perf_trace.cc', 'pages.cc', 'firmware_api.h',
                     'firmware_pins.h', 'firmware_resolver.cc', 'abi_types.h'):
            source = text(name)
            self.assertNotIn('SetScrollValue', source)
            self.assertNotIn('setScrollValue', source)
            self.assertNotIn('perf::Scroll', source)
        self.assertNotIn('Scroll', text('perf_trace.h'))
        abi = (HERE / 'verify-layer-abi.py').read_text()
        self.assertNotIn('setScrollValue', abi)
        self.assertNotIn('0x655B0', abi)
        self.assertNotIn('scroll', (HERE / 'bench-notebook-edits.example.json').read_text())

    def test_move_checks_saves_and_cover_invalidation_precede_ui_sync(self):
        move = body(text('pages.cc'), 'bool reorderCurrentPage(')
        sync = move.index('if (variant == perf::Position)')
        for required in ('openNotebookHasPluginCover(', 'page_io::backupNotebookPath(',
                         'firmware.packageSave(', 'firmware.packagePartCount(',
                         'firmware.packageIndexOfPart(', 'phase("widget-save-post")',
                         'invalidateNotebookScanEntry('):
            self.assertLess(move.index(required), sync)
        variant = move[sync:move.index('trace("pages: current page reordered")')]
        self.assertIn('firmware.widgetPositionUpdated(widget);', variant)
        stock = variant[variant.index('} else {'):]
        self.assertLess(stock.index('firmware.editorSetPart(context.editor, context.part);'),
                        stock.index('firmware.widgetRefresh(widget);'))
        self.assertNotIn('widgetSave', variant)

    def test_duplicate_stock_order_and_optimized_post_selection_save(self):
        duplicate = body(text('pages.cc'), 'bool duplicateCurrentPage(')
        save = duplicate.index('if (variant == perf::Stock)')
        selection = duplicate.index('if (variant == perf::Stock)', save + 1)
        for required in ('openNotebookHasPluginCover(', 'page_io::backupNotebookPath(',
                         'firmware.packageSave(package.get())',
                         'firmware.packagePartCount(package.get()) != oldCount + 1',
                         'firmware.packageIndexOfPart(package.get(), clone) != target',
                         'clone commit validation failed'):
            self.assertLess(duplicate.index(required), save)
        self.assertIn('firmware.widgetSave(widget);', duplicate[save:selection])
        self.assertIn('invalidateNotebookPreviewKeepingCoverVerdict(', duplicate[save:selection])
        stock = duplicate[selection:duplicate.index('} else {', selection)]
        self.assertLess(stock.index('firmware.editorSetPart(context.editor, clone);'),
                        stock.index('firmware.widgetRefresh(widget);'))
        optimized = duplicate[duplicate.index('} else {', selection):duplicate.index('trace("pages: current page duplicated")')]
        select = optimized.index('firmware.editorSetPart(context.editor, clone)')
        sync = optimized.index('firmware.widgetPositionUpdated(widget)')
        last_save = optimized.index('phase("widget-save-post")')
        self.assertLess(select, sync)
        self.assertLess(sync, last_save)
        self.assertEqual(optimized.count('firmware.editorSetPart('), 1)
        self.assertNotIn('firmware.widgetRefresh(', optimized)

    def test_layer_add_may_skip_refresh_but_delete_keeps_it(self):
        layers = text('layers_preview.cc')
        activate = body(layers, 'static bool activateNotebookLayer(')
        self.assertIn('experimentVariant("layer-activate", firmware)', activate)
        self.assertIn('if (variant != perf::None)', activate)
        self.assertLess(activate.index('saveLayerState('), activate.index('if (variant != perf::None)'))
        add = body(layers, 'static bool addNotebookLayer(')
        self.assertIn('experimentVariant("layer-add", firmware)', add)
        gate = add.index('if (variant != perf::None)')
        for required in ('page_io::backupNotebookPath(', 'saveLayerState(context->state, error)',
                         'firmware.packageSave(', 'phase("widget-save-post")'):
            self.assertLess(add.index(required), gate)
        self.assertIn('firmware.widgetRefresh(context->widget);', add[gate:])
        delete = body(layers, 'static bool deleteActiveNotebookLayer(')
        self.assertNotIn('experimentVariant', delete)
        refresh = delete.index('firmware.widgetRefresh(context->widget);')
        for required in ('page_io::backupNotebookPath(', 'saveLayerState(context->state, error)',
                         'firmware.packageSave(', 'phase("widget-save-post")',
                         'QFile::remove(previewPath);'):
            self.assertLess(delete.index(required), refresh)
        header = text('perf_trace.h')
        self.assertIn('Variant layerAdd = None;', header)
        self.assertNotIn('layerDelete', header)
        reader = body(text('perf_trace.cc'), 'Experiment readExperiment(')
        self.assertIn('pair.at(0) == "layer-add"', reader)
        self.assertIn('destination = &result.layerAdd;', reader)
        self.assertNotIn('layer-delete', reader)

    def test_every_backed_up_edit_records_its_commit_last(self):
        for name, signature, end in (
                ('pages.cc', 'bool reorderCurrentPage(', 'trace("pages: current page reordered")'),
                ('pages.cc', 'bool duplicateCurrentPage(', 'trace("pages: current page duplicated")'),
                ('layers_preview.cc', 'static bool addNotebookLayer(', 'trace("layers: native layer added and selected")'),
                ('layers_preview.cc', 'static bool deleteActiveNotebookLayer(', 'trace("layers: active native layer deleted")'),
                ('cover_editor.cc', 'bool applyNotebookCover(', 'trace("covers: new writable cover inserted as page zero")')):
            operation = body(text(name), signature)
            finish = operation.index(end)
            commit = operation.rindex('notebookCommitted(', 0, finish)
            self.assertLess(operation.index('backupNotebook'), commit)
            self.assertLess(operation.rindex('widgetSave(', 0, finish), commit)
            self.assertIn('backupPath);', operation[commit:finish])

    def test_backup_reuse_requires_matching_commit_and_existing_backup(self):
        backup = body(text('page_io.cc'), 'bool backupNotebookPath(')
        reuse = backup[backup.index('BackupMemo& memo = memoFor(sourcePath);'):backup.index('QString const stamp')]
        self.assertIn('memo.committedFingerprint.clear();', reuse)
        self.assertLess(reuse.index('memo.committedFingerprint.clear();'), reuse.index('return true;'))
        for required in ('!committed.isEmpty()', 'memo.root == backupRoot',
                         'QFileInfo(memo.backupPath).isFile()', 'fingerprint == committed'):
            self.assertIn(required, reuse)
        tail = backup[backup.index('*backupPath = destination;'):]
        self.assertIn('memo.backupPath = destination;', tail)
        self.assertIn('pruneBackups(backupRoot, identity, destination);', tail)
        self.assertLess(backup.index('statvfs(qPrintable(backupRoot)'),
                        backup.index('BackupMemo& memo = memoFor(sourcePath);'))
        self.assertLess(backup.index('available < required'),
                        backup.index('perf::note("backup-reused", 1);'))
        identity = body(text('page_io.cc'), 'QString backupIdentity(')
        self.assertIn("QLatin1Char('h')", identity)
        self.assertIn('notebookKey(notebookPath)', identity)
        committed = body(text('page_io.cc'), 'void notebookCommitted(')
        self.assertIn('if (memo.backupPath != backupPath)', committed)
        self.assertIn('if (backupPath.isEmpty())', committed)

    def test_prewarm_is_optional_bounded_and_skips_after_an_open(self):
        header = text('perf_trace.h')
        self.assertIn('bool prewarm = true;', header)
        reader = body(text('perf_trace.cc'), 'Experiment readExperiment(')
        self.assertIn('pair.at(0) == "prewarm"', reader)
        self.assertIn('result.prewarm = value == "1";', reader)
        resolver = text('firmware_resolver.cc')
        group = resolver[resolver.index('// Optional open pre-warm'):resolver.index('bool const noteSymbols')]
        self.assertIn('firmwareApi().backgroundWidgetSetType = nullptr;', group)
        self.assertNotIn('return false', group)
        prewarm = text('prewarm.cc')
        replay = body(prewarm, 'bool replayBackground(')
        self.assertIn('::operator new(Limits::kBackgroundWidgetSize, std::nothrow)', replay)
        self.assertIn('firmware.backgroundWidgetDelete(widget);', replay)
        self.assertIn('if (!firmware.backgroundWidgetCtor || !firmware.backgroundWidgetDelete', replay)
        self.assertIn('kBackgroundWidgetSize = 0x28', prewarm)
        self.assertIn('kBackgroundWidgetDeviceOffset = 0x24', prewarm)
        backgrounds = body(prewarm, 'void runBackgrounds(')
        self.assertLess(backgrounds.index('if (s.notebookOpened)'), backgrounds.index('replayBackground('))
        self.assertIn('if (!enabled())', backgrounds)
        record = body(prewarm, 'void recordOpenedNotebook(')
        self.assertLess(record.index('s.notebookOpened = true;'), record.index('save();'))
        self.assertIn('"BackgroundWidget"', record)
        self.assertIn('if (!s.recent.isEmpty() && s.recent.first() == entry)', record)
        readahead = body(prewarm, 'qint64 readaheadLibraries(')
        self.assertIn('POSIX_FADV_WILLNEED', readahead)
        self.assertNotIn('read(', readahead.replace('readahead', ''))
        preload = body(prewarm, 'void runPreload(')
        self.assertIn('watcher->idleMs() < Limits::kPreloadIdleMs', preload)
        self.assertIn('Limits::kPreloadMaxAttempts', preload)
        self.assertIn('RTLD_LAZY | RTLD_NOLOAD', preload)
        self.assertLess(preload.index('RTLD_NOLOAD'), preload.index('dlopen("/usr/local/Kobo/libiinknote.so", RTLD_LAZY)'))
        self.assertNotIn('dlclose(handle)', preload)
        watcher = body(prewarm, 'bool InputWatcher::eventFilter(')
        self.assertIn('return false;', watcher)
        self.assertNotIn('return true;', watcher)
        entry = text('plugin_entry.cc')
        self.assertLess(entry.index('trace("delayed hook initialization complete");'),
                        entry.index('cnt::prewarm::notebookEngineLoaded();'))

    def test_observer_is_armed_only_by_explicit_valid_settle_one(self):
        trace = text('perf_trace.cc')
        reader = body(trace, 'Experiment readExperiment(')
        self.assertIn('pair.at(0) == "settle"', reader)
        self.assertIn('if (value != "0" && value != "1")', reader)
        self.assertIn('result.settle = value == "1";', reader)
        constructor = body(trace, 'EditScope::EditScope(')
        self.assertEqual(trace.count('readExperiment();'), 2)
        self.assertIn('return readExperiment();', body(trace, 'Experiment currentExperiment('))
        self.assertEqual(constructor.count('readExperiment();'), 1)
        self.assertLess(constructor.index('if (!pluginState().activeEdit)'),
                        constructor.index('readExperiment();'))
        self.assertLess(constructor.index('paintObserver->cancel();'),
                        constructor.index('readExperiment();'))
        gated = body(constructor, 'if (experiment_.settle)')
        self.assertIn('new PaintObserver', gated)
        self.assertIn('paintObserver->begin(widget, total_);', gated)
        self.assertEqual(trace.count('paintObserver->begin('), 1)
        observer_constructor = body(trace, 'PaintObserver::PaintObserver(')
        self.assertNotIn('registerCallback', observer_constructor)
        self.assertNotIn('watch(', observer_constructor)
        for signature in ('PaintObserver::PaintObserver(', 'EditScope::EditScope('):
            constructor_body = body(trace, signature)
            self.assertNotIn('registerCallback', constructor_body)
            self.assertNotIn('installEventFilter', constructor_body)
        begin = body(trace, 'void PaintObserver::begin(')
        self.assertIn('watch(widget);', begin)
        self.assertIn('QInternal::registerCallback(', begin)
        watch = body(trace, 'void PaintObserver::watch(')
        self.assertIn('object->installEventFilter(this);', watch)
        self.assertEqual(trace.count('QInternal::registerCallback('), 1)
        self.assertEqual(trace.count('installEventFilter(this)'), 1)

    def test_observer_wraps_delivery_without_filter_veto_or_idle_callback(self):
        trace = text('perf_trace.cc')
        filter_body = body(trace, 'bool PaintObserver::eventFilter(')
        self.assertEqual(filter_body.count('return false;'), 1)
        self.assertNotIn('return true;', filter_body)
        self.assertIn('QEvent::Destroy', filter_body)
        self.assertIn('QEvent::ChildAdded', filter_body)
        callback = body(trace, 'bool PaintObserver::notifyCallback(')
        self.assertIn('observer->notifyInternal_(', callback)
        self.assertIn('observer->forwardingEvent_ == event', callback)
        self.assertIn('generation == observer->generation_', callback)
        self.assertIn('observer->paintNs_ += ended - began;', callback)
        self.assertIn('observer->lastPaintNs_ = observer->quietSinceNs_ = ended;', callback)
        cancel = body(trace, 'void PaintObserver::cancel(')
        self.assertIn('QInternal::unregisterCallback(', cancel)
        self.assertIn('removeEventFilter(this)', cancel)
        self.assertIn('++generation_', cancel)
        self.assertIn('250000000LL', trace)
        self.assertIn('3000000000LL', trace)
        finish = body(trace, 'void EditScope::finish(')
        self.assertLess(finish.index('trace('), finish.index('paintObserver->complete('))
        self.assertIn('std::unique_ptr<cnt::perf::PaintObserver> paintObserver;', text('plugin_state.h'))


if __name__ == '__main__':
    unittest.main()
