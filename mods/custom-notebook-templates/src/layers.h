#pragma once

#include "abi_types.h"

#include <QHash>
#include <QMutex>
#include <QPointer>
#include <QString>
#include <QVector>

#include <map>
#include <string>

class QObject;

namespace cnt {
namespace layers {

struct LayerRecord {
    QString id;
    QString name;
};

struct LayerState {
    QString notebookPath;
    QString partId;
    QString activeId;
    QVector<LayerRecord> customLayers;
};

// Layer popup construction is GUI-thread-only. Cache the already scaled card
// images so repeatedly opening the native menu does not decode every PNG again.
struct LayerContext {
    QPointer<QObject> widgetObject;
    void* widget;
    void* editor;
    SharedPart part;
    SharedPackage package;
    LayerState state;

    LayerContext() : widget(nullptr), editor(nullptr) {}
};

enum LayerOperation {
    ActivateLayerOperation,
    AddLayerOperation,
    DeleteActiveLayerOperation,
    RefreshLayerPreviewsOperation
};

struct RuntimeState {
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

} // namespace layers
} // namespace cnt
