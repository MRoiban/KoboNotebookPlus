#include "layers_service.h"

#include "firmware_api.h"
#include "settings.h"

#include <QString>

namespace cnt {
namespace layers_service {

bool applyActiveLayer(
        FirmwareApi& firmware,
        uintptr_t neboBackendPageControllerOffset,
        ToolRoutingOperations const& operations,
        layers::LayerContext const& context,
        QString const& layerId,
        QString* error) {
    if (!firmware.editorGetRenderer || !firmware.rendererGetBackend
            || !firmware.neboBackendVtable || !firmware.pageControllerInputDispatcher
            || !firmware.platformInputDispatcherGetCurrentTool) {
        if (error)
            *error = QLatin1String("Layer not selected: routing API is unavailable.");
        return false;
    }

    SharedRenderer const renderer = firmware.editorGetRenderer(context.editor);
    void* const backend = renderer
        ? firmware.rendererGetBackend(renderer.get()) : nullptr;
    void* const expectedVptr =
        static_cast<char*>(firmware.neboBackendVtable) + 8;
    if (!backend || *reinterpret_cast<void**>(backend) != expectedVptr) {
        if (error) {
            *error = QLatin1String(
                "Layer not selected: this notebook backend is not supported yet.");
        }
        return false;
    }

    // NeboBackend::selectTool on this firmware reads its PageController
    // shared_ptr object at +0x14/+0x18. The raw object is sufficient while the
    // renderer/backend shared_ptrs above remain alive.
    void* const pageController = *reinterpret_cast<void**>(
        static_cast<char*>(backend) + neboBackendPageControllerOffset);
    if (!pageController) {
        if (error)
            *error = QLatin1String("Layer not selected: page controller is missing.");
        return false;
    }

    trace("layers: routing input dispatcher query");
    SharedPlatformInputDispatcher const inputDispatcher =
        firmware.pageControllerInputDispatcher(pageController);
    if (!inputDispatcher) {
        if (error) {
            *error = QLatin1String(
                "Layer not selected: input dispatcher is unavailable.");
        }
        return false;
    }
    trace("layers: routing input dispatcher acquired");

    SharedTool const currentTool =
        firmware.platformInputDispatcherGetCurrentTool(inputDispatcher.get());
    void* const tool = currentTool.get();
    if (!tool) {
        if (error)
            *error = QLatin1String("Layer not selected: main tool is unavailable.");
        return false;
    }
    trace("layers: routing main tool acquired");

    // The current tool is what the very next stroke uses (PID+0x0c), and
    // atk::core::Pen::penDown reads its own restrictedLayer(). Restrict it
    // directly: this is the reliable path that works on ANY backend, not only a
    // DrawingBackend. Every exact eraser adapter consumes the same string;
    // DiagramEraser uses it to build layer-first polygon geometry. Its cached
    // pen is also synchronized for a later toolbar switch back to writing.
    std::string const nativeId = layerId.toUtf8().constData();
    bool currentIsEraser = false;
    bool applied = false;
    try {
        bool const erasersArmed = operations.armLayerAwareDrawingErasers(
            inputDispatcher.get(), tool, nativeId, &currentIsEraser);
        if (currentIsEraser) {
            if (!erasersArmed) {
                if (error) {
                    *error = QLatin1String(
                        "Layer not selected: eraser layer routing is unavailable.");
                }
                trace("layers: current eraser routing incomplete; selection rejected");
                return false;
            }
            applied = true;
            if (operations.isDiagramDrawingEraser(tool)) {
                trace(QLatin1String(
                    "layers: current diagram eraser layer-first geometry armed and cached pen synchronized"));
            } else {
                trace(QLatin1String(
                    "layers: current eraser restricted for selection intersection"));
            }
        } else if (operations.restrictToolToLayer(tool, nativeId)) {
            applied = true;
            trace("layers: routing current tool restriction complete");
        } else {
            trace("layers: routing current tool restriction unavailable");
        }
        // Best-effort: also pin the DrawingBackend's cached pen/selector/brush so
        // the layer survives pen/eraser/selector switches. snt/global cached
        // erasers use selection adapters; the Diagram traversal also updates
        // its cached pen for the next writing-tool switch. Never fatal — a page
        // whose main backend is not a DrawingBackend still routes through the
        // backends() traversal above.
        if (operations.restrictBackendWritingToolsToLayer(
                inputDispatcher.get(), tool, nativeId)) {
            applied = true;
        }
    } catch (...) {
        if (error) {
            *error = QLatin1String(
                "Layer not selected: a notebook writing tool rejected it.");
        }
        trace("layers: routing tool restriction threw");
        return false;
    }
    // Success once any writing tool carries the layer, or current eraser
    // routing is complete. A current DiagramEraser is accepted only after its
    // layer-first adapter is armed and cached DiagramPen is synchronized.
    if (!applied && !currentIsEraser) {
        if (error) {
            *error = QLatin1String(
                "Layer not selected: no writing tool accepted the layer.");
        }
        trace("layers: routing produced no restricted tool");
        return false;
    }
    return true;
}

// The sidecar's active ID is authoritative, but every newly opened page owns
// a fresh set of concrete cached tools. Reassert the exact string ID after
// Kobo has constructed those tools and again before showing an "active" row.
// This is deliberately idempotent and never rewrites sidecar metadata.
bool synchronizeSavedActiveLayer(
        FirmwareApi& firmware,
        uintptr_t neboBackendPageControllerOffset,
        ToolRoutingOperations const& operations,
        layers::LayerContext const& context,
        char const* reason,
        QString* error) {
    trace(QLatin1String("layers: active synchronization begin reason=")
        + QLatin1String(reason)
        + QLatin1String(" part=") + context.state.partId
        + QLatin1String(" id=") + context.state.activeId);
    if (!applyActiveLayer(
            firmware,
            neboBackendPageControllerOffset,
            operations,
            context,
            context.state.activeId,
            error)) {
        trace(QLatin1String("layers: active synchronization failed reason=")
            + QLatin1String(reason)
            + QLatin1String(" part=") + context.state.partId
            + QLatin1String(" id=") + context.state.activeId
            + QLatin1String(" error=")
            + (error ? *error : QString()));
        return false;
    }
    trace(QLatin1String("layers: active synchronization complete reason=")
        + QLatin1String(reason)
        + QLatin1String(" part=") + context.state.partId
        + QLatin1String(" id=") + context.state.activeId);
    return true;
}

} // namespace layers_service
} // namespace cnt
