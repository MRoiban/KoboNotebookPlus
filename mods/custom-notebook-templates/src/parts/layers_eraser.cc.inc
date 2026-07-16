#line 4073 "src/customnotebooktemplates.cc"

// Virtual atk::core::Tool::restrictToLayer(std::string const&) via vtable slot
// +0x54 on the concrete tool. For a Pen it stores the string penDown reads; for
// a ToolDispatcher (diagram backends) it runs the override. Returns false only
// when the tool or its vtable entry is missing.
static bool restrictToolToLayer(void* tool, std::string const& layerId) {
    if (!tool)
        return false;
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return false;
    typedef void (*ToolRestrictToLayerFn)(void*, std::string const&);
    ToolRestrictToLayerFn const restrictToLayer =
        *reinterpret_cast<ToolRestrictToLayerFn*>(
            static_cast<char*>(vptr) + kToolRestrictToLayerVtableSlot);
    if (!restrictToLayer)
        return false;
    restrictToLayer(tool, layerId);
    return true;
}

static std::string restrictedToolLayer(void* tool) {
    if (!tool)
        return std::string();
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return std::string();
    typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
    ToolRestrictedLayerFn const restrictedLayer =
        *reinterpret_cast<ToolRestrictedLayerFn*>(
            static_cast<char*>(vptr) + kToolRestrictedLayerVtableSlot);
    return restrictedLayer ? restrictedLayer(tool) : std::string();
}

// DiagramPen::penDown (BN 0x7bf1e4) calls SmartPen::penDown, which reaches
// core Pen::penDown at BN 0xa93cf8. Core Pen then reads this same virtual
// restrictedLayer() value before setStrokeLayer at BN 0xa894be. Reassert the
// per-tool desired ID immediately before the stock DiagramPen implementation;
// this is both the narrow fix and the decisive on-device trace point.
static bool layerGuardedDiagramPenDown(
    void* pen,
    void const* pointerInfo) {
    if (!firmwareApi().diagramPenPenDownOriginal)
        return false;

    std::string desired;
    {
        QMutexLocker locker(&layerState().desiredDiagramPenLayersMutex);
        std::map<void*, std::string>::const_iterator const it =
            layerState().desiredDiagramPenLayers.find(pen);
        if (it != layerState().desiredDiagramPenLayers.end())
            desired = it->second;
    }

    std::string before;
    std::string after;
    bool corrected = false;
    try {
        before = restrictedToolLayer(pen);
        if (!desired.empty() && before != desired) {
            corrected = restrictToolToLayer(pen, desired);
        }
        after = restrictedToolLayer(pen);
    } catch (...) {
        trace("layers: diagram pen-down layer guard threw; stock pen-down preserved");
    }

    if (layerState().diagramPenTraceBudget > 0) {
        --layerState().diagramPenTraceBudget;
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
    return firmwareApi().diagramPenPenDownOriginal(pen, pointerInfo);
}

static bool initializeLayerGuardedDiagramPenVtable() {
    if (layerState().diagramPenVtableReady)
        return true;
    if (!firmwareApi().diagramPenVtable)
        return false;

    memcpy(
        layerState().diagramPenVtable,
        firmwareApi().diagramPenVtable,
        kDiagramPenVtableWords * sizeof(void*));
    size_t const slot = 2
        + kDiagramPenPenDownVtableSlot / sizeof(void*);
    if (slot >= kDiagramPenVtableWords)
        return false;

    union {
        void* pointer;
        DiagramPenPenDownFn function;
    } original;
    original.pointer = layerState().diagramPenVtable[slot];
    if (!pointerMatchesVma(original.pointer, kDiagramPenPenDownVma)) {
        trace("layers: diagram pen vtable clone rejected unexpected penDown slot");
        return false;
    }
    firmwareApi().diagramPenPenDownOriginal = original.function;

    union {
        void* pointer;
        DiagramPenPenDownFn function;
    } replacement;
    replacement.function = layerGuardedDiagramPenDown;
    layerState().diagramPenVtable[slot] = replacement.pointer;
    layerState().diagramPenVtableReady = true;
    trace("layers: diagram pen-down layer guard vtable clone verified");
    return true;
}

static bool isExactOrLayerGuardedDiagramPen(void* tool) {
    if (!tool || !firmwareApi().diagramPenVtable)
        return false;
    void* const vptr = *reinterpret_cast<void**>(tool);
    return vptr == static_cast<char*>(firmwareApi().diagramPenVtable) + 8
        || (layerState().diagramPenVtableReady
            && vptr == static_cast<void*>(
                layerState().diagramPenVtable + 2));
}

static bool armLayerGuardedDiagramPen(
    void* pen,
    std::string const& layerId) {
    if (!isExactOrLayerGuardedDiagramPen(pen)
            || !initializeLayerGuardedDiagramPenVtable()) {
        return false;
    }
    void* const stockVptr = static_cast<char*>(firmwareApi().diagramPenVtable) + 8;
    if (*reinterpret_cast<void**>(pen) == stockVptr) {
        *reinterpret_cast<void**>(pen) = static_cast<void*>(
            layerState().diagramPenVtable + 2);
    }
    {
        QMutexLocker locker(&layerState().desiredDiagramPenLayersMutex);
        layerState().desiredDiagramPenLayers[pen] = layerId;
    }
    layerState().diagramPenTraceBudget = 8;
    return restrictToolToLayer(pen, layerId);
}

static void clearLayerAwareEraserSelection(void* selection) {
    if (!selection || !firmwareApi().selectionSelectNone)
        return;
    try {
        firmwareApi().selectionSelectNone(selection);
    } catch (...) {
        // The original firmware selection remains valid. Avoid allowing an
        // exception to cross the virtual callback boundary during pen input.
    }
}

// Apply the same named-layer/Intersection operation used by
// Selector::computeSelection after a concrete eraser has performed its stock
// polygon hit test and stroke-boundary adjustment.
static void filterLayerAwareEraserSelection(
    void* result,
    void* eraser,
    char const* implementation) {
    try {
        if (!eraser || !result || !firmwareApi().selectionSelectLayer || !firmwareApi().selectionIsEmpty) {
            clearLayerAwareEraserSelection(result);
            return;
        }
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (!vptr) {
            clearLayerAwareEraserSelection(result);
            return;
        }
        typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
        ToolRestrictedLayerFn const restrictedLayer =
            *reinterpret_cast<ToolRestrictedLayerFn*>(
                static_cast<char*>(vptr) + 0x58);
        if (!restrictedLayer) {
            clearLayerAwareEraserSelection(result);
            return;
        }
        std::string const& activeLayer = restrictedLayer(eraser);
        if (activeLayer.empty()) {
            clearLayerAwareEraserSelection(result);
            return;
        }
        bool const emptyBefore = firmwareApi().selectionIsEmpty(result);
        firmwareApi().selectionSelectLayer(result, activeLayer, kSelectionModeIntersect);
        bool const emptyAfter = firmwareApi().selectionIsEmpty(result);
        if (layerState().eraserTraceBudget > 0) {
            --layerState().eraserTraceBudget;
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
        clearLayerAwareEraserSelection(result);
        if (layerState().eraserTraceBudget > 0) {
            --layerState().eraserTraceBudget;
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
    void* result,
    void* eraser,
    void const* points) {
    if (!firmwareApi().drawingEraserSelectionFromPointsOriginal)
        return result;
    firmwareApi().drawingEraserSelectionFromPointsOriginal(result, eraser, points);
    filterLayerAwareEraserSelection(result, eraser, "snt");
    return result;
}

static void* layerAwarePlainDrawingEraserSelectionFromPoints(
    void* result,
    void* eraser,
    void const* points) {
    if (!firmwareApi().plainDrawingEraserSelectionFromPointsOriginal)
        return result;
    firmwareApi().plainDrawingEraserSelectionFromPointsOriginal(result, eraser, points);
    filterLayerAwareEraserSelection(result, eraser, "global");
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
    void* result,
    void* eraser,
    void const* points) {
    if (!firmwareApi().diagramEraserSelectionFromPointsOriginal)
        return result;
    firmwareApi().diagramEraserSelectionFromPointsOriginal(result, eraser, points);
    try {
        if (!result || !eraser || !points || !firmwareApi().selectionSelectLayer
                || !firmwareApi().selectionSelectPolygon || !firmwareApi().selectionSelectNone
                || !firmwareApi().selectionIsEmpty || !firmwareApi().selectionAdjustToStrokeBoundaries
                || !firmwareApi().eraserPolicy || !firmwareApi().eraserStrokerPolygon) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (!vptr) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
        ToolRestrictedLayerFn const restrictedLayer =
            *reinterpret_cast<ToolRestrictedLayerFn*>(
                static_cast<char*>(vptr) + 0x58);
        if (!restrictedLayer) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        std::string const& activeLayer = restrictedLayer(eraser);
        if (activeLayer.empty()) {
            clearLayerAwareEraserSelection(result);
            return result;
        }

        bool const stockEmpty = firmwareApi().selectionIsEmpty(result);
        firmwareApi().selectionSelectLayer(result, activeLayer, kSelectionModeReplace);
        bool const layerEmpty = firmwareApi().selectionIsEmpty(result);

        float const width = *reinterpret_cast<float const*>(
            static_cast<char const*>(eraser) + kEraserWidthOffset)
            * *reinterpret_cast<float const*>(
                static_cast<char const*>(eraser) + kEraserViewScaleOffset);
        if (!(width > 0.0f && width < 10000.0f)) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        PointerInfoVectorOpaque const& pointVector =
            *static_cast<PointerInfoVectorOpaque const*>(points);
        std::vector<SelectionPointOpaque> const polygon =
            firmwareApi().eraserStrokerPolygon(width, pointVector);
        if (polygon.empty()) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        firmwareApi().selectionSelectPolygon(
            result,
            &polygon[0],
            static_cast<int>(polygon.size()),
            kSelectionModeIntersect);
        bool geometryEmpty = firmwareApi().selectionIsEmpty(result);

        size_t const customPrefixLength = sizeof(kCustomLayerIdPrefix) - 1;
        bool const customLayer = activeLayer.compare(
            0, customPrefixLength, kCustomLayerIdPrefix) == 0;
        int const policy = firmwareApi().eraserPolicy(eraser);
        bool boundaryAdjusted = false;
        if (customLayer && policy == kEraserPolicyStroke && !geometryEmpty) {
            // Stock DrawingEraser and DiagramEraser use exactly 0.0f for this
            // first whole-stroke expansion. It expands the already layer-
            // scoped Selection; it does not perform a fresh page hit-test.
            firmwareApi().selectionAdjustToStrokeBoundaries(result, 0.0f);
            boundaryAdjusted = true;
            geometryEmpty = firmwareApi().selectionIsEmpty(result);
        }

        if (layerState().eraserTraceBudget > 0) {
            --layerState().eraserTraceBudget;
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
        clearLayerAwareEraserSelection(result);
        if (layerState().eraserTraceBudget > 0) {
            --layerState().eraserTraceBudget;
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
    void* eraser,
    void const* selection,
    void const* points) {
    if (!firmwareApi().diagramEraserUpdateSelectionOriginal)
        return 0;

    int incomingEmpty = -1;
    int committedBeforeEmpty = -1;
    int committedAfterEmpty = -1;
    bool haveRestrict = false;
    std::string activeLayer;
    try {
        if (firmwareApi().selectionIsEmpty && selection)
            incomingEmpty = firmwareApi().selectionIsEmpty(selection) ? 1 : 0;
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (vptr) {
            typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
            ToolRestrictedLayerFn const restrictedLayer =
                *reinterpret_cast<ToolRestrictedLayerFn*>(
                    static_cast<char*>(vptr) + 0x58);
            if (restrictedLayer) {
                activeLayer = restrictedLayer(eraser);
                haveRestrict = true;
            }
        }
    } catch (...) {
        if (layerState().diagramEraserObserverTraceBudget > 0) {
            --layerState().diagramEraserObserverTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser routing inspection failed; stock update preserved"));
        }
        return firmwareApi().diagramEraserUpdateSelectionOriginal(
            eraser, selection, points);
    }

    size_t const customPrefixLength = sizeof(kCustomLayerIdPrefix) - 1;
    bool const customLayer = haveRestrict
        && activeLayer.compare(
            0, customPrefixLength, kCustomLayerIdPrefix) == 0;
    if (!customLayer || !firmwareApi().coreEraserUpdateSelection) {
        return firmwareApi().diagramEraserUpdateSelectionOriginal(
            eraser, selection, points);
    }

    if (firmwareApi().selectionIsEmpty && eraser) {
        try {
            committedBeforeEmpty = firmwareApi().selectionIsEmpty(
                static_cast<char*>(eraser)
                    + kEraserFinalSelectionOffset) ? 1 : 0;
        } catch (...) {
            committedBeforeEmpty = -1;
        }
    }
    if (layerState().diagramEraserObserverTraceBudget > 0) {
        --layerState().diagramEraserObserverTraceBudget;
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
        result = firmwareApi().coreEraserUpdateSelection(eraser, selection, points);
        if (firmwareApi().selectionIsEmpty && eraser) {
            committedAfterEmpty = firmwareApi().selectionIsEmpty(
                static_cast<char*>(eraser)
                    + kEraserFinalSelectionOffset) ? 1 : 0;
        }
    } catch (...) {
        if (layerState().diagramEraserObserverTraceBudget > 0) {
            --layerState().diagramEraserObserverTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser core-only update threw; failed closed"));
        }
        return 0;
    }

    if (layerState().diagramEraserObserverTraceBudget > 0) {
        --layerState().diagramEraserObserverTraceBudget;
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
static int32_t layerAwareDiagramEraserEraseSelection(void* eraser) {
    if (!firmwareApi().diagramEraserEraseSelectionOriginal)
        return 0;

    int committedBeforeEmpty = -1;
    std::string activeLayer;
    try {
        if (firmwareApi().selectionIsEmpty && eraser) {
            committedBeforeEmpty = firmwareApi().selectionIsEmpty(
                static_cast<char*>(eraser)
                    + kEraserFinalSelectionOffset) ? 1 : 0;
        }
        activeLayer = restrictedToolLayer(eraser);
    } catch (...) {}

    if (layerState().diagramEraserObserverTraceBudget > 0) {
        --layerState().diagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser remove enter id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" committed=")
            + emptinessLabel(committedBeforeEmpty));
    }

    int32_t const result = firmwareApi().diagramEraserEraseSelectionOriginal(eraser);

    int committedAfterEmpty = -1;
    try {
        if (firmwareApi().selectionIsEmpty && eraser) {
            committedAfterEmpty = firmwareApi().selectionIsEmpty(
                static_cast<char*>(eraser)
                    + kEraserFinalSelectionOffset) ? 1 : 0;
        }
    } catch (...) {}
    if (layerState().diagramEraserObserverTraceBudget > 0) {
        --layerState().diagramEraserObserverTraceBudget;
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
static bool installLayerAwareDiagramEraserUpdateHook() {
    if (layerState().diagramEraserObserversReady)
        return true;
    if (!layerState().diagramEraserVtableReady || !firmwareApi().coreEraserUpdateSelection)
        return false;

    size_t const updateSlot = 2
        + kDiagramEraserUpdateSelectionVtableSlot / sizeof(void*);
    size_t const eraseSlot = 2
        + kDiagramEraserEraseSelectionVtableSlot / sizeof(void*);
    if (updateSlot >= kDrawingEraserVtableWords
            || eraseSlot >= kDrawingEraserVtableWords)
        return false;

    union {
        void* pointer;
        DiagramEraserUpdateSelectionFn function;
    } originalUpdate;
    originalUpdate.pointer = layerState().diagramEraserVtable[updateSlot];
    if (!pointerMatchesVma(
            originalUpdate.pointer, kDiagramEraserUpdateSelectionVma)) {
        trace(QLatin1String(
            "layers: diagram eraser update hook rejected unexpected slot"));
        return false;
    }

    union {
        void* pointer;
        DiagramEraserEraseSelectionFn function;
    } originalErase;
    originalErase.pointer = layerState().diagramEraserVtable[eraseSlot];
    if (!pointerMatchesVma(
            originalErase.pointer, kDiagramEraserEraseSelectionVma)) {
        trace(QLatin1String(
            "layers: diagram eraser remove observer rejected unexpected slot"));
        return false;
    }

    union {
        void* pointer;
        DiagramEraserUpdateSelectionFn function;
    } replacementUpdate;
    replacementUpdate.function = layerAwareDiagramEraserUpdateSelection;
    union {
        void* pointer;
        DiagramEraserEraseSelectionFn function;
    } replacementErase;
    replacementErase.function = layerAwareDiagramEraserEraseSelection;

    firmwareApi().diagramEraserUpdateSelectionOriginal = originalUpdate.function;
    firmwareApi().diagramEraserEraseSelectionOriginal = originalErase.function;
    layerState().diagramEraserVtable[updateSlot] = replacementUpdate.pointer;
    layerState().diagramEraserVtable[eraseSlot] = replacementErase.pointer;
    layerState().diagramEraserObserversReady = true;
    trace(QLatin1String(
        "layers: diagram eraser core-only update and remove observer installed"));
    return true;
}

static bool initializeLayerAwareEraserVtable(
    void* stockVtable,
    void** layerVtable,
    bool* ready,
    DrawingEraserSelectionFromPointsFn* originalOut,
    DrawingEraserSelectionFromPointsFn replacement,
    uintptr_t expectedSelectionVma,
    char const* implementation) {
    if (*ready)
        return true;
    if (!stockVtable || !firmwareApi().selectionSelectLayer || !firmwareApi().selectionSelectNone)
        return false;

    memcpy(
        layerVtable,
        stockVtable,
        kDrawingEraserVtableWords * sizeof(void*));
    size_t const slot = 2
        + kDrawingEraserSelectionFromPointsVtableSlot / sizeof(void*);
    if (slot >= kDrawingEraserVtableWords)
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

static ConcreteDrawingEraserKind concreteDrawingEraserKind(void* tool) {
    if (!tool)
        return kNotDrawingEraser;
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (firmwareApi().drawingEraserVtable
            && (vptr == static_cast<char*>(firmwareApi().drawingEraserVtable) + 8
                || (layerState().drawingEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerState().drawingEraserVtable + 2)))) {
        return kSntDrawingEraser;
    }
    if (firmwareApi().plainDrawingEraserVtable
            && (vptr == static_cast<char*>(firmwareApi().plainDrawingEraserVtable) + 8
                || (layerState().plainDrawingEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerState().plainDrawingEraserVtable + 2)))) {
        return kPlainDrawingEraser;
    }
    if (firmwareApi().diagramEraserVtable
            && (vptr == static_cast<char*>(firmwareApi().diagramEraserVtable) + 8
                || (layerState().diagramEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerState().diagramEraserVtable + 2)))) {
        return kDiagramEraser;
    }
    return kNotDrawingEraser;
}

static bool isLayerAwareDrawingEraser(void* tool) {
    return concreteDrawingEraserKind(tool) != kNotDrawingEraser;
}

static char const* exactEraserSizeClass(void* tool) {
    if (!tool)
        return nullptr;
    ConcreteDrawingEraserKind const drawingKind =
        concreteDrawingEraserKind(tool);
    if (drawingKind == kSntDrawingEraser)
        return "snt-drawing";
    if (drawingKind == kPlainDrawingEraser)
        return "global-drawing";
    if (drawingKind == kDiagramEraser)
        return "diagram";

    void* const vptr = *reinterpret_cast<void**>(tool);
    if (firmwareApi().coreEraserVtable
            && vptr == static_cast<char*>(firmwareApi().coreEraserVtable) + 8) {
        return "core";
    }
    if (firmwareApi().textEraserSntVtable
            && vptr == static_cast<char*>(firmwareApi().textEraserSntVtable) + 8) {
        return "snt-text";
    }
    if (firmwareApi().mathEraserVtable
            && vptr == static_cast<char*>(firmwareApi().mathEraserVtable) + 8) {
        return "math";
    }
    if (firmwareApi().textEraserVtable
            && vptr == static_cast<char*>(firmwareApi().textEraserVtable) + 8) {
        return "text";
    }
    return nullptr;
}

static bool applyRadiusToExactEraser(
    void* tool,
    float desiredRadius,
    char const* reason,
    char const* source) {
    char const* const eraserClass = exactEraserSizeClass(tool);
    if (!eraserClass || !firmwareApi().eraserSetRadius || !firmwareApi().eraserRadius)
        return false;

    try {
        float const beforeRadius = firmwareApi().eraserRadius(tool);
        int const beforePolicy = firmwareApi().eraserPolicy ? firmwareApi().eraserPolicy(tool) : -1;
        std::string const beforeLayer = restrictedToolLayer(tool);
        firmwareApi().eraserSetRadius(tool, desiredRadius);
        float const afterRadius = firmwareApi().eraserRadius(tool);
        int const afterPolicy = firmwareApi().eraserPolicy ? firmwareApi().eraserPolicy(tool) : -1;
        std::string const afterLayer = restrictedToolLayer(tool);
        float difference = afterRadius - desiredRadius;
        if (difference < 0.0f)
            difference = -difference;
        bool const preserved = beforePolicy == afterPolicy
            && beforeLayer == afterLayer;
        bool const radiusApplied = difference < 0.001f;
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
            + (preserved ? QLatin1String("yes") : QLatin1String("no")));
        if (!preserved)
            trace("eraser-size: setter changed policy/layer unexpectedly");
        return radiusApplied && preserved;
    } catch (...) {
        trace(QLatin1String("eraser-size: exact eraser rejected radius reason=")
            + QLatin1String(reason));
        return false;
    }
}

// Apply one plugin-owned size to every exact live core-Eraser subclass. This
// deliberately duplicates the already-proven backend map traversal instead of
// changing the working layer adapters. All shared_ptr owners remain in scope
// while their raw firmware objects are inspected or called.
static bool applyConfiguredEraserSizeForWidget(
    QObject* widgetObject,
    char const* reason) {
    if (!eraserState().sizeApisReady || !widgetObject
            || !isNotebookWidget(widgetObject)) {
        return false;
    }

    int const index = settingsStore().configuredEraserSizeIndex();
    if (!validEraserSizeIndex(index))
        return false;
    void* const editor = notePadEditor(widgetObject);
    if (!editor || !firmwareApi().editorGetRenderer || !firmwareApi().rendererGetBackend)
        return false;

    try {
        SharedRenderer const renderer = firmwareApi().editorGetRenderer(editor);
        void* const backend = renderer
            ? firmwareApi().rendererGetBackend(renderer.get()) : nullptr;
        void* const expectedNeboVptr = firmwareApi().neboBackendVtable
            ? static_cast<char*>(firmwareApi().neboBackendVtable) + 8 : nullptr;
        if (!backend || *reinterpret_cast<void**>(backend) != expectedNeboVptr) {
            trace(QLatin1String("eraser-size: unsupported backend reason=")
                + QLatin1String(reason));
            return false;
        }

        void* const pageController = *reinterpret_cast<void**>(
            static_cast<char*>(backend) + kNeboBackendPageControllerOffset);
        void* const layoutGrid = pageController
            ? *reinterpret_cast<void**>(static_cast<char*>(pageController)
                + kPageControllerLayoutGridOffset)
            : nullptr;
        if (!pageController || !layoutGrid || !firmwareApi().layoutGridLineGap
                || !firmwareApi().eraserWidthFromThicknessRatio) {
            trace(QLatin1String("eraser-size: page grid unavailable reason=")
                + QLatin1String(reason));
            return false;
        }
        float const lineGap = firmwareApi().layoutGridLineGap(layoutGrid);
        if (!(lineGap > 0.0f) || lineGap > 10000.0f) {
            trace(QLatin1String("eraser-size: invalid line gap reason=")
                + QLatin1String(reason));
            return false;
        }
        float const width = firmwareApi().eraserWidthFromThicknessRatio(
            kEraserSizeRatios[index], lineGap);
        float const radius = width * 0.5f;
        if (!(radius > 0.0f) || radius > 10000.0f) {
            trace(QLatin1String("eraser-size: invalid converted radius reason=")
                + QLatin1String(reason));
            return false;
        }

        SharedPlatformInputDispatcher const inputDispatcher =
            firmwareApi().pageControllerInputDispatcher(pageController);
        if (!inputDispatcher)
            return false;
        SharedTool const currentTool =
            firmwareApi().platformInputDispatcherGetCurrentTool(inputDispatcher.get());
        std::vector<void*> seen;
        int exactCount = 0;
        int appliedCount = 0;

        void* const currentRaw = currentTool.get();
        if (exactEraserSizeClass(currentRaw)) {
            seen.push_back(currentRaw);
            ++exactCount;
            if (applyRadiusToExactEraser(
                    currentRaw, radius, reason, "current")) {
                ++appliedCount;
            }
        }

        void* const boxFactory = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(inputDispatcher.get())
                + kPidBoxFactoryOffset);
        if (boxFactory && firmwareApi().compositeBoxFactoryBackends) {
            ActiveBackendMap const backends =
                firmwareApi().compositeBoxFactoryBackends(boxFactory);
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
                            + kActiveBackendGetToolVtableSlot);
                if (!getTool)
                    continue;
                SharedTool const cached = getTool(activeBackend, 4, emptyId);
                void* const raw = cached.get();
                if (!exactEraserSizeClass(raw)
                        || std::find(seen.begin(), seen.end(), raw)
                            != seen.end()) {
                    continue;
                }
                seen.push_back(raw);
                ++exactCount;
                QByteArray const source = QByteArray("backend:")
                    + QByteArray(
                        it->first.data(), static_cast<int>(it->first.size()));
                if (applyRadiusToExactEraser(
                        raw, radius, reason, source.constData())) {
                    ++appliedCount;
                }
            }
        }

        // DrawingBackend keeps the kind-4 shared_ptr at +0x4c. This fallback
        // covers a transient main backend not yet represented in backends().
        if (boxFactory && firmwareApi().compositeBoxFactoryMainBackend
                && firmwareApi().drawingBackendVtable) {
            SharedActiveBackend const mainBackend =
                firmwareApi().compositeBoxFactoryMainBackend(boxFactory);
            void* const rawBackend = mainBackend.get();
            void* const expectedDrawingVptr =
                static_cast<char*>(firmwareApi().drawingBackendVtable) + 8;
            if (rawBackend
                    && *reinterpret_cast<void**>(rawBackend)
                        == expectedDrawingVptr) {
                void* const rawEraser = *reinterpret_cast<void**>(
                    static_cast<char*>(rawBackend)
                        + kDrawingBackendEraserToolOffset);
                if (exactEraserSizeClass(rawEraser)
                        && std::find(seen.begin(), seen.end(), rawEraser)
                            == seen.end()) {
                    seen.push_back(rawEraser);
                    ++exactCount;
                    if (applyRadiusToExactEraser(
                            rawEraser, radius, reason, "main-fallback")) {
                        ++appliedCount;
                    }
                }
            }
        }

        trace(QLatin1String("eraser-size: fanout reason=")
            + QLatin1String(reason)
            + QLatin1String(" index=") + QString::number(index)
            + QLatin1String(" ratio=")
            + QString::number(kEraserSizeRatios[index], 'f', 2)
            + QLatin1String(" line-gap=")
            + QString::number(lineGap, 'f', 3)
            + QLatin1String(" radius=")
            + QString::number(radius, 'f', 3)
            + QLatin1String(" exact=") + QString::number(exactCount)
            + QLatin1String(" applied=") + QString::number(appliedCount));
        return exactCount > 0 && appliedCount == exactCount;
    } catch (...) {
        trace(QLatin1String("eraser-size: live fanout threw reason=")
            + QLatin1String(reason));
        return false;
    }
}

static bool armLayerAwareDrawingEraser(
    void* tool,
    std::string const& layerId) {
    ConcreteDrawingEraserKind const kind = concreteDrawingEraserKind(tool);
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
        stockVtable = firmwareApi().drawingEraserVtable;
        layerVtable = layerState().drawingEraserVtable;
        ready = &layerState().drawingEraserVtableReady;
        original = &firmwareApi().drawingEraserSelectionFromPointsOriginal;
        replacement = layerAwareDrawingEraserSelectionFromPoints;
        expectedSelectionVma = kDrawingEraserSelectionFromPointsVma;
        implementation = "snt";
    } else if (kind == kPlainDrawingEraser) {
        stockVtable = firmwareApi().plainDrawingEraserVtable;
        layerVtable = layerState().plainDrawingEraserVtable;
        ready = &layerState().plainDrawingEraserVtableReady;
        original = &firmwareApi().plainDrawingEraserSelectionFromPointsOriginal;
        replacement = layerAwarePlainDrawingEraserSelectionFromPoints;
        expectedSelectionVma = kPlainDrawingEraserSelectionFromPointsVma;
        implementation = "global";
    } else if (kind == kDiagramEraser) {
        if (!firmwareApi().selectionSelectPolygon || !firmwareApi().eraserStrokerPolygon)
            return false;
        stockVtable = firmwareApi().diagramEraserVtable;
        layerVtable = layerState().diagramEraserVtable;
        ready = &layerState().diagramEraserVtableReady;
        original = &firmwareApi().diagramEraserSelectionFromPointsOriginal;
        replacement = layerAwareDiagramEraserSelectionFromPoints;
        expectedSelectionVma = kCoreEraserSelectionFromPointsVma;
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
        if (!installLayerAwareDiagramEraserUpdateHook()) {
            trace(QLatin1String(
                "layers: diagram eraser custom-layer core-only hook unavailable"));
            return false;
        }
        layerState().diagramEraserObserverTraceBudget = 60;
    }
    if (currentVptr == stockVptr)
        *reinterpret_cast<void**>(tool) = layerVptr;
    return restrictToolToLayer(tool, layerId);
}

// ToolDispatcher::restrictToLayer uses this exact CompositeBoxFactory::backends
// map traversal and ActiveBackend vslot +0x2c to reach every cached concrete
// tool. Enumerating kind 4 here arms cached DrawingErasers before a later tool
// switch, including pages whose CompositeBoxFactory::mainBackend is a grid
// backend rather than DrawingBackend.
static bool armLayerAwareDrawingErasers(
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId,
    bool* currentIsEraser) {
    bool const currentEraser = isLayerAwareDrawingEraser(currentTool);
    trace(QLatin1String("layers: eraser probe current ")
        + toolIdentity(currentTool, false));
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
            concreteDrawingEraserKind(currentTool);
        if (kind == kSntDrawingEraser)
            ++sntEraserCount;
        else if (kind == kPlainDrawingEraser)
            ++plainEraserCount;
        else
            ++diagramEraserCount;
        if (armLayerAwareDrawingEraser(currentTool, layerId))
            ++armedCount;
    }

    if (!inputDispatcher || !firmwareApi().compositeBoxFactoryBackends) {
        trace("layers: eraser adapter backend enumeration unavailable");
        return armedCount > 0 && diagramEraserCount == 0;
    }
    void* const boxFactory = *reinterpret_cast<void**>(
        static_cast<char*>(inputDispatcher) + kPidBoxFactoryOffset);
    if (!boxFactory) {
        trace("layers: eraser adapter box factory missing");
        return armedCount > 0 && diagramEraserCount == 0;
    }

    try {
        ActiveBackendMap const backends =
            firmwareApi().compositeBoxFactoryBackends(boxFactory);
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
                        + kActiveBackendGetToolVtableSlot);
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
                        + kActiveBackendGetToolVtableSlot))
                + QLatin1String(" kind4 ") + toolIdentity(raw, true));
            ConcreteDrawingEraserKind const rawKind =
                concreteDrawingEraserKind(raw);

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
                    isExactOrLayerGuardedDiagramPen(penRaw);
                bool const penRestricted = exactDiagramPen
                    && armLayerGuardedDiagramPen(penRaw, layerId);
                if (exactDiagramPen)
                    ++diagramPenCount;
                if (penRestricted)
                    ++diagramPenRestrictedCount;
                trace(QLatin1String(
                    "layers: diagram cached pen(kind0) backend=")
                    + QString::fromUtf8(
                        it->first.data(), static_cast<int>(it->first.size()))
                    + QLatin1String(" ") + toolIdentity(penRaw, false)
                    + QLatin1String(" exact-or-guarded=")
                    + (exactDiagramPen
                        ? QLatin1String("yes") : QLatin1String("no"))
                    + QLatin1String(" restricted=")
                    + (penRestricted
                        ? QLatin1String("yes") : QLatin1String("no")));
            }
            if (!isLayerAwareDrawingEraser(raw)
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
            if (armLayerAwareDrawingEraser(raw, layerId))
                ++armedCount;
        }
    } catch (...) {
        trace("layers: eraser adapter backend enumeration threw");
    }

    if (armedCount > 0)
        layerState().eraserTraceBudget = 8;
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
static bool restrictBackendWritingToolsToLayer(
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId) {
    if (!inputDispatcher || !firmwareApi().compositeBoxFactoryMainBackend
            || !firmwareApi().drawingBackendVtable) {
        trace("layers: cached writing-tool routing APIs unavailable");
        return false;
    }
    void* const boxFactory = *reinterpret_cast<void**>(
        static_cast<char*>(inputDispatcher) + kPidBoxFactoryOffset);
    if (!boxFactory) {
        trace("layers: cached writing-tool routing box factory missing");
        return false;
    }
    SharedActiveBackend const backend =
        firmwareApi().compositeBoxFactoryMainBackend(boxFactory);
    void* const raw = backend.get();
    if (!raw) {
        trace("layers: cached writing-tool routing main backend missing");
        return false;
    }
    void* const expectedVptr = static_cast<char*>(firmwareApi().drawingBackendVtable) + 8;
    if (*reinterpret_cast<void**>(raw) != expectedVptr) {
        trace("layers: cached writing-tool routing backend is not DrawingBackend");
        return false;
    }

    void* const eraser = *reinterpret_cast<void**>(
        static_cast<char*>(raw) + kDrawingBackendEraserToolOffset);
    bool const eraserArmed = eraser
        && armLayerAwareDrawingEraser(eraser, layerId);
    trace(QLatin1String("layers: cached eraser(kind4) present=")
        + (eraser ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" current=")
        + (eraser && eraser == currentTool
            ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" adapter=")
        + (eraserArmed
            ? QLatin1String("armed-to-active-layer")
            : QLatin1String("arm-failed")));

    size_t const routedCount = sizeof(kDrawingBackendRestrictedToolOffsets)
        / sizeof(kDrawingBackendRestrictedToolOffsets[0]);
    int restricted = 0;
    for (size_t i = 0; i < routedCount; ++i) {
        void* const tool = *reinterpret_cast<void**>(
            static_cast<char*>(raw) + kDrawingBackendRestrictedToolOffsets[i]);
        bool const applied = tool && restrictToolToLayer(tool, layerId);
        if (applied)
            ++restricted;
        trace(QLatin1String("layers: cached ")
            + QLatin1String(kDrawingBackendRestrictedToolNames[i])
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
