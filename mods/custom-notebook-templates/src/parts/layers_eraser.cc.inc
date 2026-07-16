#line 4073 "src/customnotebooktemplates.cc"

static cnt::layers_eraser::Dependencies layerEraserDependencies() {
    cnt::layers_eraser::Dependencies dependencies = {
        &firmwareApi(),
        &layerState(),
        &eraserState(),
        &settingsStore(),
        &kLayerEraserPins
    };
    return dependencies;
}

// Firmware objects retain these exact callback signatures in writable vtable
// clones. Keep the ABI-facing entries in the umbrella; each thunk immediately
// supplies the process-lifetime dependency aggregate to the real service.
static bool layerGuardedDiagramPenDown(
        void* pen,
        void const* pointerInfo) {
    return cnt::layers_eraser::diagramPenDown(
        layerEraserDependencies(), pen, pointerInfo);
}

static void* layerAwareDrawingEraserSelectionFromPoints(
        void* result,
        void* eraser,
        void const* points) {
    return cnt::layers_eraser::drawingEraserSelectionFromPoints(
        layerEraserDependencies(), result, eraser, points);
}

static void* layerAwarePlainDrawingEraserSelectionFromPoints(
        void* result,
        void* eraser,
        void const* points) {
    return cnt::layers_eraser::plainDrawingEraserSelectionFromPoints(
        layerEraserDependencies(), result, eraser, points);
}

static void* layerAwareDiagramEraserSelectionFromPoints(
        void* result,
        void* eraser,
        void const* points) {
    return cnt::layers_eraser::diagramEraserSelectionFromPoints(
        layerEraserDependencies(), result, eraser, points);
}

static uint32_t layerAwareDiagramEraserUpdateSelection(
        void* eraser,
        void const* selection,
        void const* points) {
    return cnt::layers_eraser::diagramEraserUpdateSelection(
        layerEraserDependencies(), eraser, selection, points);
}

static int32_t layerAwareDiagramEraserEraseSelection(void* eraser) {
    return cnt::layers_eraser::diagramEraserEraseSelection(
        layerEraserDependencies(), eraser);
}

// ToolRoutingOperations has no context parameter. These four adapters are the
// only bridge from that stable callback ABI into the explicit service state.
static bool armLayerAwareDrawingErasers(
        void* inputDispatcher,
        void* currentTool,
        std::string const& layerId,
        bool* currentIsEraser) {
    cnt::layers_eraser::VtableCallbacks const callbacks = {
        layerGuardedDiagramPenDown,
        layerAwareDrawingEraserSelectionFromPoints,
        layerAwarePlainDrawingEraserSelectionFromPoints,
        layerAwareDiagramEraserSelectionFromPoints,
        layerAwareDiagramEraserUpdateSelection,
        layerAwareDiagramEraserEraseSelection
    };
    return cnt::layers_eraser::armLayerAwareDrawingErasers(
        layerEraserDependencies(),
        callbacks,
        inputDispatcher,
        currentTool,
        layerId,
        currentIsEraser);
}

static bool isDiagramDrawingEraser(void* tool) {
    return cnt::layers_eraser::isDiagramDrawingEraser(
        layerEraserDependencies(), tool);
}

static bool restrictToolToLayer(
        void* tool,
        std::string const& layerId) {
    return cnt::layers_eraser::restrictToolToLayer(
        layerEraserDependencies(), tool, layerId);
}

static bool restrictBackendWritingToolsToLayer(
        void* inputDispatcher,
        void* currentTool,
        std::string const& layerId) {
    cnt::layers_eraser::VtableCallbacks const callbacks = {
        layerGuardedDiagramPenDown,
        layerAwareDrawingEraserSelectionFromPoints,
        layerAwarePlainDrawingEraserSelectionFromPoints,
        layerAwareDiagramEraserSelectionFromPoints,
        layerAwareDiagramEraserUpdateSelection,
        layerAwareDiagramEraserEraseSelection
    };
    return cnt::layers_eraser::restrictBackendWritingToolsToLayer(
        layerEraserDependencies(),
        callbacks,
        inputDispatcher,
        currentTool,
        layerId);
}

static bool applyConfiguredEraserSizeForWidget(
        QObject* widgetObject,
        char const* reason) {
    return cnt::layers_eraser::applyConfiguredEraserSizeForWidget(
        layerEraserDependencies(), widgetObject, reason);
}

static cnt::layers_service::ToolRoutingOperations const
    kLayerToolRoutingOperations = {
        armLayerAwareDrawingErasers,
        isDiagramDrawingEraser,
        restrictToolToLayer,
        restrictBackendWritingToolsToLayer
    };
