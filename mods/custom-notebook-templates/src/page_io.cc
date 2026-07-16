#include "page_io.h"

#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace cnt {
namespace page_io {

bool backupNotebookPath(
        QString const& sourcePath,
        QString const& backupRoot,
        QString const& operation,
        QString* backupPath,
        QString* error) {
    QFileInfo const source(sourcePath);
    if (!sourcePath.startsWith(QLatin1String("/mnt/onboard/"))
            || !sourcePath.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive)
            || !source.isFile()
            || source.size() <= 0) {
        *error = operation
            + QLatin1String(": notebook path could not be verified.");
        return false;
    }

    QDir root;
    if (!root.mkpath(backupRoot)) {
        *error = operation
            + QLatin1String(": backup folder could not be created.");
        return false;
    }

    struct statvfs storage = {};
    if (statvfs(qPrintable(backupRoot), &storage) != 0) {
        *error = operation
            + QLatin1String(": free space could not be checked.");
        return false;
    }

    quint64 const available = static_cast<quint64>(storage.f_bavail)
        * static_cast<quint64>(storage.f_frsize);
    quint64 const required = static_cast<quint64>(source.size())
        + UINT64_C(16) * 1024 * 1024;
    if (available < required) {
        *error = operation
            + QLatin1String(": not enough space for a safety backup.");
        return false;
    }

    QString const stamp = QDateTime::currentDateTimeUtc()
        .toString(QLatin1String("yyyyMMdd-HHmmsszzz"));
    QString destination = backupRoot
        + stamp + QLatin1Char('-') + source.fileName()
        + QLatin1String(".backup");
    for (int suffix = 1; QFileInfo(destination).exists(); ++suffix) {
        destination = backupRoot
            + stamp + QLatin1Char('-') + QString::number(suffix)
            + QLatin1Char('-') + source.fileName()
            + QLatin1String(".backup");
    }

    QString const temporary = destination + QLatin1String(".tmp");
    QFile::remove(temporary);
    if (!QFile::copy(sourcePath, temporary)) {
        QFile::remove(temporary);
        *error = operation + QLatin1String(": notebook backup failed.");
        return false;
    }

    int const backupFd = open(qPrintable(temporary), O_RDONLY);
    bool const backupSynced = backupFd >= 0 && fsync(backupFd) == 0;
    if (backupFd >= 0)
        close(backupFd);
    if (!backupSynced || !QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        *error = operation + QLatin1String(": notebook backup failed.");
        return false;
    }

    *backupPath = destination;
    trace("notebook safety backup complete");
    return true;
}

} // namespace page_io
} // namespace cnt
