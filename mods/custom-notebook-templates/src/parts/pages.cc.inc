#line 2847 "src/customnotebooktemplates.cc"

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
