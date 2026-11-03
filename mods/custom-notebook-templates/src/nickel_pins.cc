#include "nickel_pins.h"

#include "plugin_runtime.h"
#include "plugin_state.h"

#include <NickelHook.h>

#include <dlfcn.h>

namespace {

enum : unsigned { kProfileCount = 2 };

char const* const kProfileFirmware[kProfileCount] = {
    "4.38.23697",
    "4.38.23852",
};

struct PinSpec {
    char const* symbol;
    uintptr_t vma[kProfileCount];
};

// ELF-relative VMAs (Binary Ninja address minus its 0x10000 base), ordered
// exactly as cnt::NickelPin. Columns follow kProfileFirmware.
PinSpec const kPins[] = {
    { "_ZN15NickelTouchMenuC1EP7QWidget18DecorationPosition",
        { 0xed6788, 0xedd790 } },
    { "_ZN15NickelTouchMenu12setAlignmentE6QFlagsIN2Qt13AlignmentFlagEE",
        { 0xed6a68, 0xedda70 } },
    { "_ZN15NickelTouchMenu13popupPositionEP7QWidget",
        { 0xed7248, 0xede250 } },
    { "_ZN9TouchMenu28setCustomPopupPositionOffsetERK6QPoint",
        { 0x11150a8, 0x111f848 } },
    { "_ZNK28AbstractNickelMenuController15popupFromWidgetEv",
        { 0xeb4f94, 0xebc004 } },
    { "_ZN22AbstractMenuController15addWidgetActionEP5QMenuP7QWidgetP7QObjectPKcbbb",
        { 0xb3ce80, 0xb427a0 } },
    { "_ZN22AbstractMenuController14grabTapGestureEP15GestureReceiver",
        { 0xb3cfac, 0xb428cc } },
    { "_ZN22AbstractMenuController10tapGestureEP15GestureReceiverP10TapGesture",
        { 0xb3d00c, 0xb4292c } },
    { "_ZN8N3Dialog8setTitleERK7QString",
        { 0x10e4168, 0x10ee908 } },
    { "_ZN15ParserInterface11imageParsedERK6VolumeRK6QImage",
        { 0x118f714, 0x119a08c } },
    { "_ZNK7Content5getIdEv",
        { 0x953d84, 0x958d64 } },
    { "_ZNK7Content10getImageIdEv",
        { 0x957628, 0x95c608 } },
    { "_ZN10PixmapView8setImageERK6QImageRK7QString",
        { 0x10fa084, 0x1104824 } },
    { "_ZN16VolumePixmapView9loadCoverEv",
        { 0xc73388, 0xc78c00 } },
    { "_ZN15FeatureSettings18excludeSyncFoldersEv",
        { 0xa04650, 0xa09db0 } },
    { "_ZN13VolumeManager20removeCommonBookDataERK6DeviceR6Volumeb",
        { 0xa6ec74, 0xa7455c } },
    { "_ZN22N3PowerWorkflowManager13showSleepViewEv",
        { 0xf3eb98, 0xf45b78 } },
    { "_ZN22N3PowerWorkflowManager16showPowerOffViewEv",
        { 0xf3ea54, 0xf45a34 } },
    { "_ZN19PowerViewController13onCoverLoadedERK6QImage",
        { 0xf425b4, 0xf49594 } },
    { "_ZN19PowerViewController11updateCoverEv",
        { 0xf430b0, 0xf4a090 } },
    { "_ZN19PowerViewController19updateReadingStatusEv",
        { 0xf42798, 0xf49778 } },
    { "_ZN25FullScreenDragonPowerView19setInfoPanelVisibleEb",
        { 0xf3a114, 0xf410f4 } },
    { "_ZN19N3SettingsPowerViewC1EP7QWidget",
        { 0x10455dc, 0x104a8dc } },
    { "_ZN23SettingItemWithCheckBoxC1EP7QWidget",
        { 0x1065824, 0x106ae5c } },
    { "_ZN23SettingItemWithDropDownC1EP7QWidget",
        { 0x106599c, 0x106afd4 } },
    { "_ZN23SettingItemWithCheckBox7setTextERK7QString",
        { 0x10653a0, 0x106a9d8 } },
    { "_ZN23SettingItemWithCheckBox10setCheckedEb",
        { 0x10653c0, 0x106a9f8 } },
    { "_ZN15SettingItemBase8setLabelERK7QString",
        { 0x10652c0, 0x106a8f8 } },
    { "_ZNK23SettingItemWithDropDown8dropDownEv",
        { 0x10653f8, 0x106aa30 } },
    { "_ZN24MultiSelectTouchDropDown7addItemERK7QStringRK8QVariantRK7QLocaleb",
        { 0x10de390, 0x10e8b30 } },
    { "_ZN13TouchDropDown15setCurrentIndexEi",
        { 0x1113230, 0x111d9d0 } },
};

static_assert(sizeof(kPins) / sizeof(kPins[0])
        == static_cast<unsigned>(cnt::NickelPin::Count),
    "every NickelPin needs exactly one audited row");

} // namespace

namespace cnt {

bool detectNickelProfile() {
    pluginState().nickelProfile = -1;
    void* const handle = dlopen("libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) {
        nh_log("libnickel unavailable; libnickel-dependent features disabled");
        return false;
    }

    uintptr_t actual[sizeof(kPins) / sizeof(kPins[0])] = {};
    bool resolved = true;
    for (unsigned pin = 0; pin < sizeof(kPins) / sizeof(kPins[0]); ++pin) {
        void* const symbol = dlsym(handle, kPins[pin].symbol);
        Dl_info image = {};
        if (!symbol || !dladdr(symbol, &image) || !image.dli_fbase) {
            nh_log("libnickel pin missing: %s", kPins[pin].symbol);
            resolved = false;
            break;
        }
        actual[pin] = (reinterpret_cast<uintptr_t>(symbol) & ~uintptr_t(1))
            - reinterpret_cast<uintptr_t>(image.dli_fbase);
    }
    dlclose(handle);

    for (unsigned profile = 0; resolved && profile < kProfileCount; ++profile) {
        bool matches = true;
        for (unsigned pin = 0; matches && pin < sizeof(kPins) / sizeof(kPins[0]); ++pin)
            matches = actual[pin] == kPins[pin].vma[profile];
        if (matches) {
            pluginState().nickelProfile = static_cast<int>(profile);
            nh_log("libnickel firmware %s profile verified (%u pins)",
                kProfileFirmware[profile],
                static_cast<unsigned>(sizeof(kPins) / sizeof(kPins[0])));
            return true;
        }
    }
    if (resolved) {
        nh_log("libnickel matches no audited profile (Content::getId at 0x%lx); "
            "libnickel-dependent features disabled",
            static_cast<unsigned long>(
                actual[static_cast<unsigned>(NickelPin::ContentGetId)]));
    }
    return false;
}

uintptr_t nickelVma(NickelPin pin) {
    int const profile = pluginState().nickelProfile;
    unsigned const index = static_cast<unsigned>(pin);
    if (profile < 0 || profile >= static_cast<int>(kProfileCount)
            || index >= sizeof(kPins) / sizeof(kPins[0]))
        return 0;
    return kPins[index].vma[profile];
}

char const* nickelFirmware() {
    int const profile = pluginState().nickelProfile;
    if (profile < 0 || profile >= static_cast<int>(kProfileCount))
        return nullptr;
    return kProfileFirmware[profile];
}

} // namespace cnt
