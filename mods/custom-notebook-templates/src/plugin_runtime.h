#pragma once

#include "layers_eraser.h"
#include "layers_service.h"

#include <cstdint>

class QObject;
struct FirmwareApi;
struct HookRuntimeState;
struct PluginState;

namespace cnt {
class SettingsStore;
namespace cover_cache {
struct State;
}
namespace eraser_menu {
struct RuntimeState;
}
namespace layers {
struct RuntimeState;
}
namespace layers_menu {
struct Pins;
}
namespace layers_preview {
struct Pins;
}
}

// One process-lifetime runtime is published before any firmware hook is
// installed. These accessors have hidden visibility under the plugin's build
// flags and are the sole bridge between the explicit entry point and hook
// translation units.
void publishPluginState(PluginState* state);
PluginState& pluginState();
FirmwareApi& firmwareApi();
cnt::cover_cache::State& coverState();
cnt::layers::RuntimeState& layerState();
cnt::eraser_menu::RuntimeState& eraserState();
cnt::SettingsStore& settingsStore();
HookRuntimeState& hookState();

char const* coverBackupRoot();
char const* pageBackupRoot();
char const* pageTransactionRoot();
int maximumNotebookPages();
int maximumDestinationNotebooks();
int maximumNotebookLayers();
uintptr_t menuLoadViewVma();
uintptr_t menuLoadViewSize();
uintptr_t volumeInPixmapViewOffset();
uintptr_t thumbnailCallbackReturnVma();
uintptr_t neboBackendPageControllerOffset();

cnt::layers_preview::Pins const& layerPreviewPins();
cnt::layers_menu::Pins const& layerMenuPins();
cnt::layers_eraser::Pins const& layerEraserPins();
cnt::layers_eraser::Dependencies layerEraserDependencies();
cnt::layers_service::ToolRoutingOperations const& layerToolRoutingOperations();

bool applyConfiguredEraserSizeForWidget(
    QObject* widgetObject,
    char const* reason);
