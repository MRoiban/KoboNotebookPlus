#pragma once

#include <QPointer>
#include <QVector>

#include <cstdint>

class NickelTouchMenu;
class QObject;
class QString;
class QWidget;
struct FirmwareApi;

namespace cnt {
class SettingsStore;
namespace layers_eraser {
struct Dependencies;
}

namespace eraser_menu {

struct RuntimeState {
    bool sizeApisReady = false;
    bool sizeMenuHooksReady = false;
    QPointer<QObject> liveSizeController;
};

typedef bool (*ApplyConfiguredSize)(QObject* widget, char const* reason);

bool installHooks(
    FirmwareApi& firmware,
    RuntimeState& state,
    void* handle);
void constructController(
    FirmwareApi& firmware,
    RuntimeState& state,
    SettingsStore& settings,
    void* controller,
    QWidget* parent,
    QVector<int> const* tools,
    QVector<int> const* brushSections,
    void* themeStorage);
void createBrushSizeRow(
    FirmwareApi& firmware,
    RuntimeState& state,
    void* controller,
    NickelTouchMenu* menu,
    QString const& title);
void afterBrushSizeIndex(
    FirmwareApi& firmware,
    RuntimeState& state,
    SettingsStore& settings,
    uintptr_t caller,
    void* controller,
    int index,
    ApplyConfiguredSize applyConfiguredSize);
void afterActiveTool(
    FirmwareApi& firmware,
    RuntimeState& state,
    SettingsStore& settings,
    uintptr_t caller,
    void* widget,
    int tool,
    layers_eraser::Dependencies const& stateDependencies);
bool queueActiveEraserReplay(
    RuntimeState& state,
    QObject* widget,
    layers_eraser::Dependencies const& stateDependencies);

} // namespace eraser_menu
} // namespace cnt
