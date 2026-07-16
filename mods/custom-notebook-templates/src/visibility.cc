#include "visibility.h"

#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

} // namespace

namespace cnt {
namespace visibility {

bool isCustomAssetContentId(QString const& contentId) {
    return contentId.startsWith(QLatin1String(kCustomAssetOnboardContentPrefix))
        || contentId.startsWith(QLatin1String(
            kCustomAssetOnboardExternalContentPrefix))
        || contentId.startsWith(QLatin1String(kCustomAssetSdContentPrefix))
        || contentId.startsWith(QLatin1String(
            kCustomAssetSdExternalContentPrefix));
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
