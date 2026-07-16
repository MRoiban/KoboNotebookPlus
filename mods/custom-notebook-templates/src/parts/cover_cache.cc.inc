#line 1769 "src/customnotebooktemplates.cc"

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

static bool loadZipApis() {
    if (zipLibraryHandle)
        return true;

    zipLibraryHandle = dlopen("libzip.so.5", RTLD_LAZY | RTLD_LOCAL);
    if (!zipLibraryHandle)
        return false;

    bool const ready =
        resolveDynamicSymbol(zipLibraryHandle, "zip_open", &zipOpen)
        && resolveDynamicSymbol(
            zipLibraryHandle, "zip_get_num_entries", &zipGetNumEntries)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_get_name", &zipGetName)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_fopen", &zipFopen)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_fread", &zipFread)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_fclose", &zipFclose)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_discard", &zipDiscard);
    if (!ready) {
        dlclose(zipLibraryHandle);
        zipLibraryHandle = nullptr;
        zipOpen = nullptr;
        zipGetNumEntries = nullptr;
        zipGetName = nullptr;
        zipFopen = nullptr;
        zipFread = nullptr;
        zipFclose = nullptr;
        zipDiscard = nullptr;
        return false;
    }

    trace("archive: read-only notebook APIs resolved");
    return true;
}

static bool readZipEntry(
    ZipArchiveOpaque* archive,
    char const* entryName,
    QByteArray* contents) {
    contents->clear();
    ZipFileOpaque* const file = zipFopen(archive, entryName, 0);
    if (!file)
        return false;

    bool valid = true;
    char block[4096];
    while (true) {
        long long const count = zipFread(file, block, sizeof(block));
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
    if (zipFclose(file) != 0)
        valid = false;
    if (!valid)
        contents->clear();
    return valid;
}

// Return 1 when the serialized page.bdom contains the exact native layer ID,
// 0 when the entry was read completely without it, and -1 when the archive
// could not be inspected. This is a read-only persistence probe; it never
// participates in the mutation decision.
static int notebookArchiveContainsLayerId(
    QString const& notebookPath,
    QString const& partId,
    QString const& layerId) {
    if (!zipOpen || !zipFopen || !zipFread || !zipFclose || !zipDiscard
            || partId.isEmpty() || layerId.isEmpty()) {
        return -1;
    }

    QByteArray const encodedPath = QFile::encodeName(notebookPath);
    int openError = 0;
    ZipArchiveOpaque* const archive = zipOpen(
        encodedPath.constData(), 16, &openError);  // ZIP_RDONLY
    if (!archive)
        return -1;

    QByteArray const entryName = QByteArray("pages/")
        + partId.toUtf8() + QByteArray("/page.bdom");
    ZipFileOpaque* const file = zipFopen(archive, entryName.constData(), 0);
    if (!file) {
        zipDiscard(archive);
        return -1;
    }

    QByteArray const needle = layerId.toUtf8();
    QByteArray carry;
    bool found = false;
    bool complete = true;
    qint64 scanned = 0;
    char block[4096];
    while (true) {
        long long const count = zipFread(file, block, sizeof(block));
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
    if (zipFclose(file) != 0)
        complete = false;
    zipDiscard(archive);
    return found ? 1 : (complete ? 0 : -1);
}

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
    ZipArchiveOpaque* const archive = zipOpen(encodedPath.constData(), 16, &openError);
    timing->zipOpenMs = elapsedMs(timer);
    if (!archive)
        return false;

    int matches = 0;
    bool readFailure = false;
    QString found;
    timer.restart();
    long long const entryCount = zipGetNumEntries(archive, 0);
    if (entryCount >= 0 && entryCount <= 65536) {
        *determinate = true;
        for (long long i = 0; i < entryCount; ++i) {
            char const* const entryName = zipGetName(
                archive, static_cast<unsigned long long>(i), 0);
            if (!entryName)
                continue;
            QByteArray const name(entryName);
            if (!name.startsWith("pages/") || !name.endsWith("/meta.json"))
                continue;

            ++timing->metadataEntries;
            QByteArray contents;
            if (!readZipEntry(archive, entryName, &contents)) {
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
    zipDiscard(archive);
    timing->scanMs = elapsedMs(timer);

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

static int countNotebookPageEntries(QString const& notebookPath) {
    if (!zipOpen || !zipGetNumEntries || !zipGetName || !zipDiscard)
        return -1;

    QByteArray const encodedPath = QFile::encodeName(notebookPath);
    int openError = 0;
    ZipArchiveOpaque* const archive = zipOpen(
        encodedPath.constData(), 16, &openError);
    if (!archive)
        return -1;

    int pages = 0;
    bool valid = true;
    long long const entryCount = zipGetNumEntries(archive, 0);
    if (entryCount < 0 || entryCount > 65536) {
        valid = false;
    } else {
        for (long long i = 0; i < entryCount; ++i) {
            char const* const entryName = zipGetName(
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
    zipDiscard(archive);
    return valid ? pages : -1;
}

static bool openNotebookHasPluginCover(void* widget, bool* hasCover) {
    if (!zipOpen
            || !zipGetNumEntries
            || !zipGetName
            || !zipFopen
            || !zipFread
            || !zipFclose
            || !zipDiscard) {
        *hasCover = false;
        return false;
    }

    QString const notebookPath = QDir::cleanPath(firmwareApi().widgetFilePath(widget));
    QString coverType;
    bool determinate = false;
    ScanTiming timing;
    *hasCover = scanNotebookCoverType(
        notebookPath, &coverType, &determinate, &timing);
    return determinate;
}

static bool cachedNotebookCoverType(
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
        QMutexLocker locker(&coverCacheMutex);
        QHash<QString, CoverScanEntry>::const_iterator const found =
            coverScanCache.constFind(notebookPath);
        if (found != coverScanCache.constEnd()) {
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
            coverScanCache.remove(notebookPath);
            locker.unlock();
            trace("cover-cache: entry invalidated (notebook changed)");
        }
    }

    ScanTiming timing;
    bool determinate = false;
    QString type;
    bool const hasCover = scanNotebookCoverType(
        notebookPath, &type, &determinate, &timing);

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
        QMutexLocker locker(&coverCacheMutex);
        if (coverScanCache.size() >= kMaximumScanCacheEntries) {
            coverScanCache.clear();
            trace("cover-cache: scan cache cleared (entry limit)");
        }
        CoverScanEntry entry;
        entry.modifiedMs = modifiedMs;
        entry.size = size;
        entry.type = hasCover ? type : QString();
        entry.hasCover = hasCover;
        coverScanCache.insert(notebookPath, entry);
    }
    if (hasCover)
        *coverType = type;
    return hasCover;
}

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

static void invalidateNotebookScanEntry(QString const& notebookPath) {
    QString const key = QDir::cleanPath(notebookPath);
    if (QFile::remove(renderedPreviewFilePath(key)))
        trace("cover-cache: persisted preview removed after cover change");
    QMutexLocker locker(&coverCacheMutex);
    if (coverScanCache.remove(key) > 0)
        trace("cover-cache: entry invalidated after plugin cover change");
    renderedCoverCache.remove(key);
}

static CustomTemplate const* customCoverForType(QString const& type) {
    for (int i = 0; i < customCovers.size(); ++i) {
        if (customCovers.at(i).id == type)
            return &customCovers.at(i);
    }
    return nullptr;
}

// Kobo's thumbnail service renders notebook ink on white but does not include
// a plugin-defined background. Treat darkness in that render as an ink mask
// and multiply it over the clean cover. Transparent pixels remain untouched,
// while black handwriting remains black. The result is deliberately opaque,
// matching the full-page cover images accepted by the existing callback.
static QImage composeCoverWithRenderedInk(
    QString const& type,
    QImage const& renderedInk) {
    CustomTemplate const* const cover = customCoverForType(type);
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

// Caller must hold coverCacheMutex.
static void evictCleanCoverImagesLocked() {
    qint64 total = 0;
    QHash<QString, CleanCoverEntry>::const_iterator it;
    for (it = cleanCoverCache.constBegin();
            it != cleanCoverCache.constEnd(); ++it) {
        total += it.value().image.byteCount();
    }
    while (total > kMaximumImageCacheBytes && cleanCoverCache.size() > 1) {
        QHash<QString, CleanCoverEntry>::iterator oldest =
            cleanCoverCache.begin();
        for (QHash<QString, CleanCoverEntry>::iterator candidate =
                cleanCoverCache.begin();
                candidate != cleanCoverCache.end(); ++candidate) {
            if (candidate.value().sequence < oldest.value().sequence)
                oldest = candidate;
        }
        total -= oldest.value().image.byteCount();
        cleanCoverCache.erase(oldest);
        trace("cover-cache: clean cover evicted (memory bound)");
    }
}

static QImage cleanCustomCoverImage(QString const& type) {
    CustomTemplate const* const cover = customCoverForType(type);
    if (!cover)
        return QImage();

    QFileInfo const info(cover->backgroundBase);
    qint64 const modifiedMs = info.lastModified().toMSecsSinceEpoch();
    qint64 const size = info.size();
    if (!info.isFile() || size <= 0)
        return QImage();

    {
        QMutexLocker locker(&coverCacheMutex);
        QHash<QString, CleanCoverEntry>::iterator const found =
            cleanCoverCache.find(type);
        if (found != cleanCoverCache.end()) {
            if (found.value().pngModifiedMs == modifiedMs
                    && found.value().pngSize == size) {
                found.value().sequence = ++cleanCoverSequence;
                return found.value().image;
            }
            cleanCoverCache.erase(found);
        }
    }

    QImage const image(cover->backgroundBase);
    if (image.isNull()
            || image.width() != kBackgroundWidth
            || image.height() != kBackgroundHeight) {
        return QImage();
    }

    QMutexLocker locker(&coverCacheMutex);
    CleanCoverEntry entry;
    entry.image = image;
    entry.pngModifiedMs = modifiedMs;
    entry.pngSize = size;
    entry.sequence = ++cleanCoverSequence;
    cleanCoverCache.insert(type, entry);
    evictCleanCoverImagesLocked();
    return image;
}

// Caller must hold coverCacheMutex.
static void evictRenderedCoverImagesLocked() {
    qint64 total = 0;
    QHash<QString, RenderedCoverEntry>::const_iterator it;
    for (it = renderedCoverCache.constBegin();
            it != renderedCoverCache.constEnd(); ++it) {
        total += it.value().image.byteCount();
    }
    while (total > kMaximumImageCacheBytes && renderedCoverCache.size() > 1) {
        QHash<QString, RenderedCoverEntry>::iterator oldest =
            renderedCoverCache.begin();
        for (QHash<QString, RenderedCoverEntry>::iterator candidate =
                renderedCoverCache.begin();
                candidate != renderedCoverCache.end(); ++candidate) {
            if (candidate.value().sequence < oldest.value().sequence)
                oldest = candidate;
        }
        total -= oldest.value().image.byteCount();
        renderedCoverCache.erase(oldest);
        trace("cover-cache: rendered preview evicted (memory bound)");
    }
}

static void cacheRenderedCoverImage(
    QString const& notebookPath,
    QString const& coverType,
    QImage const& image) {
    if (image.isNull())
        return;
    QFileInfo const info(notebookPath);
    if (!info.isFile() || info.size() <= 0)
        return;

    persistRenderedCoverImage(notebookPath, coverType, image);

    RenderedCoverEntry entry;
    entry.image = image;
    entry.notebookModifiedMs = info.lastModified().toMSecsSinceEpoch();
    entry.notebookSize = info.size();
    entry.coverType = coverType;
    QMutexLocker locker(&coverCacheMutex);
    entry.sequence = ++renderedCoverSequence;
    renderedCoverCache.insert(QDir::cleanPath(notebookPath), entry);
    evictRenderedCoverImagesLocked();
}

static QImage cachedRenderedCoverImage(
    QString const& notebookPath,
    QString const& coverType) {
    QString const key = QDir::cleanPath(notebookPath);
    QFileInfo const info(key);
    if (!info.isFile() || info.size() <= 0)
        return QImage();

    {
        QMutexLocker locker(&coverCacheMutex);
        QHash<QString, RenderedCoverEntry>::iterator const found =
            renderedCoverCache.find(key);
        if (found != renderedCoverCache.end()) {
            if (found.value().coverType != coverType) {
                renderedCoverCache.erase(found);
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
                found.value().sequence = ++renderedCoverSequence;
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
    QMutexLocker locker(&coverCacheMutex);
    entry.sequence = ++renderedCoverSequence;
    renderedCoverCache.insert(key, entry);
    evictRenderedCoverImagesLocked();
    trace("cover-cache: persisted preview restored");
    return persisted;
}

static QString notebookPathFromVolume(void const* volume) {
    QString path = firmwareApi().contentGetId(volume);
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

template <typename Function>
static bool resolvePinned(
    void* handle,
    char const* symbolName,
    uintptr_t expectedVma,
    Function* destination) {
    void* const symbol = dlsym(handle, symbolName);
    Dl_info image = {};
    if (!symbol || !dladdr(symbol, &image) || !image.dli_fbase) {
        nh_log("cover API symbol missing: %s", symbolName);
        return false;
    }

    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address = reinterpret_cast<uintptr_t>(symbol) & ~uintptr_t(1);
    if (address - base != expectedVma) {
        nh_log("cover API VMA mismatch for %s: 0x%lx, expected 0x%lx",
            symbolName,
            static_cast<unsigned long>(address - base),
            static_cast<unsigned long>(expectedVma));
        return false;
    }

    union {
        void* pointer;
        Function function;
    } converter;
    converter.pointer = symbol;
    *destination = converter.function;
    return true;
}

template <typename Function>
static bool resolvePinnedThumbVma(
    void* handle,
    char const* verifiedMarkerSymbol,
    uintptr_t expectedVma,
    Function* destination) {
    void* const marker = dlsym(handle, verifiedMarkerSymbol);
    Dl_info markerImage = {};
    if (!marker || !dladdr(marker, &markerImage) || !markerImage.dli_fbase) {
        nh_log("cover API image missing for internal VMA 0x%lx",
            static_cast<unsigned long>(expectedVma));
        return false;
    }

    uintptr_t const base = reinterpret_cast<uintptr_t>(markerImage.dli_fbase);
    void* const code = reinterpret_cast<void*>(base + expectedVma);
    Dl_info codeImage = {};
    if (!dladdr(code, &codeImage)
            || codeImage.dli_fbase != markerImage.dli_fbase) {
        nh_log("cover API internal VMA unmapped: 0x%lx",
            static_cast<unsigned long>(expectedVma));
        return false;
    }

    union {
        void* pointer;
        Function function;
    } converter;
    converter.pointer = reinterpret_cast<void*>(base + expectedVma + 1);
    *destination = converter.function;
    return true;
}

static bool resolveCoverApis(void* iinknoteHandle) {
    void* const iinkHandle = dlopen("libiink.so", RTLD_LAZY | RTLD_NOLOAD);
    void* const iinkUiRefHandle = dlopen(
        "libiinkuiref.so.1", RTLD_LAZY | RTLD_NOLOAD);
    void* const nickelHandle = dlopen(
        "libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!iinkHandle) {
        trace("covers: libiink not loaded");
        if (iinkUiRefHandle)
            dlclose(iinkUiRefHandle);
        if (nickelHandle)
            dlclose(nickelHandle);
        return false;
    }

    bool const noteSymbols =
        resolvePinned(iinknoteHandle, kCreateIInkMenuItemSymbol,
            kCreateIInkMenuItemVma, &firmwareApi().createIInkMenuItem)
        && resolvePinned(iinknoteHandle, kToolMenuTapGestureSymbol,
            kToolMenuTapGestureVma, &firmwareApi().toolMenuTapGesture)
        && resolvePinned(iinknoteHandle, kMenuSelectBackgroundSymbol,
            kMenuSelectBackgroundVma, &firmwareApi().menuSelectBackground)
        && resolvePinned(iinknoteHandle, kSetBackgroundTypeSymbol,
            kSetBackgroundTypeVma, &firmwareApi().setBackgroundTypeOriginal)
        && resolvePinned(iinknoteHandle, kWidgetSaveSymbol,
            kWidgetSaveVma, &firmwareApi().widgetSave)
        && resolvePinned(iinknoteHandle, kWidgetFilePathSymbol,
            kWidgetFilePathVma, &firmwareApi().widgetFilePath)
        && resolvePinned(iinknoteHandle, kWidgetRefreshSymbol,
            kWidgetRefreshVma, &firmwareApi().widgetRefresh)
        && resolvePinned(iinknoteHandle, kShowErrorPopupSymbol,
            kShowErrorPopupVma, &firmwareApi().showErrorPopup)
        && resolvePinned(iinknoteHandle, kBackgroundTypeSymbol,
            kBackgroundTypeVma, &firmwareApi().backgroundType)
        && resolvePinned(iinknoteHandle, kContentGetIdSymbol,
            kContentGetIdVma, &firmwareApi().contentGetId);

    bool const iinkSymbols =
        resolvePinned(iinkHandle, kEditorGetPartSymbol,
            kEditorGetPartVma, &firmwareApi().editorGetPart)
        && resolvePinned(iinkHandle, kEditorGetEngineSymbol,
            kEditorGetEngineVma, &firmwareApi().editorGetEngine)
        && resolvePinned(iinkHandle, kEditorSetPartSymbol,
            kEditorSetPartVma, &firmwareApi().editorSetPart)
        && resolvePinned(iinkHandle, kPartGetPackageSymbol,
            kPartGetPackageVma, &firmwareApi().partGetPackage)
        && resolvePinned(iinkHandle, kPartGetPageSymbol,
            kPartGetPageVma, &firmwareApi().partGetPage)
        && resolvePinned(iinkHandle, kPageDocumentSymbol,
            kPageDocumentVma, &firmwareApi().pageDocument)
        && resolvePinned(iinkHandle, kPageDestructorSymbol,
            kPageDestructorVma, &firmwareApi().pageDestructor)
        && resolvePinned(iinkHandle, kPackagePartCountSymbol,
            kPackagePartCountVma, &firmwareApi().packagePartCount)
        && resolvePinned(iinkHandle, kPackageGetPartSymbol,
            kPackageGetPartVma, &firmwareApi().packageGetPart)
        && resolvePinned(iinkHandle, kPackageIndexOfPartSymbol,
            kPackageIndexOfPartVma, &firmwareApi().packageIndexOfPart)
        && resolvePinned(iinkHandle, kPackageCreatePartSymbol,
            kPackageCreatePartVma, &firmwareApi().packageCreatePart)
        && resolvePinned(iinkHandle, kPackageClonePartSymbol,
            kPackageClonePartVma, &firmwareApi().packageClonePart)
        && resolvePinned(iinkHandle, kPackageRemovePartSymbol,
            kPackageRemovePartVma, &firmwareApi().packageRemovePart)
        && resolvePinned(iinkHandle, kPackageSaveSymbol,
            kPackageSaveVma, &firmwareApi().packageSave)
        && resolvePinned(iinkHandle, kEngineOpenPackageSymbol,
            kEngineOpenPackageVma, &firmwareApi().engineOpenPackage)
        && resolvePinned(iinkHandle, kDocumentPageCountSymbol,
            kDocumentPageCountVma, &firmwareApi().documentPageCount)
        && resolvePinned(iinkHandle, kDocumentMovePageSymbol,
            kDocumentMovePageVma, &firmwareApi().documentMovePage)
        && resolvePinned(iinkHandle, kIInkStringCtorSymbol,
            kIInkStringCtorVma, &firmwareApi().iinkStringCtor);

    bool const layerSymbols =
        resolvePinned(iinkHandle, kEditorGetRendererSymbol,
            kEditorGetRendererVma, &firmwareApi().editorGetRenderer)
        && resolvePinned(iinkHandle, kPartGetIdSymbol,
            kPartGetIdVma, &firmwareApi().partGetId)
        && resolvePinned(iinkHandle, kPageLayoutSymbol,
            kPageLayoutVma, &firmwareApi().pageLayout)
        && resolvePinned(iinkHandle, kLayoutDestructorSymbol,
            kLayoutDestructorVma, &firmwareApi().layoutDestructor)
        && resolvePinned(iinkHandle, kLayoutAppendLayerSymbol,
            kLayoutAppendLayerVma, &firmwareApi().layoutAppendLayer)
        && resolvePinned(iinkHandle, kLayoutRemoveLayerSymbol,
            kLayoutRemoveLayerVma, &firmwareApi().layoutRemoveLayer)
        && resolvePinned(iinkHandle, kAtkLayoutRawLayoutSymbol,
            kAtkLayoutRawLayoutVma, &firmwareApi().atkLayoutRawLayout)
        && resolvePinned(iinkHandle, kDocumentLayoutGetLayerSymbol,
            kDocumentLayoutGetLayerVma, &firmwareApi().documentLayoutGetLayer)
        && resolvePinned(iinkHandle, kLayerIteratorIsAtEndSymbol,
            kLayerIteratorIsAtEndVma, &firmwareApi().layerIteratorIsAtEnd)
        && resolvePinned(iinkHandle, kManagedObjectDestructorSymbol,
            kManagedObjectDestructorVma, &firmwareApi().managedObjectDestructor)
        && resolvePinned(iinkHandle, kRendererGetBackendSymbol,
            kRendererGetBackendVma, &firmwareApi().rendererGetBackend)
        && resolvePinned(iinkHandle, kRendererRestrictToLayersSymbol,
            kRendererRestrictToLayersVma, &firmwareApi().rendererRestrictToLayers)
        && resolvePinned(iinkHandle, kPageControllerInputDispatcherSymbol,
            kPageControllerInputDispatcherVma, &firmwareApi().pageControllerInputDispatcher)
        && resolvePinned(iinkHandle,
            kPlatformInputDispatcherGetCurrentToolSymbol,
            kPlatformInputDispatcherGetCurrentToolVma,
            &firmwareApi().platformInputDispatcherGetCurrentTool)
        && resolvePinned(iinkHandle, kDynamicToolDispatcherCastSymbol,
            kDynamicToolDispatcherCastVma, &firmwareApi().dynamicToolDispatcherCast)
        && resolvePinned(iinkHandle, kToolDispatcherRestrictToLayerSymbol,
            kToolDispatcherRestrictToLayerVma,
            &firmwareApi().toolDispatcherRestrictToLayer)
        && resolvePinned(iinkHandle, kIInkStringToStdStringSymbol,
            kIInkStringToStdStringVma, &firmwareApi().iinkStringToStdString)
        && resolvePinned(iinkHandle, kDocumentLayerNameSymbol,
            kDocumentLayerNameVma, &firmwareApi().documentLayerName)
        && resolvePinned(iinkHandle, kBackgroundObjectLayerNameSymbol,
            kBackgroundObjectLayerNameVma, &firmwareApi().backgroundObjectLayerName)
        && resolvePinned(iinkHandle, kNeboBackendVtableSymbol,
            kNeboBackendVtableVma, &firmwareApi().neboBackendVtable)
        && resolvePinned(iinkHandle, kCompositeBoxFactoryMainBackendSymbol,
            kCompositeBoxFactoryMainBackendVma, &firmwareApi().compositeBoxFactoryMainBackend)
        && resolvePinned(iinkHandle, kCompositeBoxFactoryBackendsSymbol,
            kCompositeBoxFactoryBackendsVma, &firmwareApi().compositeBoxFactoryBackends)
        && resolvePinned(iinkHandle, kSelectionSelectLayerSymbol,
            kSelectionSelectLayerVma, &firmwareApi().selectionSelectLayer)
        && resolvePinned(iinkHandle, kSelectionSelectPolygonSymbol,
            kSelectionSelectPolygonVma, &firmwareApi().selectionSelectPolygon)
        && resolvePinned(iinkHandle, kSelectionSelectNoneSymbol,
            kSelectionSelectNoneVma, &firmwareApi().selectionSelectNone)
        && resolvePinned(iinkHandle, kSelectionIsEmptySymbol,
            kSelectionIsEmptyVma, &firmwareApi().selectionIsEmpty)
        && resolvePinned(iinkHandle, kSelectionAdjustToStrokeBoundariesSymbol,
            kSelectionAdjustToStrokeBoundariesVma,
            &firmwareApi().selectionAdjustToStrokeBoundaries)
        && resolvePinned(iinkHandle, kEraserPolicySymbol,
            kEraserPolicyVma, &firmwareApi().eraserPolicy)
        && resolvePinned(iinkHandle, kEraserStrokerPolygonSymbol,
            kEraserStrokerPolygonVma, &firmwareApi().eraserStrokerPolygon)
        && resolvePinned(iinkHandle, kDrawingBackendVtableSymbol,
            kDrawingBackendVtableVma, &firmwareApi().drawingBackendVtable)
        && resolvePinned(iinkHandle, kDrawingEraserVtableSymbol,
            kDrawingEraserVtableVma, &firmwareApi().drawingEraserVtable)
        && resolvePinned(iinkHandle, kPlainDrawingEraserVtableSymbol,
            kPlainDrawingEraserVtableVma, &firmwareApi().plainDrawingEraserVtable)
        && resolvePinned(iinkHandle, kDiagramEraserVtableSymbol,
            kDiagramEraserVtableVma, &firmwareApi().diagramEraserVtable)
        && resolvePinned(iinkHandle, kDiagramPenVtableSymbol,
            kDiagramPenVtableVma, &firmwareApi().diagramPenVtable);

    // Eraser size uses the same live editor/backend path as layer routing, but
    // it remains a separate feature gate. The exact vtables prevent a radius
    // setter from ever being called on an arbitrary kind-4 Tool.
    bool const eraserSizeSymbols = layerSymbols
        && resolvePinned(iinkHandle, kEraserSetRadiusSymbol,
            kEraserSetRadiusVma, &firmwareApi().eraserSetRadius)
        && resolvePinned(iinkHandle, kEraserRadiusSymbol,
            kEraserRadiusVma, &firmwareApi().eraserRadius)
        && resolvePinned(iinkHandle, kEraserWidthFromThicknessRatioSymbol,
            kEraserWidthFromThicknessRatioVma,
            &firmwareApi().eraserWidthFromThicknessRatio)
        && resolvePinned(iinkHandle, kLayoutGridLineGapSymbol,
            kLayoutGridLineGapVma, &firmwareApi().layoutGridLineGap)
        && resolvePinned(iinkHandle, kCoreEraserVtableSymbol,
            kCoreEraserVtableVma, &firmwareApi().coreEraserVtable)
        && resolvePinned(iinkHandle, kTextEraserSntVtableSymbol,
            kTextEraserSntVtableVma, &firmwareApi().textEraserSntVtable)
        && resolvePinned(iinkHandle, kMathEraserVtableSymbol,
            kMathEraserVtableVma, &firmwareApi().mathEraserVtable)
        && resolvePinned(iinkHandle, kTextEraserVtableSymbol,
            kTextEraserVtableVma, &firmwareApi().textEraserVtable);
    eraserSizeApisReady = eraserSizeSymbols;

    bool const layerPreviewSymbols = iinkUiRefHandle
        && resolvePinned(iinkHandle, kEditorGetConfigurationSymbol,
            kEditorGetConfigurationVma, &firmwareApi().editorGetConfiguration)
        && resolvePinned(iinkHandle, kPageControllerExportToPngSymbol,
            kPageControllerExportToPngVma, &firmwareApi().pageControllerExportToPng)
        && resolvePinned(iinkHandle, kBackendImageDrawerMakeSharedSymbol,
            kBackendImageDrawerMakeSharedVma, &firmwareApi().backendImageDrawerMakeShared)
        && resolvePinnedThumbVma(
            iinkHandle,
            kPageControllerExportToPngSymbol,
            kStockBackendImageDrawerExportVma,
            &firmwareApi().stockBackendImageDrawerExport)
        && resolvePinned(iinkUiRefHandle, kImagePainterConstructorSymbol,
            kImagePainterConstructorVma, &firmwareApi().imagePainterConstructor)
        && resolvePinned(
            iinkUiRefHandle,
            kImagePainterDeletingDestructorSymbol,
            kImagePainterDeletingDestructorVma,
            &firmwareApi().imagePainterDeletingDestructor)
        && resolvePinned(
            iinkUiRefHandle,
            kImagePainterSetImageLoaderSymbol,
            kImagePainterSetImageLoaderVma,
            &firmwareApi().imagePainterSetImageLoader)
        && resolvePinned(
            iinkUiRefHandle,
            kUirefEditorWidgetVtableSymbol,
            kUirefEditorWidgetVtableVma,
            &firmwareApi().uirefEditorWidgetVtable);
    layerPreviewApisReady = iinkSymbols && layerSymbols && layerPreviewSymbols;

    // Best-effort: the base Eraser::updateSelection lets the diagram eraser
    // commit custom-layer ink. If it cannot be verified the eraser simply
    // keeps stock behavior; selection and pen routing remain unaffected.
    resolvePinned(iinkHandle, kCoreEraserUpdateSelectionSymbol,
        kCoreEraserUpdateSelectionVma, &firmwareApi().coreEraserUpdateSelection);

    bool const nativeLayerMenuSymbols = nickelHandle
        && resolvePinned(nickelHandle, kNickelTouchMenuConstructorSymbol,
            kNickelTouchMenuConstructorVma, &firmwareApi().nickelTouchMenuConstructor)
        && resolvePinned(nickelHandle, kNickelTouchMenuSetAlignmentSymbol,
            kNickelTouchMenuSetAlignmentVma, &firmwareApi().nickelTouchMenuSetAlignment)
        && resolvePinned(
            nickelHandle,
            kTouchMenuSetCustomPopupPositionOffsetSymbol,
            kTouchMenuSetCustomPopupPositionOffsetVma,
            &firmwareApi().touchMenuSetCustomPopupPositionOffset)
        && resolvePinned(
            nickelHandle,
            kAbstractNickelMenuControllerPopupFromWidgetSymbol,
            kAbstractNickelMenuControllerPopupFromWidgetVma,
            &firmwareApi().abstractNickelMenuControllerPopupFromWidget)
        && resolvePinned(
            nickelHandle,
            kNickelTouchMenuPopupPositionSymbol,
            kNickelTouchMenuPopupPositionVma,
            &firmwareApi().nickelTouchMenuPopupPosition);

    // Selectable layer rows use the exact native tool-row surface, but never
    // pass a synthetic layer value through IInkToolMenuWidget::setTool(). The
    // stock constructor gives us its QSS and named QLabel children; the
    // plugin supplies only their thumbnail/text and registers the same
    // GestureReceiver subobject used by IInkToolMenuController::loadView().
    bool const nativeLayerRowSymbols = nickelHandle
        && resolvePinned(
            iinknoteHandle,
            kIInkToolMenuWidgetConstructorSymbol,
            kIInkToolMenuWidgetConstructorVma,
            &firmwareApi().iInkToolMenuWidgetConstructor)
        && resolvePinned(
            iinknoteHandle,
            kIInkToolMenuWidgetSetSelectedSymbol,
            kIInkToolMenuWidgetSetSelectedVma,
            &firmwareApi().iInkToolMenuWidgetSetSelected)
        && resolvePinned(
            nickelHandle,
            kAbstractMenuControllerGrabTapGestureSymbol,
            kAbstractMenuControllerGrabTapGestureVma,
            &firmwareApi().abstractMenuControllerGrabTapGesture)
        && resolvePinned(
            nickelHandle,
            kAbstractMenuControllerTapGestureSymbol,
            kAbstractMenuControllerTapGestureVma,
            &firmwareApi().abstractMenuControllerTapGesture);

    layerHooksReady = noteSymbols && iinkSymbols && layerSymbols
        && nativeLayerMenuSymbols && nativeLayerRowSymbols;
    trace(layerHooksReady
        ? "layers: pinned layout, routing, and native menu APIs verified"
        : "layers: pinned API verification failed; feature disabled");
    trace(layerPreviewSymbols
        ? "layers: isolated preview APIs verified"
        : "layers: preview APIs unavailable; placeholder cards retained");
    trace(eraserSizeApisReady
        ? "eraser-size: pinned radius, line-grid, and exact eraser APIs verified"
        : "eraser-size: pinned engine APIs unavailable; feature disabled");

    dlclose(iinkHandle);
    if (iinkUiRefHandle)
        dlclose(iinkUiRefHandle);
    if (nickelHandle)
        dlclose(nickelHandle);
    if (!noteSymbols || !iinkSymbols) {
        trace("covers: pinned API resolution failed");
        return false;
    }

    trace("covers: pinned APIs verified");
    return true;
}

static bool pointerMatchesVma(void* pointer, uintptr_t expectedVma) {
    Dl_info image = {};
    if (!pointer || !dladdr(pointer, &image) || !image.dli_fbase)
        return false;
    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address = reinterpret_cast<uintptr_t>(pointer) & ~uintptr_t(1);
    return address - base == expectedVma;
}

// Read-only device diagnostic for polymorphic firmware objects. dladdr keeps
// this independent of ASLR while the reported VMA and nearest dynamic symbol
// let us identify an unexpected concrete backend/tool in the pinned binary.
// Function pointers may carry ARM's Thumb bit, so strip it before computing
// the image-relative address.
static QString pointerIdentity(void* pointer) {
    Dl_info image = {};
    if (!pointer || !dladdr(pointer, &image) || !image.dli_fbase)
        return QLatin1String("unmapped");
    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address = reinterpret_cast<uintptr_t>(pointer) & ~uintptr_t(1);
    QString identity = QLatin1String("0x")
        + QString::number(address - base, 16);
    if (image.dli_sname && image.dli_sname[0] != '\0')
        identity += QLatin1String(":") + QString::fromLatin1(image.dli_sname);
    return identity;
}

static QString toolIdentity(void* tool, bool includeSelectionSlot) {
    if (!tool)
        return QLatin1String("missing");
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return QLatin1String("null-vptr");
    QString identity = QLatin1String("vptr=") + pointerIdentity(vptr)
        + QLatin1String(" penDown=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + 0x3c))
        + QLatin1String(" restrict=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + kToolRestrictToLayerVtableSlot))
        + QLatin1String(" restricted=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + 0x58));
    if (includeSelectionSlot) {
        identity += QLatin1String(" selection=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + kDrawingEraserSelectionFromPointsVtableSlot))
            + QLatin1String(" update=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + kDiagramEraserUpdateSelectionVtableSlot))
            + QLatin1String(" erase=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + kDiagramEraserEraseSelectionVtableSlot));
    }
    return identity;
}
