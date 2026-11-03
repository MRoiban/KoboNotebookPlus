#pragma once

// Pure helpers for the notebook safety-backup policy (no Qt, host-testable).
//
// Policy, applied by page_io::backupNotebookPath:
//  * A backup is skipped only when the notebook's content fingerprint still
//    equals the one recorded right after this plugin's own last successful
//    edit of that notebook, and the backup taken before that edit is still on
//    disk in the same folder. Ink or any other change since then (including a
//    stock Nickel save) changes the fingerprint, so a fresh backup is made.
//    Restoring the kept backup can therefore only undo structural plugin edits
//    (moves, duplicates, layer add/delete), never handwriting.
//  * After a new backup is written, only the newest kMaxBackupsPerNotebook
//    backups of that notebook file name are kept in that folder.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cnt {
namespace backup_policy {

enum { kMaxBackupsPerNotebook = 10 };

// Largest tail that can contain a ZIP end-of-central-directory record
// (22-byte record plus a comment of at most 65535 bytes).
enum { kEndRecordSearchBytes = 22 + 65535 };

// Upper bound on the central directory we are willing to hash.
enum { kMaxCentralDirectoryBytes = 8 * 1024 * 1024 };

// Locates the central directory from the last bytes of a ZIP file. `tail`
// holds the final `tailSize` bytes of a file of `fileSize` bytes. Returns
// false for anything that is not a plain, single-disk, non-ZIP64 archive whose
// central directory lies inside the file; callers then never skip a backup.
bool centralDirectoryRange(
    unsigned char const* tail,
    std::size_t tailSize,
    std::uint64_t fileSize,
    std::uint64_t* offset,
    std::uint64_t* size);

// Canonical content signature of a central directory: for every entry, its
// name, CRC-32 and uncompressed size, in directory order. Timestamps, extra
// fields and comments are excluded, so a stock save that rewrites unchanged
// pages yields the same signature. Returns false on any malformed entry.
bool centralDirectorySignature(
    unsigned char const* directory,
    std::size_t size,
    std::string* signature);

// True when `backupName` is a backup of `notebookFileName` in the naming
// scheme "yyyyMMdd-HHmmsszzz[-N]-<notebook file name>.backup".
bool isBackupOf(std::string const& backupName, std::string const& notebookFileName);

// Names (from `names`, any order) to delete so that only the newest `keep`
// backups of `notebookFileName` remain. Newest is decided by the timestamp
// prefix and collision suffix. Names in `protectedNames` are never returned
// (they are kept in addition when older than the newest `keep`).
std::vector<std::string> backupsToPrune(
    std::vector<std::string> const& names,
    std::string const& notebookFileName,
    std::size_t keep,
    std::vector<std::string> const& protectedNames);

} // namespace backup_policy
} // namespace cnt
