#pragma once

#include "abi_types.h"
#include "templates.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace cnt {
namespace cover_cache {

// One archive-scan verdict per canonical onboard .nebo path. Negative results
// (no custom cover, or an ambiguous marker count) are cached too, so uncovered
// notebooks stop paying the ZIP scan on every tile reload. Entries are reused
// only while the notebook's modification time and size are both unchanged;
// busy or unreadable archives are never cached.
struct CoverScanEntry {
    qint64 modifiedMs;
    qint64 size;
    QString type;
    bool hasCover;
};

// Kobo's rendered first-page preview and, for plugin covers, the composed
// cover plus live ink. The parser callback populates this cache; notebook
// cards and the notebook sleep screen can both reuse it.
struct RenderedCoverEntry {
    QImage image;
    qint64 notebookModifiedMs;
    qint64 notebookSize;
    QString coverType;
    quint64 sequence;
};

// Clean source covers are the immediate notebook-card fallback until Kobo's
// thumbnail service supplies an ink render. Revalidate against the source PNG
// so replacing a cover file never leaves a stale decoded image in memory.
struct CleanCoverEntry {
    QImage image;
    qint64 pngModifiedMs;
    qint64 pngSize;
    quint64 sequence;
};

// Process-lifetime cover state is owned by PluginState. Keeping libzip's
// dynamically resolved function table beside the caches makes every operation
// explicit about the state it can observe or mutate; this translation unit
// itself owns no mutable namespace storage.
struct State {
    struct ZipApi {
        ZipOpen open = nullptr;
        ZipGetNumEntries getNumEntries = nullptr;
        ZipGetName getName = nullptr;
        ZipFopen fopen = nullptr;
        ZipFread fread = nullptr;
        ZipFclose fclose = nullptr;
        ZipDiscard discard = nullptr;
        void* libraryHandle = nullptr;
    } zip;

    QVector<templates::CustomTemplate> customCovers;
    QHash<QString, CoverScanEntry> scanCache;
    QHash<QString, RenderedCoverEntry> renderedCache;
    quint64 renderedSequence = 0;
    QHash<QString, CleanCoverEntry> cleanCache;
    quint64 cleanSequence = 0;
    QMutex cacheMutex;
    bool pickerPending = false;
    bool titlePending = false;
    bool parserHookReady = false;
    bool hooksReady = false;
    bool gridHookReady = false;
};

bool loadZipApis(State& state);
bool zipApisReady(State const& state);
int notebookArchiveContainsLayerId(
    State const& state,
    QString const& notebookPath,
    QString const& partId,
    QString const& layerId);
int countNotebookPageEntries(State const& state, QString const& notebookPath);
bool openNotebookHasPluginCover(
    State const& state,
    QString const& notebookPath,
    bool* hasCover);
bool cachedNotebookCoverType(
    State& state,
    QString const& notebookPath,
    QString* coverType,
    double pathResolveMs);
void invalidateNotebookScanEntry(State& state, QString const& notebookPath);
QImage composeCoverWithRenderedInk(
    State const& state,
    QString const& type,
    QImage const& renderedInk);
QImage cleanCustomCoverImage(State& state, QString const& type);
void cacheRenderedCoverImage(
    State& state,
    QString const& notebookPath,
    QString const& coverType,
    QImage const& image);
QImage cachedRenderedCoverImage(
    State& state,
    QString const& notebookPath,
    QString const& coverType);
QString notebookPathFromContentId(QString const& contentId);

} // namespace cover_cache
} // namespace cnt
