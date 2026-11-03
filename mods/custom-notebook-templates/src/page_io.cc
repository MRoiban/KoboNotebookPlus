#include "page_io.h"

#include "backup_policy.h"
#include "perf_trace.h"
#include "plugin_runtime.h"
#include "plugin_state.h"
#include "settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace cnt {
namespace page_io {

namespace {

QString notebookKey(QString const& path) {
    QString const canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}

BackupMemo& memoFor(QString const& path) {
    return pluginState().backups.notebooks[notebookKey(path)];
}

// Keeps the newest backups of this notebook in `backupRoot`; never touches
// the backup just written. Failures only leave extra files behind.
void pruneBackups(QString const& backupRoot, QString const& notebookFileName,
        QString const& keepPath) {
    perf::PhaseScope phase("backup-prune");
    QStringList const entries = QDir(backupRoot).entryList(
        QDir::Files | QDir::NoSymLinks | QDir::Hidden);
    std::vector<std::string> names;
    names.reserve(entries.size());
    foreach (QString const& entry, entries)
        names.push_back(entry.toUtf8().constData());
    std::vector<std::string> protectedNames;
    protectedNames.push_back(QFileInfo(keepPath).fileName().toUtf8().constData());
    std::vector<std::string> const doomed = backup_policy::backupsToPrune(
        names,
        notebookFileName.toUtf8().constData(),
        backup_policy::kMaxBackupsPerNotebook,
        protectedNames);
    int removed = 0;
    for (std::size_t i = 0; i < doomed.size(); ++i) {
        if (QFile::remove(QDir(backupRoot).filePath(
                QString::fromUtf8(doomed[i].c_str()))))
            ++removed;
    }
    if (removed) {
        perf::note("backups-pruned", removed);
        trace(QLatin1String("notebook safety backups pruned count=")
            + QString::number(removed));
    }
}

} // namespace

QByteArray notebookFingerprint(QString const& notebookPath) {
    perf::PhaseScope phase("backup-fingerprint");
    QFile file(notebookPath);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    qint64 const fileSize = file.size();
    if (fileSize < 22)
        return QByteArray();
    qint64 const tailSize = qMin<qint64>(
        fileSize, backup_policy::kEndRecordSearchBytes);
    if (!file.seek(fileSize - tailSize))
        return QByteArray();
    QByteArray const tail = file.read(tailSize);
    if (tail.size() != tailSize)
        return QByteArray();
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    if (!backup_policy::centralDirectoryRange(
            reinterpret_cast<unsigned char const*>(tail.constData()),
            static_cast<std::size_t>(tail.size()),
            static_cast<std::uint64_t>(fileSize),
            &offset,
            &size)
            || !file.seek(static_cast<qint64>(offset)))
        return QByteArray();
    QByteArray const directory = file.read(static_cast<qint64>(size));
    std::string signature;
    if (directory.size() != static_cast<int>(size)
            || !backup_policy::centralDirectorySignature(
                reinterpret_cast<unsigned char const*>(directory.constData()),
                static_cast<std::size_t>(directory.size()),
                &signature))
        return QByteArray();
    return QCryptographicHash::hash(
        QByteArray(signature.data(), static_cast<int>(signature.size())),
        QCryptographicHash::Sha1);
}

void notebookCommitted(QString const& notebookPath, QString const& backupPath) {
    if (backupPath.isEmpty())
        return;
    try {
        BackupMemo& memo = memoFor(notebookPath);
        if (memo.backupPath != backupPath)
            return;
        memo.committedFingerprint = notebookFingerprint(notebookPath);
    } catch (...) {
        // Without a fingerprint the next edit simply makes a full backup.
    }
}

bool backupNotebookPath(
        QString const& sourcePath,
        QString const& backupRoot,
        QString const& operation,
        QString* backupPath,
        QString* error) {
    perf::PhaseScope phase("backup-prepare");
    QFileInfo const source(sourcePath);
    if (!sourcePath.startsWith(QLatin1String("/mnt/onboard/"))
            || !sourcePath.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive)
            || !source.isFile()
            || source.size() <= 0) {
        *error = operation
            + QLatin1String(": notebook path could not be verified.");
        return false;
    }

    perf::note("notebook-bytes", source.size());

    // Reuse the previous backup only when nothing but this plugin's own last
    // successful edit has changed the notebook since it was taken.
    QByteArray fingerprint;
    try {
        BackupMemo& memo = memoFor(sourcePath);
        QByteArray const committed = memo.committedFingerprint;
        memo.committedFingerprint.clear();
        if (!committed.isEmpty() && memo.root == backupRoot
                && QFileInfo(memo.backupPath).isFile()) {
            fingerprint = notebookFingerprint(sourcePath);
            if (fingerprint == committed) {
                *backupPath = memo.backupPath;
                perf::note("backup-reused", 1);
                trace("notebook safety backup reused: unchanged since last plugin edit");
                return true;
            }
        }
    } catch (...) {
        // Fall through to a full backup.
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
    {
        perf::PhaseScope copyPhase("backup-copy");
        if (!QFile::copy(sourcePath, temporary)) {
            QFile::remove(temporary);
            *error = operation + QLatin1String(": notebook backup failed.");
            return false;
        }
    }

    {
        perf::PhaseScope syncPhase("backup-sync");
        int const backupFd = open(qPrintable(temporary), O_RDONLY);
        bool const backupSynced = backupFd >= 0 && fsync(backupFd) == 0;
        if (backupFd >= 0)
            close(backupFd);
        if (!backupSynced || !QFile::rename(temporary, destination)) {
            QFile::remove(temporary);
            *error = operation + QLatin1String(": notebook backup failed.");
            return false;
        }
    }

    *backupPath = destination;
    trace("notebook safety backup complete");
    try {
        BackupMemo& memo = memoFor(sourcePath);
        memo.root = backupRoot;
        memo.backupPath = destination;
        memo.committedFingerprint.clear();
        pruneBackups(backupRoot, source.fileName(), destination);
    } catch (...) {
        // The backup itself is complete; bookkeeping is best effort.
    }
    return true;
}

} // namespace page_io
} // namespace cnt
