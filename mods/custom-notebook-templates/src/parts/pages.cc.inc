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

static SharedDocument documentForPart(SharedPart const& part) {
    PageStorage page;
    firmwareApi().partGetPage(page.bytes, part.get());
    page.constructed = true;
    return firmwareApi().pageDocument(page.bytes);
}

// Verified IInkNotePadWidget offsets on firmware 4.38.23697: the guarded
// MyScript Editor object/control identity is mirrored at 0x3c/0x40, and
// BackgroundWidget is at 0x90. The plugin does not manufacture ownership from
// these raw words; live firmware owners remain in scope for every call.
static void* notePadEditor(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + 0x3c);
}

static void* notePadEditorControl(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + 0x40);
}

static void* notePadBackgroundWidget(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + 0x90);
}

static bool applyNotebookCover(void* widget, QString const& type, QString* error) {
    void* const editor = notePadEditor(widget);
    void* const editorControl = notePadEditorControl(widget);
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
    void* const backgroundWidget = notePadBackgroundWidget(widget);
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

    SharedDocument const document = documentForPart(coverPart);
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

struct PageContext {
    void* widget;
    void* editor;
    SharedPart part;
    SharedPackage package;
    SharedDocument document;
    int index;
    int count;

    PageContext()
        : widget(nullptr), editor(nullptr), index(-1), count(0) {}
};

static bool loadPageContext(void* widget, PageContext* context, QString* error) {
    if (!widget || !context) {
        *error = QLatin1String("Page operation failed: notebook is not ready.");
        return false;
    }

    void* const editor = notePadEditor(widget);
    void* const editorControl = notePadEditorControl(widget);
    if (!editor || !editorControl) {
        *error = QLatin1String("Page operation failed: notebook editor is not ready.");
        return false;
    }

    SharedPart const part = firmwareApi().editorGetPart(editor);
    SharedPackage const package = part ? firmwareApi().partGetPackage(part.get()) : SharedPackage();
    int const count = package ? firmwareApi().packagePartCount(package.get()) : 0;
    int const index = package ? firmwareApi().packageIndexOfPart(package.get(), part) : -1;
    SharedDocument const document = part ? documentForPart(part) : SharedDocument();
    if (!part
            || !package
            || !document
            || count < 1
            || count > kMaximumNotebookPages
            || index < 0
            || index >= count
            || firmwareApi().documentPageCount(document.get()) != count) {
        *error = QLatin1String("Page operation failed: current page could not be verified.");
        return false;
    }

    context->widget = widget;
    context->editor = editor;
    context->part = part;
    context->package = package;
    context->document = document;
    context->index = index;
    context->count = count;
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

static bool reorderCurrentPage(void* widget, int target, QString* error) {
    try {
        firmwareApi().widgetSave(widget);
        PageContext context;
        if (!loadPageContext(widget, &context, error))
            return false;
        if (target < 0 || target >= context.count) {
            *error = QLatin1String("Page not reordered: destination position is invalid.");
            return false;
        }
        bool hasCover = false;
        if (!cnt::cover_cache::openNotebookHasPluginCover(
                coverState(), firmwareApi().widgetFilePath(widget), &hasCover)) {
            *error = QLatin1String(
                "Page not reordered: the notebook cover could not be checked.");
            return false;
        }
        if (hasCover && (context.index == 0 || target == 0)) {
            *error = QLatin1String(
                "Page not reordered: the notebook cover must stay the first page.");
            return false;
        }
        if (target == context.index)
            return true;

        QString backupPath;
        if (!cnt::page_io::backupNotebookPath(
                firmwareApi().widgetFilePath(widget),
                QLatin1String(kPageBackupRoot),
                QLatin1String("Page not reordered"),
                &backupPath,
                error)) {
            return false;
        }

        firmwareApi().documentMovePage(context.document.get(), context.index, target);
        firmwareApi().packageSave(context.package.get());
        if (firmwareApi().packagePartCount(context.package.get()) != context.count
                || firmwareApi().packageIndexOfPart(context.package.get(), context.part) != target) {
            // Best-effort in-memory rollback. The hidden archive backup is the
            // authoritative recovery copy if this validation itself failed.
            int const actual = firmwareApi().packageIndexOfPart(context.package.get(), context.part);
            if (actual >= 0 && actual < context.count) {
                firmwareApi().documentMovePage(context.document.get(), actual, context.index);
                firmwareApi().packageSave(context.package.get());
            }
            *error = QLatin1String(
                "Page reorder could not be verified. The original notebook "
                "is preserved in the page-manager backup folder.");
            return false;
        }

        firmwareApi().widgetSave(widget);
        cnt::cover_cache::invalidateNotebookScanEntry(
            coverState(), firmwareApi().widgetFilePath(widget));
        firmwareApi().editorSetPart(context.editor, context.part);
        firmwareApi().widgetRefresh(widget);
        trace("pages: current page reordered");
        return true;
    } catch (std::exception const&) {
        *error = QLatin1String(
            "Page not reordered: the notebook engine rejected the operation.");
    } catch (...) {
        *error = QLatin1String("Page not reordered: unexpected notebook error.");
    }
    trace("pages: reorder threw and was contained");
    return false;
}

static bool duplicateCurrentPage(void* widget, QString* error) {
    SharedPackage package;
    SharedPart clone;
    int oldCount = 0;
    bool cloneAttached = false;
    try {
        firmwareApi().widgetSave(widget);
        PageContext context;
        if (!loadPageContext(widget, &context, error))
            return false;
        bool hasCover = false;
        if (!cnt::cover_cache::openNotebookHasPluginCover(
                coverState(), firmwareApi().widgetFilePath(widget), &hasCover)) {
            *error = QLatin1String(
                "Page not duplicated: the notebook cover could not be checked.");
            return false;
        }
        if (hasCover && context.index == 0) {
            *error = QLatin1String(
                "Page not duplicated: the notebook cover cannot be duplicated.");
            return false;
        }
        if (context.count >= kMaximumNotebookPages) {
            *error = QLatin1String("Page not duplicated: notebook has too many pages.");
            return false;
        }

        QString backupPath;
        if (!cnt::page_io::backupNotebookPath(
                firmwareApi().widgetFilePath(widget),
                QLatin1String(kPageBackupRoot),
                QLatin1String("Page not duplicated"),
                &backupPath,
                error)) {
            return false;
        }

        package = context.package;
        oldCount = context.count;
        clone = firmwareApi().packageClonePart(package.get(), context.part);
        cloneAttached = bool(clone);
        if (!clone
                || firmwareApi().packagePartCount(package.get()) != oldCount + 1
                || firmwareApi().packageIndexOfPart(package.get(), clone) != oldCount) {
            *error = QLatin1String("Page not duplicated: cloned page could not be verified.");
            throw std::runtime_error("clone validation failed");
        }

        SharedDocument const document = documentForPart(clone);
        int const target = context.index + 1;
        if (!document || firmwareApi().documentPageCount(document.get()) != oldCount + 1) {
            *error = QLatin1String("Page not duplicated: cloned document could not be verified.");
            throw std::runtime_error("clone document validation failed");
        }
        if (target != oldCount)
            firmwareApi().documentMovePage(document.get(), oldCount, target);
        firmwareApi().packageSave(package.get());

        if (firmwareApi().packagePartCount(package.get()) != oldCount + 1
                || firmwareApi().packageIndexOfPart(package.get(), clone) != target) {
            *error = QLatin1String(
                "Duplicate could not be verified. The original notebook is "
                "preserved in the page-manager backup folder.");
            throw std::runtime_error("clone commit validation failed");
        }

        firmwareApi().widgetSave(widget);
        cnt::cover_cache::invalidateNotebookScanEntry(
            coverState(), firmwareApi().widgetFilePath(widget));
        firmwareApi().editorSetPart(context.editor, clone);
        firmwareApi().widgetRefresh(widget);
        trace("pages: current page duplicated");
        return true;
    } catch (...) {
        if (package && clone && cloneAttached) {
            try {
                firmwareApi().packageRemovePart(package.get(), clone);
                firmwareApi().packageSave(package.get());
                if (firmwareApi().packagePartCount(package.get()) == oldCount) {
                    trace("pages: failed duplicate rolled back");
                } else {
                    trace("pages: failed duplicate rollback also failed; backup retained");
                }
            } catch (...) {
                trace("pages: failed duplicate rollback also failed; backup retained");
            }
        }
        if (error->isEmpty()) {
            *error = QLatin1String(
                "Page not duplicated: the notebook engine rejected the operation.");
        }
        return false;
    }
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
    PageContext source;
    try {
        firmwareApi().widgetSave(widget);
        if (!loadPageContext(widget, &source, error))
            return false;
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
