#pragma once

#include "layers.h"

#include <cstdint>
#include <string>

class QString;
struct FirmwareApi;

namespace cnt {
namespace layers_service {

typedef bool (*ArmLayerAwareDrawingErasers)(
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId,
    bool* currentIsEraser);
typedef bool (*IsDiagramDrawingEraser)(void* tool);
typedef bool (*RestrictToolToLayer)(
    void* tool,
    std::string const& layerId);
typedef bool (*RestrictBackendWritingToolsToLayer)(
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId);

// These callbacks keep concrete eraser kinds and writable-vtable details in
// the umbrella while the routing policy itself lives in a real translation
// unit. This is a plain aggregate: constructing the table requires no runtime
// registration or static initializer.
struct ToolRoutingOperations {
    ArmLayerAwareDrawingErasers armLayerAwareDrawingErasers;
    IsDiagramDrawingEraser isDiagramDrawingEraser;
    RestrictToolToLayer restrictToolToLayer;
    RestrictBackendWritingToolsToLayer restrictBackendWritingToolsToLayer;
};

bool applyActiveLayer(
    FirmwareApi& firmware,
    uintptr_t neboBackendPageControllerOffset,
    ToolRoutingOperations const& operations,
    layers::LayerContext const& context,
    QString const& layerId,
    QString* error);

bool synchronizeSavedActiveLayer(
    FirmwareApi& firmware,
    uintptr_t neboBackendPageControllerOffset,
    ToolRoutingOperations const& operations,
    layers::LayerContext const& context,
    char const* reason,
    QString* error);

} // namespace layers_service
} // namespace cnt
