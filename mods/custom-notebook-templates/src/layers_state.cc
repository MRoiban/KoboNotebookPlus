#include "layers_state.h"

#include "cover_cache.h"
#include "firmware_api.h"
#include "notebook_widget.h"
#include "settings.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <cstring>
#include <string>

namespace cnt {
namespace layers_state {
namespace {

static char const kLayerRoot[] = "/mnt/onboard/.kobo/custom/layers/";

struct PageStorage {
    alignas(8) unsigned char bytes[64];
    bool constructed;
    FirmwareApi* firmware;

    explicit PageStorage(FirmwareApi& firmwareApi)
        : constructed(false), firmware(&firmwareApi) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~PageStorage() {
        if (constructed)
            firmware->pageDestructor(bytes);
    }
};

struct ManagedObjectStorage {
    // myscript::engine::ManagedObject and its document wrappers are one
    // acquired _voReference pointer on this 32-bit SDK.
    alignas(4) unsigned char bytes[4];
    bool constructed;
    FirmwareApi* firmware;

    explicit ManagedObjectStorage(FirmwareApi& firmwareApi)
        : constructed(false), firmware(&firmwareApi) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~ManagedObjectStorage() {
        if (constructed && firmware->managedObjectDestructor)
            firmware->managedObjectDestructor(bytes);
    }

    bool hasObject() const {
        void* object = nullptr;
        memcpy(&object, bytes, sizeof(object));
        return object != nullptr;
    }
};
static_assert(sizeof(void*) == 4,
    "MyScript managed-object ABI requires the 32-bit Kobo target");

QString iinkStringValue(FirmwareApi& firmware, IInkStringStorage* value) {
    if (!value || !value->impl || !firmware.iinkStringToStdString)
        return QString();
    std::string utf8;
    firmware.iinkStringToStdString(&utf8, value);
    return QString::fromUtf8(utf8.data(), static_cast<int>(utf8.size()));
}

QString partStableId(FirmwareApi& firmware, SharedPart const& part) {
    if (!part || !firmware.partGetId)
        return QString();
    IInkStringStorage value = {nullptr};
    firmware.partGetId(&value, part.get());
    QString const id = iinkStringValue(firmware, &value);
    delete value.impl;
    return id;
}

bool safeLayerId(QString const& id) {
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

QString canonicalNotebookPath(QString const& notebookPath) {
    QString const canonical = QFileInfo(notebookPath).canonicalFilePath();
    return canonical.isEmpty()
        ? QDir::cleanPath(notebookPath)
        : canonical;
}

QString layerStatePath(QString const& notebookPath, QString const& partId) {
    QByteArray key = canonicalNotebookPath(notebookPath).toUtf8();
    key.append('\n');
    key.append(partId.toUtf8());
    QByteArray const digest = QCryptographicHash::hash(
        key, QCryptographicHash::Sha256).toHex();
    return QDir(QLatin1String(kLayerRoot)).filePath(
        QString::fromLatin1(digest) + QLatin1String(".json"));
}

layers::LayerState loadLayerState(
        FirmwareApi& firmware,
        QString const& notebookPath,
        QString const& partId,
        int maximumNotebookLayers) {
    layers::LayerState state;
    state.notebookPath = canonicalNotebookPath(notebookPath);
    state.partId = partId;
    state.activeId = nativeDocumentLayerId(firmware);

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
    QJsonArray const layerRows = root.value(QLatin1String("layers")).toArray();
    for (int i = 0;
            i < layerRows.size()
                && state.customLayers.size() < maximumNotebookLayers - 1;
            ++i) {
        QJsonObject const item = layerRows.at(i).toObject();
        layers::LayerRecord record;
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
    if (active == nativeDocumentLayerId(firmware) || ids.contains(active))
        state.activeId = active;
    trace(QLatin1String("layers: sidecar loaded file=")
        + QFileInfo(sidecarPath).fileName()
        + QLatin1String(" part=") + partId
        + QLatin1String(" active=") + state.activeId
        + QLatin1String(" rows=")
        + QString::number(state.customLayers.size()));
    return state;
}

bool nativeDocumentLayout(
        FirmwareApi& firmware,
        LayoutStorage const& layout,
        ManagedObjectStorage* documentLayout,
        QString* error) {
    if (!documentLayout || !firmware.atkLayoutRawLayout
            || !firmware.managedObjectDestructor) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layout API is missing.");
        return false;
    }
    try {
        firmware.atkLayoutRawLayout(documentLayout->bytes, layout.bytes);
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

bool nativeLayerExists(
        FirmwareApi& firmware,
        ManagedObjectStorage const& documentLayout,
        QString const& id,
        bool* exists,
        QString* error) {
    if (!exists || !firmware.documentLayoutGetLayer
            || !firmware.layerIteratorIsAtEnd || id.isEmpty()) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layer API is missing.");
        return false;
    }
    ManagedObjectStorage iterator(firmware);
    try {
        std::string const nativeId = id.toUtf8().constData();
        firmware.documentLayoutGetLayer(
            iterator.bytes, documentLayout.bytes, nativeId);
        iterator.constructed = true;
        *exists = iterator.hasObject()
            && !firmware.layerIteratorIsAtEnd(iterator.bytes);
        return true;
    } catch (...) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layer validation failed.");
        return false;
    }
}

bool reconcileLayerStateWithNativeLayout(
        FirmwareApi& firmware,
        SharedPart const& part,
        layers::LayerState* state,
        QString* error) {
    if (!state || !firmware.atkLayoutRawLayout
            || !firmware.documentLayoutGetLayer
            || !firmware.layerIteratorIsAtEnd
            || !firmware.managedObjectDestructor) {
        if (error)
            *error = QLatin1String("Layers unavailable: reconciliation API is missing.");
        return false;
    }

    LayoutStorage layout(firmware);
    if (!loadLayoutForPart(firmware, part, &layout, error))
        return false;

    ManagedObjectStorage documentLayout(firmware);
    if (!nativeDocumentLayout(firmware, layout, &documentLayout, error))
        return false;

    bool baseExists = false;
    if (!nativeLayerExists(
            firmware,
            documentLayout,
            nativeDocumentLayerId(firmware),
            &baseExists,
            error)
            || !baseExists) {
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layers unavailable: the native document layer is missing.");
        }
        return false;
    }

    for (int i = 0; i < state->customLayers.size(); ++i) {
        layers::LayerRecord const record = state->customLayers.at(i);
        bool exists = false;
        if (!nativeLayerExists(
                firmware, documentLayout, record.id, &exists, error)) {
            return false;
        }
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

} // namespace

LayoutStorage::LayoutStorage(FirmwareApi& firmwareApi)
    : constructed(false), firmware(&firmwareApi) {
    memset(bytes, 0, sizeof(bytes));
}

LayoutStorage::~LayoutStorage() {
    if (constructed)
        firmware->layoutDestructor(bytes);
}

QString nativeDocumentLayerId(FirmwareApi& firmware) {
    if (!firmware.documentLayerName || firmware.documentLayerName->empty())
        return QString();
    return QString::fromUtf8(
        firmware.documentLayerName->data(),
        static_cast<int>(firmware.documentLayerName->size()));
}

bool loadLayoutForPart(
        FirmwareApi& firmware,
        SharedPart const& part,
        LayoutStorage* layout,
        QString* error) {
    if (!part || !layout || !firmware.partGetPage || !firmware.pageLayout
            || !firmware.layoutDestructor) {
        if (error) {
            *error = QLatin1String(
                "Layers unavailable: page layout API is not ready.");
        }
        return false;
    }

    PageStorage page(firmware);
    firmware.partGetPage(page.bytes, part.get());
    page.constructed = true;
    firmware.pageLayout(layout->bytes, page.bytes);
    layout->constructed = true;
    return true;
}

bool nativeLayerExists(
        FirmwareApi& firmware,
        LayoutStorage const& layout,
        QString const& id,
        bool* exists,
        QString* error) {
    ManagedObjectStorage documentLayout(firmware);
    return nativeDocumentLayout(firmware, layout, &documentLayout, error)
        && nativeLayerExists(firmware, documentLayout, id, exists, error);
}

void traceSerializedLayerProbe(
        cover_cache::State& coverState,
        layers::LayerState const& state,
        QString const& id,
        char const* phase) {
    int const marker = cover_cache::notebookArchiveContainsLayerId(
        coverState, state.notebookPath, state.partId, id);
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

bool saveLayerState(layers::LayerState const& state, QString* error) {
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

    QJsonArray layerRows;
    for (int i = 0; i < state.customLayers.size(); ++i) {
        QJsonObject item;
        item.insert(QLatin1String("id"), state.customLayers.at(i).id);
        item.insert(QLatin1String("name"), state.customLayers.at(i).name);
        layerRows.append(item);
    }

    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("notebook"), state.notebookPath);
    root.insert(QLatin1String("part"), state.partId);
    root.insert(QLatin1String("active"), state.activeId);
    root.insert(QLatin1String("layers"), layerRows);

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

bool loadLayerContext(
        FirmwareApi& firmware,
        layers::RuntimeState const& runtime,
        QObject* controller,
        int maximumNotebookLayers,
        layers::LayerContext* context,
        QString* error) {
    if (!runtime.hooksReady || !context) {
        if (error)
            *error = QLatin1String("Layers unavailable: firmware APIs are not ready.");
        return false;
    }
    QObject* const widgetObject = notebook_widget::findNotebookWidget(controller);
    if (!widgetObject) {
        if (error)
            *error = QLatin1String("Layers unavailable: notebook widget was not found.");
        return false;
    }

    context->widgetObject = widgetObject;
    context->widget = widgetObject;
    context->editor = notebook_widget::notePadEditor(context->widget);
    if (!context->editor) {
        if (error)
            *error = QLatin1String("Layers unavailable: notebook editor is not ready.");
        return false;
    }
    context->part = firmware.editorGetPart(context->editor);
    context->package = context->part
        ? firmware.partGetPackage(context->part.get()) : SharedPackage();
    QString const rawNotebookPath = QDir::cleanPath(
        firmware.widgetFilePath(context->widget));
    QString const notebookPath = canonicalNotebookPath(rawNotebookPath);
    QString const partId = partStableId(firmware, context->part);
    if (!context->part || !context->package
            || !rawNotebookPath.startsWith(QLatin1String("/mnt/onboard/"))
            || partId.isEmpty()
            || nativeDocumentLayerId(firmware).isEmpty()) {
        if (error)
            *error = QLatin1String("Layers unavailable: page identity is incomplete.");
        return false;
    }
    trace(QLatin1String("layers: context raw=") + rawNotebookPath
        + QLatin1String(" canonical=") + notebookPath
        + QLatin1String(" part=") + partId
        + QLatin1String(" sidecar=")
        + QFileInfo(layerStatePath(notebookPath, partId)).fileName());
    context->state = loadLayerState(
        firmware, notebookPath, partId, maximumNotebookLayers);
    return reconcileLayerStateWithNativeLayout(
        firmware, context->part, &context->state, error);
}

} // namespace layers_state
} // namespace cnt
