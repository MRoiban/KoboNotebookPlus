#include "plugin_runtime.h"

#include "eraser_menu.h"
#include "firmware_api.h"
#include "layers_menu.h"
#include "layers_preview.h"
#include "plugin_state.h"
#include "settings.h"

#include <QObject>

#include <map>
#include <string>

namespace {

PluginState* gPluginState = nullptr;

// The alpha's file-scope diagram-pen map emitted two weak std::map destructor
// symbols. Preserve that dynamic ABI surface with an immutable empty anchor;
// all live routing data remains owned by PluginState.
std::map<void*, std::string> const kMapDestructorParityAnchor;

char const kCoverBackupRoot[] =
    "/mnt/onboard/.kobo/custom/covers/backups/";
char const kPageBackupRoot[] =
    "/mnt/onboard/.kobo/custom/page-manager/backups/";
char const kPageTransactionRoot[] =
    "/mnt/onboard/.kobo/custom/page-manager/transactions/";
int const kMaximumNotebookPages = 4096;
int const kMaximumDestinationNotebooks = 512;
int const kMaximumNotebookLayers = 16;
uintptr_t const kMenuLoadViewVma = 0x4984c;
uintptr_t const kMenuLoadViewSize = 0xe5e;
uintptr_t const kVolumeInPixmapViewOffset = 0xac;
uintptr_t const kThumbnailCallbackReturnVma = 0x5433e;
uintptr_t const kNeboBackendPageControllerOffset = 0x14;

cnt::layers_preview::Pins const kLayerPreviewPins = {
    "/mnt/onboard/.kobo/custom/layers/backups/",
    "/mnt/onboard/.kobo/custom/layers/previews/",
    kMaximumNotebookLayers,
    qint64(8) * 1024 * 1024,
    54,
    72,
    350,
    50,
    1,
    64,
    kNeboBackendPageControllerOffset,
    0x4,
    0x24,
    0x28,
    64,
    0x44,
    0x48,
    0x18,
    0x1c,
    0x20,
    0x24
};

cnt::layers_menu::Pins const kLayerMenuPins = {
    0x90,
    0x58,
    0x44,
    0x10
};

cnt::layers_eraser::Pins const kLayerEraserPins = {
    148 / sizeof(void*),
    152 / sizeof(void*),
    0x54,
    0x58,
    { 0x44, 0x54, 0x5c },
    { "pen(kinds0-2)", "selector(kind3)", "brush(kind6)" },
    0x4c,
    0x78,
    0x3c,
    0x7af1e4,
    0x562838,
    0x421314,
    0xa77470,
    0xcc,
    0x110,
    0x7c,
    0x80,
    0x75b73c,
    0x75c224,
    0xd0,
    "cnt.layer.",
    sizeof("cnt.layer.") - 1,
    0,
    2,
    0,
    0x24,
    0x2c,
    0x14,
    0xe4,
    { 0.0f, 0.25f, 0.35f, 0.70f, 1.0f }
};

bool layerGuardedDiagramPenDown(void* pen, void const* pointerInfo) {
    return cnt::layers_eraser::diagramPenDown(
        layerEraserDependencies(), pen, pointerInfo);
}

void* layerAwareDrawingEraserSelectionFromPoints(
        void* result,
        void* eraser,
        void const* points) {
    return cnt::layers_eraser::drawingEraserSelectionFromPoints(
        layerEraserDependencies(), result, eraser, points);
}

void* layerAwarePlainDrawingEraserSelectionFromPoints(
        void* result,
        void* eraser,
        void const* points) {
    return cnt::layers_eraser::plainDrawingEraserSelectionFromPoints(
        layerEraserDependencies(), result, eraser, points);
}

void* layerAwareDiagramEraserSelectionFromPoints(
        void* result,
        void* eraser,
        void const* points) {
    return cnt::layers_eraser::diagramEraserSelectionFromPoints(
        layerEraserDependencies(), result, eraser, points);
}

uint32_t layerAwareDiagramEraserUpdateSelection(
        void* eraser,
        void const* selection,
        void const* points) {
    return cnt::layers_eraser::diagramEraserUpdateSelection(
        layerEraserDependencies(), eraser, selection, points);
}

int32_t layerAwareDiagramEraserEraseSelection(void* eraser) {
    return cnt::layers_eraser::diagramEraserEraseSelection(
        layerEraserDependencies(), eraser);
}

cnt::layers_eraser::VtableCallbacks layerEraserCallbacks() {
    cnt::layers_eraser::VtableCallbacks const callbacks = {
        layerGuardedDiagramPenDown,
        layerAwareDrawingEraserSelectionFromPoints,
        layerAwarePlainDrawingEraserSelectionFromPoints,
        layerAwareDiagramEraserSelectionFromPoints,
        layerAwareDiagramEraserUpdateSelection,
        layerAwareDiagramEraserEraseSelection
    };
    return callbacks;
}

bool armLayerAwareDrawingErasers(
        void* inputDispatcher,
        void* currentTool,
        std::string const& layerId,
        bool* currentIsEraser) {
    return cnt::layers_eraser::armLayerAwareDrawingErasers(
        layerEraserDependencies(),
        layerEraserCallbacks(),
        inputDispatcher,
        currentTool,
        layerId,
        currentIsEraser);
}

bool isDiagramDrawingEraser(void* tool) {
    return cnt::layers_eraser::isDiagramDrawingEraser(
        layerEraserDependencies(), tool);
}

bool restrictToolToLayer(void* tool, std::string const& layerId) {
    return cnt::layers_eraser::restrictToolToLayer(
        layerEraserDependencies(), tool, layerId);
}

bool restrictBackendWritingToolsToLayer(
        void* inputDispatcher,
        void* currentTool,
        std::string const& layerId) {
    return cnt::layers_eraser::restrictBackendWritingToolsToLayer(
        layerEraserDependencies(),
        layerEraserCallbacks(),
        inputDispatcher,
        currentTool,
        layerId);
}

cnt::layers_service::ToolRoutingOperations const kLayerToolRoutingOperations = {
    armLayerAwareDrawingErasers,
    isDiagramDrawingEraser,
    restrictToolToLayer,
    restrictBackendWritingToolsToLayer
};

} // namespace

void publishPluginState(PluginState* state) {
    gPluginState = state;
}

PluginState& pluginState() {
    return *gPluginState;
}

FirmwareApi& firmwareApi() { return pluginState().firmware; }
cnt::cover_cache::State& coverState() { return pluginState().covers; }
cnt::layers::RuntimeState& layerState() { return pluginState().layers; }
cnt::eraser_menu::RuntimeState& eraserState() { return pluginState().eraser; }
cnt::SettingsStore& settingsStore() { return pluginState().settings; }
HookRuntimeState& hookState() { return pluginState().hooks; }

char const* coverBackupRoot() { return kCoverBackupRoot; }
char const* pageBackupRoot() { return kPageBackupRoot; }
char const* pageTransactionRoot() { return kPageTransactionRoot; }
int maximumNotebookPages() { return kMaximumNotebookPages; }
int maximumDestinationNotebooks() { return kMaximumDestinationNotebooks; }
int maximumNotebookLayers() { return kMaximumNotebookLayers; }
uintptr_t menuLoadViewVma() { return kMenuLoadViewVma; }
uintptr_t menuLoadViewSize() { return kMenuLoadViewSize; }
uintptr_t volumeInPixmapViewOffset() { return kVolumeInPixmapViewOffset; }
uintptr_t thumbnailCallbackReturnVma() { return kThumbnailCallbackReturnVma; }
uintptr_t neboBackendPageControllerOffset() {
    return kNeboBackendPageControllerOffset;
}

cnt::layers_preview::Pins const& layerPreviewPins() {
    return kLayerPreviewPins;
}

cnt::layers_menu::Pins const& layerMenuPins() { return kLayerMenuPins; }
cnt::layers_eraser::Pins const& layerEraserPins() {
    return kLayerEraserPins;
}

cnt::layers_eraser::Dependencies layerEraserDependencies() {
    cnt::layers_eraser::Dependencies const dependencies = {
        &firmwareApi(),
        &layerState(),
        &eraserState(),
        &settingsStore(),
        &kLayerEraserPins
    };
    return dependencies;
}

cnt::layers_service::ToolRoutingOperations const& layerToolRoutingOperations() {
    return kLayerToolRoutingOperations;
}

bool applyConfiguredEraserSizeForWidget(
        QObject* widgetObject,
        char const* reason) {
    return cnt::layers_eraser::applyConfiguredEraserSizeForWidget(
        layerEraserDependencies(), widgetObject, reason);
}
