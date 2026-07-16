#include "visibility.h"

#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace cnt {
namespace visibility {

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
