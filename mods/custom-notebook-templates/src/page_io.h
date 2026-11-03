#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

namespace cnt {
namespace page_io {

// Per-notebook memory of the newest safety backup and of the notebook content
// right after this plugin's last successful edit (see backup_policy.h).
struct BackupMemo {
    QString root;
    QString backupPath;
    QByteArray committedFingerprint;
};

struct RuntimeState {
    QHash<QString, BackupMemo> notebooks;
};

// Makes (or, when provably redundant, reuses) a pre-edit safety backup.
// On success *backupPath names the backup that covers the current content.
bool backupNotebookPath(
    QString const& sourcePath,
    QString const& backupRoot,
    QString const& operation,
    QString* backupPath,
    QString* error);

// Call after an edit has been fully saved, with the backup path that
// backupNotebookPath returned for that edit. Records the notebook's content
// fingerprint so that the next edit can reuse that backup while nothing else
// has changed the notebook. A different or empty backup path is ignored.
void notebookCommitted(QString const& notebookPath, QString const& backupPath);

// Name/CRC/size digest of the archive's central directory, or empty when the
// file cannot be read or is not a plain ZIP archive. Exposed for tracing.
QByteArray notebookFingerprint(QString const& notebookPath);

} // namespace page_io
} // namespace cnt
