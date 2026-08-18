#include "visibility.h"

#include "settings.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QString>
#include <QStringList>

namespace {

// SyncFileSystemCommand anchors each semicolon-separated expression beneath
// /mnt/(sd|onboard). Hide the plugin's entire support tree from content import
// while preserving its existing paths for direct QFile/MyScript access.
char const kCustomAssetExcludePattern[] =
    "(\\.kobo|koboExtStorage)/custom";
char const kCustomAssetOnboardContentPrefix[] =
    "file:///mnt/onboard/.kobo/custom/";
char const kCustomAssetOnboardExternalContentPrefix[] =
    "file:///mnt/onboard/koboExtStorage/custom/";
char const kCustomAssetSdContentPrefix[] =
    "file:///mnt/sd/.kobo/custom/";
char const kCustomAssetSdExternalContentPrefix[] =
    "file:///mnt/sd/koboExtStorage/custom/";
char const kCanonicalAssetOnboardContentPrefix[] =
    "file:///mnt/onboard/.kobo/iink/custom-notebook-templates/";
char const kCanonicalAssetOnboardExternalContentPrefix[] =
    "file:///mnt/onboard/koboExtStorage/iink/custom-notebook-templates/";
char const kCanonicalAssetSdContentPrefix[] =
    "file:///mnt/sd/.kobo/iink/custom-notebook-templates/";
char const kCanonicalAssetSdExternalContentPrefix[] =
    "file:///mnt/sd/koboExtStorage/iink/custom-notebook-templates/";
char const kLegacySupportRoot[] = "/mnt/onboard/.kobo/custom";
char const kCanonicalSupportRoot[] =
    "/mnt/onboard/.kobo/iink/custom-notebook-templates";
char const kCanonicalTemplateRoot[] =
    "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates";
char const kLegacyPathPrefix[] = "/mnt/onboard/.kobo/custom/";
char const kCanonicalPathPrefix[] =
    "/mnt/onboard/.kobo/iink/custom-notebook-templates/";

bool filesEqual(QString const& firstPath, QString const& secondPath) {
    QFileInfo const firstInfo(firstPath);
    QFileInfo const secondInfo(secondPath);
    if (!firstInfo.isFile() || !secondInfo.isFile()
            || firstInfo.size() != secondInfo.size()) {
        return false;
    }

    QFile first(firstPath);
    QFile second(secondPath);
    if (!first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly))
        return false;

    while (!first.atEnd()) {
        if (first.read(64 * 1024) != second.read(64 * 1024))
            return false;
    }
    return second.atEnd();
}

QString conflictPath(QString const& destinationPath) {
    for (int suffix = 1; suffix < 1000; ++suffix) {
        QString const candidate = destinationPath
            + QLatin1String(".legacy-conflict-")
            + QString::number(suffix);
        if (!QFileInfo(candidate).exists())
            return candidate;
    }
    return QString();
}

bool mergeDirectoryTree(
        QString const& sourcePath,
        QString const& destinationPath) {
    QDir root;
    if (!root.mkpath(destinationPath))
        return false;

    QDir source(sourcePath);
    QFileInfoList const entries = source.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot
            | QDir::Hidden | QDir::System,
        QDir::Name | QDir::DirsFirst);
    bool complete = true;
    for (int i = 0; i < entries.size(); ++i) {
        QFileInfo const& info = entries.at(i);
        QString const destination = QDir(destinationPath).filePath(
            info.fileName());
        if (info.isDir() && !info.isSymLink()) {
            if (!mergeDirectoryTree(info.absoluteFilePath(), destination))
                complete = false;
            continue;
        }

        if (!QFileInfo(destination).exists()) {
            if (!QFile::rename(info.absoluteFilePath(), destination))
                complete = false;
            continue;
        }
        if (filesEqual(info.absoluteFilePath(), destination)) {
            if (!QFile::remove(info.absoluteFilePath()))
                complete = false;
            continue;
        }

        QString const preserved = conflictPath(destination);
        if (preserved.isEmpty()
                || !QFile::rename(info.absoluteFilePath(), preserved)) {
            complete = false;
        }
    }

    QDir refreshed(sourcePath);
    bool const empty = refreshed.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot
            | QDir::Hidden | QDir::System).isEmpty();
    if (!empty || !root.rmdir(sourcePath))
        complete = false;
    return complete;
}

bool rewriteLegacyManifestPaths() {
    QString const manifestPath = QDir(
        QLatin1String(kCanonicalTemplateRoot)).filePath(
            QLatin1String("templates.json"));
    QFile source(manifestPath);
    QFileInfo const info(source);
    if (!info.exists())
        return true;
    if (!info.isFile() || info.size() < 1 || info.size() > 1024 * 1024
            || !source.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray contents = source.readAll();
    source.close();
    QByteArray const legacy(kLegacyPathPrefix);
    if (!contents.contains(legacy))
        return true;
    contents.replace(legacy, QByteArray(kCanonicalPathPrefix));

    QSaveFile destination(manifestPath);
    return destination.open(QIODevice::WriteOnly)
        && destination.write(contents) == contents.size()
        && destination.commit();
}

} // namespace

namespace cnt {
namespace visibility {

bool isCustomAssetContentId(QString const& contentId) {
    return contentId.startsWith(QLatin1String(kCustomAssetOnboardContentPrefix))
        || contentId.startsWith(QLatin1String(
            kCustomAssetOnboardExternalContentPrefix))
        || contentId.startsWith(QLatin1String(kCustomAssetSdContentPrefix))
        || contentId.startsWith(QLatin1String(
            kCustomAssetSdExternalContentPrefix))
        || contentId.startsWith(QLatin1String(
            kCanonicalAssetOnboardContentPrefix))
        || contentId.startsWith(QLatin1String(
            kCanonicalAssetOnboardExternalContentPrefix))
        || contentId.startsWith(QLatin1String(
            kCanonicalAssetSdContentPrefix))
        || contentId.startsWith(QLatin1String(
            kCanonicalAssetSdExternalContentPrefix));
}

void ensureCustomAssetSyncExclusion(QString& exclusions) {
    QString const pattern = QLatin1String(kCustomAssetExcludePattern);
    QStringList const entries = exclusions.split(
        QLatin1Char(';'), QString::SkipEmptyParts);
    if (!entries.contains(pattern)) {
        if (!exclusions.isEmpty() && !exclusions.endsWith(QLatin1Char(';')))
            exclusions.append(QLatin1Char(';'));
        exclusions.append(pattern);
    }
}

void applyBackingFilePolicy(
        RuntimeState& state,
        bool& removeBackingFile,
        QString const& contentId) {
    if (!isCustomAssetContentId(contentId))
        return;

    removeBackingFile = false;

    bool shouldTrace = false;
    {
        QMutexLocker locker(&state.traceMutex);
        if (!state.backingFilePreserved) {
            state.backingFilePreserved = true;
            shouldTrace = true;
        }
    }
    if (shouldTrace) {
        trace("asset-visibility: stale support row removed; backing file preserved");
    }
}

void applySyncExclusion(RuntimeState& state, QString& exclusions) {
    ensureCustomAssetSyncExclusion(exclusions);

    // One observation proves that the live scanner reached the interposed
    // PLT/GOT seam. Avoid logging every recursive directory visit.
    bool shouldTrace = false;
    {
        QMutexLocker locker(&state.traceMutex);
        if (!state.exclusionObserved) {
            state.exclusionObserved = true;
            shouldTrace = true;
        }
    }
    if (shouldTrace) {
        trace("asset-visibility: .kobo/custom excluded from live library scan");
    }
}

bool migrateLegacySupportTree() {
    QString const legacyRoot = QLatin1String(kLegacySupportRoot);
    QString const canonicalRoot = QLatin1String(kCanonicalSupportRoot);
    QDir root;
    bool migrated = true;

    if (QFileInfo(legacyRoot).isDir()) {
        if (!root.mkpath(QFileInfo(canonicalRoot).absolutePath())) {
            migrated = false;
        } else if (!QFileInfo(canonicalRoot).exists()) {
            migrated = root.rename(legacyRoot, canonicalRoot);
        } else {
            migrated = mergeDirectoryTree(legacyRoot, canonicalRoot);
        }
    }

    bool const templatesReady =
        root.mkpath(QLatin1String(kCanonicalTemplateRoot));
    bool const manifestRewritten = rewriteLegacyManifestPaths();
    bool const complete = migrated && templatesReady && manifestRewritten;
    trace(complete
        ? "asset-visibility: support tree is under stock-excluded .kobo/iink"
        : "asset-visibility: support-tree migration incomplete");
    return complete;
}

void hideLegacyNotebookBackups(char const* coverBackupRoot) {
    QDir directory = QDir(QLatin1String(coverBackupRoot));
    QFileInfoList const files = directory.entryInfoList(
        QDir::Files | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);
    for (int i = 0; i < files.size(); ++i) {
        QFileInfo const& info = files.at(i);
        if (!info.fileName().endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive))
            continue;
        QString const hiddenPath = info.absoluteFilePath()
            + QLatin1String(".backup");
        if (QFileInfo(hiddenPath).exists())
            continue;
        if (QFile::rename(info.absoluteFilePath(), hiddenPath))
            trace("covers: legacy notebook backup hidden from library scanner");
    }
}

} // namespace visibility
} // namespace cnt
