#pragma once

struct ZipApi {
    ZipOpen open = nullptr;
    ZipGetNumEntries getNumEntries = nullptr;
    ZipGetName getName = nullptr;
    ZipFopen fopen = nullptr;
    ZipFread fread = nullptr;
    ZipFclose fclose = nullptr;
    ZipDiscard discard = nullptr;
    void* libraryHandle = nullptr;
};

struct CoverRuntimeState {
    QVector<cnt::templates::CustomTemplate> customCovers;
    QHash<QString, CoverScanEntry> scanCache;
    QHash<QString, RenderedCoverEntry> renderedCache;
    quint64 renderedSequence = 0;
    QHash<QString, CleanCoverEntry> cleanCache;
    quint64 cleanSequence = 0;
    QMutex cacheMutex;
    bool pickerPending = false;
    bool titlePending = false;
    bool hooksReady = false;
    bool gridHookReady = false;
};

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
    ZipApi zip;
    cnt::templates::TemplateRuntimeState templates;
    CoverRuntimeState covers;
    PageRuntimeState pages;
    LayerRuntimeState layers;
    EraserRuntimeState eraser;
    cnt::SettingsStore settings;
    VisibilityRuntimeState visibility;
    HookRuntimeState hooks;
};
