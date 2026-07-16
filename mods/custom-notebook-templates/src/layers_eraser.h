#pragma once

#include "abi_types.h"
#include "layers.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

class QObject;
struct FirmwareApi;

namespace cnt {
class SettingsStore;

namespace eraser_menu {
struct RuntimeState;
}

namespace layers_eraser {

enum {
    DrawingBackendRestrictedToolCount = 3,
    EraserSizeRatioCount = 5
};

// Firmware-private offsets and policy constants remain inert data owned by
// the umbrella. The promoted implementation receives the verified process-
// lifetime table explicitly and never resolves or publishes process state
// itself.
struct Pins {
    size_t drawingEraserVtableWords;
    size_t diagramPenVtableWords;
    uintptr_t toolRestrictToLayerVtableSlot;
    uintptr_t toolRestrictedLayerVtableSlot;
    uintptr_t drawingBackendRestrictedToolOffsets[
        DrawingBackendRestrictedToolCount];
    char const* drawingBackendRestrictedToolNames[
        DrawingBackendRestrictedToolCount];
    uintptr_t drawingBackendEraserToolOffset;
    uintptr_t drawingEraserSelectionFromPointsVtableSlot;
    uintptr_t diagramPenPenDownVtableSlot;
    uintptr_t diagramPenPenDownVma;
    uintptr_t drawingEraserSelectionFromPointsVma;
    uintptr_t plainDrawingEraserSelectionFromPointsVma;
    uintptr_t coreEraserSelectionFromPointsVma;
    uintptr_t eraserWidthOffset;
    uintptr_t eraserViewScaleOffset;
    uintptr_t diagramEraserUpdateSelectionVtableSlot;
    uintptr_t diagramEraserEraseSelectionVtableSlot;
    uintptr_t diagramEraserUpdateSelectionVma;
    uintptr_t diagramEraserEraseSelectionVma;
    uintptr_t eraserFinalSelectionOffset;
    char const* customLayerIdPrefix;
    size_t customLayerIdPrefixLength;
    int selectionModeReplace;
    int selectionModeIntersect;
    int eraserPolicyStroke;
    uintptr_t pidBoxFactoryOffset;
    uintptr_t activeBackendGetToolVtableSlot;
    uintptr_t neboBackendPageControllerOffset;
    uintptr_t pageControllerLayoutGridOffset;
    float eraserSizeRatios[EraserSizeRatioCount];
};

// These pointers all refer to the process-lifetime PluginState. Keeping the
// aggregate POD makes callback entry deterministic and introduces no global
// constructor or hidden registration path.
struct Dependencies {
    FirmwareApi* firmware;
    layers::RuntimeState* runtime;
    eraser_menu::RuntimeState* eraserRuntime;
    SettingsStore* settings;
    Pins const* pins;
};

// Firmware calls only the exact typed umbrella thunks. The real translation
// unit receives their addresses as inert data when it validates and publishes
// writable vtable clones.
struct VtableCallbacks {
    DiagramPenPenDownFn diagramPenPenDown;
    DrawingEraserSelectionFromPointsFn drawingEraserSelectionFromPoints;
    DrawingEraserSelectionFromPointsFn plainDrawingEraserSelectionFromPoints;
    DrawingEraserSelectionFromPointsFn diagramEraserSelectionFromPoints;
    DiagramEraserUpdateSelectionFn diagramEraserUpdateSelection;
    DiagramEraserEraseSelectionFn diagramEraserEraseSelection;
};

static_assert(std::is_pod<Pins>::value,
    "layer eraser pins must remain inert POD data");
static_assert(std::is_pod<Dependencies>::value,
    "layer eraser dependencies must remain copyable POD data");
static_assert(std::is_pod<VtableCallbacks>::value,
    "layer eraser callback table must remain inert POD data");

bool diagramPenDown(
    Dependencies const& dependencies,
    void* pen,
    void const* pointerInfo);
void* drawingEraserSelectionFromPoints(
    Dependencies const& dependencies,
    void* result,
    void* eraser,
    void const* points);
void* plainDrawingEraserSelectionFromPoints(
    Dependencies const& dependencies,
    void* result,
    void* eraser,
    void const* points);
void* diagramEraserSelectionFromPoints(
    Dependencies const& dependencies,
    void* result,
    void* eraser,
    void const* points);
uint32_t diagramEraserUpdateSelection(
    Dependencies const& dependencies,
    void* eraser,
    void const* selection,
    void const* points);
int32_t diagramEraserEraseSelection(
    Dependencies const& dependencies,
    void* eraser);

bool armLayerAwareDrawingErasers(
    Dependencies const& dependencies,
    VtableCallbacks const& callbacks,
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId,
    bool* currentIsEraser);
bool isDiagramDrawingEraser(
    Dependencies const& dependencies,
    void* tool);
bool restrictToolToLayer(
    Dependencies const& dependencies,
    void* tool,
    std::string const& layerId);
bool restrictBackendWritingToolsToLayer(
    Dependencies const& dependencies,
    VtableCallbacks const& callbacks,
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId);
bool applyConfiguredEraserSizeForWidget(
    Dependencies const& dependencies,
    QObject* widgetObject,
    char const* reason);

} // namespace layers_eraser
} // namespace cnt
