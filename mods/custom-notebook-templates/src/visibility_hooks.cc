#include "visibility_hooks.h"

#include "abi_types.h"
#include "firmware_api.h"
#include "firmware_resolver.h"
#include "nickel_pins.h"
#include "plugin_runtime.h"
#include "settings.h"

#include <NickelHook.h>

#include <dlfcn.h>

namespace {

char const kExcludeSyncFoldersSymbol[] =
    "_ZN15FeatureSettings18excludeSyncFoldersEv";
char const kContentGetIdSymbol[] = "_ZNK7Content5getIdEv";
char const kRemoveCommonBookDataSymbol[] =
    "_ZN13VolumeManager20removeCommonBookDataERK6DeviceR6Volumeb";

} // namespace

namespace cnt {
namespace visibility_hooks {

bool install() {
    // This image plugin is loaded by Nickel itself, so libnickel must already
    // be present. Install before waiting for libiinknote: filesystem sync can
    // begin before the notebook UI library is naturally loaded. Excluding a
    // previously indexed directory makes its rows stale; stock Nickel passes
    // removeBackingFile=true while pruning those rows. The preservation hook
    // must therefore validate before the exclusion hook is allowed to run.
    void* const handle = dlopen(
        "libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) {
        trace("asset-visibility: libnickel unavailable; scanner unchanged");
        return false;
    }

    ContentGetId resolvedContentGetId = nullptr;
    RemoveCommonBookData resolvedRemoveCommonBookData = nullptr;
    ExcludeSyncFolders resolvedExcludeSyncFolders = nullptr;
    if (!resolvePinned(handle, kExcludeSyncFoldersSymbol,
            cnt::nickelVma(cnt::NickelPin::FeatureSettingsExcludeSyncFolders), &resolvedExcludeSyncFolders)
            || !resolvePinned(handle, kContentGetIdSymbol,
                cnt::nickelVma(cnt::NickelPin::ContentGetId), &resolvedContentGetId)
            || !resolvePinned(handle, kRemoveCommonBookDataSymbol,
                cnt::nickelVma(cnt::NickelPin::VolumeManagerRemoveCommonBookData), &resolvedRemoveCommonBookData)) {
        trace("asset-visibility: preservation symbols mismatch; scanner unchanged");
        dlclose(handle);
        return false;
    }

    firmwareApi().contentGetId = resolvedContentGetId;
    firmwareApi().removeCommonBookDataOriginal = resolvedRemoveCommonBookData;
    RemoveCommonBookDataAddress preservationReplacement;
    preservationReplacement.function = _cnt_remove_common_book_data_hook;
    void* const originalPreservation = nh_dlhook(
        handle, kRemoveCommonBookDataSymbol, preservationReplacement.pointer);
    if (!pointerMatchesVma(
            originalPreservation, cnt::nickelVma(cnt::NickelPin::VolumeManagerRemoveCommonBookData))) {
        // nh_dlhook returns dlsym(stock), not the displaced GOT entry. If it
        // returned a usable stock address after patching an unexpected image,
        // write that stock target back; otherwise exclusion stays disabled.
        bool restored = false;
        if (originalPreservation) {
            void* const rollbackResult = nh_dlhook(
                handle, kRemoveCommonBookDataSymbol, originalPreservation);
            restored = pointerMatchesVma(
                rollbackResult, cnt::nickelVma(cnt::NickelPin::VolumeManagerRemoveCommonBookData));
        }
        trace(restored
            ? "asset-visibility: preservation hook mismatch; stock target restored"
            : "asset-visibility: preservation hook mismatch; scanner unchanged");
        dlclose(handle);
        return false;
    }
    firmwareApi().removeCommonBookDataOriginal =
        reinterpret_cast<RemoveCommonBookData>(originalPreservation);

    // Set the pass-through before mutating the second GOT entry. Even if
    // validation rejects it, the wrapper preserves the complete stock/user
    // setting rather than replacing it.
    firmwareApi().excludeSyncFoldersOriginal = resolvedExcludeSyncFolders;
    ExcludeSyncFoldersAddress exclusionReplacement;
    exclusionReplacement.function = _cnt_exclude_sync_folders_hook;
    void* const originalExclusion = nh_dlhook(
        handle, kExcludeSyncFoldersSymbol, exclusionReplacement.pointer);
    if (!pointerMatchesVma(originalExclusion, cnt::nickelVma(cnt::NickelPin::FeatureSettingsExcludeSyncFolders))) {
        bool exclusionRestored = false;
        if (originalExclusion) {
            void* const rollbackResult = nh_dlhook(
                handle, kExcludeSyncFoldersSymbol, originalExclusion);
            exclusionRestored = pointerMatchesVma(
                rollbackResult, cnt::nickelVma(cnt::NickelPin::FeatureSettingsExcludeSyncFolders));
        }
        void* const preservationRollback = nh_dlhook(
            handle, kRemoveCommonBookDataSymbol, originalPreservation);
        bool const preservationRestored =
            pointerMatchesVma(
                preservationRollback, cnt::nickelVma(cnt::NickelPin::VolumeManagerRemoveCommonBookData));
        trace(exclusionRestored && preservationRestored
            ? "asset-visibility: exclusion mismatch; both stock targets restored"
            : "asset-visibility: exclusion mismatch; scanner unchanged, rollback unverified");
        dlclose(handle);
        return false;
    }
    firmwareApi().excludeSyncFoldersOriginal =
        reinterpret_cast<ExcludeSyncFolders>(originalExclusion);

    dlclose(handle);
    trace("asset-visibility: backing-file guard and scanner exclusion installed");
    return true;
}

} // namespace visibility_hooks
} // namespace cnt
