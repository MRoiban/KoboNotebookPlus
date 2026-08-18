#include "abi_types.h"
#include "cover_cache.h"
#include "covermenureceiver.h"
#include "eraser_menu.h"
#include "export_range.h"
#include "firmware_api.h"
#include "layers_menu.h"
#include "layers_preview.h"
#include "layers_service.h"
#include "layers_state.h"
#include "notebook_hook_services.h"
#include "notebook_gestures.h"
#include "notebook_menu.h"
#include "notebook_sleep.h"
#include "notebook_widget.h"
#include "page_actions.h"
#include "plugin_runtime.h"
#include "plugin_state.h"

#include <QElapsedTimer>
#include <QImage>
#include <QMenu>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

#include <cstdint>

using cnt::trace;

namespace {

class RenderVolumeDepthGuard {
public:
    explicit RenderVolumeDepthGuard(HookRuntimeState& state)
        : state_(state), outermost_(state.renderVolumeDepth == 0) {
        ++state_.renderVolumeDepth;
    }

    ~RenderVolumeDepthGuard() {
        if (state_.renderVolumeDepth > 0)
            --state_.renderVolumeDepth;
    }

    bool outermost() const {
        return outermost_;
    }

private:
    HookRuntimeState& state_;
    bool outermost_;
};

struct NotebookOpenPerformance {
    QElapsedTimer total;
    bool enabled = false;
    bool eraserQueued = false;
    qint64 stockMs = 0;
    qint64 observerMs = 0;
    qint64 eraserMs = 0;
    qint64 layerContextMs = 0;
    qint64 layerRouteMs = 0;
    QString layerResult = QLatin1String("disabled");

    NotebookOpenPerformance() {
        total.start();
    }

    ~NotebookOpenPerformance() {
        if (!enabled)
            return;
        try {
            qint64 const totalMs = total.elapsed();
            qint64 const pluginMs =
                totalMs >= stockMs ? totalMs - stockMs : 0;
            trace(QLatin1String("perf: notebook-open total-ms=")
                + QString::number(totalMs)
                + QLatin1String(" stock-ms=") + QString::number(stockMs)
                + QLatin1String(" plugin-ms=") + QString::number(pluginMs)
                + QLatin1String(" observers-ms=") + QString::number(observerMs)
                + QLatin1String(" eraser-ms=") + QString::number(eraserMs)
                + QLatin1String(" eraser-queued=")
                + (eraserQueued
                    ? QLatin1String("yes") : QLatin1String("no"))
                + QLatin1String(" layer-context-ms=")
                + QString::number(layerContextMs)
                + QLatin1String(" layer-route-ms=")
                + QString::number(layerRouteMs)
                + QLatin1String(" layer=") + layerResult);
        } catch (...) {
            // Performance diagnostics never affect notebook lifecycle.
        }
    }
};

} // namespace

extern "C" __attribute__((visibility("default")))
void _cnt_remove_common_book_data_hook(
    void* manager,
    void const* device,
    void* volume,
    bool removeBackingFile) {
    if (!firmwareApi().removeCommonBookDataOriginal)
        return;

    if (removeBackingFile && firmwareApi().contentGetId && volume) {
        QString const id = firmwareApi().contentGetId(volume);
        cnt::visibility::applyBackingFilePolicy(
            pluginState().visibility, removeBackingFile, id);
    }

    firmwareApi().removeCommonBookDataOriginal(
        manager, device, volume, removeBackingFile);
}

extern "C" __attribute__((visibility("default")))
QString _cnt_exclude_sync_folders_hook(void* featureSettings) {
    if (!firmwareApi().excludeSyncFoldersOriginal)
        return QString();

    QString exclusions = firmwareApi().excludeSyncFoldersOriginal(featureSettings);
    cnt::visibility::applySyncExclusion(pluginState().visibility, exclusions);
    return exclusions;
}

extern "C" __attribute__((visibility("default")))
QVector<BackgroundOption> _cnt_background_options_hook() {
    trace("backgroundOptions hook invoked");
    QVector<BackgroundOption> options = firmwareApi().backgroundOptionsOriginal();
    cnt::notebook_hook_services::routeBackgroundOptions(
        pluginState().templates, coverState(), options);
    return options;
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_dialog_title_hook(void* dialog, QString const& title) {
    if (!firmwareApi().setDialogTitleOriginal)
        return;
    cnt::notebook_hook_services::routeCoverDialogTitle(
        firmwareApi(), coverState(), dialog, title);
}

extern "C" __attribute__((visibility("default")))
void _cnt_parser_image_parsed_hook(
    void* parser,
    void const* volume,
    QImage const& image) {
    if (!firmwareApi().parserImageParsedOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    cnt::notebook_hook_services::routeParserImage(
        firmwareApi(),
        coverState(),
        caller,
        thumbnailCallbackReturnVma(),
        parser,
        volume,
        image);
}

extern "C" __attribute__((visibility("default")))
void _cnt_volume_load_cover_hook(void* view) {
    if (!firmwareApi().volumeLoadCoverOriginal)
        return;

    // Preserve Kobo's normal cache/download/default-cover behavior first.
    firmwareApi().volumeLoadCoverOriginal(view);
    cnt::notebook_hook_services::augmentNotebookGridCover(
        firmwareApi(), coverState(), view, volumeInPixmapViewOffset());
}

extern "C" __attribute__((visibility("default")))
void _cnt_tool_menu_constructor_hook(
    void* controller,
    QWidget* parent,
    QVector<int> const* tools,
    QVector<int> const* brushSections,
    void* themeStorage) {
    cnt::eraser_menu::constructController(
        firmwareApi(),
        eraserState(),
        settingsStore(),
        controller,
        parent,
        tools,
        brushSections,
        themeStorage);
}

extern "C" __attribute__((visibility("default")))
void _cnt_create_brush_size_row_hook(
    void* controller,
    NickelTouchMenu* menu,
    QString const& title) {
    cnt::eraser_menu::createBrushSizeRow(
        firmwareApi(), eraserState(), controller, menu, title);
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_brush_size_index_hook(void* controller, int index) {
    if (!firmwareApi().setBrushSizeIndexOriginal)
        return;
    // Preserve every stock theme/button update first, including unknown future
    // callers. Only the two pinned firmware callsites below have plugin work.
    firmwareApi().setBrushSizeIndexOriginal(controller, index);
    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    cnt::eraser_menu::afterBrushSizeIndex(
        firmwareApi(),
        eraserState(),
        settingsStore(),
        caller,
        controller,
        index,
        applyConfiguredEraserSizeForWidget);
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_active_tool_hook(void* widget, int tool) {
    if (!firmwareApi().setActiveToolOriginal)
        return;

    firmwareApi().setActiveToolOriginal(widget, tool);
    if (hookState().renderVolumeDepth > 0)
        return;
    cnt::eraser_menu::invalidateActiveEraserReplay(
        reinterpret_cast<QObject*>(widget));
    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    cnt::eraser_menu::afterActiveTool(
        firmwareApi(),
        eraserState(),
        settingsStore(),
        caller,
        widget,
        tool,
        layerEraserDependencies());
}

extern "C" __attribute__((visibility("default")))
void _cnt_render_volume_hook(void* widget, void const* volume) {
    if (!firmwareApi().renderVolumeOriginal)
        return;

    NotebookOpenPerformance performance;
    bool outermost = false;
    {
        RenderVolumeDepthGuard const renderGuard(hookState());
        outermost = renderGuard.outermost();
        if (outermost) {
            cnt::eraser_menu::invalidateActiveEraserReplay(
                reinterpret_cast<QObject*>(widget));
            cnt::layers_state::invalidateLiveLayerContext(layerState());
            cnt::notebook_gestures::invalidateNotebookWidget(
                pluginState().notebookGestures,
                reinterpret_cast<QWidget*>(widget));
        }

        QElapsedTimer stockTimer;
        stockTimer.start();
        // Stock renderVolume synchronously constructs the editor/backend/tool
        // and, on its saved-page path, installs the final part after
        // setupVolume. The caller reaches this symbol through the PLT, so
        // running after the original is the first hookable point where all
        // three identities are coherent.
        firmwareApi().renderVolumeOriginal(widget, volume);
        performance.stockMs = stockTimer.elapsed();
    }
    if (!outermost)
        return;
    if (!widget)
        return;

    QObject* const object = reinterpret_cast<QObject*>(widget);
    if (!cnt::notebook_widget::isNotebookWidget(object))
        return;
    performance.enabled = true;

    QElapsedTimer phase;
    phase.start();
    cnt::notebook_gestures::observeNotebookWidget(
        firmwareApi(),
        pluginState().notebookGestures,
        reinterpret_cast<QWidget*>(widget));
    cnt::notebook_sleep::observeNotebookWidget(
        pluginState().notebookSleep,
        reinterpret_cast<QWidget*>(widget));
    performance.observerMs = phase.elapsed();
    if (!hookState().notebookLifecycleHooksReady)
        return;
    if (eraserState().sizeApisReady) {
        phase.restart();
        try {
            performance.eraserQueued =
                cnt::eraser_menu::queueActiveEraserReplay(
                    eraserState(),
                    object,
                    layerEraserDependencies(),
                    40);
        } catch (...) {
            trace("eraser-size: notebook-open replay queue threw; stock radius preserved");
        }
        performance.eraserMs = phase.elapsed();
    }
    if (!layerState().hooksReady)
        return;

    try {
        trace("layers: notebook render complete; restoring saved active layer");
        cnt::layers::LayerContext context;
        QString error;
        phase.restart();
        if (!cnt::layers_state::loadLayerContext(
                firmwareApi(),
                layerState(),
                object,
                maximumNotebookLayers(),
                &context,
                &error,
                cnt::layers_state::AllowBaseOnlyOpenFastPath)) {
            performance.layerContextMs = phase.elapsed();
            performance.layerResult = QLatin1String("unavailable");
            cnt::layers_state::invalidateLiveLayerContext(layerState());
            trace(QLatin1String(
                "layers: notebook-open context unavailable; metadata preserved: ")
                + error);
            return;
        }
        performance.layerContextMs = phase.elapsed();
        bool const baseOnly = cnt::layers_state::isBaseOnlyLayerState(
            firmwareApi(), context.state);
        if (baseOnly) {
            cnt::layers_state::rememberLiveLayerContext(
                firmwareApi(), layerState(), context);
            performance.layerResult = QLatin1String("base-fast");
            return;
        }

        phase.restart();
        if (!cnt::layers_service::synchronizeSavedActiveLayer(
                firmwareApi(),
                neboBackendPageControllerOffset(),
                layerToolRoutingOperations(),
                context,
                "notebook-open",
                &error)) {
            performance.layerRouteMs = phase.elapsed();
            performance.layerResult = QLatin1String("route-failed");
            cnt::layers_state::invalidateLiveLayerContext(layerState());
            trace(QLatin1String(
                "layers: notebook-open active layer not restored; metadata preserved: ")
                + error);
            return;
        }
        performance.layerRouteMs = phase.elapsed();
        cnt::layers_state::rememberLiveLayerContext(
            firmwareApi(), layerState(), context);
        performance.layerResult = QLatin1String("routed");
    } catch (...) {
        // Opening the stock notebook must remain safe even if a passive restore
        // observes a transient engine state. No metadata is changed here.
        performance.layerResult = QLatin1String("threw");
        cnt::layers_state::invalidateLiveLayerContext(layerState());
        trace("layers: notebook-open active synchronization threw; metadata preserved");
    }
}

extern "C" __attribute__((visibility("default")))
void _cnt_export_volume_hook(
        void* exporter,
        void const* volume,
        QString const& destination,
        bool fullNotebook,
        int partIndex) {
    cnt::export_range::exportVolume(
        firmwareApi(),
        pluginState().exportRange,
        exporter,
        volume,
        destination,
        fullNotebook,
        partIndex,
        exportRangeRoot(),
        exportQualityPath());
}

extern "C" __attribute__((visibility("default")))
void _cnt_exporter_constructor_hook(void* exporter) {
    cnt::export_range::constructExporter(
        firmwareApi(), pluginState().exportRange, exporter);
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_tool_theme_hook(void* widget, void* theme) {
    if (!firmwareApi().setToolThemeOriginal)
        return;
    firmwareApi().setToolThemeOriginal(widget, theme);
    // renderVolume installs a provisional tool/theme before its final saved
    // part is coherent. The outer render hook performs one authoritative pass.
    if (hookState().renderVolumeDepth > 0)
        return;
    if (!hookState().notebookLifecycleHooksReady || !widget)
        return;

    QObject* const object = reinterpret_cast<QObject*>(widget);
    if (!cnt::notebook_widget::isNotebookWidget(object))
        return;
    if (eraserState().sizeApisReady) {
        try {
            cnt::eraser_menu::invalidateActiveEraserReplay(object);
            cnt::eraser_menu::queueActiveEraserReplay(
                eraserState(), object, layerEraserDependencies());
        } catch (...) {
            trace("eraser-size: tool-theme replay queue threw; stock radius preserved");
        }
    }
    if (!layerState().hooksReady)
        return;
    try {
        cnt::layers::LayerContext context;
        QString error;
        bool const cacheHit = cnt::layers_state::loadCachedRoutingContext(
            firmwareApi(), layerState(), object, &context, &error);
        if (!cacheHit) {
            error.clear();
            if (!cnt::layers_state::loadLayerContext(
                    firmwareApi(),
                    layerState(),
                    object,
                    maximumNotebookLayers(),
                    &context,
                    &error)) {
                cnt::layers_state::invalidateLiveLayerContext(layerState());
                trace(QLatin1String(
                    "layers: setToolTheme context unavailable: ") + error);
                return;
            }
        }

        bool const baseOnly = cnt::layers_state::isBaseOnlyLayerState(
            firmwareApi(), context.state);
        // On a validated same-page cache hit Kobo's stock replacement tool is
        // already on the native base layer. A miss can mean the same widget
        // changed pages, so reapply base once to clear an old custom restriction.
        if (cacheHit && baseOnly)
            return;

        if (!cnt::layers_service::applyActiveLayer(
                firmwareApi(),
                neboBackendPageControllerOffset(),
                layerToolRoutingOperations(),
                context,
                context.state.activeId,
                &error)) {
            cnt::layers_state::invalidateLiveLayerContext(layerState());
            trace(QLatin1String(
                "layers: active layer could not be reapplied after tool change: ")
                + error);
            return;
        }
        cnt::layers_state::rememberLiveLayerContext(
            firmwareApi(), layerState(), context);
        if (!baseOnly)
            trace("layers: active layer reapplied after tool change");
    } catch (...) {
        cnt::layers_state::invalidateLiveLayerContext(layerState());
        trace("layers: setToolTheme synchronization threw; stock theme preserved");
    }
}

extern "C" __attribute__((visibility("default")))
void _cnt_add_widget_action_hook(
    void* controller,
    QMenu* menu,
    QWidget* widget,
    QObject* receiver,
    char const* member,
    bool closesMenu,
    bool enabled,
    bool separatorAfter) {
    if (!firmwareApi().addWidgetActionOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    bool const exportMenuCaller = controller
        && menu
        && firmwareApi().iinknoteBase
        && caller >= firmwareApi().iinknoteBase + exportMenuLoadViewVma()
        && caller < firmwareApi().iinknoteBase
            + exportMenuLoadViewVma() + exportMenuLoadViewSize();
    if (exportMenuCaller
            && !menu->property("_cnt_export_range_menu_opened").toBool()) {
        menu->setProperty("_cnt_export_range_menu_opened", true);
        cnt::export_range::noteExportMenuOpened(
            firmwareApi(),
            pluginState().exportRange,
            reinterpret_cast<QObject*>(controller));
        cnt::export_range::promptForQuality(
            firmwareApi(),
            pluginState().exportRange,
            controller,
            reinterpret_cast<QObject*>(controller));
    }

    firmwareApi().addWidgetActionOriginal(
        controller, menu, widget, receiver, member,
        closesMenu, enabled, separatorAfter);
    if (exportMenuCaller
            && pluginState().exportRange.hooksReady
            && caller == firmwareApi().iinknoteBase
                + exportMenuFullActionReturnVma()
            && !menu->property("_cnt_export_range_action_added").toBool()) {
        menu->setProperty("_cnt_export_range_action_added", true);
        QPixmap noIcon;
        cnt::export_range::addMenuContribution(
            firmwareApi(),
            pluginState().exportRange,
            controller,
            reinterpret_cast<QObject*>(controller),
            menu,
            noIcon,
            maximumNotebookPages());
    }

    if ((!coverState().hooksReady && !pluginState().pages.hooksReady
            && !layerState().hooksReady)
            || !controller
            || !menu
            || !firmwareApi().iinknoteBase
            || caller < firmwareApi().iinknoteBase + menuLoadViewVma()
            || caller >= firmwareApi().iinknoteBase
                + menuLoadViewVma() + menuLoadViewSize()
            || menu->property("_cnt_notebook_actions_added").toBool()) {
        return;
    }

    menu->setProperty("_cnt_notebook_actions_added", true);
    QObject* const controllerObject = reinterpret_cast<QObject*>(controller);
    QPixmap noIcon;
    QString const sleepPinText = layerState().hooksReady
            && pluginState().notebookSleep.coreHooksReady
        ? cnt::notebook_sleep::pinnedPageMenuText(
            pluginState().notebookSleep,
            controllerObject,
            maximumNotebookPages())
        : QString();

    cnt::notebook_menu::addCoverContribution(
        firmwareApi(),
        coverState(),
        controller,
        controllerObject,
        menu,
        noIcon,
        coverBackupRoot());

    if (layerState().hooksReady) {
        cnt::layers_preview::Dependencies const previewDependencies = {
            &firmwareApi(),
            &layerState(),
            &coverState(),
            layerToolRoutingOperations(),
            layerPreviewPins()
        };
        cnt::layers_menu::Dependencies const menuDependencies = {
            previewDependencies,
            layerMenuPins()
        };
        CoverMenuReceiver* const layerReceiver = new CoverMenuReceiver(
            controllerObject, menu);
        QObject::connect(
            layerReceiver,
            &CoverMenuReceiver::layersRequested,
            [menuDependencies](QObject* target) {
                if (target)
                    cnt::layers_menu::showPopup(menuDependencies, target);
            });
        QWidget* const layerItem = firmwareApi().createIInkMenuItem(
            controller,
            menu,
            QLatin1String("Layers"),
            noIcon,
            false);
        if (layerItem) {
            firmwareApi().addWidgetActionOriginal(
                controller,
                menu,
                layerItem,
                layerReceiver,
                SLOT(activateLayers()),
                true,
                true,
                sleepPinText.isEmpty());
            trace("layers: native notebook menu item added");
        } else {
            layerReceiver->deleteLater();
            trace("layers: native menu item creation failed");
        }
    }

    if (!sleepPinText.isEmpty()) {
        cnt::notebook_menu::addSleepPinContribution(
            firmwareApi(),
            pluginState().notebookSleep,
            controller,
            controllerObject,
            menu,
            noIcon,
            sleepPinText,
            maximumNotebookPages());
    }

    if (pluginState().pages.hooksReady) {
        cnt::page_actions::Dependencies const pageDependencies = {
            &firmwareApi(),
            &coverState(),
            maximumNotebookPages(),
            pageBackupRoot()
        };
        cnt::notebook_menu::addPageContribution(
            firmwareApi(),
            controller,
            controllerObject,
            menu,
            noIcon,
            pageDependencies);
    }
}
