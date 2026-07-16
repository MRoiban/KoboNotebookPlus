#include "layers_preview.h"

#include "cover_cache.h"
#include "firmware_api.h"
#include "layers_state.h"
#include "notebook_widget.h"
#include "page_io.h"
#include "settings.h"

#include <QByteArray>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QObject>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QPixmap>
#include <QSize>
#include <QTimer>
#include <QUuid>

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <unistd.h>

namespace cnt {
namespace layers_preview {
namespace {

struct ImagePainterDeletingDeleter {
    ImagePainterDeletingDestructor destructor;

    void operator()(ImagePainterOpaque* painter) const {
        if (painter && destructor)
            destructor(painter);
    }
};

static bool copyLiveLayerPreviewImageLoader(
    Dependencies const& dependencies,
    layers::LayerContext const& context,
    SharedImageLoader* loaderResult,
    QString* error) {
    FirmwareApi& firmware = *dependencies.firmware;
    Pins const& pins = dependencies.pins;
    if (!loaderResult || !context.widget || !context.widgetObject
            || context.widgetObject.data()
                != reinterpret_cast<QObject*>(context.widget)
            || !firmware.uirefEditorWidgetVtable) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook UI is no longer live.");
        }
        return false;
    }

    char* const widgetBytes = static_cast<char*>(context.widget);
    void* const editorWidgetGuard = *reinterpret_cast<void**>(
        widgetBytes + pins.notePadEditorWidgetGuardOffset);
    // Match IInkNotePadWidget's own QPointer check before following +0x48:
    // a non-null ExternalRefCountData whose strongref word at +4 is non-zero.
    if (!editorWidgetGuard
            || *reinterpret_cast<volatile int const*>(
                static_cast<char*>(editorWidgetGuard) + 4) == 0) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook canvas is closing.");
        }
        return false;
    }

    void* const editorWidget = *reinterpret_cast<void**>(
        widgetBytes + pins.notePadEditorWidgetObjectOffset);
    void* const expectedEditorWidgetVptr =
        static_cast<char*>(firmware.uirefEditorWidgetVtable) + 8;
    if (!editorWidget
            || *reinterpret_cast<void**>(editorWidget)
                != expectedEditorWidgetVptr) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook canvas type changed.");
        }
        return false;
    }

    char* const editorWidgetBytes = static_cast<char*>(editorWidget);
    void* const liveEditor = *reinterpret_cast<void**>(
        editorWidgetBytes + pins.editorWidgetEditorObjectOffset);
    void* const liveEditorControl = *reinterpret_cast<void**>(
        editorWidgetBytes + pins.editorWidgetEditorControlOffset);
    if (!liveEditorControl || liveEditor != context.editor
            || liveEditorControl != cnt::notebook_widget::notePadEditorControl(context.widget)) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook canvas is out of sync.");
        }
        return false;
    }

    void* const loaderObject = *reinterpret_cast<void**>(
        editorWidgetBytes + pins.editorWidgetImageLoaderObjectOffset);
    void* const loaderControl = *reinterpret_cast<void**>(
        editorWidgetBytes + pins.editorWidgetImageLoaderControlOffset);
    if (!loaderObject || !loaderControl) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook image loader is missing.");
        }
        return false;
    }

    try {
        // Copy the real strong owner; never synthesize ImageLoader's private
        // layout and never retain only its raw object pointer.
        SharedImageLoader const& liveLoader =
            *reinterpret_cast<SharedImageLoader const*>(
                editorWidgetBytes + pins.editorWidgetImageLoaderObjectOffset);
        SharedImageLoader const loader = liveLoader;
        if (!loader || loader.get() != loaderObject) {
            if (error) {
                *error = QLatin1String(
                    "Layer preview unavailable: notebook image loader changed.");
            }
            return false;
        }
        *loaderResult = loader;
    } catch (...) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook image loader copy failed.");
        }
        return false;
    }

    trace("layers: preview live image loader acquired");
    return true;
}

static bool createLayerPreviewDrawer(
    Dependencies const& dependencies,
    layers::LayerContext const& context,
    SharedImagePainter* painterResult,
    SharedBackendImageDrawer* drawerResult,
    QString* error) {
    FirmwareApi& firmware = *dependencies.firmware;
    Pins const& pins = dependencies.pins;
    if (!painterResult || !drawerResult || !firmware.editorGetEngine
            || !firmware.editorGetConfiguration || !firmware.imagePainterConstructor
            || !firmware.imagePainterDeletingDestructor
            || !firmware.imagePainterSetImageLoader
            || !firmware.backendImageDrawerMakeShared) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: stock image writer APIs are missing.");
        }
        return false;
    }

    SharedEngine const engine = firmware.editorGetEngine(context.editor);
    SharedConfiguration const configuration =
        firmware.editorGetConfiguration(context.editor);
    if (!engine || !configuration) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: editor services are missing.");
        }
        return false;
    }

    SharedImageLoader imageLoader;
    if (!copyLiveLayerPreviewImageLoader(
            dependencies, context, &imageLoader, error)) {
        return false;
    }

    void* painterMemory = nullptr;
    try {
        painterMemory = ::operator new(pins.imagePainterObjectBytes);
        memset(painterMemory, 0, pins.imagePainterObjectBytes);
        firmware.imagePainterConstructor(painterMemory);
    } catch (...) {
        if (painterMemory)
            ::operator delete(painterMemory);
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: image painter construction failed.");
        }
        return false;
    }

    SharedImagePainter painter;
    SharedBackendImageDrawer drawer;
    try {
        // IImagePainter is ImagePainter's primary base at +0. The custom
        // owner calls Kobo's deleting destructor exactly once; the drawer
        // copies this shared owner before export starts.
        painter.reset(
            reinterpret_cast<ImagePainterOpaque*>(painterMemory),
            ImagePainterDeletingDeleter{
                firmware.imagePainterDeletingDestructor});
        firmware.imagePainterSetImageLoader(painter.get(), imageLoader);
        SharedEngine engineArgument = engine;
        SharedConfiguration configurationArgument = configuration;
        firmware.backendImageDrawerMakeShared(
            &drawer,
            0,
            nullptr,
            &engineArgument,
            &painter,
            &configurationArgument);
    } catch (...) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: image drawer construction failed.");
        }
        return false;
    }
    if (!drawer) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: image drawer is missing.");
        }
        return false;
    }

    *painterResult = painter;
    *drawerResult = drawer;
    return true;
}

struct StockPreviewCallbackContext {
    enum { StorageBytes = 0x28 };
    alignas(4) unsigned char bytes[StorageBytes];

    StockPreviewCallbackContext(
            Pins const& pins,
            void* drawer,
            void* backend) {
        memset(bytes, 0, sizeof(bytes));
        memcpy(
            bytes + pins.stockPreviewDrawerOffset,
            &drawer,
            sizeof(drawer));
        memcpy(
            bytes + pins.stockPreviewBackendOffset,
            &backend,
            sizeof(backend));
    }

    static bool supports(Pins const& pins) {
        return pins.stockPreviewContextBytes
                == static_cast<size_t>(StorageBytes)
            && pins.stockPreviewDrawerOffset
                <= static_cast<uintptr_t>(StorageBytes - sizeof(void*))
            && pins.stockPreviewBackendOffset
                <= static_cast<uintptr_t>(StorageBytes - sizeof(void*));
    }
};

class LayerPreviewRendererListener {
public:
    LayerPreviewRendererListener(
        FirmwareApi* firmware,
        Pins const& pins,
        SharedImagePainter const& painter,
        SharedBackendImageDrawer const& drawer,
        SharedRenderer const& liveRenderer,
        void* backend,
        std::string const& layerId,
        bool* restrictionApplied,
        bool* writerInvoked)
        : firmware_(firmware),
          pins_(pins),
          painter_(painter),
          drawer_(drawer),
          liveRenderer_(liveRenderer),
          backend_(backend),
          layerId_(layerId),
          restrictionApplied_(restrictionApplied),
          writerInvoked_(writerInvoked) {}

    virtual ~LayerPreviewRendererListener() {}

    // RendererListener has one pure virtual callback after its two destructor
    // slots. Its hard-float call shape is recovered from exportToPNG at
    // 0x513392..0x5133d0. After restricting only the fresh export renderer,
    // call the exact anonymous writer callback used by EditorImpl::export_.
    // That firmware callback owns every private Page/Layout/Selection
    // temporary and invokes BackendImageDrawer::drawImage with stock ABI.
    virtual void exportImage(
        SharedRenderer const& renderer,
        void const* selection,
        ExtentOpaque extent,
        unsigned int flags,
        std::string const& path) {
        if (restrictionApplied_)
            *restrictionApplied_ = false;
        if (writerInvoked_)
            *writerInvoked_ = false;
        try {
            if (renderer && firmware_->rendererRestrictToLayers) {
                std::vector<std::string> ids(1, layerId_);
                if (firmware_->backgroundObjectLayerName
                        && !firmware_->backgroundObjectLayerName->empty()
                        && *firmware_->backgroundObjectLayerName != layerId_) {
                    ids.push_back(*firmware_->backgroundObjectLayerName);
                }
                firmware_->rendererRestrictToLayers(renderer.get(), ids);
                if (restrictionApplied_)
                    *restrictionApplied_ = true;
            }
        } catch (...) {
            trace("layers: preview renderer restriction failed");
        }

        if (!firmware_->stockBackendImageDrawerExport || !drawer_ || !backend_)
            return;
        StockPreviewCallbackContext callback(pins_, drawer_.get(), backend_);
        if (writerInvoked_)
            *writerInvoked_ = true;
        try {
            firmware_->stockBackendImageDrawerExport(
                callback.bytes, renderer, selection, extent, flags, path);
        } catch (...) {
            trace("layers: stock preview image writer threw");
        }
    }

private:
    FirmwareApi* firmware_;
    Pins pins_;
    SharedImagePainter painter_;
    SharedBackendImageDrawer drawer_;
    SharedRenderer liveRenderer_;
    void* backend_;
    std::string layerId_;
    bool* restrictionApplied_;
    bool* writerInvoked_;
};

static bool liveNeboControllerAndBackend(
    Dependencies const& dependencies,
    layers::LayerContext const& context,
    SharedRenderer* rendererKeepAlive,
    void** pageController,
    void** backendResult,
    QString* error) {
    FirmwareApi& firmware = *dependencies.firmware;
    if (!rendererKeepAlive || !pageController || !backendResult
            || !firmware.editorGetRenderer || !firmware.rendererGetBackend
            || !firmware.neboBackendVtable) {
        if (error)
            *error = QLatin1String("Layer preview unavailable: renderer APIs are missing.");
        return false;
    }

    *rendererKeepAlive = firmware.editorGetRenderer(context.editor);
    void* const backend = *rendererKeepAlive
        ? firmware.rendererGetBackend(rendererKeepAlive->get()) : nullptr;
    void* const expectedVptr =
        static_cast<char*>(firmware.neboBackendVtable) + 8;
    if (!backend || *reinterpret_cast<void**>(backend) != expectedVptr) {
        if (error)
            *error = QLatin1String("Layer preview unavailable for this notebook backend.");
        return false;
    }

    *pageController = *reinterpret_cast<void**>(
        static_cast<char*>(backend)
            + dependencies.pins.neboBackendPageControllerOffset);
    if (!*pageController) {
        if (error)
            *error = QLatin1String("Layer preview unavailable: page controller is missing.");
        return false;
    }
    *backendResult = backend;
    return true;
}

static QString layerPreviewPath(
        Dependencies const& dependencies,
        layers::LayerState const& state,
        QString const& id) {
    QByteArray key = state.notebookPath.toUtf8();
    key.append('\n');
    key.append(state.partId.toUtf8());
    key.append('\n');
    key.append(id.toUtf8());
    QByteArray const digest = QCryptographicHash::hash(
        key, QCryptographicHash::Sha256).toHex();
    return QDir(QLatin1String(dependencies.pins.layerPreviewRoot)).filePath(
        QString::fromLatin1(digest) + QLatin1String(".png"));
}

static bool layerPreviewNeedsRefreshImpl(
    Dependencies const& dependencies,
    layers::LayerState const& state,
    QString const& id) {
    QFileInfo const preview(layerPreviewPath(dependencies, state, id));
    QFileInfo const notebook(state.notebookPath);
    return !preview.isFile()
        || preview.size() <= 0
        || preview.size() > dependencies.pins.maximumLayerPreviewBytes
        || (notebook.isFile() && preview.lastModified() < notebook.lastModified());
}

static bool layerPreviewCacheUsable(
    Dependencies const& dependencies,
    layers::LayerState const& state,
    QString const& id) {
    QFileInfo const preview(layerPreviewPath(dependencies, state, id));
    return preview.isFile()
        && preview.size() > 0
        && preview.size() <= dependencies.pins.maximumLayerPreviewBytes;
}

class LayerPreviewActiveGuard {
public:
    explicit LayerPreviewActiveGuard(QObject* widgetObject)
        : widgetObject_(widgetObject) {
        if (widgetObject_)
            widgetObject_->setProperty("_cnt_layer_preview_active", true);
    }

    ~LayerPreviewActiveGuard() {
        if (widgetObject_)
            widgetObject_->setProperty("_cnt_layer_preview_active", false);
    }

private:
    QPointer<QObject> widgetObject_;
};

static bool generateLayerPreview(
    Dependencies const& dependencies,
    layers::LayerContext const& context,
    QString const& id,
    QString* error) {
    FirmwareApi& firmware = *dependencies.firmware;
    layers::RuntimeState& runtime = *dependencies.runtime;
    Pins const& pins = dependencies.pins;
    if (!runtime.previewApisReady || !firmware.rendererRestrictToLayers
            || !firmware.pageControllerExportToPng
            || !firmware.stockBackendImageDrawerExport
            || !StockPreviewCallbackContext::supports(pins)) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: firmware export APIs are not ready.");
        }
        return false;
    }
    if (!QDir().mkpath(QLatin1String(pins.layerPreviewRoot))) {
        if (error)
            *error = QLatin1String("Layer preview unavailable: cache directory failed.");
        return false;
    }
    if (!context.widgetObject
            || context.widgetObject->property("_cnt_layer_preview_active").toBool()) {
        if (error)
            *error = QLatin1String("Layer preview deferred: renderer is busy.");
        return false;
    }
    LayerPreviewActiveGuard const previewActive(context.widgetObject);

    SharedRenderer rendererKeepAlive;
    void* pageController = nullptr;
    void* backend = nullptr;
    if (!liveNeboControllerAndBackend(
            dependencies,
            context,
            &rendererKeepAlive,
            &pageController,
            &backend,
            error)) {
        return false;
    }
    SharedImagePainter painter;
    SharedBackendImageDrawer drawer;
    if (!createLayerPreviewDrawer(
            dependencies, context, &painter, &drawer, error)) {
        return false;
    }

    QString const finalPath = layerPreviewPath(
        dependencies, context.state, id);
    QString const uniqueSuffix = QString::number(getpid())
        + QLatin1String("-")
        + QUuid::createUuid().toString().remove('{').remove('}');
    QString const temporaryPath = finalPath
        + QLatin1String(".tmp-") + uniqueSuffix
        + QLatin1String(".png");
    QFile::remove(temporaryPath);
    bool restrictionApplied = false;
    bool writerInvoked = false;
    std::string const nativeId = id.toUtf8().constData();
    std::shared_ptr<LayerPreviewRendererListener> const proxy =
        std::make_shared<LayerPreviewRendererListener>(
            &firmware,
            pins,
            painter,
            drawer,
            rendererKeepAlive,
            backend,
            nativeId,
            &restrictionApplied,
            &writerInvoked);
    SharedRendererListener const opaqueProxy(
        proxy,
        reinterpret_cast<RendererListenerOpaque*>(proxy.get()));

    bool exported = false;
    trace(QLatin1String("layers: isolated preview export begin id=") + id);
    try {
        std::string const output = temporaryPath.toUtf8().constData();
        exported = firmware.pageControllerExportToPng(
            pageController, SharedBox(), output, opaqueProxy, 0);
    } catch (...) {
        exported = false;
    }

    QFileInfo const generated(temporaryPath);
    trace(QLatin1String("layers: isolated preview export result id=") + id
        + QLatin1String(" exported=")
        + (exported ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" restricted=")
        + (restrictionApplied ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" writer=")
        + (writerInvoked ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" file=")
        + (generated.isFile() ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" bytes=") + QString::number(generated.size()));
    if (!exported || !restrictionApplied || !writerInvoked
            || !generated.isFile()
            || generated.size() <= 0
            || generated.size() > pins.maximumLayerPreviewBytes) {
        QFile::remove(temporaryPath);
        if (error) {
            *error = QLatin1String(
                "Layer preview failed: the isolated renderer produced no valid PNG.");
        }
        return false;
    }
    QImageReader validationReader(temporaryPath);
    QSize const decodedSize = validationReader.size();
    if (!decodedSize.isValid() || decodedSize.width() > 4096
            || decodedSize.height() > 4096) {
        QFile::remove(temporaryPath);
        if (error)
            *error = QLatin1String("Layer preview failed: exported image is invalid.");
        return false;
    }
    QImage const validation = validationReader.read();
    if (validation.isNull()) {
        QFile::remove(temporaryPath);
        if (error)
            *error = QLatin1String("Layer preview failed: exported image is unreadable.");
        return false;
    }

    // The menu never needs the 1024x1024 export after validation. Persist the
    // exact page-shaped payload it displays so future popup opens read only a
    // few kilobytes. Keep the full export as a safe fallback if Qt cannot
    // encode the derivative for any reason.
    QString const cardTemporaryPath = finalPath
        + QLatin1String(".card-") + uniqueSuffix + QLatin1String(".png");
    QFile::remove(cardTemporaryPath);
    QImage const card = validation.scaled(
        QSize(
            pins.layerPreviewCardWidth - 6,
            pins.layerPreviewCardHeight - 6),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    QString cacheCandidatePath = temporaryPath;
    bool cardSaved = !card.isNull()
        && card.save(cardTemporaryPath, "PNG");
    QFileInfo const cardFile(cardTemporaryPath);
    if (cardSaved && cardFile.isFile() && cardFile.size() > 0
            && cardFile.size() <= pins.maximumLayerPreviewBytes) {
        cacheCandidatePath = cardTemporaryPath;
    } else {
        cardSaved = false;
        QFile::remove(cardTemporaryPath);
    }
    trace(QLatin1String("layers: preview card cache optimized id=") + id
        + QLatin1String(" optimized=")
        + (cardSaved ? QLatin1String("yes") : QLatin1String("no")));

    QString const previousPath = finalPath + QLatin1String(".previous");
    QFile::remove(previousPath);
    if (QFileInfo(finalPath).exists() && !QFile::rename(finalPath, previousPath)) {
        QFile::remove(temporaryPath);
        QFile::remove(cardTemporaryPath);
        if (error)
            *error = QLatin1String("Layer preview failed: old cache is busy.");
        return false;
    }
    if (!QFile::rename(cacheCandidatePath, finalPath)) {
        QFile::rename(previousPath, finalPath);
        QFile::remove(temporaryPath);
        QFile::remove(cardTemporaryPath);
        if (error)
            *error = QLatin1String("Layer preview failed: cache update was not atomic.");
        return false;
    }
    QFile::remove(temporaryPath);
    QFile::remove(cardTemporaryPath);
    QFile::remove(previousPath);
    // FAT timestamps are coarse and two tiny PNGs can occasionally have the
    // same byte count. Explicit eviction guarantees the just-written card is
    // decoded once instead of reusing an indistinguishable old memory entry.
    runtime.previewCardCache.remove(finalPath);
    return true;
}

static bool readLayerPreviewCard(
    Dependencies const& dependencies,
    layers::LayerState const& state,
    QString const& id,
    QImage* result) {
    layers::RuntimeState& runtime = *dependencies.runtime;
    Pins const& pins = dependencies.pins;
    if (!result || !layerPreviewCacheUsable(dependencies, state, id))
        return false;
    QString const path = layerPreviewPath(dependencies, state, id);
    QFileInfo const cached(path);
    qint64 const modifiedMs = cached.lastModified().toMSecsSinceEpoch();
    QHash<QString, LayerPreviewCardCacheEntry>::iterator found =
        runtime.previewCardCache.find(path);
    if (found != runtime.previewCardCache.end()
            && found->modifiedMs == modifiedMs
            && found->size == cached.size()
            && !found->image.isNull()) {
        found->sequence = ++runtime.previewCardCacheSequence;
        *result = found->image;
        return true;
    }
    if (found != runtime.previewCardCache.end())
        runtime.previewCardCache.erase(found);

    QImageReader reader(path);
    QSize const decodedSize = reader.size();
    if (!decodedSize.isValid() || decodedSize.width() > 4096
            || decodedSize.height() > 4096) {
        return false;
    }
    QSize const target = decodedSize.scaled(
        QSize(
            pins.layerPreviewCardWidth - 6,
            pins.layerPreviewCardHeight - 6),
        Qt::KeepAspectRatio);
    if (target.isValid() && target != decodedSize)
        reader.setScaledSize(target);
    QImage image = reader.read();
    if (image.isNull())
        return false;
    if (image.width() > pins.layerPreviewCardWidth - 6
            || image.height() > pins.layerPreviewCardHeight - 6) {
        image = image.scaled(
            QSize(
                pins.layerPreviewCardWidth - 6,
                pins.layerPreviewCardHeight - 6),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }
    if (image.isNull())
        return false;

    if (runtime.previewCardCache.size()
            >= pins.maximumCardCacheEntries) {
        QHash<QString, LayerPreviewCardCacheEntry>::iterator oldest =
            runtime.previewCardCache.begin();
        for (QHash<QString, LayerPreviewCardCacheEntry>::iterator it =
                 runtime.previewCardCache.begin();
                it != runtime.previewCardCache.end(); ++it) {
            if (it->sequence < oldest->sequence)
                oldest = it;
        }
        if (oldest != runtime.previewCardCache.end())
            runtime.previewCardCache.erase(oldest);
    }
    LayerPreviewCardCacheEntry entry;
    entry.image = image;
    entry.modifiedMs = modifiedMs;
    entry.size = cached.size();
    entry.sequence = ++runtime.previewCardCacheSequence;
    runtime.previewCardCache.insert(path, entry);
    *result = image;
    return true;
}

static QPixmap layerPreviewImpl(
    Dependencies const& dependencies,
    layers::LayerState const& state,
    QString const& id,
    QString const& name,
    bool active,
    bool* cacheDrawnResult) {
    Pins const& pins = dependencies.pins;
    QPixmap preview(
        pins.layerPreviewCardWidth,
        pins.layerPreviewCardHeight);
    preview.fill(Qt::white);
    QPainter painter(&preview);
    bool cacheDrawn = false;
    // A stale but valid cache is still a better first frame than a text
    // placeholder. The popup queues its replacement only after it is visible.
    QImage image;
    if (readLayerPreviewCard(dependencies, state, id, &image)) {
        QPoint const origin(
            (preview.width() - image.width()) / 2,
            (preview.height() - image.height()) / 2);
        painter.drawImage(origin, image);
        cacheDrawn = true;
    }
    painter.setPen(QPen(active ? Qt::black : QColor(110, 110, 110), active ? 3 : 1));
    painter.drawRect(2, 2, preview.width() - 5, preview.height() - 5);
    if (!cacheDrawn) {
        painter.setPen(Qt::black);
        QFont font = painter.font();
        font.setBold(active);
        font.setPointSize(11);
        painter.setFont(font);
        painter.drawText(preview.rect().adjusted(6, 6, -6, -6),
            Qt::AlignCenter | Qt::TextWordWrap, name);
    }
    if (cacheDrawnResult)
        *cacheDrawnResult = cacheDrawn;
    return preview;
}

static void showLayerErrorImpl(
        Dependencies const& dependencies,
        void* widget,
        QString const& error) {
    trace("layers: operation failed safely");
    if (dependencies.firmware->showErrorPopup)
        dependencies.firmware->showErrorPopup(widget, error);
}

static bool addNotebookLayer(
        Dependencies const& dependencies,
        layers::LayerContext* context,
        QString* error) {
    FirmwareApi& firmware = *dependencies.firmware;
    Pins const& pins = dependencies.pins;
    if (!context || context->state.customLayers.size() + 1
            >= pins.maximumNotebookLayers) {
        if (error)
            *error = QLatin1String("Layer not added: this notebook reached the layer limit.");
        return false;
    }

    firmware.widgetSave(context->widget);
    QString backupPath;
    if (!cnt::page_io::backupNotebookPath(
            context->state.notebookPath,
            QLatin1String(pins.layerBackupRoot),
            QLatin1String("Add layer"),
            &backupPath,
            error)) {
        return false;
    }

    cnt::layers_state::LayoutStorage layout(firmware);
    if (!cnt::layers_state::loadLayoutForPart(
            firmware, context->part, &layout, error))
        return false;
    layers::LayerRecord record;
    record.id = QLatin1String("cnt.layer.")
        + QUuid::createUuid().toString().remove('{').remove('}');
    record.name = QLatin1String("Layer ")
        + QString::number(context->state.customLayers.size() + 2);
    std::string const nativeId = record.id.toUtf8().constData();
    if (!firmware.layoutAppendLayer(layout.bytes, nativeId)) {
        if (error)
            *error = QLatin1String("Layer not added: document rejected the new layer.");
        return false;
    }
    bool addedExists = false;
    if (!cnt::layers_state::nativeLayerExists(
            firmware, layout, record.id, &addedExists, error)
            || !addedExists) {
        try {
            firmware.layoutRemoveLayer(layout.bytes, nativeId);
        } catch (...) {
            trace("layers: failed add could not be rolled back in memory");
        }
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layer not added: the native layer could not be verified. "
                "The notebook backup was retained.");
        }
        return false;
    }

    layers::LayerState const previous = context->state;
    context->state.customLayers.append(record);
    context->state.activeId = record.id;
    if (!cnt::layers_service::applyActiveLayer(
            firmware,
            pins.neboBackendPageControllerOffset,
            dependencies.toolRoutingOperations,
            *context,
            record.id,
            error)
            || !cnt::layers_state::saveLayerState(context->state, error)) {
        firmware.layoutRemoveLayer(layout.bytes, nativeId);
        context->state = previous;
        cnt::layers_state::saveLayerState(previous, nullptr);
        cnt::layers_service::applyActiveLayer(
            firmware,
            pins.neboBackendPageControllerOffset,
            dependencies.toolRoutingOperations,
            *context,
            previous.activeId,
            nullptr);
        return false;
    }

    trace("layers: package save begin after native add");
    firmware.packageSave(context->package.get());
    trace("layers: package save complete after native add");
    firmware.widgetSave(context->widget);
    trace("layers: widget save complete after native add");
    cnt::layers_state::traceSerializedLayerProbe(
        *dependencies.coverCache,
        context->state,
        record.id,
        "after-add-save");
    firmware.widgetRefresh(context->widget);
    trace("layers: native layer added and selected");
    return true;
}

static bool activateNotebookLayer(
    Dependencies const& dependencies,
    layers::LayerContext* context,
    QString const& id,
    QString* error) {
    FirmwareApi& firmware = *dependencies.firmware;
    Pins const& pins = dependencies.pins;
    if (!context)
        return false;
    bool known = id
        == cnt::layers_state::nativeDocumentLayerId(firmware);
    for (int i = 0; !known && i < context->state.customLayers.size(); ++i)
        known = context->state.customLayers.at(i).id == id;
    if (!known) {
        if (error)
            *error = QLatin1String("Layer not selected: layer metadata is stale.");
        return false;
    }

    QString const previous = context->state.activeId;
    if (!cnt::layers_service::applyActiveLayer(
            firmware,
            pins.neboBackendPageControllerOffset,
            dependencies.toolRoutingOperations,
            *context,
            id,
            error))
        return false;
    context->state.activeId = id;
    if (!cnt::layers_state::saveLayerState(context->state, error)) {
        context->state.activeId = previous;
        cnt::layers_service::applyActiveLayer(
            firmware,
            pins.neboBackendPageControllerOffset,
            dependencies.toolRoutingOperations,
            *context,
            previous,
            nullptr);
        return false;
    }
    // Keep the live renderer unrestricted. Firmware uses restrictToLayers()
    // only on isolated export/thumbnail renderers; mutating the live
    // RenderPad vector from this menu callback races its render lifecycle.
    // The active ToolDispatcher still routes all future strokes natively.
    firmware.widgetRefresh(context->widget);
    trace("layers: active layer changed");
    return true;
}

static bool deleteActiveNotebookLayer(
        Dependencies const& dependencies,
        layers::LayerContext* context,
        QString* error) {
    FirmwareApi& firmware = *dependencies.firmware;
    Pins const& pins = dependencies.pins;
    if (!context || context->state.activeId
            == cnt::layers_state::nativeDocumentLayerId(firmware)) {
        if (error)
            *error = QLatin1String("Layer not deleted: the base layer is protected.");
        return false;
    }

    int index = -1;
    for (int i = 0; i < context->state.customLayers.size(); ++i) {
        if (context->state.customLayers.at(i).id == context->state.activeId) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (error)
            *error = QLatin1String("Layer not deleted: layer metadata is stale.");
        return false;
    }

    firmware.widgetSave(context->widget);
    QString backupPath;
    if (!cnt::page_io::backupNotebookPath(
            context->state.notebookPath,
            QLatin1String(pins.layerBackupRoot),
            QLatin1String("Delete layer"),
            &backupPath,
            error)) {
        return false;
    }

    cnt::layers_state::LayoutStorage layout(firmware);
    if (!cnt::layers_state::loadLayoutForPart(
            firmware, context->part, &layout, error))
        return false;
    QString const deletedId = context->state.activeId;
    std::string const nativeId = deletedId.toUtf8().constData();
    QString const previewPath = layerPreviewPath(
        dependencies, context->state, context->state.activeId);
    firmware.layoutRemoveLayer(layout.bytes, nativeId);
    bool stillExists = true;
    if (!cnt::layers_state::nativeLayerExists(
            firmware,
            layout,
            context->state.activeId,
            &stillExists,
            error)) {
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layer removal could not be verified. The notebook backup "
                "was retained.");
        }
        return false;
    }
    if (stillExists) {
        if (error) {
            *error = QLatin1String(
                "Layer not deleted: the native document kept the layer. "
                "The notebook backup was retained.");
        }
        return false;
    }
    context->state.customLayers.remove(index);
    context->state.activeId =
        cnt::layers_state::nativeDocumentLayerId(firmware);
    if (!cnt::layers_service::applyActiveLayer(
            firmware,
            pins.neboBackendPageControllerOffset,
            dependencies.toolRoutingOperations,
            *context,
            context->state.activeId,
            error)
            || !cnt::layers_state::saveLayerState(context->state, error)) {
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layer was removed, but its UI state could not be finalized. "
                "A notebook backup is available.");
        }
        return false;
    }
    trace("layers: package save begin after native delete");
    firmware.packageSave(context->package.get());
    trace("layers: package save complete after native delete");
    firmware.widgetSave(context->widget);
    trace("layers: widget save complete after native delete");
    cnt::layers_state::traceSerializedLayerProbe(
        *dependencies.coverCache,
        context->state,
        deletedId,
        "after-delete-save");
    QFile::remove(previewPath);
    dependencies.runtime->previewCardCache.remove(previewPath);
    firmware.widgetRefresh(context->widget);
    trace("layers: active native layer deleted");
    return true;
}

static bool refreshLayerPreviews(
        Dependencies const& dependencies,
        layers::LayerContext* context,
        QString* error) {
    if (!context)
        return false;
    FirmwareApi& firmware = *dependencies.firmware;
    firmware.widgetSave(context->widget);

    QVector<QString> ids;
    ids.append(cnt::layers_state::nativeDocumentLayerId(firmware));
    for (int i = 0; i < context->state.customLayers.size(); ++i)
        ids.append(context->state.customLayers.at(i).id);

    int generated = 0;
    QString firstError;
    for (int i = 0; i < ids.size(); ++i) {
        QString itemError;
        if (generateLayerPreview(
                dependencies, *context, ids.at(i), &itemError)) {
            ++generated;
        } else if (firstError.isEmpty()) {
            firstError = itemError;
        }
    }
    if (generated != ids.size()) {
        if (error) {
            *error = QLatin1String("Generated ") + QString::number(generated)
                + QLatin1String(" of ") + QString::number(ids.size())
                + QLatin1String(" layer previews. ") + firstError;
        }
        trace("layers: isolated preview refresh incomplete");
        return false;
    }
    trace("layers: isolated preview cache refreshed");
    return true;
}

static bool performLayerOperationImpl(
        Dependencies const& dependencies,
        layers::LayerContext* context,
        layers::LayerOperation operation,
        QString const& id,
        QString* error) {
    if (operation == layers::AddLayerOperation)
        return addNotebookLayer(dependencies, context, error);
    if (operation == layers::DeleteActiveLayerOperation)
        return deleteActiveNotebookLayer(dependencies, context, error);
    if (operation == layers::RefreshLayerPreviewsOperation)
        return refreshLayerPreviews(dependencies, context, error);
    return activateNotebookLayer(dependencies, context, id, error);
}

static QString layerPopupRowLabelImpl(
    QString const& name,
    bool active,
    bool previewPending) {
    QString label = name;
    if (active)
        label += QLatin1String("  (active)");
    if (previewPending)
        label += QLatin1String("  (preview pending)");
    return label;
}

struct DeferredLayerPreviewRefresh {
    Dependencies dependencies;
    QPointer<QObject> controller;
    layers::LayerState expectedState;
    QVector<DeferredLayerPreviewRow> rows;
    QPointer<QTimer> timer;
    int next;
    int attempted;
    int generated;
    QElapsedTimer elapsed;

    explicit DeferredLayerPreviewRefresh(Dependencies dependencies_)
        : dependencies(dependencies_),
          next(0),
          attempted(0),
          generated(0) {}
};

// MyScript renderers are GUI-thread-affine, so this deliberately does not use
// a worker. The native popup is constructed from cached images first; a
// parented timer then exports one stale row per turn of QMenu's nested event
// loop. Closing the popup destroys the timer and cancels all remaining work.
static void continueDeferredLayerPreviewRefresh(
    std::shared_ptr<DeferredLayerPreviewRefresh> const& state) {
    if (!state || !state->timer || !state->controller)
        return;
    Dependencies const& dependencies = state->dependencies;
    FirmwareApi& firmware = *dependencies.firmware;
    layers::RuntimeState& runtime = *dependencies.runtime;
    Pins const& pins = dependencies.pins;
    if (state->next >= state->rows.size()
            || state->attempted >= pins.deferredBudget) {
        trace(QLatin1String("layers: deferred popup preview refresh complete attempted=")
            + QString::number(state->attempted)
            + QLatin1String(" generated=") + QString::number(state->generated)
            + QLatin1String(" remaining=")
            + QString::number(state->rows.size() - state->next)
            + QLatin1String(" ms=") + QString::number(state->elapsed.elapsed()));
        return;
    }

    DeferredLayerPreviewRow const row = state->rows.at(state->next++);
    ++state->attempted;
    QElapsedTimer itemTimer;
    itemTimer.start();

    // Do not retain raw Editor/PageController pointers across event-loop
    // turns. Reload the live notebook context and prove that it is still the
    // exact page for which this work was queued before touching MyScript.
    layers::LayerContext context;
    QString contextError;
    if (!cnt::layers_state::loadLayerContext(
            firmware,
            runtime,
            state->controller,
            pins.maximumNotebookLayers,
            &context,
            &contextError)
            || context.state.notebookPath != state->expectedState.notebookPath
            || context.state.partId != state->expectedState.partId) {
        trace(QLatin1String("layers: deferred popup preview cancelled context=")
            + contextError);
        return;
    }
    if (context.widgetObject->property("_cnt_layer_operation_active").toBool()) {
        --state->next;
        --state->attempted;
        state->timer->start(pins.deferredNextMs);
        return;
    }
    bool knownLayer = row.id
        == cnt::layers_state::nativeDocumentLayerId(firmware);
    for (int i = 0; !knownLayer && i < context.state.customLayers.size(); ++i)
        knownLayer = context.state.customLayers.at(i).id == row.id;
    if (!knownLayer) {
        trace(QLatin1String("layers: deferred popup preview cancelled missing id=")
            + row.id);
        return;
    }

    QString itemError;
    bool const wasStale = layerPreviewNeedsRefreshImpl(
        dependencies, context.state, row.id);
    bool refreshed = !wasStale;
    if (wasStale)
        refreshed = generateLayerPreview(
            dependencies, context, row.id, &itemError);
    if (refreshed) {
        if (wasStale)
            ++state->generated;
        if (row.previewLabel) {
            row.previewLabel->setPixmap(layerPreviewImpl(
                dependencies,
                context.state,
                row.id,
                row.name,
                row.active,
                nullptr));
        }
        if (row.textLabel)
            row.textLabel->setText(
                layerPopupRowLabelImpl(row.name, row.active, false));
    } else {
        trace(QLatin1String("layers: deferred popup preview retained cache id=")
            + row.id + QLatin1String(" error=") + itemError);
    }
    trace(QLatin1String("layers: deferred popup preview row id=") + row.id
        + QLatin1String(" generated=")
        + (refreshed ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" ms=") + QString::number(itemTimer.elapsed()));

    if (state->next < state->rows.size()
            && state->attempted < pins.deferredBudget) {
        state->timer->start(pins.deferredNextMs);
    } else {
        trace(QLatin1String("layers: deferred popup preview refresh complete attempted=")
            + QString::number(state->attempted)
            + QLatin1String(" generated=") + QString::number(state->generated)
            + QLatin1String(" remaining=")
            + QString::number(state->rows.size() - state->next)
            + QLatin1String(" ms=") + QString::number(state->elapsed.elapsed()));
    }
}

static void startDeferredLayerPreviewRefreshImpl(
    Dependencies dependencies,
    QObject* controller,
    layers::LayerContext const& context,
    QMenu* popup,
    QVector<DeferredLayerPreviewRow> const& rows) {
    if (!popup || rows.isEmpty() || !dependencies.runtime->previewApisReady) {
        trace(QLatin1String("layers: deferred popup preview refresh queued=0"));
        return;
    }

    std::shared_ptr<DeferredLayerPreviewRefresh> const state(
        new DeferredLayerPreviewRefresh(dependencies));
    state->controller = controller;
    state->expectedState = context.state;
    state->rows = rows;
    state->elapsed.start();
    QTimer* const timer = new QTimer(popup);
    timer->setSingleShot(true);
    state->timer = timer;
    QObject::connect(timer, &QTimer::timeout, [state]() {
        continueDeferredLayerPreviewRefresh(state);
    });
    timer->start(dependencies.pins.deferredStartMs);
    trace(QLatin1String("layers: deferred popup preview refresh queued=")
        + QString::number(rows.size()));
}

} // namespace

void showLayerError(
        Dependencies dependencies,
        void* widget,
        QString const& error) {
    showLayerErrorImpl(dependencies, widget, error);
}

bool performLayerOperation(
        Dependencies dependencies,
        layers::LayerContext* context,
        layers::LayerOperation operation,
        QString const& id,
        QString* error) {
    return performLayerOperationImpl(
        dependencies, context, operation, id, error);
}

bool layerPreviewNeedsRefresh(
        Dependencies dependencies,
        layers::LayerState const& state,
        QString const& id) {
    return layerPreviewNeedsRefreshImpl(dependencies, state, id);
}

QPixmap layerPreview(
        Dependencies dependencies,
        layers::LayerState const& state,
        QString const& id,
        QString const& name,
        bool active,
        bool* cacheDrawnResult) {
    return layerPreviewImpl(
        dependencies,
        state,
        id,
        name,
        active,
        cacheDrawnResult);
}

QString layerPopupRowLabel(
        QString const& name,
        bool active,
        bool previewPending) {
    return layerPopupRowLabelImpl(name, active, previewPending);
}

void startDeferredLayerPreviewRefresh(
        Dependencies dependencies,
        QObject* controller,
        layers::LayerContext const& context,
        QMenu* popup,
        QVector<DeferredLayerPreviewRow> const& rows) {
    startDeferredLayerPreviewRefreshImpl(
        dependencies, controller, context, popup, rows);
}

} // namespace layers_preview
} // namespace cnt
