#pragma once

#include <QImage>
#include <QPointer>
#include <QString>
#include <QtGlobal>

class QLocale;
class QVariant;
class QWidget;

namespace cnt {
namespace notebook_sleep {

typedef void (*PowerWorkflowView)(void* manager);
typedef void (*PowerCoverLoaded)(void* controller, QImage const& image);
typedef void (*PowerUpdateCover)(void* controller);
typedef void (*PowerUpdateReadingStatus)(void* controller);
typedef void (*PowerSetInfoPanelVisible)(void* view, bool visible);
typedef void (*SettingsPowerViewConstructor)(void* view, QWidget* parent);
typedef void (*SettingCheckBoxConstructor)(void* item, QWidget* parent);
typedef void (*SettingDropDownConstructor)(void* item, QWidget* parent);
typedef void (*SettingCheckBoxSetText)(void* item, QString const& text);
typedef void (*SettingCheckBoxSetChecked)(void* item, bool checked);
typedef void (*SettingItemSetLabel)(void* item, QString const& label);
typedef void* (*SettingDropDown)(void const* item);
typedef void (*DropDownAddItem)(
    void* dropDown,
    QString const& text,
    QVariant const& data,
    QLocale const& locale,
    bool selected);
typedef void (*DropDownSetCurrentIndex)(void* dropDown, int index);

struct NativeApi {
    PowerWorkflowView showSleepViewOriginal = nullptr;
    PowerWorkflowView showPowerOffViewOriginal = nullptr;
    PowerCoverLoaded onCoverLoadedOriginal = nullptr;
    PowerUpdateCover updateCoverOriginal = nullptr;
    PowerUpdateReadingStatus updateReadingStatusOriginal = nullptr;
    PowerSetInfoPanelVisible setInfoPanelVisibleOriginal = nullptr;
    SettingsPowerViewConstructor settingsPowerViewConstructorOriginal = nullptr;
    SettingCheckBoxConstructor checkBoxConstructor = nullptr;
    SettingDropDownConstructor dropDownConstructor = nullptr;
    SettingCheckBoxSetText checkBoxSetText = nullptr;
    SettingCheckBoxSetChecked checkBoxSetChecked = nullptr;
    SettingItemSetLabel itemSetLabel = nullptr;
    SettingDropDown dropDown = nullptr;
    DropDownAddItem dropDownAddItem = nullptr;
    DropDownSetCurrentIndex dropDownSetCurrentIndex = nullptr;
};

struct RuntimeState {
    NativeApi api;
    QPointer<QWidget> activeNotebook;
    QImage pendingImage;
    QString cachedBackgroundType;
    QString cachedBackgroundPath;
    QImage cachedBackgroundImage;
    qint64 cachedBackgroundModifiedMs = -1;
    qint64 cachedBackgroundSize = -1;
    bool pendingNotebookImage = false;
    bool coreHooksReady = false;
    bool settingsHookReady = false;
    bool renderHookReady = false;
    bool extentTraceRecorded = false;
};

bool installHooks(RuntimeState& state);
void observeNotebookWidget(RuntimeState& state, QWidget* widget);
void prepareNotebookImage(RuntimeState& state);
void augmentPowerSettings(RuntimeState& state, QWidget* view);

} // namespace notebook_sleep
} // namespace cnt

extern "C" __attribute__((visibility("default")))
void _cnt_show_sleep_view_hook(void* manager);

extern "C" __attribute__((visibility("default")))
void _cnt_show_power_off_view_hook(void* manager);

extern "C" __attribute__((visibility("default")))
void _cnt_power_cover_loaded_hook(void* controller, QImage const& image);

extern "C" __attribute__((visibility("default")))
void _cnt_power_update_cover_hook(void* controller);

extern "C" __attribute__((visibility("default")))
void _cnt_power_update_reading_status_hook(void* controller);

extern "C" __attribute__((visibility("default")))
void _cnt_power_set_info_panel_visible_hook(void* view, bool visible);

extern "C" __attribute__((visibility("default")))
void _cnt_settings_power_view_constructor_hook(void* view, QWidget* parent);
