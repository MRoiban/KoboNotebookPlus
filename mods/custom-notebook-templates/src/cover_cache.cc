#include "cover_cache.h"

#include "fs_util.h"
#include "settings.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <dlfcn.h>

namespace cnt {
namespace cover_cache {
namespace {

char const kRenderedPreviewRoot[] =
    "/mnt/onboard/.kobo/custom/previews/";
int const kBackgroundWidth = 1404;
int const kBackgroundHeight = 1872;
int const kMaximumPageMetadataSize = 256 * 1024;
int const kMaximumScanCacheEntries = 2048;
qint64 const kMaximumImageCacheBytes = qint64(64) * 1024 * 1024;
int const kMaximumPersistedPreviews = 128;

template <typename Function>
static bool resolveDynamicSymbol(
    void* handle,
    char const* symbolName,
    Function* destination) {
    union {
        void* pointer;
        Function function;
    } converter = {};
    converter.pointer = dlsym(handle, symbolName);
    if (!converter.pointer)
        return false;
    *destination = converter.function;
    return true;
}

} // namespace

bool loadZipApis(State& state) {
    State::ZipApi& zip = state.zip;
    if (zip.libraryHandle)
        return true;

    zip.libraryHandle = dlopen("libzip.so.5", RTLD_LAZY | RTLD_LOCAL);
    if (!zip.libraryHandle)
        return false;

    bool const ready =
        resolveDynamicSymbol(zip.libraryHandle, "zip_open", &zip.open)
        && resolveDynamicSymbol(
            zip.libraryHandle, "zip_get_num_entries", &zip.getNumEntries)
        && resolveDynamicSymbol(zip.libraryHandle, "zip_get_name", &zip.getName)
        && resolveDynamicSymbol(zip.libraryHandle, "zip_fopen", &zip.fopen)
        && resolveDynamicSymbol(zip.libraryHandle, "zip_fread", &zip.fread)
        && resolveDynamicSymbol(zip.libraryHandle, "zip_fclose", &zip.fclose)
        && resolveDynamicSymbol(zip.libraryHandle, "zip_discard", &zip.discard);
    if (!ready) {
        dlclose(zip.libraryHandle);
        zip.libraryHandle = nullptr;
        zip.open = nullptr;
        zip.getNumEntries = nullptr;
        zip.getName = nullptr;
        zip.fopen = nullptr;
        zip.fread = nullptr;
        zip.fclose = nullptr;
        zip.discard = nullptr;
        return false;
    }

    trace("archive: read-only notebook APIs resolved");
    return true;
}

bool zipApisReady(State const& state) {
    return state.zip.open;
}

namespace {

static bool readZipEntry(
    State::ZipApi const& zip,
    ZipArchiveOpaque* archive,
    char const* entryName,
    QByteArray* contents) {
    contents->clear();
    ZipFileOpaque* const file = zip.fopen(archive, entryName, 0);
    if (!file)
        return false;

    bool valid = true;
    char block[4096];
    while (true) {
        long long const count = zip.fread(file, block, sizeof(block));
        if (count < 0) {
            valid = false;
            break;
        }
        if (count == 0)
            break;
        if (contents->size() + count > kMaximumPageMetadataSize) {
            valid = false;
            break;
        }
        contents->append(block, static_cast<int>(count));
    }
    if (zip.fclose(file) != 0)
        valid = false;
    if (!valid)
        contents->clear();
    return valid;
}

} // namespace

// Return 1 when the serialized page.bdom contains the exact native layer ID,
// 0 when the entry was read completely without it, and -1 when the archive
// could not be inspected. This is a read-only persistence probe; it never
// participates in the mutation decision.
int notebookArchiveContainsLayerId(
    State const& state,
    QString const& notebookPath,
    QString const& partId,
    QString const& layerId) {
    State::ZipApi const& zip = state.zip;
    if (!zip.open || !zip.fopen || !zip.fread || !zip.fclose || !zip.discard
            || partId.isEmpty() || layerId.isEmpty()) {
        return -1;
    }

    QByteArray const encodedPath = QFile::encodeName(notebookPath);
    int openError = 0;
    ZipArchiveOpaque* const archive = zip.open(
        encodedPath.constData(), 16, &openError);  // ZIP_RDONLY
    if (!archive)
        return -1;

    QByteArray const entryName = QByteArray("pages/")
        + partId.toUtf8() + QByteArray("/page.bdom");
    ZipFileOpaque* const file = zip.fopen(archive, entryName.constData(), 0);
    if (!file) {
        zip.discard(archive);
        return -1;
    }

    QByteArray const needle = layerId.toUtf8();
    QByteArray carry;
    bool found = false;
    bool complete = true;
    qint64 scanned = 0;
    char block[4096];
    while (true) {
        long long const count = zip.fread(file, block, sizeof(block));
        if (count < 0) {
            complete = false;
            break;
        }
        if (count == 0)
            break;
        scanned += count;
        if (scanned > qint64(32) * 1024 * 1024) {
            complete = false;
            break;
        }
        QByteArray window = carry;
        window.append(block, static_cast<int>(count));
        if (window.contains(needle)) {
            found = true;
            break;
        }
        int const retained = std::min(needle.size() - 1, window.size());
        carry = retained > 0 ? window.right(retained) : QByteArray();
    }
    if (zip.fclose(file) != 0)
        complete = false;
    zip.discard(archive);
    return found ? 1 : (complete ? 0 : -1);
}

namespace {

static QString coverTypeFromMetadata(QByteArray const& contents) {
    QJsonParseError parseError;
    QJsonDocument const document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QString();

    QJsonObject const metadata = document.object()
        .value(QLatin1String("iink-user-metadata")).toObject();
    QJsonObject const kobo = metadata.value(QLatin1String("kobo")).toObject();
    QString const type = kobo.value(QLatin1String("backgroundType")).toString();
    return type.startsWith(QLatin1String("Custom_Cover_"))
        ? type
        : QString();
}

struct ScanTiming {
    double zipOpenMs;
    double scanMs;
    int metadataEntries;

    ScanTiming() : zipOpenMs(0.0), scanMs(0.0), metadataEntries(0) {}
};

static bool scanNotebookCoverType(
    State::ZipApi const& zip,
    QString const& notebookPath,
    QString* coverType,
    bool* determinate,
    ScanTiming* timing) {
    coverType->clear();
    *determinate = false;
    QByteArray const encodedPath = QFile::encodeName(notebookPath);
    int openError = 0;
    QElapsedTimer timer;
    timer.start();
    // ZIP_RDONLY. A notebook being saved or otherwise unreadable simply uses
    // Kobo's original preview for this pass, and the verdict is not cached.
    ZipArchiveOpaque* const archive = zip.open(encodedPath.constData(), 16, &openError);
    timing->zipOpenMs = cnt::fs_util::elapsedMs(timer);
    if (!archive)
        return false;

    int matches = 0;
    bool readFailure = false;
    QString found;
    timer.restart();
    long long const entryCount = zip.getNumEntries(archive, 0);
    if (entryCount >= 0 && entryCount <= 65536) {
        *determinate = true;
        for (long long i = 0; i < entryCount; ++i) {
            char const* const entryName = zip.getName(
                archive, static_cast<unsigned long long>(i), 0);
            if (!entryName)
                continue;
            QByteArray const name(entryName);
            if (!name.startsWith("pages/") || !name.endsWith("/meta.json"))
                continue;

            ++timing->metadataEntries;
            QByteArray contents;
            if (!readZipEntry(zip, archive, entryName, &contents)) {
                readFailure = true;
                continue;
            }
            QString const type = coverTypeFromMetadata(contents);
            if (type.isEmpty())
                continue;
            found = type;
            ++matches;
            if (matches > 1)
                break;
        }
    }
    zip.discard(archive);
    timing->scanMs = cnt::fs_util::elapsedMs(timer);

    // An unreadable page entry may be a save in progress; keep this pass's
    // verdict out of the cache so the next pass rescans.
    if (readFailure)
        *determinate = false;

    // A cover inserted by this plugin is the sole Custom_Cover page. Multiple
    // candidates are ambiguous, so preserve Kobo's original preview.
    if (matches != 1)
        return false;
    *coverType = found;
    return true;
}

} // namespace

int countNotebookPageEntries(
        State const& state,
        QString const& notebookPath) {
    State::ZipApi const& zip = state.zip;
    if (!zip.open || !zip.getNumEntries || !zip.getName || !zip.discard)
        return -1;

    QByteArray const encodedPath = QFile::encodeName(notebookPath);
    int openError = 0;
    ZipArchiveOpaque* const archive = zip.open(
        encodedPath.constData(), 16, &openError);
    if (!archive)
        return -1;

    int pages = 0;
    bool valid = true;
    long long const entryCount = zip.getNumEntries(archive, 0);
    if (entryCount < 0 || entryCount > 65536) {
        valid = false;
    } else {
        for (long long i = 0; i < entryCount; ++i) {
            char const* const entryName = zip.getName(
                archive, static_cast<unsigned long long>(i), 0);
            if (!entryName) {
                valid = false;
                break;
            }
            QByteArray const name(entryName);
            if (name.startsWith("pages/") && name.endsWith("/meta.json"))
                ++pages;
        }
    }
    zip.discard(archive);
    return valid ? pages : -1;
}

bool openNotebookHasPluginCover(
        State const& state,
        QString const& notebookPathValue,
        bool* hasCover) {
    State::ZipApi const& zip = state.zip;
    if (!zip.open
            || !zip.getNumEntries
            || !zip.getName
            || !zip.fopen
            || !zip.fread
            || !zip.fclose
            || !zip.discard) {
        *hasCover = false;
        return false;
    }

    QString const notebookPath = QDir::cleanPath(notebookPathValue);
    QString coverType;
    bool determinate = false;
    ScanTiming timing;
    *hasCover = scanNotebookCoverType(
        zip, notebookPath, &coverType, &determinate, &timing);
    return determinate;
}

bool cachedNotebookCoverType(
    State& state,
    QString const& notebookPath,
    QString* coverType,
    double pathResolveMs) {
    coverType->clear();
    QFileInfo const before(notebookPath);
    qint64 const modifiedMs = before.lastModified().toMSecsSinceEpoch();
    qint64 const size = before.size();
    if (!before.isFile() || size <= 0)
        return false;

    char line[224];
    {
        QMutexLocker locker(&state.cacheMutex);
        QHash<QString, CoverScanEntry>::const_iterator const found =
            state.scanCache.constFind(notebookPath);
        if (found != state.scanCache.constEnd()) {
            if (found.value().modifiedMs == modifiedMs
                    && found.value().size == size) {
                *coverType = found.value().type;
                bool const hasCover = found.value().hasCover;
                locker.unlock();
                snprintf(line, sizeof(line),
                    "cover-cache: hit (%s) path-resolve=%.1fms",
                    hasCover ? "cover" : "no cover", pathResolveMs);
                trace(line);
                return hasCover;
            }
            state.scanCache.remove(notebookPath);
            locker.unlock();
            trace("cover-cache: entry invalidated (notebook changed)");
        }
    }

    ScanTiming timing;
    bool determinate = false;
    QString type;
    bool const hasCover = scanNotebookCoverType(
        state.zip, notebookPath, &type, &determinate, &timing);

    // Only cache a verdict that describes the file we originally measured.
    QFileInfo const after(notebookPath);
    bool const stable = after.isFile()
        && after.size() == size
        && after.lastModified().toMSecsSinceEpoch() == modifiedMs;

    snprintf(line, sizeof(line),
        "cover-cache: miss path-resolve=%.1fms zip-open=%.1fms "
        "metadata-scan=%.1fms pages=%d verdict=%s%s",
        pathResolveMs, timing.zipOpenMs, timing.scanMs,
        timing.metadataEntries,
        hasCover ? "cover" : "no cover",
        (determinate && stable) ? " (cached)" : " (not cached)");
    trace(line);

    if (determinate && stable) {
        QMutexLocker locker(&state.cacheMutex);
        if (state.scanCache.size() >= kMaximumScanCacheEntries) {
            state.scanCache.clear();
            trace("cover-cache: scan cache cleared (entry limit)");
        }
        CoverScanEntry entry;
        entry.modifiedMs = modifiedMs;
        entry.size = size;
        entry.type = hasCover ? type : QString();
        entry.hasCover = hasCover;
        state.scanCache.insert(notebookPath, entry);
    }
    if (hasCover)
        *coverType = type;
    return hasCover;
}

namespace {

// Composed cover previews are persisted to plugin-owned storage so notebook
// cards can show cover and ink immediately after a restart. The in-memory
// cache dies with the process, while Kobo's thumbnailer considers its own
// cache fresh and does not call the parser again, so without the file the
// card would fall back to the clean cover after every boot.
static QString renderedPreviewFilePath(QString const& notebookPath) {
    QByteArray const digest = QCryptographicHash::hash(
        QDir::cleanPath(notebookPath).toUtf8(), QCryptographicHash::Sha1);
    QString path = QString::fromLatin1(kRenderedPreviewRoot);
    path += QString::fromLatin1(digest.toHex().constData());
    path += QLatin1String(".png");
    return path;
}

static void pruneRenderedPreviewFiles() {
    QDir const directory(QString::fromLatin1(kRenderedPreviewRoot));
    QFileInfoList files = directory.entryInfoList(
        QStringList() << QLatin1String("*.png"), QDir::Files, QDir::Time);
    while (files.size() > kMaximumPersistedPreviews) {
        QFileInfo const oldest = files.takeLast();
        if (!QFile::remove(oldest.absoluteFilePath()))
            break;
        trace("cover-cache: persisted preview pruned");
    }
}

static void persistRenderedCoverImage(
    QString const& notebookPath,
    QString const& coverType,
    QImage const& image) {
    if (!QDir().mkpath(QString::fromLatin1(kRenderedPreviewRoot))) {
        trace("cover-cache: preview directory unavailable");
        return;
    }
    QImage annotated = image;
    annotated.setText(QLatin1String("cnt-cover-type"), coverType);
    QSaveFile file(renderedPreviewFilePath(notebookPath));
    if (!file.open(QIODevice::WriteOnly)
            || !annotated.save(&file, "PNG")
            || !file.commit()) {
        trace("cover-cache: preview persist failed");
        return;
    }
    pruneRenderedPreviewFiles();
    trace("cover-cache: composed preview persisted");
}

static QImage loadPersistedCoverImage(
    QString const& notebookPath,
    QString const& coverType) {
    QString const path = renderedPreviewFilePath(notebookPath);
    if (!QFileInfo(path).isFile())
        return QImage();
    QImage const image(path);
    if (image.isNull()
            || image.width() != kBackgroundWidth
            || image.height() != kBackgroundHeight
            || image.text(QLatin1String("cnt-cover-type")) != coverType) {
        QFile::remove(path);
        trace("cover-cache: persisted preview rejected");
        return QImage();
    }
    return image;
}

} // namespace

void invalidateNotebookScanEntry(
        State& state,
        QString const& notebookPath) {
    QString const key = QDir::cleanPath(notebookPath);
    if (QFile::remove(renderedPreviewFilePath(key)))
        trace("cover-cache: persisted preview removed after cover change");
    QMutexLocker locker(&state.cacheMutex);
    if (state.scanCache.remove(key) > 0)
        trace("cover-cache: entry invalidated after plugin cover change");
    state.renderedCache.remove(key);
}

namespace {

static templates::CustomTemplate const* customCoverForType(
        State const& state,
        QString const& type) {
    for (int i = 0; i < state.customCovers.size(); ++i) {
        if (state.customCovers.at(i).id == type)
            return &state.customCovers.at(i);
    }
    return nullptr;
}

} // namespace

// Kobo's thumbnail service renders notebook ink on white but does not include
// a plugin-defined background. Treat darkness in that render as an ink mask
// and multiply it over the clean cover. Transparent pixels remain untouched,
// while black handwriting remains black. The result is deliberately opaque,
// matching the full-page cover images accepted by the existing callback.
QImage composeCoverWithRenderedInk(
    State const& state,
    QString const& type,
    QImage const& renderedInk) {
    templates::CustomTemplate const* const cover =
        customCoverForType(state, type);
    if (!cover || renderedInk.isNull())
        return QImage();

    QImage composed(cover->backgroundBase);
    if (composed.isNull()
            || composed.width() != kBackgroundWidth
            || composed.height() != kBackgroundHeight) {
        return QImage();
    }
    composed = composed.convertToFormat(QImage::Format_RGB32);

    QImage ink = renderedInk.convertToFormat(QImage::Format_ARGB32);
    if (ink.size() != composed.size()) {
        ink = ink.scaled(
            composed.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (ink.isNull())
        return QImage();

    for (int y = 0; y < composed.height(); ++y) {
        QRgb* const destination =
            reinterpret_cast<QRgb*>(composed.scanLine(y));
        QRgb const* const source =
            reinterpret_cast<QRgb const*>(ink.constScanLine(y));
        for (int x = 0; x < composed.width(); ++x) {
            int const darkness =
                ((255 - qGray(source[x])) * qAlpha(source[x]) + 127) / 255;
            if (darkness == 0)
                continue;
            int const keep = 255 - darkness;
            QRgb const base = destination[x];
            destination[x] = qRgb(
                (qRed(base) * keep + 127) / 255,
                (qGreen(base) * keep + 127) / 255,
                (qBlue(base) * keep + 127) / 255);
        }
    }
    return composed;
}

namespace {

// Caller must hold coverCacheMutex.
static void evictCleanCoverImagesLocked(State& state) {
    qint64 total = 0;
    QHash<QString, CleanCoverEntry>::const_iterator it;
    for (it = state.cleanCache.constBegin();
            it != state.cleanCache.constEnd(); ++it) {
        total += it.value().image.byteCount();
    }
    while (total > kMaximumImageCacheBytes && state.cleanCache.size() > 1) {
        QHash<QString, CleanCoverEntry>::iterator oldest =
            state.cleanCache.begin();
        for (QHash<QString, CleanCoverEntry>::iterator candidate =
                state.cleanCache.begin();
                candidate != state.cleanCache.end(); ++candidate) {
            if (candidate.value().sequence < oldest.value().sequence)
                oldest = candidate;
        }
        total -= oldest.value().image.byteCount();
        state.cleanCache.erase(oldest);
        trace("cover-cache: clean cover evicted (memory bound)");
    }
}

} // namespace

QImage cleanCustomCoverImage(State& state, QString const& type) {
    templates::CustomTemplate const* const cover =
        customCoverForType(state, type);
    if (!cover)
        return QImage();

    QFileInfo const info(cover->backgroundBase);
    qint64 const modifiedMs = info.lastModified().toMSecsSinceEpoch();
    qint64 const size = info.size();
    if (!info.isFile() || size <= 0)
        return QImage();

    {
        QMutexLocker locker(&state.cacheMutex);
        QHash<QString, CleanCoverEntry>::iterator const found =
            state.cleanCache.find(type);
        if (found != state.cleanCache.end()) {
            if (found.value().pngModifiedMs == modifiedMs
                    && found.value().pngSize == size) {
                found.value().sequence = ++state.cleanSequence;
                return found.value().image;
            }
            state.cleanCache.erase(found);
        }
    }

    QImage const image(cover->backgroundBase);
    if (image.isNull()
            || image.width() != kBackgroundWidth
            || image.height() != kBackgroundHeight) {
        return QImage();
    }

    QMutexLocker locker(&state.cacheMutex);
    CleanCoverEntry entry;
    entry.image = image;
    entry.pngModifiedMs = modifiedMs;
    entry.pngSize = size;
    entry.sequence = ++state.cleanSequence;
    state.cleanCache.insert(type, entry);
    evictCleanCoverImagesLocked(state);
    return image;
}

namespace {

// Caller must hold coverCacheMutex.
static void evictRenderedCoverImagesLocked(State& state) {
    qint64 total = 0;
    QHash<QString, RenderedCoverEntry>::const_iterator it;
    for (it = state.renderedCache.constBegin();
            it != state.renderedCache.constEnd(); ++it) {
        total += it.value().image.byteCount();
    }
    while (total > kMaximumImageCacheBytes && state.renderedCache.size() > 1) {
        QHash<QString, RenderedCoverEntry>::iterator oldest =
            state.renderedCache.begin();
        for (QHash<QString, RenderedCoverEntry>::iterator candidate =
                state.renderedCache.begin();
                candidate != state.renderedCache.end(); ++candidate) {
            if (candidate.value().sequence < oldest.value().sequence)
                oldest = candidate;
        }
        total -= oldest.value().image.byteCount();
        state.renderedCache.erase(oldest);
        trace("cover-cache: rendered preview evicted (memory bound)");
    }
}

} // namespace

void cacheRenderedCoverImage(
    State& state,
    QString const& notebookPath,
    QString const& coverType,
    QImage const& image) {
    if (image.isNull())
        return;
    QFileInfo const info(notebookPath);
    if (!info.isFile() || info.size() <= 0)
        return;

    QImage cachedImage = image;
    if (coverType.isEmpty()
            && (cachedImage.width() != kBackgroundWidth
                || cachedImage.height() != kBackgroundHeight)) {
        cachedImage = cachedImage.scaled(
            kBackgroundWidth,
            kBackgroundHeight,
            Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation);
    }
    if (cachedImage.isNull())
        return;

    persistRenderedCoverImage(notebookPath, coverType, cachedImage);

    RenderedCoverEntry entry;
    entry.image = cachedImage;
    entry.notebookModifiedMs = info.lastModified().toMSecsSinceEpoch();
    entry.notebookSize = info.size();
    entry.coverType = coverType;
    QMutexLocker locker(&state.cacheMutex);
    entry.sequence = ++state.renderedSequence;
    state.renderedCache.insert(QDir::cleanPath(notebookPath), entry);
    evictRenderedCoverImagesLocked(state);
}

QImage cachedRenderedCoverImage(
    State& state,
    QString const& notebookPath,
    QString const& coverType) {
    QString const key = QDir::cleanPath(notebookPath);
    QFileInfo const info(key);
    if (!info.isFile() || info.size() <= 0)
        return QImage();

    {
        QMutexLocker locker(&state.cacheMutex);
        QHash<QString, RenderedCoverEntry>::iterator const found =
            state.renderedCache.find(key);
        if (found != state.renderedCache.end()) {
            if (found.value().coverType != coverType) {
                state.renderedCache.erase(found);
            } else {
                // MyScript can emit the completed ink render before
                // IInkNotePadWidget's final save updates the .nebo mtime and
                // size. Do not throw that valid preview away merely because
                // the file settles afterward. It remains the best available
                // thumbnail until the next renderer callback replaces it.
                // A cover change through this plugin explicitly clears the
                // entry in invalidateNotebookScanEntry(), and a type mismatch
                // above also clears it.
                if (found.value().notebookModifiedMs
                            != info.lastModified().toMSecsSinceEpoch()
                        || found.value().notebookSize != info.size()) {
                    trace("cover-cache: retaining rendered ink across final "
                          "notebook save");
                }
                found.value().sequence = ++state.renderedSequence;
                return found.value().image;
            }
        }
    }

    QImage const persisted = loadPersistedCoverImage(key, coverType);
    if (persisted.isNull())
        return QImage();

    RenderedCoverEntry entry;
    entry.image = persisted;
    entry.notebookModifiedMs = info.lastModified().toMSecsSinceEpoch();
    entry.notebookSize = info.size();
    entry.coverType = coverType;
    QMutexLocker locker(&state.cacheMutex);
    entry.sequence = ++state.renderedSequence;
    state.renderedCache.insert(key, entry);
    evictRenderedCoverImagesLocked(state);
    trace("cover-cache: persisted preview restored");
    return persisted;
}

QString notebookPathFromContentId(QString const& contentId) {
    QString path = contentId;
    if (path.startsWith(QLatin1String("file:")))
        path = QUrl(path).toLocalFile();
    if (!path.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive))
        path.append(QLatin1String(".nebo"));
    path = QDir::cleanPath(path);
    if (!path.startsWith(QLatin1String("/mnt/onboard/"))
            || !QFileInfo(path).isFile()) {
        return QString();
    }
    return path;
}

} // namespace cover_cache
} // namespace cnt
