#pragma once

class QString;

namespace cnt {
namespace page_io {

bool backupNotebookPath(
    QString const& sourcePath,
    QString const& backupRoot,
    QString const& operation,
    QString* backupPath,
    QString* error);

} // namespace page_io
} // namespace cnt
