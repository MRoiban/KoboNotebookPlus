#line 5469 "src/customnotebooktemplates.cc"

static bool copyLiveLayerPreviewImageLoader(
    LayerContext const& context,
    SharedImageLoader* loaderResult,
    QString* error) {
    if (!loaderResult || !context.widget || !context.widgetObject
            || context.widgetObject.data()
                != reinterpret_cast<QObject*>(context.widget)
            || !firmwareApi().uirefEditorWidgetVtable) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook UI is no longer live.");
        }
        return false;
    }

    char* const widgetBytes = static_cast<char*>(context.widget);
    void* const editorWidgetGuard = *reinterpret_cast<void**>(
        widgetBytes + kNotePadEditorWidgetGuardOffset);
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
        widgetBytes + kNotePadEditorWidgetObjectOffset);
    void* const expectedEditorWidgetVptr =
        static_cast<char*>(firmwareApi().uirefEditorWidgetVtable) + 8;
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
        editorWidgetBytes + kEditorWidgetEditorObjectOffset);
    void* const liveEditorControl = *reinterpret_cast<void**>(
        editorWidgetBytes + kEditorWidgetEditorControlOffset);
    if (!liveEditorControl || liveEditor != context.editor
            || liveEditorControl != notePadEditorControl(context.widget)) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook canvas is out of sync.");
        }
        return false;
    }

    void* const loaderObject = *reinterpret_cast<void**>(
        editorWidgetBytes + kEditorWidgetImageLoaderObjectOffset);
    void* const loaderControl = *reinterpret_cast<void**>(
        editorWidgetBytes + kEditorWidgetImageLoaderControlOffset);
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
                editorWidgetBytes + kEditorWidgetImageLoaderObjectOffset);
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
    LayerContext const& context,
    SharedImagePainter* painterResult,
    SharedBackendImageDrawer* drawerResult,
    QString* error) {
    if (!painterResult || !drawerResult || !firmwareApi().editorGetEngine
            || !firmwareApi().editorGetConfiguration || !firmwareApi().imagePainterConstructor
            || !firmwareApi().imagePainterDeletingDestructor
            || !firmwareApi().imagePainterSetImageLoader
            || !firmwareApi().backendImageDrawerMakeShared) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: stock image writer APIs are missing.");
        }
        return false;
    }

    SharedEngine const engine = firmwareApi().editorGetEngine(context.editor);
    SharedConfiguration const configuration =
        firmwareApi().editorGetConfiguration(context.editor);
    if (!engine || !configuration) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: editor services are missing.");
        }
        return false;
    }

    SharedImageLoader imageLoader;
    if (!copyLiveLayerPreviewImageLoader(context, &imageLoader, error))
        return false;

    void* painterMemory = nullptr;
    try {
        painterMemory = ::operator new(kImagePainterObjectBytes);
        memset(painterMemory, 0, kImagePainterObjectBytes);
        firmwareApi().imagePainterConstructor(painterMemory);
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
            ImagePainterDeletingDeleter());
        firmwareApi().imagePainterSetImageLoader(painter.get(), imageLoader);
        SharedEngine engineArgument = engine;
        SharedConfiguration configurationArgument = configuration;
        firmwareApi().backendImageDrawerMakeShared(
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
    alignas(4) unsigned char bytes[kStockPreviewContextBytes];

    StockPreviewCallbackContext(void* drawer, void* backend) {
        memset(bytes, 0, sizeof(bytes));
        memcpy(bytes + kStockPreviewDrawerOffset, &drawer, sizeof(drawer));
        memcpy(bytes + kStockPreviewBackendOffset, &backend, sizeof(backend));
    }
};

class LayerPreviewRendererListener {
public:
    LayerPreviewRendererListener(
        SharedImagePainter const& painter,
        SharedBackendImageDrawer const& drawer,
        SharedRenderer const& liveRenderer,
        void* backend,
        std::string const& layerId,
        bool* restrictionApplied,
        bool* writerInvoked)
        : painter_(painter),
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
            if (renderer && firmwareApi().rendererRestrictToLayers) {
                std::vector<std::string> ids(1, layerId_);
                if (firmwareApi().backgroundObjectLayerName
                        && !firmwareApi().backgroundObjectLayerName->empty()
                        && *firmwareApi().backgroundObjectLayerName != layerId_) {
                    ids.push_back(*firmwareApi().backgroundObjectLayerName);
                }
                firmwareApi().rendererRestrictToLayers(renderer.get(), ids);
                if (restrictionApplied_)
                    *restrictionApplied_ = true;
            }
        } catch (...) {
            trace("layers: preview renderer restriction failed");
        }

        if (!firmwareApi().stockBackendImageDrawerExport || !drawer_ || !backend_)
            return;
        StockPreviewCallbackContext callback(drawer_.get(), backend_);
        if (writerInvoked_)
            *writerInvoked_ = true;
        try {
            firmwareApi().stockBackendImageDrawerExport(
                callback.bytes, renderer, selection, extent, flags, path);
        } catch (...) {
            trace("layers: stock preview image writer threw");
        }
    }

private:
    SharedImagePainter painter_;
    SharedBackendImageDrawer drawer_;
    SharedRenderer liveRenderer_;
    void* backend_;
    std::string layerId_;
    bool* restrictionApplied_;
    bool* writerInvoked_;
};

static bool liveNeboControllerAndBackend(
    LayerContext const& context,
    SharedRenderer* rendererKeepAlive,
    void** pageController,
    void** backendResult,
    QString* error) {
    if (!rendererKeepAlive || !pageController || !backendResult
            || !firmwareApi().editorGetRenderer || !firmwareApi().rendererGetBackend || !firmwareApi().neboBackendVtable) {
        if (error)
            *error = QLatin1String("Layer preview unavailable: renderer APIs are missing.");
        return false;
    }

    *rendererKeepAlive = firmwareApi().editorGetRenderer(context.editor);
    void* const backend = *rendererKeepAlive
        ? firmwareApi().rendererGetBackend(rendererKeepAlive->get()) : nullptr;
    void* const expectedVptr = static_cast<char*>(firmwareApi().neboBackendVtable) + 8;
    if (!backend || *reinterpret_cast<void**>(backend) != expectedVptr) {
        if (error)
            *error = QLatin1String("Layer preview unavailable for this notebook backend.");
        return false;
    }

    *pageController = *reinterpret_cast<void**>(
        static_cast<char*>(backend) + kNeboBackendPageControllerOffset);
    if (!*pageController) {
        if (error)
            *error = QLatin1String("Layer preview unavailable: page controller is missing.");
        return false;
    }
    *backendResult = backend;
    return true;
}

static QString layerPreviewPath(LayerState const& state, QString const& id) {
    QByteArray key = state.notebookPath.toUtf8();
    key.append('\n');
    key.append(state.partId.toUtf8());
    key.append('\n');
    key.append(id.toUtf8());
    QByteArray const digest = QCryptographicHash::hash(
        key, QCryptographicHash::Sha256).toHex();
    return QDir(QLatin1String(kLayerPreviewRoot)).filePath(
        QString::fromLatin1(digest) + QLatin1String(".png"));
}

static bool layerPreviewNeedsRefresh(
    LayerState const& state,
    QString const& id) {
    QFileInfo const preview(layerPreviewPath(state, id));
    QFileInfo const notebook(state.notebookPath);
    return !preview.isFile()
        || preview.size() <= 0
        || preview.size() > kMaximumLayerPreviewBytes
        || (notebook.isFile() && preview.lastModified() < notebook.lastModified());
}

static bool layerPreviewCacheUsable(
    LayerState const& state,
    QString const& id) {
    QFileInfo const preview(layerPreviewPath(state, id));
    return preview.isFile()
        && preview.size() > 0
        && preview.size() <= kMaximumLayerPreviewBytes;
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
    LayerContext const& context,
    QString const& id,
    QString* error) {
    if (!layerPreviewApisReady || !firmwareApi().rendererRestrictToLayers
            || !firmwareApi().pageControllerExportToPng || !firmwareApi().stockBackendImageDrawerExport) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: firmware export APIs are not ready.");
        }
        return false;
    }
    if (!QDir().mkpath(QLatin1String(kLayerPreviewRoot))) {
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
            context, &rendererKeepAlive, &pageController, &backend, error)) {
        return false;
    }
    SharedImagePainter painter;
    SharedBackendImageDrawer drawer;
    if (!createLayerPreviewDrawer(
            context, &painter, &drawer, error)) {
        return false;
    }

    QString const finalPath = layerPreviewPath(context.state, id);
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
        exported = firmwareApi().pageControllerExportToPng(
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
            || generated.size() > kMaximumLayerPreviewBytes) {
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
        QSize(kLayerPreviewCardWidth - 6, kLayerPreviewCardHeight - 6),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    QString cacheCandidatePath = temporaryPath;
    bool cardSaved = !card.isNull()
        && card.save(cardTemporaryPath, "PNG");
    QFileInfo const cardFile(cardTemporaryPath);
    if (cardSaved && cardFile.isFile() && cardFile.size() > 0
            && cardFile.size() <= kMaximumLayerPreviewBytes) {
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
    layerPreviewCardCache.remove(finalPath);
    return true;
}

static bool readLayerPreviewCard(
    LayerState const& state,
    QString const& id,
    QImage* result) {
    if (!result || !layerPreviewCacheUsable(state, id))
        return false;
    QString const path = layerPreviewPath(state, id);
    QFileInfo const cached(path);
    qint64 const modifiedMs = cached.lastModified().toMSecsSinceEpoch();
    QHash<QString, LayerPreviewCardCacheEntry>::iterator found =
        layerPreviewCardCache.find(path);
    if (found != layerPreviewCardCache.end()
            && found->modifiedMs == modifiedMs
            && found->size == cached.size()
            && !found->image.isNull()) {
        found->sequence = ++layerPreviewCardCacheSequence;
        *result = found->image;
        return true;
    }
    if (found != layerPreviewCardCache.end())
        layerPreviewCardCache.erase(found);

    QImageReader reader(path);
    QSize const decodedSize = reader.size();
    if (!decodedSize.isValid() || decodedSize.width() > 4096
            || decodedSize.height() > 4096) {
        return false;
    }
    QSize const target = decodedSize.scaled(
        QSize(kLayerPreviewCardWidth - 6, kLayerPreviewCardHeight - 6),
        Qt::KeepAspectRatio);
    if (target.isValid() && target != decodedSize)
        reader.setScaledSize(target);
    QImage image = reader.read();
    if (image.isNull())
        return false;
    if (image.width() > kLayerPreviewCardWidth - 6
            || image.height() > kLayerPreviewCardHeight - 6) {
        image = image.scaled(
            QSize(kLayerPreviewCardWidth - 6, kLayerPreviewCardHeight - 6),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }
    if (image.isNull())
        return false;

    if (layerPreviewCardCache.size() >= kMaximumLayerPreviewCardCacheEntries) {
        QHash<QString, LayerPreviewCardCacheEntry>::iterator oldest =
            layerPreviewCardCache.begin();
        for (QHash<QString, LayerPreviewCardCacheEntry>::iterator it =
                 layerPreviewCardCache.begin();
                it != layerPreviewCardCache.end(); ++it) {
            if (it->sequence < oldest->sequence)
                oldest = it;
        }
        if (oldest != layerPreviewCardCache.end())
            layerPreviewCardCache.erase(oldest);
    }
    LayerPreviewCardCacheEntry entry;
    entry.image = image;
    entry.modifiedMs = modifiedMs;
    entry.size = cached.size();
    entry.sequence = ++layerPreviewCardCacheSequence;
    layerPreviewCardCache.insert(path, entry);
    *result = image;
    return true;
}

static QPixmap layerPreview(
    LayerState const& state,
    QString const& id,
    QString const& name,
    bool active,
    bool* cacheDrawnResult = nullptr) {
    QPixmap preview(kLayerPreviewCardWidth, kLayerPreviewCardHeight);
    preview.fill(Qt::white);
    QPainter painter(&preview);
    bool cacheDrawn = false;
    // A stale but valid cache is still a better first frame than a text
    // placeholder. The popup queues its replacement only after it is visible.
    QImage image;
    if (readLayerPreviewCard(state, id, &image)) {
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

static void showLayerError(void* widget, QString const& error) {
    trace("layers: operation failed safely");
    if (firmwareApi().showErrorPopup)
        firmwareApi().showErrorPopup(widget, error);
}

static bool addNotebookLayer(LayerContext* context, QString* error) {
    if (!context || context->state.customLayers.size() + 1 >= kMaximumNotebookLayers) {
        if (error)
            *error = QLatin1String("Layer not added: this notebook reached the layer limit.");
        return false;
    }

    firmwareApi().widgetSave(context->widget);
    QString backupPath;
    if (!backupNotebookPath(
            context->state.notebookPath,
            QLatin1String(kLayerBackupRoot),
            QLatin1String("Add layer"),
            &backupPath,
            error)) {
        return false;
    }

    LayoutStorage layout;
    if (!layoutForPart(context->part, &layout, error))
        return false;
    LayerRecord record;
    record.id = QLatin1String("cnt.layer.")
        + QUuid::createUuid().toString().remove('{').remove('}');
    record.name = QLatin1String("Layer ")
        + QString::number(context->state.customLayers.size() + 2);
    std::string const nativeId = record.id.toUtf8().constData();
    if (!firmwareApi().layoutAppendLayer(layout.bytes, nativeId)) {
        if (error)
            *error = QLatin1String("Layer not added: document rejected the new layer.");
        return false;
    }
    bool addedExists = false;
    if (!nativeLayerExists(layout, record.id, &addedExists, error)
            || !addedExists) {
        try {
            firmwareApi().layoutRemoveLayer(layout.bytes, nativeId);
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

    LayerState const previous = context->state;
    context->state.customLayers.append(record);
    context->state.activeId = record.id;
    if (!applyActiveLayer(*context, record.id, error)
            || !saveLayerState(context->state, error)) {
        firmwareApi().layoutRemoveLayer(layout.bytes, nativeId);
        context->state = previous;
        saveLayerState(previous, nullptr);
        applyActiveLayer(*context, previous.activeId, nullptr);
        return false;
    }

    trace("layers: package save begin after native add");
    firmwareApi().packageSave(context->package.get());
    trace("layers: package save complete after native add");
    firmwareApi().widgetSave(context->widget);
    trace("layers: widget save complete after native add");
    traceSerializedLayerProbe(context->state, record.id, "after-add-save");
    firmwareApi().widgetRefresh(context->widget);
    trace("layers: native layer added and selected");
    return true;
}

static bool activateNotebookLayer(
    LayerContext* context,
    QString const& id,
    QString* error) {
    if (!context)
        return false;
    bool known = id == nativeDocumentLayerId();
    for (int i = 0; !known && i < context->state.customLayers.size(); ++i)
        known = context->state.customLayers.at(i).id == id;
    if (!known) {
        if (error)
            *error = QLatin1String("Layer not selected: layer metadata is stale.");
        return false;
    }

    QString const previous = context->state.activeId;
    if (!applyActiveLayer(*context, id, error))
        return false;
    context->state.activeId = id;
    if (!saveLayerState(context->state, error)) {
        context->state.activeId = previous;
        applyActiveLayer(*context, previous, nullptr);
        return false;
    }
    // Keep the live renderer unrestricted. Firmware uses restrictToLayers()
    // only on isolated export/thumbnail renderers; mutating the live
    // RenderPad vector from this menu callback races its render lifecycle.
    // The active ToolDispatcher still routes all future strokes natively.
    firmwareApi().widgetRefresh(context->widget);
    trace("layers: active layer changed");
    return true;
}

static bool deleteActiveNotebookLayer(LayerContext* context, QString* error) {
    if (!context || context->state.activeId == nativeDocumentLayerId()) {
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

    firmwareApi().widgetSave(context->widget);
    QString backupPath;
    if (!backupNotebookPath(
            context->state.notebookPath,
            QLatin1String(kLayerBackupRoot),
            QLatin1String("Delete layer"),
            &backupPath,
            error)) {
        return false;
    }

    LayoutStorage layout;
    if (!layoutForPart(context->part, &layout, error))
        return false;
    QString const deletedId = context->state.activeId;
    std::string const nativeId = deletedId.toUtf8().constData();
    QString const previewPath = layerPreviewPath(
        context->state, context->state.activeId);
    firmwareApi().layoutRemoveLayer(layout.bytes, nativeId);
    bool stillExists = true;
    if (!nativeLayerExists(
            layout, context->state.activeId, &stillExists, error)) {
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
    context->state.activeId = nativeDocumentLayerId();
    if (!applyActiveLayer(*context, context->state.activeId, error)
            || !saveLayerState(context->state, error)) {
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layer was removed, but its UI state could not be finalized. "
                "A notebook backup is available.");
        }
        return false;
    }
    trace("layers: package save begin after native delete");
    firmwareApi().packageSave(context->package.get());
    trace("layers: package save complete after native delete");
    firmwareApi().widgetSave(context->widget);
    trace("layers: widget save complete after native delete");
    traceSerializedLayerProbe(context->state, deletedId, "after-delete-save");
    QFile::remove(previewPath);
    layerPreviewCardCache.remove(previewPath);
    firmwareApi().widgetRefresh(context->widget);
    trace("layers: active native layer deleted");
    return true;
}

static bool refreshLayerPreviews(LayerContext* context, QString* error) {
    if (!context)
        return false;
    firmwareApi().widgetSave(context->widget);

    QVector<QString> ids;
    ids.append(nativeDocumentLayerId());
    for (int i = 0; i < context->state.customLayers.size(); ++i)
        ids.append(context->state.customLayers.at(i).id);

    int generated = 0;
    QString firstError;
    for (int i = 0; i < ids.size(); ++i) {
        QString itemError;
        if (generateLayerPreview(*context, ids.at(i), &itemError))
            ++generated;
        else if (firstError.isEmpty())
            firstError = itemError;
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

static QString layerPopupRowLabel(
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

struct DeferredLayerPreviewRow {
    QString id;
    QString name;
    bool active;
    QPointer<QLabel> previewLabel;
    QPointer<QLabel> textLabel;
};

struct DeferredLayerPreviewRefresh {
    QPointer<QObject> controller;
    LayerState expectedState;
    QVector<DeferredLayerPreviewRow> rows;
    QPointer<QTimer> timer;
    int next;
    int attempted;
    int generated;
    QElapsedTimer elapsed;

    DeferredLayerPreviewRefresh()
        : next(0), attempted(0), generated(0) {}
};

// MyScript renderers are GUI-thread-affine, so this deliberately does not use
// a worker. The native popup is constructed from cached images first; a
// parented timer then exports one stale row per turn of QMenu's nested event
// loop. Closing the popup destroys the timer and cancels all remaining work.
static void continueDeferredLayerPreviewRefresh(
    std::shared_ptr<DeferredLayerPreviewRefresh> const& state) {
    if (!state || !state->timer || !state->controller)
        return;
    if (state->next >= state->rows.size()
            || state->attempted >= kLayerPreviewDeferredBudget) {
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
    LayerContext context;
    QString contextError;
    if (!loadLayerContext(state->controller, &context, &contextError)
            || context.state.notebookPath != state->expectedState.notebookPath
            || context.state.partId != state->expectedState.partId) {
        trace(QLatin1String("layers: deferred popup preview cancelled context=")
            + contextError);
        return;
    }
    if (context.widgetObject->property("_cnt_layer_operation_active").toBool()) {
        --state->next;
        --state->attempted;
        state->timer->start(kLayerPreviewDeferredNextMs);
        return;
    }
    bool knownLayer = row.id == nativeDocumentLayerId();
    for (int i = 0; !knownLayer && i < context.state.customLayers.size(); ++i)
        knownLayer = context.state.customLayers.at(i).id == row.id;
    if (!knownLayer) {
        trace(QLatin1String("layers: deferred popup preview cancelled missing id=")
            + row.id);
        return;
    }

    QString itemError;
    bool const wasStale = layerPreviewNeedsRefresh(context.state, row.id);
    bool refreshed = !wasStale;
    if (wasStale)
        refreshed = generateLayerPreview(context, row.id, &itemError);
    if (refreshed) {
        if (wasStale)
            ++state->generated;
        if (row.previewLabel) {
            row.previewLabel->setPixmap(layerPreview(
                context.state, row.id, row.name, row.active));
        }
        if (row.textLabel)
            row.textLabel->setText(layerPopupRowLabel(row.name, row.active, false));
    } else {
        trace(QLatin1String("layers: deferred popup preview retained cache id=")
            + row.id + QLatin1String(" error=") + itemError);
    }
    trace(QLatin1String("layers: deferred popup preview row id=") + row.id
        + QLatin1String(" generated=")
        + (refreshed ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" ms=") + QString::number(itemTimer.elapsed()));

    if (state->next < state->rows.size()
            && state->attempted < kLayerPreviewDeferredBudget) {
        state->timer->start(kLayerPreviewDeferredNextMs);
    } else {
        trace(QLatin1String("layers: deferred popup preview refresh complete attempted=")
            + QString::number(state->attempted)
            + QLatin1String(" generated=") + QString::number(state->generated)
            + QLatin1String(" remaining=")
            + QString::number(state->rows.size() - state->next)
            + QLatin1String(" ms=") + QString::number(state->elapsed.elapsed()));
    }
}

static void startDeferredLayerPreviewRefresh(
    QObject* controller,
    LayerContext const& context,
    QMenu* popup,
    QVector<DeferredLayerPreviewRow> const& rows) {
    if (!popup || rows.isEmpty() || !layerPreviewApisReady) {
        trace(QLatin1String("layers: deferred popup preview refresh queued=0"));
        return;
    }

    std::shared_ptr<DeferredLayerPreviewRefresh> const state(
        new DeferredLayerPreviewRefresh);
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
    timer->start(kLayerPreviewDeferredStartMs);
    trace(QLatin1String("layers: deferred popup preview refresh queued=")
        + QString::number(rows.size()));
}
