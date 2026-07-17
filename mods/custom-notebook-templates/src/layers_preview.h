#pragma once

#include "layers.h"
#include "layers_service.h"

#include <QPointer>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <cstddef>
#include <cstdint>
#include <type_traits>

class QLabel;
class QMenu;
class QObject;
class QImage;
class QPixmap;
struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}

namespace layers_preview {

// All preview policy limits and firmware-private object offsets enter the
// promoted translation unit as inert data. The umbrella defines the
// process-lifetime aggregate from values pinned by the ABI verifier; this type
// has no constructor or default initialization.
struct Pins {
    char const* layerBackupRoot;
    char const* layerPreviewRoot;
    int maximumNotebookLayers;
    qint64 maximumLayerPreviewBytes;
    int layerPreviewCardWidth;
    int layerPreviewCardHeight;
    int deferredStartMs;
    int deferredNextMs;
    int deferredBudget;
    int maximumCardCacheEntries;
    uintptr_t neboBackendPageControllerOffset;
    uintptr_t stockPreviewDrawerOffset;
    uintptr_t stockPreviewBackendOffset;
    size_t stockPreviewContextBytes;
    size_t imagePainterObjectBytes;
    uintptr_t notePadEditorWidgetGuardOffset;
    uintptr_t notePadEditorWidgetObjectOffset;
    uintptr_t editorWidgetEditorObjectOffset;
    uintptr_t editorWidgetEditorControlOffset;
    uintptr_t editorWidgetImageLoaderObjectOffset;
    uintptr_t editorWidgetImageLoaderControlOffset;
};

// Copyable POD passed through menu glue and copied into deferred timer state.
// Its object pointers are owned by process-lifetime PluginState, its callbacks
// point into the loaded plugin image, and Pins contains only scalar values and
// pointers to process-lifetime string literals.
struct Dependencies {
    FirmwareApi* firmware;
    layers::RuntimeState* runtime;
    cover_cache::State* coverCache;
    layers_service::ToolRoutingOperations toolRoutingOperations;
    Pins pins;
};
static_assert(std::is_pod<Pins>::value,
    "layer preview pins must remain inert POD data");
static_assert(std::is_pod<Dependencies>::value,
    "deferred layer preview dependencies must remain copyable POD data");

// The menu owns these labels. QPointer clears them if the popup is destroyed
// before the GUI-thread preview timer gets its next turn.
struct DeferredLayerPreviewRow {
    QString id;
    QString name;
    bool active;
    QPointer<QLabel> previewLabel;
    QPointer<QLabel> textLabel;
};

// Snapshot of the live editor's page-to-view mapping. It is captured before
// the isolated exporter runs so the sleep compositor can undo the exporter's
// bounding-box normalization without guessing the device DPI or zoom.
struct LivePageView {
    RendererViewTransformOpaque transform;
};

void showLayerError(
    Dependencies dependencies,
    void* widget,
    QString const& error);

bool performLayerOperation(
    Dependencies dependencies,
    layers::LayerContext* context,
    layers::LayerOperation operation,
    QString const& id,
    QString* error);

bool layerPreviewNeedsRefresh(
    Dependencies dependencies,
    layers::LayerState const& state,
    QString const& id);

// Export the live selection through Kobo's isolated renderer and return its
// exact MyScript page-space extent. Unlike layer cards, this does not restrict
// the renderer, persist a cache file, or mutate the live editor.
bool exportCurrentPageImage(
    Dependencies dependencies,
    QObject* notebookWidget,
    QImage* image,
    ExtentOpaque* extent,
    LivePageView* liveView,
    QString* error);

QPixmap layerPreview(
    Dependencies dependencies,
    layers::LayerState const& state,
    QString const& id,
    QString const& name,
    bool active,
    bool* cacheDrawnResult = nullptr);

QString layerPopupRowLabel(
    QString const& name,
    bool active,
    bool previewPending);

void startDeferredLayerPreviewRefresh(
    Dependencies dependencies,
    QObject* controller,
    layers::LayerContext const& context,
    QMenu* popup,
    QVector<DeferredLayerPreviewRow> const& rows);

} // namespace layers_preview
} // namespace cnt
