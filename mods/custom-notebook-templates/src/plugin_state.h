#pragma once

struct PageRuntimeState {
    bool hooksReady = false;
};

struct LayerRuntimeState {
    void* drawingEraserVtable[148 / sizeof(void*)] = {};
    void* plainDrawingEraserVtable[148 / sizeof(void*)] = {};
    void* diagramEraserVtable[148 / sizeof(void*)] = {};
    bool drawingEraserVtableReady = false;
    bool plainDrawingEraserVtableReady = false;
    bool diagramEraserVtableReady = false;
    bool diagramEraserObserversReady = false;
    int eraserTraceBudget = 0;
    int diagramEraserObserverTraceBudget = 0;

    void* diagramPenVtable[152 / sizeof(void*)] = {};
    bool diagramPenVtableReady = false;
    int diagramPenTraceBudget = 0;
    std::map<void*, std::string> desiredDiagramPenLayers;
    QMutex desiredDiagramPenLayersMutex;

    QHash<QString, LayerPreviewCardCacheEntry> previewCardCache;
    quint64 previewCardCacheSequence = 0;

    bool hooksReady = false;
    bool previewApisReady = false;
};

struct EraserRuntimeState {
    bool sizeApisReady = false;
    bool sizeMenuHooksReady = false;
    QPointer<QObject> liveSizeController;
};

struct VisibilityRuntimeState {
    QMutex traceMutex;
    bool exclusionObserved = false;
    bool backingFilePreserved = false;
};

struct HookRuntimeState {
    QTimer* timer = nullptr;
    bool notebookLifecycleHooksReady = false;
};

// Process-lifetime state is published before the first hook mutation. The
// object is intentionally never moved or destroyed: firmware callbacks and
// writable vtable clones may remain reachable through Nickel shutdown.
struct PluginState {
    FirmwareApi firmware = {};
    cnt::templates::TemplateRuntimeState templates;
    cnt::cover_cache::State covers;
    PageRuntimeState pages;
    LayerRuntimeState layers;
    EraserRuntimeState eraser;
    cnt::SettingsStore settings;
    VisibilityRuntimeState visibility;
    HookRuntimeState hooks;
};
