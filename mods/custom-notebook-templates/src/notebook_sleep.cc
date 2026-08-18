#include "notebook_sleep.h"

#include "cover_cache.h"
#include "firmware_api.h"
#include "firmware_resolver.h"
#include "layers_preview.h"
#include "notebook_widget.h"
#include "notebooksleeprefreshreceiver.h"
#include "notebooksleepsettingsreceiver.h"
#include "pages.h"
#include "plugin_runtime.h"
#include "plugin_state.h"
#include "settings.h"

#include <NickelHook.h>

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLocale>
#include <QMap>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QRegion>
#include <QRectF>
#include <QSaveFile>
#include <QSize>
#include <QStringList>
#include <QTransform>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <dlfcn.h>
#include <new>

namespace {

enum FirmwarePin : uintptr_t {
    ShowSleepViewVma = 0xf3eb98,
    ShowPowerOffViewVma = 0xf3ea54,
    OnCoverLoadedVma = 0xf425b4,
    UpdateCoverVma = 0xf430b0,
    UpdateReadingStatusVma = 0xf42798,
    SetInfoPanelVisibleVma = 0xf3a114,
    SettingsPowerViewConstructorVma = 0x10455dc,
    CheckBoxConstructorVma = 0x1065824,
    DropDownConstructorVma = 0x106599c,
    CheckBoxSetTextVma = 0x10653a0,
    CheckBoxSetCheckedVma = 0x10653c0,
    ItemSetLabelVma = 0x10652c0,
    DropDownVma = 0x10653f8,
    DropDownAddItemVma = 0x10de390,
    DropDownSetCurrentIndexVma = 0x1113230,
};

enum LayoutPin {
    SettingItemObjectBytes = 0x7c,
    FallbackScreenWidth = 1404,
    FallbackScreenHeight = 1872,
    MaximumBackgroundPngBytes = 32 * 1024 * 1024,
    MaximumPinnedStateBytes = 4096,
    MaximumPinnedPartIdCharacters = 128,
};

char const kPinnedPageStatePath[] =
    "/mnt/onboard/.kobo/iink/custom-notebook-templates/notebook-sleep/pinned-page.json";
char const kPinnedPageImagePath[] =
    "/mnt/onboard/.kobo/iink/custom-notebook-templates/notebook-sleep/pinned-page.png";

template <typename Function>
void* functionPointer(Function function) {
    union {
        Function function;
        void* pointer;
    } value = {};
    value.function = function;
    return value.pointer;
}

template <typename Function>
bool installPinnedHook(
        void* handle,
        char const* symbol,
        uintptr_t vma,
        Function replacement,
        Function* original) {
    Function resolved = nullptr;
    if (!resolvePinned(handle, symbol, vma, &resolved))
        return false;
    *original = resolved;
    void* const hooked = nh_dlhook(
        handle, symbol, functionPointer(replacement));
    if (!pointerMatchesVma(hooked, vma))
        return false;
    union {
        void* pointer;
        Function function;
    } value = {};
    value.pointer = hooked;
    *original = value.function;
    return true;
}

static bool validPinnedPartId(QString const& id) {
    if (id.isEmpty() || id.size() > MaximumPinnedPartIdCharacters)
        return false;
    for (int i = 0; i < id.size(); ++i) {
        ushort const value = id.at(i).unicode();
        bool const letter = (value >= 'a' && value <= 'z')
            || (value >= 'A' && value <= 'Z');
        bool const digit = value >= '0' && value <= '9';
        if (!(letter || digit || value == '-' || value == '_'
                || value == '.')) {
            return false;
        }
    }
    return true;
}

static bool validPinnedNotebookPath(QString const& path) {
    return !path.isEmpty()
        && path == QDir::cleanPath(path)
        && path.startsWith(QLatin1String("/mnt/onboard/"))
        && path.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive)
        && !path.contains(QLatin1String("/../"));
}

static QString canonicalNotebookPath(QString const& path) {
    QFileInfo const info(path);
    QString const canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty()
        ? info.absoluteFilePath() : canonical);
}

static bool migrateLegacyPinnedFile(
        QString const& legacyPath,
        QString const& destinationPath) {
    if (!QFileInfo(legacyPath).exists())
        return true;
    if (QFileInfo(destinationPath).exists())
        return QFile::remove(legacyPath);
    if (!QDir().mkpath(QFileInfo(destinationPath).absolutePath()))
        return false;
    if (QFile::rename(legacyPath, destinationPath))
        return true;
    if (!QFile::copy(legacyPath, destinationPath))
        return false;
    return QFile::remove(legacyPath);
}

static void removeLegacyPinnedTemplateArtifacts() {
    QFile::remove(QLatin1String(
        "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page-icon.png"));
    QFile::remove(QLatin1String(
        "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page_condor.png"));
}

static void migrateLegacyPinnedPageFiles() {
    QString const legacyState = QLatin1String(
        "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page.json");
    QString const legacyImage = QLatin1String(
        "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page.png");
    bool const hadLegacy = QFileInfo(legacyState).exists()
        || QFileInfo(legacyImage).exists()
        || QFileInfo(QLatin1String(
            "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page-icon.png"))
            .exists()
        || QFileInfo(QLatin1String(
            "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page_condor.png"))
            .exists();
    if (!hadLegacy)
        return;

    bool const stateMigrated = migrateLegacyPinnedFile(
        legacyState,
        QLatin1String(kPinnedPageStatePath));
    bool const imageMigrated = migrateLegacyPinnedFile(
        legacyImage,
        QLatin1String(kPinnedPageImagePath));
    removeLegacyPinnedTemplateArtifacts();
    if (stateMigrated && imageMigrated)
        cnt::trace("notebook-sleep: legacy pinned cache migrated");
    else
        cnt::trace("notebook-sleep: legacy pinned cache migration incomplete");
}

static void clearPinnedPageMemory(
        cnt::notebook_sleep::RuntimeState& state) {
    state.pinnedNotebookPath.clear();
    state.pinnedPartId.clear();
    state.pinnedPageImage = QImage();
    state.pinnedPageGeneration = 0;
    state.pinnedPageValid = false;
    state.pinnedPageImageLoaded = false;
    state.pinnedPageRenderedInSession = false;
}

static void loadPinnedPageState(
        cnt::notebook_sleep::RuntimeState& state) {
    if (state.pinnedPageLoaded)
        return;
    state.pinnedPageLoaded = true;
    clearPinnedPageMemory(state);

    QString statePath = QLatin1String(kPinnedPageStatePath);
    if (!QFileInfo(statePath).exists()) {
        QString const legacyPath = QLatin1String(
            "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page.json");
        if (QFileInfo(legacyPath).exists())
            statePath = legacyPath;
    }
    QFile file(statePath);
    QFileInfo const info(file);
    if (!info.exists()) {
        cnt::trace("notebook-sleep: no pinned page");
        return;
    }
    if (!info.isFile() || info.size() < 1
            || info.size() > MaximumPinnedStateBytes
            || !file.open(QIODevice::ReadOnly)) {
        cnt::trace("notebook-sleep: pinned page state unreadable; preserving file");
        return;
    }

    QJsonParseError parseError;
    QJsonDocument const document = QJsonDocument::fromJson(
        file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
        cnt::trace("notebook-sleep: pinned page state malformed; preserving file");
        return;
    }
    QJsonObject const object = document.object();
    QJsonValue const versionValue = object.value(QLatin1String("version"));
    double const version = versionValue.toDouble(-1.0);
    QString const notebookPath = object.value(
        QLatin1String("notebookPath")).toString();
    QString const partId = object.value(
        QLatin1String("partId")).toString();
    if (version != 1.0
            || !validPinnedNotebookPath(notebookPath)
            || !validPinnedPartId(partId)) {
        cnt::trace("notebook-sleep: pinned page state invalid; preserving file");
        return;
    }

    state.pinnedNotebookPath = notebookPath;
    state.pinnedPartId = partId;
    state.pinnedPageValid = true;
    cnt::trace("notebook-sleep: pinned page state restored");
}

static bool removePinnedPageState(
        cnt::notebook_sleep::RuntimeState& state) {
    bool removed = true;
    QStringList const statePaths = QStringList()
        << QLatin1String(kPinnedPageStatePath)
        << QLatin1String(
            "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page.json");
    for (QString const& statePath : statePaths) {
        if (QFileInfo(statePath).exists() && !QFile::remove(statePath))
            removed = false;
    }

    QStringList const imagePaths = QStringList()
        << QLatin1String(kPinnedPageImagePath)
        << QLatin1String(
            "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page.png");
    for (QString const& imagePath : imagePaths) {
        if (QFileInfo(imagePath).exists() && !QFile::remove(imagePath)) {
            cnt::trace(
                "notebook-sleep: orphaned pinned image could not be removed");
        }
    }
    removeLegacyPinnedTemplateArtifacts();
    clearPinnedPageMemory(state);
    state.pinnedPageLoaded = true;
    return removed;
}

static void discardUnavailablePinnedPage(
        cnt::notebook_sleep::RuntimeState& state,
        char const* reason) {
    if (!removePinnedPageState(state)) {
        clearPinnedPageMemory(state);
        state.pinnedPageLoaded = true;
        cnt::trace("notebook-sleep: stale pinned state could not be removed");
    }
    cnt::trace(reason);
}

static bool persistPinnedPageState(
        cnt::notebook_sleep::RuntimeState& state,
        QString const& notebookPath,
        QString const& partId) {
    if (!validPinnedNotebookPath(notebookPath)
            || !validPinnedPartId(partId)
            || !QDir().mkpath(
                QFileInfo(QLatin1String(kPinnedPageStatePath))
                    .absolutePath())) {
        return false;
    }

    // A fixed cache filename is safe only after the previous identity's frame
    // is gone. If it cannot be removed, preserve the old pin rather than risk
    // associating that bitmap with the new page.
    QString const imagePath = QLatin1String(kPinnedPageImagePath);
    if (QFileInfo(imagePath).exists() && !QFile::remove(imagePath))
        return false;

    QString const statePath = QLatin1String(kPinnedPageStatePath);
    QSaveFile file(statePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QJsonObject object;
    object.insert(QLatin1String("version"), 1);
    object.insert(QLatin1String("notebookPath"), notebookPath);
    object.insert(QLatin1String("partId"), partId);
    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    if (file.write(bytes) != bytes.size() || !file.commit())
        return false;

    QFile::remove(QLatin1String(
        "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page.json"));
    removeLegacyPinnedTemplateArtifacts();

    clearPinnedPageMemory(state);
    state.pinnedPageLoaded = true;
    state.pinnedNotebookPath = notebookPath;
    state.pinnedPartId = partId;
    state.pinnedPageValid = true;
    return true;
}

static bool currentPageIdentity(
        QObject* controller,
        int maximumPageCount,
        QObject** widgetResult,
        QString* notebookPath,
        QString* partId,
        QString* error) {
    if (widgetResult)
        *widgetResult = nullptr;
    if (notebookPath)
        notebookPath->clear();
    if (partId)
        partId->clear();

    QObject* const widget = cnt::notebook_widget::findNotebookWidget(controller);
    if (!widget) {
        if (error)
            *error = QLatin1String("Sleep page not changed: notebook is not ready.");
        return false;
    }
    if (widgetResult)
        *widgetResult = widget;
    try {
        FirmwareApi& firmware = firmwareApi();
        if (!firmware.editorGetPart || !firmware.partGetPackage
                || !firmware.packagePartCount || !firmware.packageIndexOfPart
                || !firmware.widgetFilePath || !firmware.partGetId
                || !firmware.iinkStringToStdString) {
            if (error) {
                *error = QLatin1String(
                    "Sleep page not changed: firmware APIs are unavailable.");
            }
            return false;
        }
        void* const editor = cnt::notebook_widget::notePadEditor(widget);
        void* const editorControl =
            cnt::notebook_widget::notePadEditorControl(widget);
        SharedPart const part = editor && editorControl
            ? firmware.editorGetPart(editor) : SharedPart();
        SharedPackage const package = part
            ? firmware.partGetPackage(part.get()) : SharedPackage();
        int const count = package
            ? firmware.packagePartCount(package.get()) : 0;
        int const index = package
            ? firmware.packageIndexOfPart(package.get(), part) : -1;
        if (!part || !package || count < 1 || count > maximumPageCount
                || index < 0 || index >= count) {
            if (error) {
                *error = QLatin1String(
                    "Sleep page not changed: current page could not be verified.");
            }
            return false;
        }
        QString const path = canonicalNotebookPath(
            firmware.widgetFilePath(widget));
        QString const id = cnt::pages::partStableId(
            firmware, part);
        if (!validPinnedNotebookPath(path) || !validPinnedPartId(id)) {
            if (error) {
                *error = QLatin1String(
                    "Sleep page not changed: notebook identity is invalid.");
            }
            return false;
        }
        if (notebookPath)
            *notebookPath = path;
        if (partId)
            *partId = id;
        return true;
    } catch (...) {
        if (error) {
            *error = QLatin1String(
                "Sleep page not changed: notebook engine rejected the request.");
        }
        return false;
    }
}

static bool activeNotebookWidget(QWidget* widget) {
    if (!widget
            || !cnt::notebook_widget::isNotebookWidget(widget)
            || !widget->isVisible()
            || widget->visibleRegion().isEmpty()) {
        return false;
    }
    QWidget* const window = widget->window();
    return window && window->isVisible();
}

static QImage fitToPowerView(QImage const& source) {
    if (source.isNull())
        return QImage();
    QSize const target(FallbackScreenWidth, FallbackScreenHeight);
    if (source.size() == target)
        return source.convertToFormat(QImage::Format_RGB32);

    QImage const scaled = source.scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull())
        return QImage();
    QImage result(target, QImage::Format_RGB32);
    result.fill(Qt::white);
    QPainter painter(&result);
    painter.drawImage(
        (target.width() - scaled.width()) / 2,
        (target.height() - scaled.height()) / 2,
        scaled);
    return result;
}

static cnt::layers_preview::Dependencies previewDependencies() {
    cnt::layers_preview::Dependencies const dependencies = {
        &firmwareApi(),
        &layerState(),
        &coverState(),
        layerToolRoutingOperations(),
        layerPreviewPins()
    };
    return dependencies;
}

static QString condorVariantPath(QString const& basePath) {
    if (basePath.endsWith(QLatin1String("_condor.png"), Qt::CaseInsensitive))
        return basePath;
    if (!basePath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        return QString();
    QString path = basePath;
    path.chop(4);
    path.append(QLatin1String("_condor.png"));
    return path;
}

static void invalidateCurrentPageCache(
        cnt::notebook_sleep::RuntimeState& state) {
    state.cachedPageImage = QImage();
    state.cachedPagePath.clear();
    state.cachedPageIndex = -1;
    state.cachedPageValid = false;
}

static int currentPageIndex(QWidget* widget) {
    cnt::pages::PageContext context;
    QString error;
    if (!cnt::pages::loadPageContext(
            firmwareApi(), widget, maximumNotebookPages(), &context, &error)) {
        return -1;
    }
    return context.index;
}

static void publishCurrentPageCache(
        cnt::notebook_sleep::RuntimeState& state,
        QImage const& image,
        QString const& notebookPath,
        int pageIndex,
        quint64 generation) {
    // The exporter runs synchronously on the GUI thread, but a firmware call
    // could still have pumped events; publish only if nothing changed while
    // the frame was rendered.
    if (image.isNull() || pageIndex < 0 || generation != state.editGeneration)
        return;
    state.cachedPageImage = image;
    state.cachedPagePath = notebookPath;
    state.cachedPageIndex = pageIndex;
    state.cachedPageGeneration = generation;
    state.cachedPageValid = true;
}

static bool currentPageCacheMatches(
        cnt::notebook_sleep::RuntimeState const& state,
        QString const& notebookPath,
        int pageIndex) {
    return state.cachedPageValid
        && !state.cachedPageImage.isNull()
        && state.cachedPageGeneration == state.editGeneration
        && state.cachedPagePath == notebookPath
        && pageIndex >= 0
        && state.cachedPageIndex == pageIndex;
}

static void clearBackgroundCache(
        cnt::notebook_sleep::RuntimeState& state) {
    state.cachedBackgroundType.clear();
    state.cachedBackgroundPath.clear();
    state.cachedBackgroundImage = QImage();
    state.cachedBackgroundModifiedMs = -1;
    state.cachedBackgroundSize = -1;
}

static QImage activePageBackground(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget) {
    FirmwareApi& firmware = firmwareApi();
    void* const backgroundWidget = widget
        ? cnt::notebook_widget::notePadBackgroundWidget(widget) : nullptr;
    if (!backgroundWidget || !firmware.backgroundType || !firmware.rendererMap)
        return QImage();

    QString const type = firmware.backgroundType(backgroundWidget);
    if (type.isEmpty())
        return QImage();

    // Background maps are initialized once per Nickel process. On the normal
    // repeated-sleep path, avoid even a map lookup or resource probe. External
    // papers retain the cheap mtime/size validation used by the other caches.
    if (state.cachedBackgroundType == type
            && !state.cachedBackgroundPath.isEmpty()
            && !state.cachedBackgroundImage.isNull()) {
        if (state.cachedBackgroundPath.startsWith(QLatin1String(":/")))
            return state.cachedBackgroundImage;
        QFileInfo const cachedInfo(state.cachedBackgroundPath);
        if (cachedInfo.isFile()
                && cachedInfo.size() == state.cachedBackgroundSize
                && cachedInfo.lastModified().toMSecsSinceEpoch()
                    == state.cachedBackgroundModifiedMs) {
            return state.cachedBackgroundImage;
        }
    }

    QString const basePath = firmware.rendererMap->value(type);
    if (basePath.isEmpty())
        return QImage();

    QString path = condorVariantPath(basePath);
    if (path.isEmpty() || !QFile::exists(path))
        path = basePath;
    bool const immutableResource = path.startsWith(QLatin1String(":/"));
    QFileInfo const info(path);
    qint64 const size = immutableResource ? -1 : info.size();
    qint64 const modifiedMs = immutableResource
        ? -1 : info.lastModified().toMSecsSinceEpoch();
    if (!QFile::exists(path)
            || (!immutableResource
                && (!info.isFile() || size <= 0
                    || size > MaximumBackgroundPngBytes))) {
        clearBackgroundCache(state);
        cnt::trace("notebook-sleep: active template image unavailable");
        return QImage();
    }

    if (state.cachedBackgroundType == type
            && state.cachedBackgroundPath == path
            && !state.cachedBackgroundImage.isNull()
            && (immutableResource
                || (state.cachedBackgroundModifiedMs == modifiedMs
                    && state.cachedBackgroundSize == size))) {
        return state.cachedBackgroundImage;
    }

    QImageReader reader(path);
    QSize const decodedSize = reader.size();
    if (decodedSize != QSize(FallbackScreenWidth, FallbackScreenHeight)) {
        clearBackgroundCache(state);
        cnt::trace("notebook-sleep: active template dimensions rejected");
        return QImage();
    }
    QImage const decoded = reader.read();
    if (decoded.isNull()) {
        clearBackgroundCache(state);
        cnt::trace("notebook-sleep: active template decode failed");
        return QImage();
    }

    // The device-specific paper PNG can be a transparent line overlay.
    // Flatten it once onto white and keep exactly one decoded template in
    // memory; repeated sleeps on the same paper avoid file I/O and decoding.
    QImage opaque(decodedSize, QImage::Format_RGB32);
    opaque.fill(Qt::white);
    QPainter backgroundPainter(&opaque);
    backgroundPainter.drawImage(0, 0, decoded);
    backgroundPainter.end();
    if (opaque.isNull()) {
        clearBackgroundCache(state);
        return QImage();
    }

    state.cachedBackgroundType = type;
    state.cachedBackgroundPath = path;
    state.cachedBackgroundImage = opaque;
    state.cachedBackgroundModifiedMs = modifiedMs;
    state.cachedBackgroundSize = size;
    return state.cachedBackgroundImage;
}

static QImage compositeActiveBackground(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget,
        QImage const& renderedPage,
        ExtentOpaque const& extent,
        cnt::layers_preview::LivePageView const& liveView,
        QPoint const& canvasOrigin) {
    if (renderedPage.isNull())
        return QImage();
    if (!std::isfinite(extent.left) || !std::isfinite(extent.top)
            || !std::isfinite(extent.right) || !std::isfinite(extent.bottom)
            || extent.right <= extent.left || extent.bottom <= extent.top) {
        cnt::trace("notebook-sleep: renderer extent rejected");
        return QImage();
    }
    RendererViewTransformOpaque const& transform = liveView.transform;
    if (!std::isfinite(transform.xx) || !std::isfinite(transform.xy)
            || !std::isfinite(transform.x0) || !std::isfinite(transform.yx)
            || !std::isfinite(transform.yy) || !std::isfinite(transform.y0)) {
        cnt::trace("notebook-sleep: renderer view transform rejected");
        return QImage();
    }

    // QTransform's six-argument constructor stores the cross terms in the
    // opposite textual order from atk::core::Transform. Spell out the
    // conversion so the equations remain:
    //
    //   screenX = xx*pageX + xy*pageY + x0 + canvasX
    //   screenY = yx*pageX + yy*pageY + y0 + canvasY
    QTransform const pageToScreen(
        transform.xx,
        transform.yx,
        transform.xy,
        transform.yy,
        transform.x0 + canvasOrigin.x(),
        transform.y0 + canvasOrigin.y());
    if (!pageToScreen.isInvertible()) {
        cnt::trace("notebook-sleep: renderer view transform is singular");
        return QImage();
    }
    QRectF const pageBounds(
        0.0, 0.0, FallbackScreenWidth, FallbackScreenHeight);

    // The exporter maps its MyScript page-space extent into the complete PNG.
    // Apply the live renderer's exact affine mapping to reverse that
    // normalization. This preserves the live zoom, independent X/Y device
    // scales, pan, and the canvas layout offset without a DPI approximation.
    QRectF const modelExtent(
        extent.left,
        extent.top,
        extent.right - extent.left,
        extent.bottom - extent.top);
    QRectF const pageExtentBounds = pageToScreen.mapRect(modelExtent);
    if (!pageExtentBounds.intersects(pageBounds)
            || pageExtentBounds.width() > FallbackScreenWidth * 4.0
            || pageExtentBounds.height() > FallbackScreenHeight * 4.0
            || pageExtentBounds.left() < -FallbackScreenWidth * 4.0
            || pageExtentBounds.top() < -FallbackScreenHeight * 4.0) {
        cnt::trace("notebook-sleep: renderer extent outside page");
        return QImage();
    }

    QImage result = activePageBackground(state, widget);
    if (result.isNull()) {
        result = QImage(
            QSize(FallbackScreenWidth, FallbackScreenHeight),
            QImage::Format_RGB32);
        result.fill(Qt::white);
    }
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    if (renderedPage.size() != pageExtentBounds.size().toSize())
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setWorldTransform(pageToScreen);
    painter.drawImage(
        modelExtent,
        renderedPage,
        QRectF(renderedPage.rect()));
    painter.end();
    cnt::trace("notebook-sleep: ink restored to renderer extent");
    return result;
}

static QImage exportedCurrentPage(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget) {
    QImage image;
    ExtentOpaque extent = {0.0f, 0.0f, 0.0f, 0.0f};
    cnt::layers_preview::LivePageView liveView = {};
    QString error;
    if (cnt::layers_preview::exportCurrentPageImage(
            previewDependencies(),
            widget,
            &image,
            &extent,
            &liveView,
            &error)) {
        QWidget* canvas = nullptr;
        void* const rawBackground = widget
            ? cnt::notebook_widget::notePadBackgroundWidget(widget) : nullptr;
        if (rawBackground) {
            canvas = qobject_cast<QWidget*>(
                reinterpret_cast<QObject*>(rawBackground));
        }
        QWidget* const window = widget ? widget->window() : nullptr;
        if (!canvas || !window || canvas->window() != window)
            canvas = widget;
        QPoint const canvasOrigin = canvas && window
            ? canvas->mapTo(window, QPoint(0, 0)) : QPoint(0, 0);
        if (!state.extentTraceRecorded) {
            state.extentTraceRecorded = true;
            RendererViewTransformOpaque const& transform = liveView.transform;
            cnt::trace(
                QLatin1String("notebook-sleep: renderer bounds left=")
                + QString::number(extent.left, 'f', 2)
                + QLatin1String(" top=") + QString::number(extent.top, 'f', 2)
                + QLatin1String(" right=") + QString::number(extent.right, 'f', 2)
                + QLatin1String(" bottom=") + QString::number(extent.bottom, 'f', 2)
                + QLatin1String(" png=") + QString::number(image.width())
                + QLatin1Char('x') + QString::number(image.height())
                + QLatin1String(" transform=[")
                + QString::number(transform.xx, 'f', 4) + QLatin1Char(',')
                + QString::number(transform.xy, 'f', 4) + QLatin1Char(',')
                + QString::number(transform.x0, 'f', 2) + QLatin1Char(';')
                + QString::number(transform.yx, 'f', 4) + QLatin1Char(',')
                + QString::number(transform.yy, 'f', 4) + QLatin1Char(',')
                + QString::number(transform.y0, 'f', 2)
                + QLatin1String("] canvas=")
                + QString::number(canvasOrigin.x()) + QLatin1Char(',')
                + QString::number(canvasOrigin.y())
                + QLatin1Char(' ')
                + QString::number(canvas ? canvas->width() : 0)
                + QLatin1Char('x')
                + QString::number(canvas ? canvas->height() : 0)
                + QLatin1String(" unit=mm"));
        }
        return compositeActiveBackground(
            state, widget, image, extent, liveView, canvasOrigin);
    }
    if (!error.isEmpty())
        cnt::trace(QLatin1String("notebook-sleep: isolated export unavailable: ") + error);
    return QImage();
}

static QImage loadPinnedPageImage(
        cnt::notebook_sleep::RuntimeState& state) {
    if (state.pinnedPageImageLoaded)
        return state.pinnedPageImage;
    state.pinnedPageImageLoaded = true;
    state.pinnedPageImage = QImage();

    QString imagePath = QLatin1String(kPinnedPageImagePath);
    if (!QFileInfo(imagePath).exists()) {
        QString const legacyPath = QLatin1String(
            "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page.png");
        if (QFileInfo(legacyPath).exists())
            imagePath = legacyPath;
    }
    QFileInfo const info(imagePath);
    if (!info.isFile() || info.size() <= 0
            || info.size() > MaximumBackgroundPngBytes) {
        return QImage();
    }
    QImageReader reader(imagePath);
    if (reader.size() != QSize(FallbackScreenWidth, FallbackScreenHeight))
        return QImage();
    QImage const image = reader.read();
    if (image.isNull()
            || image.size() != QSize(
                FallbackScreenWidth, FallbackScreenHeight)) {
        return QImage();
    }
    state.pinnedPageImage = image.convertToFormat(QImage::Format_RGB32);
    return state.pinnedPageImage;
}

static bool persistPinnedPageImage(QImage const& image) {
    if (image.isNull()
            || image.size() != QSize(
                FallbackScreenWidth, FallbackScreenHeight)) {
        return false;
    }
    QString const imagePath = QLatin1String(kPinnedPageImagePath);
    if (!QDir().mkpath(QFileInfo(imagePath).absolutePath()))
        return false;
    QSaveFile file(imagePath);
    if (!file.open(QIODevice::WriteOnly)
            || !image.save(&file, "PNG")
            || !file.commit()) {
        return false;
    }
    QFile::remove(QLatin1String(
        "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/pinned-sleep-page.png"));
    removeLegacyPinnedTemplateArtifacts();
    return true;
}

enum PinnedRenderStatus {
    PinnedRenderUnavailable,
    PinnedRenderMissing,
    PinnedRenderSucceeded,
};

class EditorPartRestoreGuard {
public:
    EditorPartRestoreGuard(
            FirmwareApi& firmware,
            void* editor,
            SharedPart const& original)
        : firmware_(&firmware), editor_(editor), original_(original), armed_(false) {}

    ~EditorPartRestoreGuard() {
        if (!armed_ || !firmware_ || !editor_ || !original_)
            return;
        try {
            firmware_->editorSetPart(editor_, original_);
            cnt::trace("notebook-sleep: pinned page renderer restored current page");
        } catch (...) {
            cnt::trace("notebook-sleep: pinned page renderer restore threw");
        }
    }

    void arm() { armed_ = true; }

private:
    FirmwareApi* firmware_;
    void* editor_;
    SharedPart original_;
    bool armed_;

    EditorPartRestoreGuard(EditorPartRestoreGuard const&);
    EditorPartRestoreGuard& operator=(EditorPartRestoreGuard const&);
};

static SharedPart findPinnedPart(
        FirmwareApi& firmware,
        SharedPackage const& package,
        SharedPart const& current,
        QString const& partId,
        int maximumPageCount,
        bool* determinate) {
    if (determinate)
        *determinate = false;
    if (!package || !current || !validPinnedPartId(partId) || !determinate)
        return SharedPart();
    QString const currentId = cnt::pages::partStableId(firmware, current);
    if (currentId.isEmpty())
        return SharedPart();
    if (currentId == partId) {
        *determinate = true;
        return current;
    }

    int const count = firmware.packagePartCount(package.get());
    if (count < 1 || count > maximumPageCount)
        return SharedPart();
    for (int i = 0; i < count; ++i) {
        SharedPart const candidate = firmware.packageGetPart(package.get(), i);
        if (!candidate)
            return SharedPart();
        QString const candidateId = cnt::pages::partStableId(
            firmware, candidate);
        if (candidateId.isEmpty())
            return SharedPart();
        if (candidateId == partId) {
            *determinate = true;
            return candidate;
        }
    }
    *determinate = true;
    return SharedPart();
}

static PinnedRenderStatus renderPinnedPage(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget,
        int maximumPageCount,
        QImage* result) {
    if (result)
        *result = QImage();
    if (!widget || !result || !cnt::notebook_widget::isNotebookWidget(widget))
        return PinnedRenderUnavailable;

    FirmwareApi& firmware = firmwareApi();
    void* const editor = cnt::notebook_widget::notePadEditor(widget);
    if (!editor || !firmware.editorGetPart || !firmware.partGetPackage
            || !firmware.packagePartCount || !firmware.packageGetPart
            || !firmware.editorSetPart) {
        return PinnedRenderUnavailable;
    }
    try {
        SharedPart const original = firmware.editorGetPart(editor);
        SharedPackage const package = original
            ? firmware.partGetPackage(original.get()) : SharedPackage();
        if (!original || !package)
            return PinnedRenderUnavailable;
        bool partScanDeterminate = false;
        SharedPart const target = findPinnedPart(
            firmware,
            package,
            original,
            state.pinnedPartId,
            maximumPageCount,
            &partScanDeterminate);
        if (!target)
            return partScanDeterminate
                ? PinnedRenderMissing : PinnedRenderUnavailable;

        EditorPartRestoreGuard restore(firmware, editor, original);
        if (target.get() != original.get()) {
            restore.arm();
            firmware.editorSetPart(editor, target);
            cnt::trace(
                "notebook-sleep: pinned page installed temporarily for render");
        }
        QImage const rendered = exportedCurrentPage(state, widget);
        QImage const fitted = fitToPowerView(rendered);
        if (fitted.isNull())
            return PinnedRenderUnavailable;
        *result = fitted;
        return PinnedRenderSucceeded;
    } catch (...) {
        cnt::trace("notebook-sleep: pinned page render threw");
        return PinnedRenderUnavailable;
    }
}

static bool preparePinnedPageImage(
        cnt::notebook_sleep::RuntimeState& state) {
    loadPinnedPageState(state);
    if (!state.pinnedPageValid)
        return false;

    QFileInfo const notebookInfo(state.pinnedNotebookPath);
    if (!notebookInfo.isFile()) {
        discardUnavailablePinnedPage(
            state,
            "notebook-sleep: pinned notebook missing; pin cleared");
        return false;
    }

    QWidget* const widget = state.activeNotebook.data();
    QString const widgetPath = widget
        ? canonicalNotebookPath(firmwareApi().widgetFilePath(widget))
        : QString();
    bool const matchingLiveNotebook = widget
        && cnt::notebook_widget::isNotebookWidget(widget)
        && widgetPath == state.pinnedNotebookPath;
    bool const matchingVisibleNotebook = matchingLiveNotebook
        && activeNotebookWidget(widget);

    // When the pinned notebook is not the visible editor, validate the stable
    // page ID against the serialized archive before trusting its cached frame.
    // A conclusive miss clears the pin; an unreadable/busy archive preserves
    // it but falls back to Kobo's normal behavior for this power transition.
    if (!matchingVisibleNotebook) {
        int const archiveStatus =
            cnt::cover_cache::notebookArchiveContainsPageId(
                coverState(),
                state.pinnedNotebookPath,
                state.pinnedPartId);
        if (archiveStatus == 0) {
            discardUnavailablePinnedPage(
                state,
                "notebook-sleep: pinned page deleted; pin cleared");
            return false;
        }
        if (archiveStatus < 0) {
            cnt::trace(
                "notebook-sleep: pinned page could not be validated; normal behavior preserved");
            return false;
        }
    }

    QImage image;
    bool rendered = false;
    bool memoryHit = false;
    if (matchingLiveNotebook
            && state.pinnedPageRenderedInSession
            && state.pinnedPageGeneration == state.editGeneration
            && !state.pinnedPageImage.isNull()) {
        image = state.pinnedPageImage;
        memoryHit = true;
    } else if (matchingLiveNotebook
            && !matchingVisibleNotebook
            && !(image = loadPinnedPageImage(state)).isNull()) {
        // Input elsewhere in Nickel also bumps the conservative edit
        // generation while a hidden notebook widget survives. Prefer the
        // validated persisted frame in that case; only a visible notebook (or
        // a missing first frame) justifies another MyScript render.
    } else if (matchingLiveNotebook) {
        PinnedRenderStatus const status = renderPinnedPage(
            state, widget, maximumNotebookPages(), &image);
        if (status == PinnedRenderMissing) {
            discardUnavailablePinnedPage(
                state,
                "notebook-sleep: pinned page missing; pin cleared");
            return false;
        }
        if (status == PinnedRenderSucceeded) {
            state.pinnedPageImage = image;
            state.pinnedPageImageLoaded = true;
            state.pinnedPageRenderedInSession = true;
            state.pinnedPageGeneration = state.editGeneration;
            rendered = true;
            if (!persistPinnedPageImage(image)) {
                cnt::trace(
                    "notebook-sleep: pinned page frame cache write failed; memory retained");
            }
        } else {
            image = loadPinnedPageImage(state);
        }
    } else {
        image = loadPinnedPageImage(state);
    }

    if (image.isNull()) {
        cnt::trace(
            "notebook-sleep: pinned page frame unavailable; normal behavior preserved");
        return false;
    }
    state.pendingImage = image;
    state.pendingNotebookImage = true;
    cnt::trace(QLatin1String("notebook-sleep: pinned page armed for power view source=")
        + (rendered
            ? QLatin1String("render")
            : (memoryHit
                ? QLatin1String("memory") : QLatin1String("disk"))));
    return true;
}

static QImage notebookCover(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget,
        QString const& notebookPath) {
    QString type;
    if (cnt::cover_cache::zipApisReady(coverState())
            && cnt::cover_cache::cachedNotebookCoverType(
                coverState(), notebookPath, &type, 0.0)) {
        QImage image = cnt::cover_cache::cachedRenderedCoverImage(
            coverState(), notebookPath, type);
        if (image.isNull())
            image = cnt::cover_cache::cleanCustomCoverImage(coverState(), type);
        if (!image.isNull())
            return image;
    }

    QImage const stock = cnt::cover_cache::cachedRenderedCoverImage(
        coverState(), notebookPath, QString());
    if (!stock.isNull())
        return stock;

    // When Kobo has not requested a thumbnail yet, page zero can still use
    // the full-page exporter without changing parts. Other pages preserve the
    // stock book cover until a page-zero thumbnail becomes available.
    cnt::pages::PageContext context;
    QString error;
    if (cnt::pages::loadPageContext(
            firmwareApi(), widget, maximumNotebookPages(), &context, &error)
            && context.index == 0) {
        return exportedCurrentPage(state, widget);
    }
    return QImage();
}

} // namespace

NotebookSleepRefreshReceiver::NotebookSleepRefreshReceiver(QObject* parent)
    : QObject(parent) {}

bool NotebookSleepRefreshReceiver::eventFilter(
        QObject* watched, QEvent* event) {
    cnt::notebook_sleep::RuntimeState& state =
        pluginState().notebookSleep;

    switch (event->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
    case QEvent::KeyPress:
    case QEvent::Wheel: {
        // Any interaction can change the page, layers, zoom, or ink, so the
        // cached frame conservatively dies here. The per-event cost while a
        // notebook is open is one increment. This watcher never accepts or
        // consumes input and never renders.
        if (state.activeNotebook) {
            ++state.editGeneration;
            state.cachedPageValid = false;
        } else if (!state.cachedPageImage.isNull()) {
            // The notebook closed; do not keep a ~10 MiB frame alive for it.
            invalidateCurrentPageCache(state);
        }
        break;
    }
    case QEvent::Hide:
    case QEvent::Destroy:
        if (watched == state.activeNotebook.data()
                && !state.cachedPageImage.isNull()) {
            // A hidden/closing notebook no longer needs to pin a full-screen
            // ~10 MiB frame. The persisted pinned-page image remains the
            // authoritative fallback outside the live notebook view.
            invalidateCurrentPageCache(state);
        }
        break;
    default:
        break;
    }
    return false;
}

NotebookSleepSettingsReceiver::NotebookSleepSettingsReceiver(
        cnt::SettingsStore* settings,
        QWidget* selectorRow,
        cnt::NotebookSleepSettings const& initial,
        QObject* parent)
    : QObject(parent),
      settings_(settings),
      selectorRow_(selectorRow),
      enabled_(initial.enabled),
      mode_(initial.mode) {}

void NotebookSleepSettingsReceiver::enabledChanged(bool enabled) {
    enabled_ = enabled;
    if (selectorRow_)
        selectorRow_->setEnabled(enabled);
    persist();
}

void NotebookSleepSettingsReceiver::modeChanged(int index) {
    if (index != cnt::NotebookSleepCover
            && index != cnt::NotebookSleepCurrentPage) {
        return;
    }
    mode_ = static_cast<cnt::NotebookSleepImageMode>(index);
    persist();
}

void NotebookSleepSettingsReceiver::persist() {
    if (settings_)
        settings_->persistNotebookSleep(enabled_, mode_);
}

namespace cnt {
namespace notebook_sleep {

bool installHooks(RuntimeState& state) {
    // This runs before automatic template discovery. Move caches created by
    // earlier builds out of the template directory before Nickel can expose
    // their full-page PNG as a notebook paper.
    migrateLegacyPinnedPageFiles();

    void* const handle = dlopen(
        "libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) {
        trace("notebook-sleep: libnickel handle unavailable");
        return false;
    }

    bool const core =
        installPinnedHook(
            handle,
            "_ZN22N3PowerWorkflowManager13showSleepViewEv",
            ShowSleepViewVma,
            _cnt_show_sleep_view_hook,
            &state.api.showSleepViewOriginal)
        && installPinnedHook(
            handle,
            "_ZN22N3PowerWorkflowManager16showPowerOffViewEv",
            ShowPowerOffViewVma,
            _cnt_show_power_off_view_hook,
            &state.api.showPowerOffViewOriginal)
        && installPinnedHook(
            handle,
            "_ZN19PowerViewController13onCoverLoadedERK6QImage",
            OnCoverLoadedVma,
            _cnt_power_cover_loaded_hook,
            &state.api.onCoverLoadedOriginal)
        && installPinnedHook(
            handle,
            "_ZN19PowerViewController11updateCoverEv",
            UpdateCoverVma,
            _cnt_power_update_cover_hook,
            &state.api.updateCoverOriginal)
        && installPinnedHook(
            handle,
            "_ZN19PowerViewController19updateReadingStatusEv",
            UpdateReadingStatusVma,
            _cnt_power_update_reading_status_hook,
            &state.api.updateReadingStatusOriginal)
        && installPinnedHook(
            handle,
            "_ZN25FullScreenDragonPowerView19setInfoPanelVisibleEb",
            SetInfoPanelVisibleVma,
            _cnt_power_set_info_panel_visible_hook,
            &state.api.setInfoPanelVisibleOriginal);
    state.coreHooksReady = core;
    if (core)
        trace("notebook-sleep: guarded power workflow hooks installed");
    else
        trace("notebook-sleep: power workflow hook validation failed; feature disabled");

    bool const nativeSettingsApis =
        resolvePinned(handle, "_ZN23SettingItemWithCheckBoxC1EP7QWidget",
            CheckBoxConstructorVma, &state.api.checkBoxConstructor)
        && resolvePinned(handle, "_ZN23SettingItemWithDropDownC1EP7QWidget",
            DropDownConstructorVma, &state.api.dropDownConstructor)
        && resolvePinned(handle, "_ZN23SettingItemWithCheckBox7setTextERK7QString",
            CheckBoxSetTextVma, &state.api.checkBoxSetText)
        && resolvePinned(handle, "_ZN23SettingItemWithCheckBox10setCheckedEb",
            CheckBoxSetCheckedVma, &state.api.checkBoxSetChecked)
        && resolvePinned(handle, "_ZN15SettingItemBase8setLabelERK7QString",
            ItemSetLabelVma, &state.api.itemSetLabel)
        && resolvePinned(handle, "_ZNK23SettingItemWithDropDown8dropDownEv",
            DropDownVma, &state.api.dropDown)
        && resolvePinned(
            handle,
            "_ZN24MultiSelectTouchDropDown7addItemERK7QStringRK8QVariantRK7QLocaleb",
            DropDownAddItemVma,
            &state.api.dropDownAddItem)
        && resolvePinned(handle, "_ZN13TouchDropDown15setCurrentIndexEi",
            DropDownSetCurrentIndexVma, &state.api.dropDownSetCurrentIndex);
    if (core && nativeSettingsApis
            && installPinnedHook(
                handle,
                "_ZN19N3SettingsPowerViewC1EP7QWidget",
                SettingsPowerViewConstructorVma,
                _cnt_settings_power_view_constructor_hook,
                &state.api.settingsPowerViewConstructorOriginal)) {
        state.settingsHookReady = true;
        trace("notebook-sleep: native power-settings hook installed");
    } else {
        trace("notebook-sleep: native power-settings hook unavailable");
    }
    dlclose(handle);
    return state.coreHooksReady;
}

void observeNotebookWidget(RuntimeState& state, QWidget* widget) {
    if (!widget || !notebook_widget::isNotebookWidget(widget))
        return;
    if (state.activeNotebook.data() != widget)
        invalidateCurrentPageCache(state);
    state.activeNotebook = widget;
    state.renderHookReady = true;
    QCoreApplication* const application = QCoreApplication::instance();
    if (!state.refreshReceiver && application) {
        NotebookSleepRefreshReceiver* const receiver =
            new NotebookSleepRefreshReceiver(application);
        application->installEventFilter(receiver);
        state.refreshReceiver = receiver;
        trace("notebook-sleep: page-cache invalidation watcher installed");
    }
    trace("notebook-sleep: live notebook observed");
}

QString pinnedPageMenuText(
        RuntimeState& state,
        QObject* controller,
        int maximumPageCount) {
    if (!state.coreHooksReady || !controller)
        return QString();

    QString notebookPath;
    QString partId;
    QString error;
    if (!currentPageIdentity(
            controller,
            maximumPageCount,
            nullptr,
            &notebookPath,
            &partId,
            &error)) {
        trace(QLatin1String("notebook-sleep: pin menu unavailable: ") + error);
        return QString();
    }
    try {
        loadPinnedPageState(state);
        bool const currentIsPinned = state.pinnedPageValid
            && state.pinnedNotebookPath == notebookPath
            && state.pinnedPartId == partId;
        return currentIsPinned
            ? QLatin1String("Unpin sleep page (pinned)")
            : QLatin1String("Pin current page for sleep");
    } catch (...) {
        trace("notebook-sleep: pin menu state check threw; row skipped");
        return QString();
    }
}

void togglePinnedPageForController(
        RuntimeState& state,
        QObject* controller,
        int maximumPageCount) {
    QObject* widget = nullptr;
    QString notebookPath;
    QString partId;
    QString error;
    if (!currentPageIdentity(
            controller,
            maximumPageCount,
            &widget,
            &notebookPath,
            &partId,
            &error)) {
        if (widget && firmwareApi().showErrorPopup)
            firmwareApi().showErrorPopup(widget, error);
        else
            trace(QLatin1String("notebook-sleep: pin toggle failed: ") + error);
        return;
    }

    bool succeeded = false;
    try {
        loadPinnedPageState(state);
        bool const currentIsPinned = state.pinnedPageValid
            && state.pinnedNotebookPath == notebookPath
            && state.pinnedPartId == partId;
        if (currentIsPinned) {
            succeeded = removePinnedPageState(state);
            if (succeeded)
                trace("notebook-sleep: sleep page unpinned");
            else
                error = QLatin1String("Sleep page could not be unpinned.");
        } else {
            succeeded = persistPinnedPageState(
                state, notebookPath, partId);
            if (succeeded) {
                // The menu action persists metadata only. Rendering is
                // deliberately deferred until prepareNotebookImage at the
                // next power boundary.
                trace("notebook-sleep: current page pinned; render deferred to power boundary");
            } else {
                error = QLatin1String(
                    "Current page could not be pinned for sleep.");
            }
        }
    } catch (...) {
        error = QLatin1String(
            "Sleep page not changed: unexpected persistence error.");
        trace("notebook-sleep: pin toggle threw and was contained");
    }
    if (!succeeded && widget && firmwareApi().showErrorPopup)
        firmwareApi().showErrorPopup(widget, error);
}

void prepareNotebookImage(RuntimeState& state) {
    state.pendingNotebookImage = false;
    state.pendingImage = QImage();
    if (!state.coreHooksReady)
        return;

    // A menu pin is an explicit global override. It is evaluated before the
    // general notebook-sleep setting, so unpinning returns immediately to the
    // configured Cover/Current page behavior while a valid pin works even if
    // that general toggle is currently off.
    try {
        if (preparePinnedPageImage(state))
            return;
    } catch (...) {
        state.pendingImage = QImage();
        state.pendingNotebookImage = false;
        trace("notebook-sleep: pinned page preparation threw; normal behavior preserved");
    }

    NotebookSleepSettings const configured =
        settingsStore().configuredNotebookSleep();
    if (!configured.enabled)
        return;
    QWidget* const widget = state.activeNotebook.data();
    if (!state.renderHookReady || !activeNotebookWidget(widget)) {
        trace("notebook-sleep: no visible notebook at power boundary; stock cover preserved");
        return;
    }

    try {
        QElapsedTimer prepareTimer;
        prepareTimer.start();
        QString const notebookPath = QFileInfo(
            firmwareApi().widgetFilePath(widget)).absoluteFilePath();
        if (!notebookPath.startsWith(QLatin1String("/mnt/onboard/"))
                || !notebookPath.endsWith(
                    QLatin1String(".nebo"), Qt::CaseInsensitive)) {
            trace("notebook-sleep: active notebook path rejected; stock cover preserved");
            return;
        }
        QImage image;
        bool cacheHit = false;
        qint64 renderMs = 0;
        qint64 fitMs = 0;
        if (configured.mode == NotebookSleepCurrentPage) {
            quint64 const generation = state.editGeneration;
            int const pageIndex = currentPageIndex(widget);
            if (currentPageCacheMatches(state, notebookPath, pageIndex)) {
                image = state.cachedPageImage;
                cacheHit = true;
            } else {
                QElapsedTimer stageTimer;
                stageTimer.start();
                QImage const rendered = exportedCurrentPage(state, widget);
                renderMs = stageTimer.restart();
                image = fitToPowerView(rendered);
                fitMs = stageTimer.elapsed();
                publishCurrentPageCache(
                    state, image, notebookPath, pageIndex, generation);
            }
        } else {
            QElapsedTimer stageTimer;
            stageTimer.start();
            QImage const rendered = notebookCover(state, widget, notebookPath);
            renderMs = stageTimer.restart();
            image = fitToPowerView(rendered);
            fitMs = stageTimer.elapsed();
        }
        if (image.isNull()) {
            trace("notebook-sleep: notebook image unavailable; stock cover preserved");
            return;
        }
        state.pendingImage = image;
        state.pendingNotebookImage = true;
        trace((configured.mode == NotebookSleepCurrentPage
                ? QLatin1String("notebook-sleep: current page armed for power view")
                : QLatin1String("notebook-sleep: notebook cover armed for power view"))
            + QLatin1String(" cache=")
            + (cacheHit ? QLatin1String("hit") : QLatin1String("miss"))
            + QLatin1String(" renderMs=") + QString::number(renderMs)
            + QLatin1String(" fitMs=") + QString::number(fitMs)
            + QLatin1String(" totalMs=")
            + QString::number(prepareTimer.elapsed()));
    } catch (...) {
        state.pendingImage = QImage();
        state.pendingNotebookImage = false;
        trace("notebook-sleep: image preparation threw; stock cover preserved");
    }
}

void augmentPowerSettings(RuntimeState& state, QWidget* view) {
    if (!state.settingsHookReady || !view
            || view->findChild<QWidget*>(
                QLatin1String("_cntShowNotebookOnSleep"))) {
        return;
    }
    QWidget* const container = view->findChild<QWidget*>(
        QLatin1String("coverFullscreenOptions"));
    QVBoxLayout* const layout = container
        ? qobject_cast<QVBoxLayout*>(container->layout()) : nullptr;
    if (!container || !layout) {
        trace("notebook-sleep: coverFullscreenLayout not found; settings rows skipped");
        return;
    }

    void* toggleMemory = nullptr;
    void* selectorMemory = nullptr;
    bool toggleConstructed = false;
    bool selectorConstructed = false;
    try {
        toggleMemory = ::operator new(SettingItemObjectBytes);
        state.api.checkBoxConstructor(toggleMemory, container);
        toggleConstructed = true;
        selectorMemory = ::operator new(SettingItemObjectBytes);
        state.api.dropDownConstructor(selectorMemory, container);
        selectorConstructed = true;
    } catch (...) {
        if (toggleConstructed)
            reinterpret_cast<QWidget*>(toggleMemory)->deleteLater();
        else if (toggleMemory)
            ::operator delete(toggleMemory);
        if (selectorConstructed)
            reinterpret_cast<QWidget*>(selectorMemory)->deleteLater();
        else if (selectorMemory)
            ::operator delete(selectorMemory);
        trace("notebook-sleep: native settings row construction failed");
        return;
    }

    QWidget* const toggle = reinterpret_cast<QWidget*>(toggleMemory);
    QWidget* const selector = reinterpret_cast<QWidget*>(selectorMemory);
    toggle->setObjectName(QLatin1String("_cntShowNotebookOnSleep"));
    selector->setObjectName(QLatin1String("_cntNotebookSleepImage"));
    // Match Ui_N3SettingsPowerView::retranslateUi exactly: the inherited
    // SettingItemBase label owns the left column, while setText() labels only
    // the compact checkbox in the right column. Putting the setting name in
    // setText() centers the entire control instead of aligning it with Kobo's
    // native power rows.
    state.api.itemSetLabel(
        toggle, QLatin1String("Show notebook on sleep:"));
    state.api.checkBoxSetText(toggle, QLatin1String("On"));
    state.api.itemSetLabel(
        selector, QLatin1String("Notebook sleep image:"));

    void* const dropDown = state.api.dropDown(selector);
    if (!dropDown) {
        toggle->deleteLater();
        selector->deleteLater();
        trace("notebook-sleep: native selector missing; settings rows skipped");
        return;
    }
    QLocale const locale;
    state.api.dropDownAddItem(
        dropDown,
        QLatin1String("Cover"),
        QVariant(NotebookSleepCover),
        locale,
        false);
    state.api.dropDownAddItem(
        dropDown,
        QLatin1String("Current page"),
        QVariant(NotebookSleepCurrentPage),
        locale,
        false);

    NotebookSleepSettings const configured =
        settingsStore().configuredNotebookSleep();
    state.api.checkBoxSetChecked(toggle, configured.enabled);
    state.api.dropDownSetCurrentIndex(dropDown, configured.mode);
    selector->setEnabled(configured.enabled);

    NotebookSleepSettingsReceiver* const receiver =
        new NotebookSleepSettingsReceiver(
            &settingsStore(), selector, configured, view);
    bool const toggleConnected = QObject::connect(
        toggle,
        SIGNAL(toggled(bool)),
        receiver,
        SLOT(enabledChanged(bool)));
    bool const selectorConnected = QObject::connect(
        reinterpret_cast<QObject*>(dropDown),
        SIGNAL(currentIndexChanged(int)),
        receiver,
        SLOT(modeChanged(int)));
    if (!toggleConnected || !selectorConnected) {
        toggle->deleteLater();
        selector->deleteLater();
        receiver->deleteLater();
        trace("notebook-sleep: native settings signal connection failed");
        return;
    }
    layout->addWidget(toggle);
    layout->addWidget(selector);
    trace("notebook-sleep: native power-settings rows added");
}

} // namespace notebook_sleep
} // namespace cnt

extern "C" __attribute__((visibility("default")))
void _cnt_show_sleep_view_hook(void* manager) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    cnt::notebook_sleep::prepareNotebookImage(state);
    if (state.api.showSleepViewOriginal)
        state.api.showSleepViewOriginal(manager);
}

extern "C" __attribute__((visibility("default")))
void _cnt_show_power_off_view_hook(void* manager) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    cnt::notebook_sleep::prepareNotebookImage(state);
    if (state.api.showPowerOffViewOriginal)
        state.api.showPowerOffViewOriginal(manager);
}

extern "C" __attribute__((visibility("default")))
void _cnt_power_cover_loaded_hook(void* controller, QImage const& image) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (!state.api.onCoverLoadedOriginal)
        return;
    if (state.pendingNotebookImage)
        cnt::trace("notebook-sleep: asynchronous cover replaced");
    state.api.onCoverLoadedOriginal(
        controller,
        state.pendingNotebookImage ? state.pendingImage : image);
}

extern "C" __attribute__((visibility("default")))
void _cnt_power_update_cover_hook(void* controller) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (state.pendingNotebookImage
            && !state.pendingImage.isNull()
            && state.api.onCoverLoadedOriginal) {
        // updateCover() normally connects imageReady to a QSlotObject which
        // stores onCoverLoaded's direct member-function address. That path
        // bypasses the symbol jump slot. Feed the prepared image through the
        // stock slot here and do not start a later book-cover load which could
        // overwrite it.
        state.api.onCoverLoadedOriginal(controller, state.pendingImage);
        cnt::trace("notebook-sleep: prepared image applied at updateCover");
        return;
    }
    if (state.api.updateCoverOriginal)
        state.api.updateCoverOriginal(controller);
}

extern "C" __attribute__((visibility("default")))
void _cnt_power_update_reading_status_hook(void* controller) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (!state.pendingNotebookImage && state.api.updateReadingStatusOriginal)
        state.api.updateReadingStatusOriginal(controller);
}

extern "C" __attribute__((visibility("default")))
void _cnt_power_set_info_panel_visible_hook(void* view, bool visible) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (state.api.setInfoPanelVisibleOriginal) {
        state.api.setInfoPanelVisibleOriginal(
            view, state.pendingNotebookImage ? false : visible);
    }
}

extern "C" __attribute__((visibility("default")))
void _cnt_settings_power_view_constructor_hook(void* view, QWidget* parent) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (!state.api.settingsPowerViewConstructorOriginal)
        return;
    state.api.settingsPowerViewConstructorOriginal(view, parent);
    cnt::notebook_sleep::augmentPowerSettings(
        state, reinterpret_cast<QWidget*>(view));
}
