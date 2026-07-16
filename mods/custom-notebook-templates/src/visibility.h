#ifndef CNT_VISIBILITY_H
#define CNT_VISIBILITY_H

#include <QMutex>

class QString;

namespace cnt {
namespace visibility {

struct RuntimeState {
    QMutex traceMutex;
    bool exclusionObserved = false;
    bool backingFilePreserved = false;
};

bool isCustomAssetContentId(QString const& contentId);
void ensureCustomAssetSyncExclusion(QString& exclusions);
void applyBackingFilePolicy(
    RuntimeState& state,
    bool& removeBackingFile,
    QString const& contentId);
void applySyncExclusion(RuntimeState& state, QString& exclusions);
void hideLegacyNotebookBackups(char const* coverBackupRoot);

} // namespace visibility
} // namespace cnt

#endif
