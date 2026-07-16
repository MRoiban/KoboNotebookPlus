#line 3659 "src/customnotebooktemplates.cc"

static bool isNotebookWidget(QObject* object) {
    return object
        && object->metaObject()
        && strcmp(object->metaObject()->className(), "IInkNotePadWidget") == 0;
}

static QObject* findNotebookWidget(QObject* controller) {
    QObject* root = controller;
    for (QObject* current = controller; current; current = current->parent()) {
        if (isNotebookWidget(current))
            return current;
        root = current;
    }

    // The menu controller is normally parented directly to the note-pad
    // widget. Keep a guarded descendant fallback for this pinned firmware's
    // alternate ownership path.
    QObjectList const descendants = root->findChildren<QObject*>();
    for (int i = 0; i < descendants.size(); ++i) {
        if (isNotebookWidget(descendants.at(i)))
            return descendants.at(i);
    }
    return nullptr;
}

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

static QString nativeDocumentLayerId() {
    if (!firmwareApi().documentLayerName || firmwareApi().documentLayerName->empty())
        return QString();
    return QString::fromUtf8(
        firmwareApi().documentLayerName->data(),
        static_cast<int>(firmwareApi().documentLayerName->size()));
}

static bool safeLayerId(QString const& id) {
    if (!id.startsWith(QLatin1String("cnt.layer.")) || id.size() > 96)
        return false;
    for (int i = 0; i < id.size(); ++i) {
        ushort const value = id.at(i).unicode();
        if (!((value >= 'a' && value <= 'z')
                || (value >= '0' && value <= '9')
                || value == '.' || value == '-')) {
            return false;
        }
    }
    return true;
}

static QString canonicalNotebookPath(QString const& notebookPath) {
    QString const canonical = QFileInfo(notebookPath).canonicalFilePath();
    return canonical.isEmpty()
        ? QDir::cleanPath(notebookPath)
        : canonical;
}

static QString layerStatePath(QString const& notebookPath, QString const& partId) {
    QByteArray key = canonicalNotebookPath(notebookPath).toUtf8();
    key.append('\n');
    key.append(partId.toUtf8());
    QByteArray const digest = QCryptographicHash::hash(
        key, QCryptographicHash::Sha256).toHex();
    return QDir(QLatin1String(kLayerRoot)).filePath(
        QString::fromLatin1(digest) + QLatin1String(".json"));
}

static void traceSerializedLayerProbe(
    LayerState const& state,
    QString const& id,
    char const* phase) {
    int const marker = cnt::cover_cache::notebookArchiveContainsLayerId(
        coverState(), state.notebookPath, state.partId, id);
    QString markerText = QLatin1String("indeterminate");
    if (marker > 0)
        markerText = QLatin1String("present");
    else if (marker == 0)
        markerText = QLatin1String("absent");
    QFileInfo const notebook(state.notebookPath);
    trace(QLatin1String("layers: archive probe phase=")
        + QLatin1String(phase)
        + QLatin1String(" part=") + state.partId
        + QLatin1String(" id=") + id
        + QLatin1String(" marker=") + markerText
        + QLatin1String(" bytes=") + QString::number(notebook.size())
        + QLatin1String(" mtime-ms=")
        + QString::number(notebook.lastModified().toMSecsSinceEpoch()));
}

static bool saveLayerState(LayerState const& state, QString* error) {
    if (state.notebookPath.isEmpty() || state.partId.isEmpty()) {
        if (error)
            *error = QLatin1String("Layer state not saved: page identity is missing.");
        return false;
    }
    if (!QDir().mkpath(QLatin1String(kLayerRoot))) {
        if (error)
            *error = QLatin1String("Layer state not saved: storage directory failed.");
        return false;
    }

    QJsonArray layers;
    for (int i = 0; i < state.customLayers.size(); ++i) {
        QJsonObject item;
        item.insert(QLatin1String("id"), state.customLayers.at(i).id);
        item.insert(QLatin1String("name"), state.customLayers.at(i).name);
        layers.append(item);
    }

    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("notebook"), state.notebookPath);
    root.insert(QLatin1String("part"), state.partId);
    root.insert(QLatin1String("active"), state.activeId);
    root.insert(QLatin1String("layers"), layers);

    QSaveFile file(layerStatePath(state.notebookPath, state.partId));
    if (!file.open(QIODevice::WriteOnly)
            || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
            || !file.commit()) {
        if (error)
            *error = QLatin1String("Layer state not saved: metadata write failed.");
        return false;
    }
    trace(QLatin1String("layers: sidecar saved file=")
        + QFileInfo(layerStatePath(state.notebookPath, state.partId)).fileName()
        + QLatin1String(" part=") + state.partId
        + QLatin1String(" active=") + state.activeId
        + QLatin1String(" rows=")
        + QString::number(state.customLayers.size()));
    return true;
}

static LayerState loadLayerState(
    QString const& notebookPath,
    QString const& partId) {
    LayerState state;
    state.notebookPath = canonicalNotebookPath(notebookPath);
    state.partId = partId;
    state.activeId = nativeDocumentLayerId();

    QString const sidecarPath = layerStatePath(state.notebookPath, partId);
    QFile file(sidecarPath);
    if (!file.exists()) {
        trace(QLatin1String("layers: sidecar absent file=")
            + QFileInfo(sidecarPath).fileName()
            + QLatin1String(" part=") + partId);
        return state;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
        trace(QLatin1String("layers: sidecar unreadable file=")
            + QFileInfo(sidecarPath).fileName());
        return state;
    }
    QJsonParseError parseError;
    QJsonDocument const document = QJsonDocument::fromJson(
        file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        trace(QLatin1String("layers: sidecar JSON rejected file=")
            + QFileInfo(sidecarPath).fileName());
        return state;
    }

    QJsonObject const root = document.object();
    QString const storedNotebook = root.value(QLatin1String("notebook")).toString();
    QString const storedPart = root.value(QLatin1String("part")).toString();
    if (root.value(QLatin1String("version")).toInt() != 1
            || canonicalNotebookPath(storedNotebook) != state.notebookPath
            || storedPart != partId) {
        trace(QLatin1String("layers: sidecar identity rejected file=")
            + QFileInfo(sidecarPath).fileName()
            + QLatin1String(" expected-part=") + partId
            + QLatin1String(" stored-part=") + storedPart);
        return state;
    }

    QStringList ids;
    QJsonArray const layers = root.value(QLatin1String("layers")).toArray();
    for (int i = 0;
            i < layers.size() && state.customLayers.size() < kMaximumNotebookLayers - 1;
            ++i) {
        QJsonObject const item = layers.at(i).toObject();
        LayerRecord record;
        record.id = item.value(QLatin1String("id")).toString();
        record.name = item.value(QLatin1String("name")).toString().trimmed();
        if (!safeLayerId(record.id) || ids.contains(record.id))
            continue;
        if (record.name.isEmpty() || record.name.size() > 64)
            record.name = QLatin1String("Layer ")
                + QString::number(state.customLayers.size() + 2);
        ids.append(record.id);
        state.customLayers.append(record);
    }

    QString const active = root.value(QLatin1String("active")).toString();
    if (active == nativeDocumentLayerId() || ids.contains(active))
        state.activeId = active;
    trace(QLatin1String("layers: sidecar loaded file=")
        + QFileInfo(sidecarPath).fileName()
        + QLatin1String(" part=") + partId
        + QLatin1String(" active=") + state.activeId
        + QLatin1String(" rows=")
        + QString::number(state.customLayers.size()));
    return state;
}

static bool nativeDocumentLayout(
    LayoutStorage const& layout,
    ManagedObjectStorage* documentLayout,
    QString* error) {
    if (!documentLayout || !firmwareApi().atkLayoutRawLayout || !firmwareApi().managedObjectDestructor) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layout API is missing.");
        return false;
    }
    try {
        firmwareApi().atkLayoutRawLayout(documentLayout->bytes, layout.bytes);
        documentLayout->constructed = true;
    } catch (...) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layout lookup failed.");
        return false;
    }
    if (!documentLayout->hasObject()) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layout is empty.");
        return false;
    }
    return true;
}

static bool nativeLayerExists(
    ManagedObjectStorage const& documentLayout,
    QString const& id,
    bool* exists,
    QString* error) {
    if (!exists || !firmwareApi().documentLayoutGetLayer || !firmwareApi().layerIteratorIsAtEnd
            || id.isEmpty()) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layer API is missing.");
        return false;
    }
    ManagedObjectStorage iterator;
    try {
        std::string const nativeId = id.toUtf8().constData();
        firmwareApi().documentLayoutGetLayer(iterator.bytes, documentLayout.bytes, nativeId);
        iterator.constructed = true;
        *exists = iterator.hasObject() && !firmwareApi().layerIteratorIsAtEnd(iterator.bytes);
        return true;
    } catch (...) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layer validation failed.");
        return false;
    }
}

static bool nativeLayerExists(
    LayoutStorage const& layout,
    QString const& id,
    bool* exists,
    QString* error) {
    ManagedObjectStorage documentLayout;
    return nativeDocumentLayout(layout, &documentLayout, error)
        && nativeLayerExists(documentLayout, id, exists, error);
}

static bool reconcileLayerStateWithNativeLayout(
    SharedPart const& part,
    LayerState* state,
    QString* error) {
    if (!state || !firmwareApi().atkLayoutRawLayout || !firmwareApi().documentLayoutGetLayer
            || !firmwareApi().layerIteratorIsAtEnd || !firmwareApi().managedObjectDestructor) {
        if (error)
            *error = QLatin1String("Layers unavailable: reconciliation API is missing.");
        return false;
    }

    LayoutStorage layout;
    if (!layoutForPart(part, &layout, error))
        return false;

    ManagedObjectStorage documentLayout;
    if (!nativeDocumentLayout(layout, &documentLayout, error))
        return false;

    bool baseExists = false;
    if (!nativeLayerExists(
            documentLayout, nativeDocumentLayerId(), &baseExists, error)
            || !baseExists) {
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layers unavailable: the native document layer is missing.");
        }
        return false;
    }

    for (int i = 0; i < state->customLayers.size(); ++i) {
        LayerRecord const record = state->customLayers.at(i);
        bool exists = false;
        if (!nativeLayerExists(documentLayout, record.id, &exists, error))
            return false;
        trace(QLatin1String("layers: native layer probe id=") + record.id
            + QLatin1String(" exists=")
            + (exists ? QLatin1String("yes") : QLatin1String("no")));
        if (!exists) {
            // A passive lifecycle callback can observe a page while its
            // native layout is still being reconstructed. Never turn one
            // transient negative lookup into permanent metadata loss. Explicit
            // add/delete paths already update the sidecar transactionally.
            trace(QLatin1String(
                "layers: native layer missing; sidecar metadata preserved id=")
                + record.id);
            if (error) {
                *error = QLatin1String(
                    "Layers are temporarily unavailable: a saved native layer "
                    "is not loaded. Its metadata was preserved.");
            }
            return false;
        }
    }

    trace(QLatin1String("layers: native reconciliation verified rows=")
        + QString::number(state->customLayers.size())
        + QLatin1String(" active=") + state->activeId);
    return true;
}

static bool loadLayerContext(
    QObject* controller,
    LayerContext* context,
    QString* error) {
    if (!layerState().hooksReady || !context) {
        if (error)
            *error = QLatin1String("Layers unavailable: firmware APIs are not ready.");
        return false;
    }
    QObject* const widgetObject = findNotebookWidget(controller);
    if (!widgetObject) {
        if (error)
            *error = QLatin1String("Layers unavailable: notebook widget was not found.");
        return false;
    }

    context->widgetObject = widgetObject;
    context->widget = widgetObject;
    context->editor = notePadEditor(context->widget);
    if (!context->editor) {
        if (error)
            *error = QLatin1String("Layers unavailable: notebook editor is not ready.");
        return false;
    }
    context->part = firmwareApi().editorGetPart(context->editor);
    context->package = context->part
        ? firmwareApi().partGetPackage(context->part.get()) : SharedPackage();
    QString const rawNotebookPath = QDir::cleanPath(firmwareApi().widgetFilePath(context->widget));
    QString const notebookPath = canonicalNotebookPath(rawNotebookPath);
    QString const partId = partStableId(context->part);
    if (!context->part || !context->package
            || !rawNotebookPath.startsWith(QLatin1String("/mnt/onboard/"))
            || partId.isEmpty()
            || nativeDocumentLayerId().isEmpty()) {
        if (error)
            *error = QLatin1String("Layers unavailable: page identity is incomplete.");
        return false;
    }
    trace(QLatin1String("layers: context raw=") + rawNotebookPath
        + QLatin1String(" canonical=") + notebookPath
        + QLatin1String(" part=") + partId
        + QLatin1String(" sidecar=")
        + QFileInfo(layerStatePath(notebookPath, partId)).fileName());
    context->state = loadLayerState(notebookPath, partId);
    return reconcileLayerStateWithNativeLayout(
        context->part, &context->state, error);
}
