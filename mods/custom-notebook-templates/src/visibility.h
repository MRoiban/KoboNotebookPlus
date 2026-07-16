#ifndef CNT_VISIBILITY_H
#define CNT_VISIBILITY_H

class QString;

namespace cnt {
namespace visibility {

bool isCustomAssetContentId(QString const& contentId);
void ensureCustomAssetSyncExclusion(QString& exclusions);
void hideLegacyNotebookBackups(char const* coverBackupRoot);

} // namespace visibility
} // namespace cnt

#endif
