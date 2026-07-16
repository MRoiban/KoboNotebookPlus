#line 2847 "src/customnotebooktemplates.cc"

static bool backupNotebook(void* widget, QString* backupPath, QString* error) {
    return cnt::page_io::backupNotebookPath(
        firmwareApi().widgetFilePath(widget),
        QLatin1String(kCoverBackupRoot),
        QLatin1String("Cover not changed"),
        backupPath,
        error);
}

struct PageStorage {
    alignas(8) unsigned char bytes[64];
    bool constructed;

    PageStorage() : constructed(false) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~PageStorage() {
        if (constructed)
            firmwareApi().pageDestructor(bytes);
    }
};

struct ScopedIInkString {
    IInkStringStorage value;

    explicit ScopedIInkString(char const* utf8) {
        value.impl = nullptr;
        firmwareApi().iinkStringCtor(&value, utf8);
    }

    ~ScopedIInkString() {
        delete value.impl;
    }
};

struct LayoutStorage {
    // atk::core::Layout contains a shared document-layout pointer followed by
    // an atk::core::Page. The pinned destructor accesses the Page at +0x8.
    // Keep generous aligned storage so the firmware owns every construction
    // and destruction detail.
    alignas(8) unsigned char bytes[128];
    bool constructed;

    LayoutStorage() : constructed(false) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~LayoutStorage() {
        if (constructed)
            firmwareApi().layoutDestructor(bytes);
    }
};

struct ManagedObjectStorage {
    // myscript::engine::ManagedObject and its document wrappers are one
    // acquired _voReference pointer on this 32-bit SDK.
    alignas(4) unsigned char bytes[4];
    bool constructed;

    ManagedObjectStorage() : constructed(false) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~ManagedObjectStorage() {
        if (constructed && firmwareApi().managedObjectDestructor)
            firmwareApi().managedObjectDestructor(bytes);
    }

    bool hasObject() const {
        void* object = nullptr;
        memcpy(&object, bytes, sizeof(object));
        return object != nullptr;
    }
};
static_assert(sizeof(void*) == 4,
    "MyScript managed-object ABI requires the 32-bit Kobo target");

static QString iinkStringValue(IInkStringStorage* value) {
    if (!value || !value->impl || !firmwareApi().iinkStringToStdString)
        return QString();
    std::string utf8;
    firmwareApi().iinkStringToStdString(&utf8, value);
    return QString::fromUtf8(utf8.data(), static_cast<int>(utf8.size()));
}

static QString partStableId(SharedPart const& part) {
    if (!part || !firmwareApi().partGetId)
        return QString();
    IInkStringStorage value = {nullptr};
    firmwareApi().partGetId(&value, part.get());
    QString const id = iinkStringValue(&value);
    delete value.impl;
    return id;
}

static bool layoutForPart(
    SharedPart const& part,
    LayoutStorage* layout,
    QString* error) {
    if (!part || !layout || !firmwareApi().partGetPage || !firmwareApi().pageLayout || !firmwareApi().layoutDestructor) {
        if (error)
            *error = QLatin1String("Layers unavailable: page layout API is not ready.");
        return false;
    }

    PageStorage page;
    firmwareApi().partGetPage(page.bytes, part.get());
    page.constructed = true;
    firmwareApi().pageLayout(layout->bytes, page.bytes);
    layout->constructed = true;
    return true;
}

static bool applyNotebookCover(void* widget, QString const& type, QString* error) {
    void* const editor = cnt::notebook_widget::notePadEditor(widget);
    void* const editorControl = cnt::notebook_widget::notePadEditorControl(widget);
    if (!editor || !editorControl) {
        *error = QLatin1String("Cover not changed: notebook editor is not ready.");
        return false;
    }

    firmwareApi().widgetSave(widget);
    SharedPart const originalPart = firmwareApi().editorGetPart(editor);
    if (!originalPart) {
        *error = QLatin1String("Cover not changed: current page was not found.");
        return false;
    }

    SharedPackage const package = firmwareApi().partGetPackage(originalPart.get());
    if (!package) {
        *error = QLatin1String("Cover not changed: notebook package was not found.");
        return false;
    }

    int const oldCount = firmwareApi().packagePartCount(package.get());
    if (oldCount < 1 || oldCount > 4096) {
        *error = QLatin1String("Cover not changed: notebook page count is invalid.");
        return false;
    }

    SharedPart const firstPart = firmwareApi().packageGetPart(package.get(), 0);
    if (!firstPart) {
        *error = QLatin1String("Cover not changed: first page was not found.");
        return false;
    }

    firmwareApi().editorSetPart(editor, firstPart);
    void* const backgroundWidget = cnt::notebook_widget::notePadBackgroundWidget(widget);
    if (!backgroundWidget) {
        firmwareApi().editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: page background is not ready.");
        return false;
    }

    QString const firstBackground = firmwareApi().backgroundType(backgroundWidget);
    bool const alreadyHasCover = firstBackground.startsWith(
        QLatin1String("Custom_Cover_"));

    QString backupPath;
    if (!backupNotebook(widget, &backupPath, error)) {
        firmwareApi().editorSetPart(editor, originalPart);
        return false;
    }

    if (alreadyHasCover) {
        firmwareApi().setBackgroundTypeOriginal(widget, type);
        firmwareApi().widgetSave(widget);
        firmwareApi().packageSave(package.get());
        firmwareApi().widgetRefresh(widget);
        cnt::cover_cache::invalidateNotebookScanEntry(
            coverState(), firmwareApi().widgetFilePath(widget));
        trace("covers: existing cover background changed");
        return true;
    }

    ScopedIInkString rawContent("Raw Content");
    if (!rawContent.value.impl) {
        firmwareApi().editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: page type could not be created.");
        return false;
    }

    SharedPart const coverPart = firmwareApi().packageCreatePart(package.get(), rawContent.value);
    if (!coverPart || firmwareApi().packagePartCount(package.get()) != oldCount + 1) {
        firmwareApi().editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: a new page could not be created.");
        return false;
    }

    firmwareApi().editorSetPart(editor, coverPart);
    firmwareApi().setBackgroundTypeOriginal(widget, type);
    firmwareApi().widgetSave(widget);
    firmwareApi().packageSave(package.get());
    // The archive is mutated from here on; drop any cached verdict even if a
    // later validation step fails.
    cnt::cover_cache::invalidateNotebookScanEntry(
        coverState(), firmwareApi().widgetFilePath(widget));

    SharedDocument const document = cnt::pages::documentForPart(
        firmwareApi(), coverPart);
    int const appendedIndex = firmwareApi().packageIndexOfPart(package.get(), coverPart);
    if (!document
            || firmwareApi().documentPageCount(document.get()) != oldCount + 1
            || appendedIndex != oldCount) {
        *error = QLatin1String(
            "Cover page was added at the end, but could not be moved safely. "
            "The original notebook backup is in the covers/backups folder.");
        trace("covers: pre-move validation failed");
        return false;
    }

    firmwareApi().documentMovePage(document.get(), appendedIndex, 0);
    firmwareApi().packageSave(package.get());
    if (firmwareApi().packageIndexOfPart(package.get(), coverPart) != 0
            || firmwareApi().packagePartCount(package.get()) != oldCount + 1) {
        *error = QLatin1String(
            "Cover move could not be verified. The original notebook backup "
            "is in the covers/backups folder.");
        trace("covers: post-move validation failed");
        return false;
    }

    firmwareApi().editorSetPart(editor, coverPart);
    firmwareApi().widgetRefresh(widget);
    cnt::cover_cache::invalidateNotebookScanEntry(
        coverState(), firmwareApi().widgetFilePath(widget));
    trace("covers: new writable cover inserted as page zero");
    return true;
}

static bool writePageTransaction(
    QString const& path,
    QString const& stage,
    QString const& source,
    QString const& destination,
    QString const& sourceBackup,
    QString const& destinationBackup) {
    QJsonObject object;
    object.insert(QLatin1String("version"), 1);
    object.insert(QLatin1String("operation"), QLatin1String("move-page"));
    object.insert(QLatin1String("stage"), stage);
    object.insert(QLatin1String("source"), source);
    object.insert(QLatin1String("destination"), destination);
    object.insert(QLatin1String("sourceBackup"), sourceBackup);
    object.insert(QLatin1String("destinationBackup"), destinationBackup);
    object.insert(
        QLatin1String("updatedUtc"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QByteArray const bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

struct NotebookDestination {
    QString label;
    QString path;
};

static QVector<NotebookDestination> notebookDestinations(QString const& sourcePath) {
    QVector<NotebookDestination> values;
    QDirIterator iterator(
        QLatin1String("/mnt/onboard"),
        QStringList() << QLatin1String("*.nebo"),
        QDir::Files | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (iterator.hasNext() && values.size() < kMaximumDestinationNotebooks) {
        QString const path = QDir::cleanPath(iterator.next());
        QFileInfo const info(path);
        if (path == sourcePath
                || path.startsWith(QLatin1String("/mnt/onboard/.kobo/"))
                || !info.isFile()
                || info.isSymLink()
                || info.size() <= 0) {
            continue;
        }
        QString label = QDir(QLatin1String("/mnt/onboard"))
            .relativeFilePath(path);
        if (label.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive))
            label.chop(5);
        NotebookDestination destination;
        destination.label = label;
        destination.path = path;
        values.append(destination);
    }
    std::sort(values.begin(), values.end(), [](NotebookDestination const& a,
                                                NotebookDestination const& b) {
        return QString::localeAwareCompare(a.label, b.label) < 0;
    });
    return values;
}

static bool moveCurrentPageToNotebook(
    void* widget,
    QString const& destinationPath,
    QString* error) {
    bool destinationVerified = false;
    bool sourceRemoved = false;
    cnt::pages::PageContext source;
    try {
        firmwareApi().widgetSave(widget);
        if (!cnt::pages::loadPageContext(
                firmwareApi(),
                widget,
                kMaximumNotebookPages,
                &source,
                error)) {
            return false;
        }
        if (source.count <= 1) {
            *error = QLatin1String(
                "Page not moved: a notebook must keep at least one page.");
            return false;
        }

        QString const sourcePath = QDir::cleanPath(firmwareApi().widgetFilePath(widget));
        QString const destination = QDir::cleanPath(destinationPath);
        QFileInfo const destinationInfo(destination);
        if (destination == sourcePath
                || !destination.startsWith(QLatin1String("/mnt/onboard/"))
                || destination.startsWith(QLatin1String("/mnt/onboard/.kobo/"))
                || !destination.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive)
                || !destinationInfo.isFile()
                || destinationInfo.isSymLink()
                || destinationInfo.size() <= 0) {
            *error = QLatin1String("Page not moved: destination notebook is invalid.");
            return false;
        }

        QString sourceBackup;
        QString destinationBackup;
        if (!cnt::page_io::backupNotebookPath(
                sourcePath,
                QLatin1String(kPageBackupRoot),
                QLatin1String("Page not moved"),
                &sourceBackup,
                error)
                || !cnt::page_io::backupNotebookPath(
                    destination,
                    QLatin1String(kPageBackupRoot),
                    QLatin1String("Page not moved"),
                    &destinationBackup,
                    error)) {
            return false;
        }

        QDir root;
        if (!root.mkpath(QLatin1String(kPageTransactionRoot))) {
            *error = QLatin1String("Page not moved: transaction folder could not be created.");
            return false;
        }
        QString const transactionPath = QLatin1String(kPageTransactionRoot)
            + QDateTime::currentDateTimeUtc().toString(
                QLatin1String("yyyyMMdd-HHmmsszzz"))
            + QLatin1String(".json");
        if (!writePageTransaction(
                transactionPath,
                QLatin1String("prepared"),
                sourcePath,
                destination,
                sourceBackup,
                destinationBackup)) {
            *error = QLatin1String("Page not moved: safety journal could not be written.");
            return false;
        }

        SharedEngine const engine = firmwareApi().editorGetEngine(source.editor);
        if (!engine) {
            *error = QLatin1String("Page not moved: notebook engine is unavailable.");
            return false;
        }
        QByteArray const destinationUtf8 = destination.toUtf8();
        ScopedIInkString destinationString(destinationUtf8.constData());
        if (!destinationString.value.impl) {
            *error = QLatin1String("Page not moved: destination path could not be opened.");
            return false;
        }

        // Binary Ninja verified that option zero is EXISTING. It cannot create
        // or truncate a package if the destination path is wrong.
        SharedPackage destinationPackage = firmwareApi().engineOpenPackage(
            engine.get(), destinationString.value, 0);
        int const destinationCount = destinationPackage
            ? firmwareApi().packagePartCount(destinationPackage.get()) : 0;
        if (!destinationPackage
                || destinationCount < 1
                || destinationCount >= kMaximumNotebookPages) {
            *error = QLatin1String("Page not moved: destination page count is invalid.");
            return false;
        }

        SharedPart destinationPart = firmwareApi().packageClonePart(
            destinationPackage.get(), source.part);
        if (!destinationPart
                || firmwareApi().packagePartCount(destinationPackage.get()) != destinationCount + 1
                || firmwareApi().packageIndexOfPart(destinationPackage.get(), destinationPart)
                    != destinationCount) {
            *error = QLatin1String("Page not moved: destination copy could not be verified.");
            return false;
        }
        firmwareApi().packageSave(destinationPackage.get());
        cnt::cover_cache::invalidateNotebookScanEntry(
            coverState(), destination);
        destinationPart.reset();
        destinationPackage.reset();

        // Reopening is insufficient because MyScript's process-global path
        // registry may return a new package wrapper over the same cached
        // Document. Count serialized page metadata directly in the ZIP.
        if (cnt::cover_cache::countNotebookPageEntries(
                coverState(), destination) != destinationCount + 1) {
            *error = QLatin1String(
                "Page was not removed from its source because the destination "
                "copy could not be verified. Both backups were retained.");
            return false;
        }
        destinationVerified = true;
        if (!writePageTransaction(
                transactionPath,
                QLatin1String("destination-verified"),
                sourcePath,
                destination,
                sourceBackup,
                destinationBackup)) {
            *error = QLatin1String(
                "The page was safely copied, but not removed because the "
                "transaction journal could not be updated.");
            return false;
        }

        int const oldIndex = source.index;
        int const oldCount = source.count;
        firmwareApi().packageRemovePart(source.package.get(), source.part);
        sourceRemoved = true;
        if (firmwareApi().packagePartCount(source.package.get()) != oldCount - 1) {
            *error = QLatin1String(
                "The page was copied, but removal from the source could not be "
                "verified. Both backups were retained.");
            return false;
        }

        int const replacementIndex = oldIndex == oldCount - 1
            ? oldIndex - 1 : oldIndex;
        SharedPart const replacement = firmwareApi().packageGetPart(
            source.package.get(), replacementIndex);
        if (!replacement) {
            *error = QLatin1String(
                "The page was safely copied, but no replacement source page "
                "could be selected.");
            return false;
        }

        firmwareApi().editorSetPart(source.editor, replacement);
        firmwareApi().packageSave(source.package.get());
        cnt::cover_cache::invalidateNotebookScanEntry(
            coverState(), sourcePath);
        if (firmwareApi().packagePartCount(source.package.get()) != oldCount - 1
                || cnt::cover_cache::countNotebookPageEntries(
                    coverState(), sourcePath) != oldCount - 1) {
            *error = QLatin1String(
                "The page was copied, but removal from the source could not be "
                "verified. Both backups were retained.");
            return false;
        }

        firmwareApi().widgetSave(widget);
        cnt::cover_cache::invalidateNotebookScanEntry(
            coverState(), sourcePath);
        writePageTransaction(
            transactionPath,
            QLatin1String("complete"),
            sourcePath,
            destination,
            sourceBackup,
            destinationBackup);
        firmwareApi().widgetRefresh(widget);
        trace("pages: current page moved between notebooks");
        return true;
    } catch (std::exception const&) {
        *error = destinationVerified
            ? QLatin1String(
                "The destination copy is safe, but the notebook engine stopped "
                "before source removal completed. Review both notebooks.")
            : QLatin1String(
                "Page not moved: the notebook engine rejected the destination copy.");
    } catch (...) {
        *error = destinationVerified
            ? QLatin1String(
                "The destination copy is safe, but the move did not finish. "
                "Review both notebooks.")
            : QLatin1String("Page not moved: unexpected notebook error.");
    }

    if (!sourceRemoved && source.editor && source.part) {
        try {
            if (firmwareApi().packageIndexOfPart(source.package.get(), source.part) >= 0)
                firmwareApi().editorSetPart(source.editor, source.part);
        } catch (...) {
            trace("pages: source editor could not be restored after failed move");
        }
    }
    firmwareApi().widgetRefresh(widget);
    trace("pages: cross-notebook move stopped safely");
    return false;
}
