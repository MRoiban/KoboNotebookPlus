#include "layers_eraser.h"

#include "eraser_menu.h"
#include "firmware_api.h"
#include "notebook_widget.h"
#include "settings.h"

#include <QByteArray>
#include <QMutexLocker>
#include <QObject>
#include <QString>

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>

namespace cnt {
namespace layers_eraser {
namespace {

static FirmwareApi& firmwareApi(Dependencies const& dependencies) {
    return *dependencies.firmware;
}

static layers::RuntimeState& layerState(
        Dependencies const& dependencies) {
    return *dependencies.runtime;
}

static eraser_menu::RuntimeState& eraserState(
        Dependencies const& dependencies) {
    return *dependencies.eraserRuntime;
}

static SettingsStore& settingsStore(Dependencies const& dependencies) {
    return *dependencies.settings;
}

static Pins const& firmwarePins(Dependencies const& dependencies) {
    return *dependencies.pins;
}

static bool pointerMatchesVma(void* pointer, uintptr_t expectedVma) {
    Dl_info image = {};
    if (!pointer || !dladdr(pointer, &image) || !image.dli_fbase)
        return false;
    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address =
        reinterpret_cast<uintptr_t>(pointer) & ~uintptr_t(1);
    return address - base == expectedVma;
}

static QString pointerIdentity(void* pointer) {
    Dl_info image = {};
    if (!pointer || !dladdr(pointer, &image) || !image.dli_fbase)
        return QLatin1String("unmapped");
    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address =
        reinterpret_cast<uintptr_t>(pointer) & ~uintptr_t(1);
    QString identity = QLatin1String("0x")
        + QString::number(address - base, 16);
    if (image.dli_sname && image.dli_sname[0] != '\0')
        identity += QLatin1String(":") + QString::fromLatin1(image.dli_sname);
    return identity;
}

static QString toolIdentity(
        Dependencies const& dependencies,
        void* tool,
        bool includeSelectionSlot) {
    Pins const& pins = firmwarePins(dependencies);
    if (!tool)
        return QLatin1String("missing");
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return QLatin1String("null-vptr");
    QString identity = QLatin1String("vptr=") + pointerIdentity(vptr)
        + QLatin1String(" penDown=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + pins.diagramPenPenDownVtableSlot))
        + QLatin1String(" restrict=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + pins.toolRestrictToLayerVtableSlot))
        + QLatin1String(" restricted=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + pins.toolRestrictedLayerVtableSlot));
    if (includeSelectionSlot) {
        identity += QLatin1String(" selection=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + pins.drawingEraserSelectionFromPointsVtableSlot))
            + QLatin1String(" update=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + pins.diagramEraserUpdateSelectionVtableSlot))
            + QLatin1String(" erase=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + pins.diagramEraserEraseSelectionVtableSlot));
    }
    return identity;
}

// Virtual atk::core::Tool::restrictToLayer(std::string const&) via vtable slot
// +0x54 on the concrete tool. For a Pen it stores the string penDown reads; for
// a ToolDispatcher (diagram backends) it runs the override. Returns false only
// when the tool or its vtable entry is missing.
static bool restrictToolToLayerImpl(
        Dependencies const& dependencies,
        void* tool,
        std::string const& layerId) {
    if (!tool)
        return false;
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return false;
    typedef void (*ToolRestrictToLayerFn)(void*, std::string const&);
    ToolRestrictToLayerFn const restrictToLayer =
        *reinterpret_cast<ToolRestrictToLayerFn*>(
            static_cast<char*>(vptr) + firmwarePins(dependencies).toolRestrictToLayerVtableSlot);
    if (!restrictToLayer)
        return false;
    restrictToLayer(tool, layerId);
    return true;
}

static std::string restrictedToolLayer(
        Dependencies const& dependencies,
        void* tool) {
    if (!tool)
        return std::string();
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return std::string();
    typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
    ToolRestrictedLayerFn const restrictedLayer =
        *reinterpret_cast<ToolRestrictedLayerFn*>(
            static_cast<char*>(vptr) + firmwarePins(dependencies).toolRestrictedLayerVtableSlot);
    return restrictedLayer ? restrictedLayer(tool) : std::string();
}

// DiagramPen::penDown (BN 0x7bf1e4) calls SmartPen::penDown, which reaches
// core Pen::penDown at BN 0xa93cf8. Core Pen then reads this same virtual
// restrictedLayer() value before setStrokeLayer at BN 0xa894be. Reassert the
// per-tool desired ID immediately before the stock DiagramPen implementation;
// this is both the narrow fix and the decisive on-device trace point.
static bool layerGuardedDiagramPenDown(
    Dependencies const& dependencies,
    void* pen,
    void const* pointerInfo) {
    if (!firmwareApi(dependencies).diagramPenPenDownOriginal)
        return false;

    std::string desired;
    {
        QMutexLocker locker(&layerState(dependencies).desiredDiagramPenLayersMutex);
        std::map<void*, std::string>::const_iterator const it =
            layerState(dependencies).desiredDiagramPenLayers.find(pen);
        if (it != layerState(dependencies).desiredDiagramPenLayers.end())
            desired = it->second;
    }

    std::string before;
    std::string after;
    bool corrected = false;
    try {
        before = restrictedToolLayer(dependencies, pen);
        if (!desired.empty() && before != desired) {
            corrected = restrictToolToLayerImpl(dependencies, pen, desired);
        }
        after = restrictedToolLayer(dependencies, pen);
    } catch (...) {
        trace("layers: diagram pen-down layer guard threw; stock pen-down preserved");
    }

    if (layerState(dependencies).diagramPenTraceBudget > 0) {
        --layerState(dependencies).diagramPenTraceBudget;
        trace(QLatin1String("layers: diagram pen-down layer guard desired=")
            + QString::fromUtf8(
                desired.data(), static_cast<int>(desired.size()))
            + QLatin1String(" before=")
            + QString::fromUtf8(before.data(), static_cast<int>(before.size()))
            + QLatin1String(" after=")
            + QString::fromUtf8(after.data(), static_cast<int>(after.size()))
            + QLatin1String(" corrected=")
            + (corrected ? QLatin1String("yes") : QLatin1String("no")));
    }
    return firmwareApi(dependencies).diagramPenPenDownOriginal(pen, pointerInfo);
}

static bool initializeLayerGuardedDiagramPenVtable(
        Dependencies const& dependencies,
        VtableCallbacks const& callbacks) {
    if (layerState(dependencies).diagramPenVtableReady)
        return true;
    if (!firmwareApi(dependencies).diagramPenVtable)
        return false;

    memcpy(
        layerState(dependencies).diagramPenVtable,
        firmwareApi(dependencies).diagramPenVtable,
        firmwarePins(dependencies).diagramPenVtableWords * sizeof(void*));
    size_t const slot = 2
        + firmwarePins(dependencies).diagramPenPenDownVtableSlot / sizeof(void*);
    if (slot >= firmwarePins(dependencies).diagramPenVtableWords)
        return false;

    union {
        void* pointer;
        DiagramPenPenDownFn function;
    } original;
    original.pointer = layerState(dependencies).diagramPenVtable[slot];
    if (!pointerMatchesVma(original.pointer, firmwarePins(dependencies).diagramPenPenDownVma)) {
        trace("layers: diagram pen vtable clone rejected unexpected penDown slot");
        return false;
    }
    firmwareApi(dependencies).diagramPenPenDownOriginal = original.function;

    union {
        void* pointer;
        DiagramPenPenDownFn function;
    } replacement;
    replacement.function = callbacks.diagramPenPenDown;
    layerState(dependencies).diagramPenVtable[slot] = replacement.pointer;
    layerState(dependencies).diagramPenVtableReady = true;
    trace("layers: diagram pen-down layer guard vtable clone verified");
    return true;
}

static bool isExactOrLayerGuardedDiagramPen(
        Dependencies const& dependencies,
        void* tool) {
    if (!tool || !firmwareApi(dependencies).diagramPenVtable)
        return false;
    void* const vptr = *reinterpret_cast<void**>(tool);
    return vptr == static_cast<char*>(firmwareApi(dependencies).diagramPenVtable) + 8
        || (layerState(dependencies).diagramPenVtableReady
            && vptr == static_cast<void*>(
                layerState(dependencies).diagramPenVtable + 2));
}

static bool armLayerGuardedDiagramPen(
    Dependencies const& dependencies,
    VtableCallbacks const& callbacks,
    void* pen,
    std::string const& layerId) {
    if (!isExactOrLayerGuardedDiagramPen(dependencies, pen)
            || !initializeLayerGuardedDiagramPenVtable(
                dependencies, callbacks)) {
        return false;
    }
    void* const stockVptr = static_cast<char*>(firmwareApi(dependencies).diagramPenVtable) + 8;
    if (*reinterpret_cast<void**>(pen) == stockVptr) {
        *reinterpret_cast<void**>(pen) = static_cast<void*>(
            layerState(dependencies).diagramPenVtable + 2);
    }
    {
        QMutexLocker locker(&layerState(dependencies).desiredDiagramPenLayersMutex);
        layerState(dependencies).desiredDiagramPenLayers[pen] = layerId;
    }
    layerState(dependencies).diagramPenTraceBudget = 8;
    return restrictToolToLayerImpl(dependencies, pen, layerId);
}

static void clearLayerAwareEraserSelection(
        Dependencies const& dependencies,
        void* selection) {
    if (!selection || !firmwareApi(dependencies).selectionSelectNone)
        return;
    try {
        firmwareApi(dependencies).selectionSelectNone(selection);
    } catch (...) {
        // The original firmware selection remains valid. Avoid allowing an
        // exception to cross the virtual callback boundary during pen input.
    }
}

// Apply the same named-layer/Intersection operation used by
// Selector::computeSelection after a concrete eraser has performed its stock
// polygon hit test and stroke-boundary adjustment.
static void filterLayerAwareEraserSelection(
    Dependencies const& dependencies,
    void* result,
    void* eraser,
    char const* implementation) {
    try {
        if (!eraser || !result || !firmwareApi(dependencies).selectionSelectLayer || !firmwareApi(dependencies).selectionIsEmpty) {
            clearLayerAwareEraserSelection(dependencies, result);
            return;
        }
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (!vptr) {
            clearLayerAwareEraserSelection(dependencies, result);
            return;
        }
        typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
        ToolRestrictedLayerFn const restrictedLayer =
            *reinterpret_cast<ToolRestrictedLayerFn*>(
                static_cast<char*>(vptr)
                    + firmwarePins(dependencies).toolRestrictedLayerVtableSlot);
        if (!restrictedLayer) {
            clearLayerAwareEraserSelection(dependencies, result);
            return;
        }
        std::string const& activeLayer = restrictedLayer(eraser);
        if (activeLayer.empty()) {
            clearLayerAwareEraserSelection(dependencies, result);
            return;
        }
        bool const emptyBefore = firmwareApi(dependencies).selectionIsEmpty(result);
        firmwareApi(dependencies).selectionSelectLayer(result, activeLayer, firmwarePins(dependencies).selectionModeIntersect);
        bool const emptyAfter = firmwareApi(dependencies).selectionIsEmpty(result);
        if (layerState(dependencies).eraserTraceBudget > 0) {
            --layerState(dependencies).eraserTraceBudget;
            trace(QLatin1String(
                "layers: eraser selection intersected active layer id=")
                + QString::fromUtf8(
                    activeLayer.data(), static_cast<int>(activeLayer.size()))
                + QLatin1String(" mode=2 implementation=")
                + QLatin1String(implementation)
                + QLatin1String(" before=")
                + (emptyBefore ? QLatin1String("empty")
                               : QLatin1String("nonempty"))
                + QLatin1String(" after=")
                + (emptyAfter ? QLatin1String("empty")
                              : QLatin1String("nonempty")));
        }
    } catch (...) {
        clearLayerAwareEraserSelection(dependencies, result);
        if (layerState(dependencies).eraserTraceBudget > 0) {
            --layerState(dependencies).eraserTraceBudget;
            trace(QLatin1String(
                "layers: eraser layer intersection failed closed implementation=")
                + QLatin1String(implementation));
        }
    }
}

// ARM returns atk::core::Selection by hidden result pointer. Preserve that
// exact ABI for both concrete implementations: call the corresponding Kobo
// original first, then filter the returned selection in place.
static void* layerAwareDrawingEraserSelectionFromPoints(
    Dependencies const& dependencies,
    void* result,
    void* eraser,
    void const* points) {
    if (!firmwareApi(dependencies).drawingEraserSelectionFromPointsOriginal)
        return result;
    firmwareApi(dependencies).drawingEraserSelectionFromPointsOriginal(result, eraser, points);
    filterLayerAwareEraserSelection(
        dependencies, result, eraser, "snt");
    return result;
}

static void* layerAwarePlainDrawingEraserSelectionFromPoints(
    Dependencies const& dependencies,
    void* result,
    void* eraser,
    void const* points) {
    if (!firmwareApi(dependencies).plainDrawingEraserSelectionFromPointsOriginal)
        return result;
    firmwareApi(dependencies).plainDrawingEraserSelectionFromPointsOriginal(result, eraser, points);
    filterLayerAwareEraserSelection(
        dependencies, result, eraser, "global");
    return result;
}

// Core Eraser::selectionFromPoints constructs a fresh Selection and applies
// its polygon with Replace. On raw-content Diagram pages that Replace step is
// already scoped to the document layer, so intersecting a custom layer after
// the fact cannot recover its strokes. Reuse the stock call to construct the
// correctly bound Selection, then deliberately discard its contents and
// reproduce Selector::computeSelection's verified order: layer/Replace first,
// polygon/Intersection second. Kobo's own strokerPolygon supplies the exact
// eraser geometry from the opaque PointerInfo vector. The custom-layer +0x7c
// path deliberately bypasses DiagramEraser's semantic transform, so restore
// its policy-0 whole-stroke behavior here while the mutable, layer-scoped
// Selection is still available. Policy 1 remains precise/brush erasing.
static void* layerAwareDiagramEraserSelectionFromPoints(
    Dependencies const& dependencies,
    void* result,
    void* eraser,
    void const* points) {
    if (!firmwareApi(dependencies).diagramEraserSelectionFromPointsOriginal)
        return result;
    firmwareApi(dependencies).diagramEraserSelectionFromPointsOriginal(result, eraser, points);
    try {
        if (!result || !eraser || !points || !firmwareApi(dependencies).selectionSelectLayer
                || !firmwareApi(dependencies).selectionSelectPolygon || !firmwareApi(dependencies).selectionSelectNone
                || !firmwareApi(dependencies).selectionIsEmpty || !firmwareApi(dependencies).selectionAdjustToStrokeBoundaries
                || !firmwareApi(dependencies).eraserPolicy || !firmwareApi(dependencies).eraserStrokerPolygon) {
            clearLayerAwareEraserSelection(dependencies, result);
            return result;
        }
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (!vptr) {
            clearLayerAwareEraserSelection(dependencies, result);
            return result;
        }
        typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
        ToolRestrictedLayerFn const restrictedLayer =
            *reinterpret_cast<ToolRestrictedLayerFn*>(
                static_cast<char*>(vptr)
                    + firmwarePins(dependencies).toolRestrictedLayerVtableSlot);
        if (!restrictedLayer) {
            clearLayerAwareEraserSelection(dependencies, result);
            return result;
        }
        std::string const& activeLayer = restrictedLayer(eraser);
        if (activeLayer.empty()) {
            clearLayerAwareEraserSelection(dependencies, result);
            return result;
        }

        bool const stockEmpty = firmwareApi(dependencies).selectionIsEmpty(result);
        firmwareApi(dependencies).selectionSelectLayer(result, activeLayer, firmwarePins(dependencies).selectionModeReplace);
        bool const layerEmpty = firmwareApi(dependencies).selectionIsEmpty(result);

        float const width = *reinterpret_cast<float const*>(
            static_cast<char const*>(eraser) + firmwarePins(dependencies).eraserWidthOffset)
            * *reinterpret_cast<float const*>(
                static_cast<char const*>(eraser) + firmwarePins(dependencies).eraserViewScaleOffset);
        if (!(width > 0.0f && width < 10000.0f)) {
            clearLayerAwareEraserSelection(dependencies, result);
            return result;
        }
        PointerInfoVectorOpaque const& pointVector =
            *static_cast<PointerInfoVectorOpaque const*>(points);
        std::vector<SelectionPointOpaque> const polygon =
            firmwareApi(dependencies).eraserStrokerPolygon(width, pointVector);
        if (polygon.empty()) {
            clearLayerAwareEraserSelection(dependencies, result);
            return result;
        }
        firmwareApi(dependencies).selectionSelectPolygon(
            result,
            &polygon[0],
            static_cast<int>(polygon.size()),
            firmwarePins(dependencies).selectionModeIntersect);
        bool geometryEmpty = firmwareApi(dependencies).selectionIsEmpty(result);

        size_t const customPrefixLength =
            firmwarePins(dependencies).customLayerIdPrefixLength;
        bool const customLayer = activeLayer.compare(
            0,
            customPrefixLength,
            firmwarePins(dependencies).customLayerIdPrefix) == 0;
        int const policy = firmwareApi(dependencies).eraserPolicy(eraser);
        bool boundaryAdjusted = false;
        if (customLayer && policy == firmwarePins(dependencies).eraserPolicyStroke && !geometryEmpty) {
            // Stock DrawingEraser and DiagramEraser use exactly 0.0f for this
            // first whole-stroke expansion. It expands the already layer-
            // scoped Selection; it does not perform a fresh page hit-test.
            firmwareApi(dependencies).selectionAdjustToStrokeBoundaries(result, 0.0f);
            boundaryAdjusted = true;
            geometryEmpty = firmwareApi(dependencies).selectionIsEmpty(result);
        }

        if (layerState(dependencies).eraserTraceBudget > 0) {
            --layerState(dependencies).eraserTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser layer-first selection id=")
                + QString::fromUtf8(
                    activeLayer.data(), static_cast<int>(activeLayer.size()))
                + QLatin1String(" layer-mode=0 polygon-mode=2 stock=")
                + (stockEmpty ? QLatin1String("empty")
                              : QLatin1String("nonempty"))
                + QLatin1String(" layer=")
                + (layerEmpty ? QLatin1String("empty")
                              : QLatin1String("nonempty"))
                + QLatin1String(" geometry=")
                + (geometryEmpty ? QLatin1String("empty")
                                 : QLatin1String("nonempty"))
                + QLatin1String(" policy=")
                + QString::number(policy)
                + QLatin1String(" boundary-adjusted=")
                + (boundaryAdjusted ? QLatin1String("yes")
                                    : QLatin1String("no"))
                + QLatin1String(" polygon-points=")
                + QString::number(static_cast<int>(polygon.size())));
        }
    } catch (...) {
        clearLayerAwareEraserSelection(dependencies, result);
        if (layerState(dependencies).eraserTraceBudget > 0) {
            --layerState(dependencies).eraserTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser layer-first selection failed closed"));
        }
    }
    return result;
}

// DiagramEraser::updateSelection (+0x7c) receives our +0x78 layer-scoped hit
// selection as `selection`, but its stock diagram override rebuilds the erase
// set from layoutGroup/selectByType/itemsIntersecting. Hardware traces show
// that override returns for Layer 2 but does not reach the wrapper's post-call
// trace for Layer 3, despite both +0x78 selections becoming non-empty. For only
// plugin-created cnt.layer.* IDs, bypass that diagram-only transform and call
// core Eraser::updateSelection directly. It clones/unions `selection` into the
// final Eraser+0xd0 member and returns; core Eraser::penUp then invokes vslot
// +0x80, whose DiagramEraser implementation removes that same selection. The
// document/base layer continues through the exact stock override.
static QLatin1String emptinessLabel(int state) {
    if (state == 0)
        return QLatin1String("nonempty");
    if (state == 1)
        return QLatin1String("empty");
    return QLatin1String("unknown");
}

static uint32_t layerAwareDiagramEraserUpdateSelection(
    Dependencies const& dependencies,
    void* eraser,
    void const* selection,
    void const* points) {
    if (!firmwareApi(dependencies).diagramEraserUpdateSelectionOriginal)
        return 0;

    int incomingEmpty = -1;
    int committedBeforeEmpty = -1;
    int committedAfterEmpty = -1;
    bool haveRestrict = false;
    std::string activeLayer;
    try {
        if (firmwareApi(dependencies).selectionIsEmpty && selection)
            incomingEmpty = firmwareApi(dependencies).selectionIsEmpty(selection) ? 1 : 0;
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (vptr) {
            typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
            ToolRestrictedLayerFn const restrictedLayer =
                *reinterpret_cast<ToolRestrictedLayerFn*>(
                    static_cast<char*>(vptr)
                        + firmwarePins(dependencies).toolRestrictedLayerVtableSlot);
            if (restrictedLayer) {
                activeLayer = restrictedLayer(eraser);
                haveRestrict = true;
            }
        }
    } catch (...) {
        if (layerState(dependencies).diagramEraserObserverTraceBudget > 0) {
            --layerState(dependencies).diagramEraserObserverTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser routing inspection failed; stock update preserved"));
        }
        return firmwareApi(dependencies).diagramEraserUpdateSelectionOriginal(
            eraser, selection, points);
    }

    size_t const customPrefixLength =
        firmwarePins(dependencies).customLayerIdPrefixLength;
    bool const customLayer = haveRestrict
        && activeLayer.compare(
            0,
            customPrefixLength,
            firmwarePins(dependencies).customLayerIdPrefix) == 0;
    if (!customLayer || !firmwareApi(dependencies).coreEraserUpdateSelection) {
        return firmwareApi(dependencies).diagramEraserUpdateSelectionOriginal(
            eraser, selection, points);
    }

    if (firmwareApi(dependencies).selectionIsEmpty && eraser) {
        try {
            committedBeforeEmpty = firmwareApi(dependencies).selectionIsEmpty(
                static_cast<char*>(eraser)
                    + firmwarePins(dependencies).eraserFinalSelectionOffset) ? 1 : 0;
        } catch (...) {
            committedBeforeEmpty = -1;
        }
    }
    if (layerState(dependencies).diagramEraserObserverTraceBudget > 0) {
        --layerState(dependencies).diagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser core-only enter id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" incoming=")
            + emptinessLabel(incomingEmpty)
            + QLatin1String(" committed-before=")
            + emptinessLabel(committedBeforeEmpty));
    }

    uint32_t result = 0;
    try {
        result = firmwareApi(dependencies).coreEraserUpdateSelection(eraser, selection, points);
        if (firmwareApi(dependencies).selectionIsEmpty && eraser) {
            committedAfterEmpty = firmwareApi(dependencies).selectionIsEmpty(
                static_cast<char*>(eraser)
                    + firmwarePins(dependencies).eraserFinalSelectionOffset) ? 1 : 0;
        }
    } catch (...) {
        if (layerState(dependencies).diagramEraserObserverTraceBudget > 0) {
            --layerState(dependencies).diagramEraserObserverTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser core-only update threw; failed closed"));
        }
        return 0;
    }

    if (layerState(dependencies).diagramEraserObserverTraceBudget > 0) {
        --layerState(dependencies).diagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser core-only exit id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" committed-after=")
            + emptinessLabel(committedAfterEmpty)
            + QLatin1String(" changed=")
            + QLatin1String(result ? "yes" : "no"));
    }
    return result;
}

// Core Eraser::penUp loads vslot +0x80 at BN 0xa86f0a and calls it at
// 0xa86f0e. Observe the exact final Selection which DiagramEraser removes,
// while preserving the stock function's single call, return value, and
// exception behavior.
static int32_t layerAwareDiagramEraserEraseSelection(
        Dependencies const& dependencies,
        void* eraser) {
    if (!firmwareApi(dependencies).diagramEraserEraseSelectionOriginal)
        return 0;

    int committedBeforeEmpty = -1;
    std::string activeLayer;
    try {
        if (firmwareApi(dependencies).selectionIsEmpty && eraser) {
            committedBeforeEmpty = firmwareApi(dependencies).selectionIsEmpty(
                static_cast<char*>(eraser)
                    + firmwarePins(dependencies).eraserFinalSelectionOffset) ? 1 : 0;
        }
        activeLayer = restrictedToolLayer(dependencies, eraser);
    } catch (...) {}

    if (layerState(dependencies).diagramEraserObserverTraceBudget > 0) {
        --layerState(dependencies).diagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser remove enter id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" committed=")
            + emptinessLabel(committedBeforeEmpty));
    }

    int32_t const result = firmwareApi(dependencies).diagramEraserEraseSelectionOriginal(eraser);

    int committedAfterEmpty = -1;
    try {
        if (firmwareApi(dependencies).selectionIsEmpty && eraser) {
            committedAfterEmpty = firmwareApi(dependencies).selectionIsEmpty(
                static_cast<char*>(eraser)
                    + firmwarePins(dependencies).eraserFinalSelectionOffset) ? 1 : 0;
        }
    } catch (...) {}
    if (layerState(dependencies).diagramEraserObserverTraceBudget > 0) {
        --layerState(dependencies).diagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser remove exit id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" committed-after=")
            + emptinessLabel(committedAfterEmpty)
            + QLatin1String(" removed=")
            + QLatin1String(result ? "yes" : "no"));
    }
    return result;
}

// Installs the +0x7c custom-layer update and +0x80 removal observer into the
// already-cloned DiagramEraser vtable. Requires the base +0x78 clone to be
// verified first. Both stock slots are validated before either is replaced,
// so the object vptr is never published with a partial downstream clone.
static bool installLayerAwareDiagramEraserUpdateHook(
        Dependencies const& dependencies,
        VtableCallbacks const& callbacks) {
    if (layerState(dependencies).diagramEraserObserversReady)
        return true;
    if (!layerState(dependencies).diagramEraserVtableReady || !firmwareApi(dependencies).coreEraserUpdateSelection)
        return false;

    size_t const updateSlot = 2
        + firmwarePins(dependencies).diagramEraserUpdateSelectionVtableSlot / sizeof(void*);
    size_t const eraseSlot = 2
        + firmwarePins(dependencies).diagramEraserEraseSelectionVtableSlot / sizeof(void*);
    if (updateSlot >= firmwarePins(dependencies).drawingEraserVtableWords
            || eraseSlot >= firmwarePins(dependencies).drawingEraserVtableWords)
        return false;

    union {
        void* pointer;
        DiagramEraserUpdateSelectionFn function;
    } originalUpdate;
    originalUpdate.pointer = layerState(dependencies).diagramEraserVtable[updateSlot];
    if (!pointerMatchesVma(
            originalUpdate.pointer, firmwarePins(dependencies).diagramEraserUpdateSelectionVma)) {
        trace(QLatin1String(
            "layers: diagram eraser update hook rejected unexpected slot"));
        return false;
    }

    union {
        void* pointer;
        DiagramEraserEraseSelectionFn function;
    } originalErase;
    originalErase.pointer = layerState(dependencies).diagramEraserVtable[eraseSlot];
    if (!pointerMatchesVma(
            originalErase.pointer, firmwarePins(dependencies).diagramEraserEraseSelectionVma)) {
        trace(QLatin1String(
            "layers: diagram eraser remove observer rejected unexpected slot"));
        return false;
    }

    union {
        void* pointer;
        DiagramEraserUpdateSelectionFn function;
    } replacementUpdate;
    replacementUpdate.function = callbacks.diagramEraserUpdateSelection;
    union {
        void* pointer;
        DiagramEraserEraseSelectionFn function;
    } replacementErase;
    replacementErase.function = callbacks.diagramEraserEraseSelection;

    firmwareApi(dependencies).diagramEraserUpdateSelectionOriginal = originalUpdate.function;
    firmwareApi(dependencies).diagramEraserEraseSelectionOriginal = originalErase.function;
    layerState(dependencies).diagramEraserVtable[updateSlot] = replacementUpdate.pointer;
    layerState(dependencies).diagramEraserVtable[eraseSlot] = replacementErase.pointer;
    layerState(dependencies).diagramEraserObserversReady = true;
    trace(QLatin1String(
        "layers: diagram eraser core-only update and remove observer installed"));
    return true;
}

static bool initializeLayerAwareEraserVtable(
    Dependencies const& dependencies,
    void* stockVtable,
    void** layerVtable,
    bool* ready,
    DrawingEraserSelectionFromPointsFn* originalOut,
    DrawingEraserSelectionFromPointsFn replacement,
    uintptr_t expectedSelectionVma,
    char const* implementation) {
    if (*ready)
        return true;
    if (!stockVtable || !firmwareApi(dependencies).selectionSelectLayer || !firmwareApi(dependencies).selectionSelectNone)
        return false;

    memcpy(
        layerVtable,
        stockVtable,
        firmwarePins(dependencies).drawingEraserVtableWords * sizeof(void*));
    size_t const slot = 2
        + firmwarePins(dependencies).drawingEraserSelectionFromPointsVtableSlot / sizeof(void*);
    if (slot >= firmwarePins(dependencies).drawingEraserVtableWords)
        return false;

    union {
        void* pointer;
        DrawingEraserSelectionFromPointsFn function;
    } original;
    original.pointer = layerVtable[slot];
    if (!pointerMatchesVma(original.pointer, expectedSelectionVma)) {
        trace(QLatin1String(
            "layers: eraser vtable clone rejected unexpected selection slot implementation=")
            + QLatin1String(implementation));
        return false;
    }
    *originalOut = original.function;

    union {
        void* pointer;
        DrawingEraserSelectionFromPointsFn function;
    } replacementPointer;
    replacementPointer.function = replacement;
    layerVtable[slot] = replacementPointer.pointer;
    *ready = true;
    trace(QLatin1String(
        "layers: layer-aware eraser vtable clone verified implementation=")
        + QLatin1String(implementation));
    return true;
}

enum ConcreteDrawingEraserKind {
    kNotDrawingEraser,
    kSntDrawingEraser,
    kPlainDrawingEraser,
    kDiagramEraser
};

static ConcreteDrawingEraserKind concreteDrawingEraserKind(
        Dependencies const& dependencies,
        void* tool) {
    if (!tool)
        return kNotDrawingEraser;
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (firmwareApi(dependencies).drawingEraserVtable
            && (vptr == static_cast<char*>(firmwareApi(dependencies).drawingEraserVtable) + 8
                || (layerState(dependencies).drawingEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerState(dependencies).drawingEraserVtable + 2)))) {
        return kSntDrawingEraser;
    }
    if (firmwareApi(dependencies).plainDrawingEraserVtable
            && (vptr == static_cast<char*>(firmwareApi(dependencies).plainDrawingEraserVtable) + 8
                || (layerState(dependencies).plainDrawingEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerState(dependencies).plainDrawingEraserVtable + 2)))) {
        return kPlainDrawingEraser;
    }
    if (firmwareApi(dependencies).diagramEraserVtable
            && (vptr == static_cast<char*>(firmwareApi(dependencies).diagramEraserVtable) + 8
                || (layerState(dependencies).diagramEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerState(dependencies).diagramEraserVtable + 2)))) {
        return kDiagramEraser;
    }
    return kNotDrawingEraser;
}

static bool isLayerAwareDrawingEraser(
        Dependencies const& dependencies,
        void* tool) {
    return concreteDrawingEraserKind(dependencies, tool)
        != kNotDrawingEraser;
}

static char const* exactEraserSizeClass(
        Dependencies const& dependencies,
        void* tool) {
    if (!tool)
        return nullptr;
    ConcreteDrawingEraserKind const drawingKind =
        concreteDrawingEraserKind(dependencies, tool);
    if (drawingKind == kSntDrawingEraser)
        return "snt-drawing";
    if (drawingKind == kPlainDrawingEraser)
        return "global-drawing";
    if (drawingKind == kDiagramEraser)
        return "diagram";

    void* const vptr = *reinterpret_cast<void**>(tool);
    if (firmwareApi(dependencies).coreEraserVtable
            && vptr == static_cast<char*>(firmwareApi(dependencies).coreEraserVtable) + 8) {
        return "core";
    }
    if (firmwareApi(dependencies).textEraserSntVtable
            && vptr == static_cast<char*>(firmwareApi(dependencies).textEraserSntVtable) + 8) {
        return "snt-text";
    }
    if (firmwareApi(dependencies).mathEraserVtable
            && vptr == static_cast<char*>(firmwareApi(dependencies).mathEraserVtable) + 8) {
        return "math";
    }
    if (firmwareApi(dependencies).textEraserVtable
            && vptr == static_cast<char*>(firmwareApi(dependencies).textEraserVtable) + 8) {
        return "text";
    }
    return nullptr;
}

static bool applyConfigurationToExactEraser(
    Dependencies const& dependencies,
    void* tool,
    float desiredRadius,
    bool applyPolicy,
    int desiredPolicy,
    char const* reason,
    char const* source) {
    char const* const eraserClass =
        exactEraserSizeClass(dependencies, tool);
    if (!eraserClass || !firmwareApi(dependencies).eraserSetRadius
            || !firmwareApi(dependencies).eraserRadius
            || (applyPolicy
                && (!firmwareApi(dependencies).eraserSetPolicy
                    || !firmwareApi(dependencies).eraserPolicy))) {
        return false;
    }

    try {
        float const beforeRadius = firmwareApi(dependencies).eraserRadius(tool);
        int const beforePolicy = firmwareApi(dependencies).eraserPolicy ? firmwareApi(dependencies).eraserPolicy(tool) : -1;
        std::string const beforeLayer = restrictedToolLayer(dependencies, tool);
        firmwareApi(dependencies).eraserSetRadius(tool, desiredRadius);
        if (applyPolicy)
            firmwareApi(dependencies).eraserSetPolicy(tool, desiredPolicy);
        float const afterRadius = firmwareApi(dependencies).eraserRadius(tool);
        int const afterPolicy = firmwareApi(dependencies).eraserPolicy ? firmwareApi(dependencies).eraserPolicy(tool) : -1;
        std::string const afterLayer = restrictedToolLayer(dependencies, tool);
        float difference = afterRadius - desiredRadius;
        if (difference < 0.0f)
            difference = -difference;
        bool const policyVerified = applyPolicy
            ? afterPolicy == desiredPolicy
            : beforePolicy == afterPolicy;
        bool const layerPreserved = beforeLayer == afterLayer;
        bool const radiusApplied = difference < 0.001f;
        if (applyPolicy) {
            trace(QLatin1String("eraser-state: apply reason=")
                + QLatin1String(reason)
                + QLatin1String(" source=") + QLatin1String(source)
                + QLatin1String(" class=") + QLatin1String(eraserClass)
                + QLatin1String(" before-policy=")
                + QString::number(beforePolicy)
                + QLatin1String(" requested-policy=")
                + QString::number(desiredPolicy)
                + QLatin1String(" after-policy=")
                + QString::number(afterPolicy)
                + QLatin1String(" before-radius=")
                + QString::number(beforeRadius, 'f', 3)
                + QLatin1String(" requested-radius=")
                + QString::number(desiredRadius, 'f', 3)
                + QLatin1String(" after-radius=")
                + QString::number(afterRadius, 'f', 3)
                + QLatin1String(" before-layer=")
                + QString::fromUtf8(
                    beforeLayer.data(), static_cast<int>(beforeLayer.size()))
                + QLatin1String(" after-layer=")
                + QString::fromUtf8(
                    afterLayer.data(), static_cast<int>(afterLayer.size()))
                + QLatin1String(" verified=")
                + (policyVerified && radiusApplied && layerPreserved
                    ? QLatin1String("yes") : QLatin1String("no")));
            if (!layerPreserved)
                trace("eraser-state: setters changed restricted layer unexpectedly");
        } else {
            trace(QLatin1String("eraser-size: apply reason=")
                + QLatin1String(reason)
                + QLatin1String(" source=") + QLatin1String(source)
                + QLatin1String(" class=") + QLatin1String(eraserClass)
                + QLatin1String(" before=")
                + QString::number(beforeRadius, 'f', 3)
                + QLatin1String(" requested=")
                + QString::number(desiredRadius, 'f', 3)
                + QLatin1String(" after=")
                + QString::number(afterRadius, 'f', 3)
                + QLatin1String(" policy=") + QString::number(afterPolicy)
                + QLatin1String(" layer=")
                + QString::fromUtf8(
                    afterLayer.data(), static_cast<int>(afterLayer.size()))
                + QLatin1String(" policy-layer-preserved=")
                + (policyVerified && layerPreserved
                    ? QLatin1String("yes") : QLatin1String("no")));
            if (!policyVerified || !layerPreserved)
                trace("eraser-size: setter changed policy/layer unexpectedly");
        }
        return radiusApplied && policyVerified && layerPreserved;
    } catch (...) {
        trace(QLatin1String(applyPolicy ? "eraser-state" : "eraser-size")
            + QLatin1String(": exact eraser rejected configuration reason=")
            + QLatin1String(reason));
        return false;
    }
}

// Apply plugin-owned eraser state to every exact live core-Eraser subclass.
// The size-only entry leaves policy untouched; the state entry verifies an
// exact policy/radius readback. All shared_ptr owners remain in scope while
// their raw firmware objects are inspected or called.
static bool applyConfiguredEraserConfigurationForWidgetImpl(
    Dependencies const& dependencies,
    QObject* widgetObject,
    bool applyPolicy,
    int desiredPolicy,
    char const* reason) {
    if (!eraserState(dependencies).sizeApisReady || !widgetObject
            || !cnt::notebook_widget::isNotebookWidget(widgetObject)) {
        return false;
    }
    if (applyPolicy && (desiredPolicy < 0 || desiredPolicy > 1
            || !firmwareApi(dependencies).eraserSetPolicy
            || !firmwareApi(dependencies).eraserPolicy)) {
        trace(QLatin1String("eraser-state: invalid or unavailable policy reason=")
            + QLatin1String(reason));
        return false;
    }

    int const index = settingsStore(dependencies).configuredEraserSizeIndex();
    if (!validEraserSizeIndex(index))
        return false;
    void* const editor = cnt::notebook_widget::notePadEditor(widgetObject);
    if (!editor || !firmwareApi(dependencies).editorGetRenderer || !firmwareApi(dependencies).rendererGetBackend)
        return false;

    try {
        SharedRenderer const renderer = firmwareApi(dependencies).editorGetRenderer(editor);
        void* const backend = renderer
            ? firmwareApi(dependencies).rendererGetBackend(renderer.get()) : nullptr;
        void* const expectedNeboVptr = firmwareApi(dependencies).neboBackendVtable
            ? static_cast<char*>(firmwareApi(dependencies).neboBackendVtable) + 8 : nullptr;
        if (!backend || *reinterpret_cast<void**>(backend) != expectedNeboVptr) {
            trace(QLatin1String(applyPolicy ? "eraser-state" : "eraser-size")
                + QLatin1String(": unsupported backend reason=")
                + QLatin1String(reason));
            return false;
        }

        void* const pageController = *reinterpret_cast<void**>(
            static_cast<char*>(backend) + firmwarePins(dependencies).neboBackendPageControllerOffset);
        void* const layoutGrid = pageController
            ? *reinterpret_cast<void**>(static_cast<char*>(pageController)
                + firmwarePins(dependencies).pageControllerLayoutGridOffset)
            : nullptr;
        if (!pageController || !layoutGrid || !firmwareApi(dependencies).layoutGridLineGap
                || !firmwareApi(dependencies).eraserWidthFromThicknessRatio) {
            trace(QLatin1String(applyPolicy ? "eraser-state" : "eraser-size")
                + QLatin1String(": page grid unavailable reason=")
                + QLatin1String(reason));
            return false;
        }
        float const lineGap = firmwareApi(dependencies).layoutGridLineGap(layoutGrid);
        if (!(lineGap > 0.0f) || lineGap > 10000.0f) {
            trace(QLatin1String(applyPolicy ? "eraser-state" : "eraser-size")
                + QLatin1String(": invalid line gap reason=")
                + QLatin1String(reason));
            return false;
        }
        float const width = firmwareApi(dependencies).eraserWidthFromThicknessRatio(
            firmwarePins(dependencies).eraserSizeRatios[index], lineGap);
        float const radius = width * 0.5f;
        if (!(radius > 0.0f) || radius > 10000.0f) {
            trace(QLatin1String(applyPolicy ? "eraser-state" : "eraser-size")
                + QLatin1String(": invalid converted radius reason=")
                + QLatin1String(reason));
            return false;
        }

        SharedPlatformInputDispatcher const inputDispatcher =
            firmwareApi(dependencies).pageControllerInputDispatcher(pageController);
        if (!inputDispatcher)
            return false;
        SharedTool const currentTool =
            firmwareApi(dependencies).platformInputDispatcherGetCurrentTool(inputDispatcher.get());
        std::vector<void*> seen;
        int exactCount = 0;
        int appliedCount = 0;

        void* const currentRaw = currentTool.get();
        if (exactEraserSizeClass(dependencies, currentRaw)) {
            seen.push_back(currentRaw);
            ++exactCount;
            if (applyConfigurationToExactEraser(
                    dependencies,
                    currentRaw,
                    radius,
                    applyPolicy,
                    desiredPolicy,
                    reason,
                    "current")) {
                ++appliedCount;
            }
        }

        void* const boxFactory = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(inputDispatcher.get())
                + firmwarePins(dependencies).pidBoxFactoryOffset);
        if (boxFactory && firmwareApi(dependencies).compositeBoxFactoryBackends) {
            ActiveBackendMap const backends =
                firmwareApi(dependencies).compositeBoxFactoryBackends(boxFactory);
            std::string const emptyId;
            for (ActiveBackendMap::const_iterator it = backends.begin();
                    it != backends.end(); ++it) {
                void* const activeBackend = it->second.get();
                if (!activeBackend)
                    continue;
                void* const vptr = *reinterpret_cast<void**>(activeBackend);
                if (!vptr)
                    continue;
                typedef SharedTool (*ActiveBackendGetToolFn)(
                    void*, int, std::string const&);
                ActiveBackendGetToolFn const getTool =
                    *reinterpret_cast<ActiveBackendGetToolFn*>(
                        static_cast<char*>(vptr)
                            + firmwarePins(dependencies).activeBackendGetToolVtableSlot);
                if (!getTool)
                    continue;
                SharedTool const cached = getTool(activeBackend, 4, emptyId);
                void* const raw = cached.get();
                if (!exactEraserSizeClass(dependencies, raw)
                        || std::find(seen.begin(), seen.end(), raw)
                            != seen.end()) {
                    continue;
                }
                seen.push_back(raw);
                ++exactCount;
                QByteArray const source = QByteArray("backend:")
                    + QByteArray(
                        it->first.data(), static_cast<int>(it->first.size()));
                if (applyConfigurationToExactEraser(
                        dependencies,
                        raw,
                        radius,
                        applyPolicy,
                        desiredPolicy,
                        reason,
                        source.constData())) {
                    ++appliedCount;
                }
            }
        }

        // DrawingBackend keeps the kind-4 shared_ptr at +0x4c. This fallback
        // covers a transient main backend not yet represented in backends().
        if (boxFactory && firmwareApi(dependencies).compositeBoxFactoryMainBackend
                && firmwareApi(dependencies).drawingBackendVtable) {
            SharedActiveBackend const mainBackend =
                firmwareApi(dependencies).compositeBoxFactoryMainBackend(boxFactory);
            void* const rawBackend = mainBackend.get();
            void* const expectedDrawingVptr =
                static_cast<char*>(firmwareApi(dependencies).drawingBackendVtable) + 8;
            if (rawBackend
                    && *reinterpret_cast<void**>(rawBackend)
                        == expectedDrawingVptr) {
                void* const rawEraser = *reinterpret_cast<void**>(
                    static_cast<char*>(rawBackend)
                        + firmwarePins(dependencies).drawingBackendEraserToolOffset);
                if (exactEraserSizeClass(dependencies, rawEraser)
                        && std::find(seen.begin(), seen.end(), rawEraser)
                            == seen.end()) {
                    seen.push_back(rawEraser);
                    ++exactCount;
                    if (applyConfigurationToExactEraser(
                            dependencies,
                            rawEraser,
                            radius,
                            applyPolicy,
                            desiredPolicy,
                            reason,
                            "main-fallback")) {
                        ++appliedCount;
                    }
                }
            }
        }

        trace(QLatin1String(applyPolicy ? "eraser-state" : "eraser-size")
            + QLatin1String(": fanout reason=")
            + QLatin1String(reason)
            + (applyPolicy
                ? QLatin1String(" policy=") + QString::number(desiredPolicy)
                : QString())
            + QLatin1String(" index=") + QString::number(index)
            + QLatin1String(" ratio=")
            + QString::number(firmwarePins(dependencies).eraserSizeRatios[index], 'f', 2)
            + QLatin1String(" line-gap=")
            + QString::number(lineGap, 'f', 3)
            + QLatin1String(" radius=")
            + QString::number(radius, 'f', 3)
            + QLatin1String(" exact=") + QString::number(exactCount)
            + QLatin1String(" applied=") + QString::number(appliedCount));
        return exactCount > 0 && appliedCount == exactCount;
    } catch (...) {
        trace(QLatin1String(applyPolicy ? "eraser-state" : "eraser-size")
            + QLatin1String(": live fanout threw reason=")
            + QLatin1String(reason));
        return false;
    }
}

static bool applyConfiguredEraserSizeForWidgetImpl(
        Dependencies const& dependencies,
        QObject* widgetObject,
        char const* reason) {
    return applyConfiguredEraserConfigurationForWidgetImpl(
        dependencies, widgetObject, false, -1, reason);
}

static bool applyConfiguredEraserStateForWidgetImpl(
        Dependencies const& dependencies,
        QObject* widgetObject,
        int desiredPolicy,
        char const* reason) {
    return applyConfiguredEraserConfigurationForWidgetImpl(
        dependencies, widgetObject, true, desiredPolicy, reason);
}

static bool armLayerAwareDrawingEraser(
    Dependencies const& dependencies,
    VtableCallbacks const& callbacks,
    void* tool,
    std::string const& layerId) {
    ConcreteDrawingEraserKind const kind =
        concreteDrawingEraserKind(dependencies, tool);
    if (kind == kNotDrawingEraser)
        return false;

    void* stockVtable = nullptr;
    void** layerVtable = nullptr;
    bool* ready = nullptr;
    DrawingEraserSelectionFromPointsFn* original = nullptr;
    DrawingEraserSelectionFromPointsFn replacement = nullptr;
    uintptr_t expectedSelectionVma = 0;
    char const* implementation = nullptr;
    if (kind == kSntDrawingEraser) {
        stockVtable = firmwareApi(dependencies).drawingEraserVtable;
        layerVtable = layerState(dependencies).drawingEraserVtable;
        ready = &layerState(dependencies).drawingEraserVtableReady;
        original = &firmwareApi(dependencies).drawingEraserSelectionFromPointsOriginal;
        replacement = callbacks.drawingEraserSelectionFromPoints;
        expectedSelectionVma = firmwarePins(dependencies).drawingEraserSelectionFromPointsVma;
        implementation = "snt";
    } else if (kind == kPlainDrawingEraser) {
        stockVtable = firmwareApi(dependencies).plainDrawingEraserVtable;
        layerVtable = layerState(dependencies).plainDrawingEraserVtable;
        ready = &layerState(dependencies).plainDrawingEraserVtableReady;
        original = &firmwareApi(dependencies).plainDrawingEraserSelectionFromPointsOriginal;
        replacement = callbacks.plainDrawingEraserSelectionFromPoints;
        expectedSelectionVma = firmwarePins(dependencies).plainDrawingEraserSelectionFromPointsVma;
        implementation = "global";
    } else if (kind == kDiagramEraser) {
        if (!firmwareApi(dependencies).selectionSelectPolygon || !firmwareApi(dependencies).eraserStrokerPolygon)
            return false;
        stockVtable = firmwareApi(dependencies).diagramEraserVtable;
        layerVtable = layerState(dependencies).diagramEraserVtable;
        ready = &layerState(dependencies).diagramEraserVtableReady;
        original = &firmwareApi(dependencies).diagramEraserSelectionFromPointsOriginal;
        replacement = callbacks.diagramEraserSelectionFromPoints;
        expectedSelectionVma = firmwarePins(dependencies).coreEraserSelectionFromPointsVma;
        implementation = "diagram-layer-first";
    }

    void* const stockVptr = static_cast<char*>(stockVtable) + 8;
    void* const layerVptr = static_cast<void*>(layerVtable + 2);
    void* const currentVptr = *reinterpret_cast<void**>(tool);
    if (currentVptr != stockVptr
            && (!*ready
                || currentVptr != layerVptr)) {
        return false;
    }
    if (!initializeLayerAwareEraserVtable(
            dependencies,
            stockVtable,
            layerVtable,
            ready,
            original,
            replacement,
            expectedSelectionVma,
            implementation)) {
        return false;
    }
    if (kind == kDiagramEraser) {
        if (!installLayerAwareDiagramEraserUpdateHook(
                dependencies, callbacks)) {
            trace(QLatin1String(
                "layers: diagram eraser custom-layer core-only hook unavailable"));
            return false;
        }
        layerState(dependencies).diagramEraserObserverTraceBudget = 60;
    }
    if (currentVptr == stockVptr)
        *reinterpret_cast<void**>(tool) = layerVptr;
    return restrictToolToLayerImpl(dependencies, tool, layerId);
}

// ToolDispatcher::restrictToLayer uses this exact CompositeBoxFactory::backends
// map traversal and ActiveBackend vslot +0x2c to reach every cached concrete
// tool. Enumerating kind 4 here arms cached DrawingErasers before a later tool
// switch, including pages whose CompositeBoxFactory::mainBackend is a grid
// backend rather than DrawingBackend.
static bool armLayerAwareDrawingErasersImpl(
    Dependencies const& dependencies,
    VtableCallbacks const& callbacks,
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId,
    bool* currentIsEraser) {
    bool const currentEraser =
        isLayerAwareDrawingEraser(dependencies, currentTool);
    trace(QLatin1String("layers: eraser probe current ")
        + toolIdentity(dependencies, currentTool, false));
    if (currentIsEraser)
        *currentIsEraser = currentEraser;

    std::vector<void*> seen;
    int backendCount = 0;
    int drawingEraserCount = 0;
    int sntEraserCount = 0;
    int plainEraserCount = 0;
    int diagramEraserCount = 0;
    int armedCount = 0;
    int diagramPenCount = 0;
    int diagramPenRestrictedCount = 0;
    if (currentEraser) {
        seen.push_back(currentTool);
        ++drawingEraserCount;
        ConcreteDrawingEraserKind const kind =
            concreteDrawingEraserKind(dependencies, currentTool);
        if (kind == kSntDrawingEraser)
            ++sntEraserCount;
        else if (kind == kPlainDrawingEraser)
            ++plainEraserCount;
        else
            ++diagramEraserCount;
        if (armLayerAwareDrawingEraser(
                dependencies, callbacks, currentTool, layerId)) {
            ++armedCount;
        }
    }

    if (!inputDispatcher || !firmwareApi(dependencies).compositeBoxFactoryBackends) {
        trace("layers: eraser adapter backend enumeration unavailable");
        return armedCount > 0 && diagramEraserCount == 0;
    }
    void* const boxFactory = *reinterpret_cast<void**>(
        static_cast<char*>(inputDispatcher) + firmwarePins(dependencies).pidBoxFactoryOffset);
    if (!boxFactory) {
        trace("layers: eraser adapter box factory missing");
        return armedCount > 0 && diagramEraserCount == 0;
    }

    try {
        ActiveBackendMap const backends =
            firmwareApi(dependencies).compositeBoxFactoryBackends(boxFactory);
        std::string const emptyId;
        for (ActiveBackendMap::const_iterator it = backends.begin();
                it != backends.end(); ++it) {
            ++backendCount;
            void* const backend = it->second.get();
            if (!backend)
                continue;
            void* const vptr = *reinterpret_cast<void**>(backend);
            if (!vptr)
                continue;
            typedef SharedTool (*ActiveBackendGetToolFn)(
                void*, int, std::string const&);
            ActiveBackendGetToolFn const getTool =
                *reinterpret_cast<ActiveBackendGetToolFn*>(
                    static_cast<char*>(vptr)
                        + firmwarePins(dependencies).activeBackendGetToolVtableSlot);
            if (!getTool)
                continue;

            SharedTool const eraser = getTool(backend, 4, emptyId);
            void* const raw = eraser.get();
            trace(QLatin1String("layers: eraser probe backend=")
                + QString::fromUtf8(
                    it->first.data(), static_cast<int>(it->first.size()))
                + QLatin1String(" backend-vptr=") + pointerIdentity(vptr)
                + QLatin1String(" getTool=")
                + pointerIdentity(*reinterpret_cast<void**>(
                    static_cast<char*>(vptr)
                        + firmwarePins(dependencies).activeBackendGetToolVtableSlot))
                + QLatin1String(" kind4 ")
                + toolIdentity(dependencies, raw, true));
            ConcreteDrawingEraserKind const rawKind =
                concreteDrawingEraserKind(dependencies, raw);

            // DiagramActiveBackend::getTool maps kind 4 to its cached
            // DiagramEraser at backend+0x64 (BN 0x738c4e) and pen kinds 0-2
            // to its cached DiagramPen at +0x5c (BN 0x738c76). Only after the
            // kind-4 result proves this is the exact Diagram backend do we
            // fetch kind 0. DiagramPen::restrictToLayer assigns the selected
            // ID both to Tool state and locked Diagram data+0x60 (BN
            // 0x7c0e76..0x7c0e8c). The pen-down guard below independently
            // reasserts that ID at the exact point core Pen consumes it.
            if (rawKind == kDiagramEraser) {
                SharedTool const pen = getTool(backend, 0, emptyId);
                void* const penRaw = pen.get();
                bool const exactDiagramPen =
                    isExactOrLayerGuardedDiagramPen(dependencies, penRaw);
                bool const penRestricted = exactDiagramPen
                    && armLayerGuardedDiagramPen(
                        dependencies, callbacks, penRaw, layerId);
                if (exactDiagramPen)
                    ++diagramPenCount;
                if (penRestricted)
                    ++diagramPenRestrictedCount;
                trace(QLatin1String(
                    "layers: diagram cached pen(kind0) backend=")
                    + QString::fromUtf8(
                        it->first.data(), static_cast<int>(it->first.size()))
                    + QLatin1String(" ")
                    + toolIdentity(dependencies, penRaw, false)
                    + QLatin1String(" exact-or-guarded=")
                    + (exactDiagramPen
                        ? QLatin1String("yes") : QLatin1String("no"))
                    + QLatin1String(" restricted=")
                    + (penRestricted
                        ? QLatin1String("yes") : QLatin1String("no")));
            }
            if (!isLayerAwareDrawingEraser(dependencies, raw)
                    || std::find(seen.begin(), seen.end(), raw) != seen.end()) {
                continue;
            }
            seen.push_back(raw);
            ++drawingEraserCount;
            ConcreteDrawingEraserKind const kind = rawKind;
            if (kind == kSntDrawingEraser)
                ++sntEraserCount;
            else if (kind == kPlainDrawingEraser)
                ++plainEraserCount;
            else
                ++diagramEraserCount;
            if (armLayerAwareDrawingEraser(
                    dependencies, callbacks, raw, layerId)) {
                ++armedCount;
            }
        }
    } catch (...) {
        trace("layers: eraser adapter backend enumeration threw");
    }

    if (armedCount > 0)
        layerState(dependencies).eraserTraceBudget = 8;
    bool const diagramRoutingReady = diagramEraserCount == 0
        || diagramPenRestrictedCount > 0;
    trace(QLatin1String("layers: eraser adapters backends=")
        + QString::number(backendCount)
        + QLatin1String(" drawing=")
        + QString::number(drawingEraserCount)
        + QLatin1String(" snt=") + QString::number(sntEraserCount)
        + QLatin1String(" global=") + QString::number(plainEraserCount)
        + QLatin1String(" diagram=") + QString::number(diagramEraserCount)
        + QLatin1String(" armed=") + QString::number(armedCount)
        + QLatin1String(" diagram-pens=") + QString::number(diagramPenCount)
        + QLatin1String(" pen-restricted=")
        + QString::number(diagramPenRestrictedCount)
        + QLatin1String(" diagram-pen-cache=")
        + (diagramRoutingReady
            ? QLatin1String("ready") : QLatin1String("missing"))
        + QLatin1String(" current=")
        + (currentEraser
            ? QLatin1String("yes") : QLatin1String("no")));
    return armedCount > 0 && diagramRoutingReady;
}

// Mirror DrawingBackend's writing caches. Pen kinds 0-2 share +0x44 and kind
// 6's DrawingBrush also uses atk::core::Pen::penDown, so both writing paths
// retain the layer across category changes. All exact eraser classes are
// armed separately with narrow selection adapters; Diagram uses layer-first
// geometry while the other two intersect their stock selections.
static bool restrictBackendWritingToolsToLayerImpl(
    Dependencies const& dependencies,
    VtableCallbacks const& callbacks,
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId) {
    if (!inputDispatcher || !firmwareApi(dependencies).compositeBoxFactoryMainBackend
            || !firmwareApi(dependencies).drawingBackendVtable) {
        trace("layers: cached writing-tool routing APIs unavailable");
        return false;
    }
    void* const boxFactory = *reinterpret_cast<void**>(
        static_cast<char*>(inputDispatcher) + firmwarePins(dependencies).pidBoxFactoryOffset);
    if (!boxFactory) {
        trace("layers: cached writing-tool routing box factory missing");
        return false;
    }
    SharedActiveBackend const backend =
        firmwareApi(dependencies).compositeBoxFactoryMainBackend(boxFactory);
    void* const raw = backend.get();
    if (!raw) {
        trace("layers: cached writing-tool routing main backend missing");
        return false;
    }
    void* const expectedVptr = static_cast<char*>(firmwareApi(dependencies).drawingBackendVtable) + 8;
    if (*reinterpret_cast<void**>(raw) != expectedVptr) {
        trace("layers: cached writing-tool routing backend is not DrawingBackend");
        return false;
    }

    void* const eraser = *reinterpret_cast<void**>(
        static_cast<char*>(raw) + firmwarePins(dependencies).drawingBackendEraserToolOffset);
    bool const eraserArmed = eraser
        && armLayerAwareDrawingEraser(
            dependencies, callbacks, eraser, layerId);
    trace(QLatin1String("layers: cached eraser(kind4) present=")
        + (eraser ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" current=")
        + (eraser && eraser == currentTool
            ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" adapter=")
        + (eraserArmed
            ? QLatin1String("armed-to-active-layer")
            : QLatin1String("arm-failed")));

    size_t const routedCount = sizeof(firmwarePins(dependencies).drawingBackendRestrictedToolOffsets)
        / sizeof(firmwarePins(dependencies).drawingBackendRestrictedToolOffsets[0]);
    int restricted = 0;
    for (size_t i = 0; i < routedCount; ++i) {
        void* const tool = *reinterpret_cast<void**>(
            static_cast<char*>(raw) + firmwarePins(dependencies).drawingBackendRestrictedToolOffsets[i]);
        bool const applied = tool
            && restrictToolToLayerImpl(dependencies, tool, layerId);
        if (applied)
            ++restricted;
        trace(QLatin1String("layers: cached ")
            + QLatin1String(firmwarePins(dependencies).drawingBackendRestrictedToolNames[i])
            + QLatin1String(" present=")
            + (tool ? QLatin1String("yes") : QLatin1String("no"))
            + QLatin1String(" current=")
            + (tool && tool == currentTool
                ? QLatin1String("yes") : QLatin1String("no"))
            + QLatin1String(" restricted=")
            + (applied ? QLatin1String("yes") : QLatin1String("no")));
    }

    // Best-effort. The pen is offset[0], attempted first, so restricted > 0 means
    // at least the primary writing tool now carries the layer. Eraser arming and
    // full completeness are reported for the trace but do not gate pen routing.
    if (restricted == static_cast<int>(routedCount) && eraserArmed)
        trace("layers: pen, selector, brush restricted; eraser layer adapter armed");
    else if (restricted > 0)
        trace("layers: cached writing-tool restriction partial");
    else
        trace("layers: cached writing-tool restriction found no writing tool");
    return restricted > 0;
}

static bool isDiagramDrawingEraserImpl(
        Dependencies const& dependencies,
        void* tool) {
    return concreteDrawingEraserKind(dependencies, tool) == kDiagramEraser;
}

} // namespace

bool diagramPenDown(
        Dependencies const& dependencies,
        void* pen,
        void const* pointerInfo) {
    return layerGuardedDiagramPenDown(dependencies, pen, pointerInfo);
}

void* drawingEraserSelectionFromPoints(
        Dependencies const& dependencies,
        void* result,
        void* eraser,
        void const* points) {
    return layerAwareDrawingEraserSelectionFromPoints(
        dependencies, result, eraser, points);
}

void* plainDrawingEraserSelectionFromPoints(
        Dependencies const& dependencies,
        void* result,
        void* eraser,
        void const* points) {
    return layerAwarePlainDrawingEraserSelectionFromPoints(
        dependencies, result, eraser, points);
}

void* diagramEraserSelectionFromPoints(
        Dependencies const& dependencies,
        void* result,
        void* eraser,
        void const* points) {
    return layerAwareDiagramEraserSelectionFromPoints(
        dependencies, result, eraser, points);
}

uint32_t diagramEraserUpdateSelection(
        Dependencies const& dependencies,
        void* eraser,
        void const* selection,
        void const* points) {
    return layerAwareDiagramEraserUpdateSelection(
        dependencies, eraser, selection, points);
}

int32_t diagramEraserEraseSelection(
        Dependencies const& dependencies,
        void* eraser) {
    return layerAwareDiagramEraserEraseSelection(dependencies, eraser);
}

bool armLayerAwareDrawingErasers(
        Dependencies const& dependencies,
        VtableCallbacks const& callbacks,
        void* inputDispatcher,
        void* currentTool,
        std::string const& layerId,
        bool* currentIsEraser) {
    return armLayerAwareDrawingErasersImpl(
        dependencies,
        callbacks,
        inputDispatcher,
        currentTool,
        layerId,
        currentIsEraser);
}

bool isDiagramDrawingEraser(
        Dependencies const& dependencies,
        void* tool) {
    return isDiagramDrawingEraserImpl(dependencies, tool);
}

bool restrictToolToLayer(
        Dependencies const& dependencies,
        void* tool,
        std::string const& layerId) {
    return restrictToolToLayerImpl(dependencies, tool, layerId);
}

bool restrictBackendWritingToolsToLayer(
        Dependencies const& dependencies,
        VtableCallbacks const& callbacks,
        void* inputDispatcher,
        void* currentTool,
        std::string const& layerId) {
    return restrictBackendWritingToolsToLayerImpl(
        dependencies,
        callbacks,
        inputDispatcher,
        currentTool,
        layerId);
}

bool applyConfiguredEraserSizeForWidget(
        Dependencies const& dependencies,
        QObject* widgetObject,
        char const* reason) {
    return applyConfiguredEraserSizeForWidgetImpl(
        dependencies, widgetObject, reason);
}

bool applyConfiguredEraserStateForWidget(
        Dependencies const& dependencies,
        QObject* widgetObject,
        int desiredPolicy,
        char const* reason) {
    return applyConfiguredEraserStateForWidgetImpl(
        dependencies, widgetObject, desiredPolicy, reason);
}

} // namespace layers_eraser
} // namespace cnt
