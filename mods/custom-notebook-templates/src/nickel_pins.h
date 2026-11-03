#pragma once

#include <cstdint>

namespace cnt {

// Every libnickel.so.1.0.0 function the plugin resolves, calls, or hooks.
//
// libnickel is the only plugin-relevant firmware image whose layout differs
// between the audited firmwares: libiink.so, libiinkuiref.so.1.0.0, and iink
// are byte-identical in 4.38.23697 and 4.38.23852, and libiinknote.so differs
// only inside one embedded QSS resource (no code, symbol, or relocation
// change). Each libnickel function listed here was compared instruction by
// instruction across both images (scripts/verify-layer-abi.py pins the exact
// facts per profile); only their addresses moved.
enum class NickelPin : unsigned {
    NickelTouchMenuConstructor,
    NickelTouchMenuSetAlignment,
    NickelTouchMenuPopupPosition,
    TouchMenuSetCustomPopupPositionOffset,
    AbstractNickelMenuControllerPopupFromWidget,
    AbstractMenuControllerAddWidgetAction,
    AbstractMenuControllerGrabTapGesture,
    AbstractMenuControllerTapGesture,
    DialogSetTitle,
    ParserImageParsed,
    ContentGetId,
    ContentGetImageId,
    PixmapViewSetImage,
    VolumePixmapViewLoadCover,
    FeatureSettingsExcludeSyncFolders,
    VolumeManagerRemoveCommonBookData,
    PowerWorkflowShowSleepView,
    PowerWorkflowShowPowerOffView,
    PowerViewOnCoverLoaded,
    PowerViewUpdateCover,
    PowerViewUpdateReadingStatus,
    DragonPowerViewSetInfoPanelVisible,
    SettingsPowerViewConstructor,
    CheckBoxConstructor,
    DropDownConstructor,
    CheckBoxSetText,
    CheckBoxSetChecked,
    ItemSetLabel,
    DropDownDropDown,
    DropDownAddItem,
    DropDownSetCurrentIndex,
    Count,
};

// Selects the audited libnickel profile in which *every* pin above resolves
// to its exact ELF-relative VMA. Must run once at init, before any libnickel
// symbol is resolved or hooked. When no profile matches, nickelVma() returns
// 0 so every pinned resolution and hook validation fails closed.
bool detectNickelProfile();
uintptr_t nickelVma(NickelPin pin);
char const* nickelFirmware();

} // namespace cnt
