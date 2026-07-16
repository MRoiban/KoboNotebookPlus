#pragma once

#include <QPointer>

#include <cstdint>

class QObject;
struct FirmwareApi;

namespace cnt {
namespace eraser_menu {

struct RuntimeState {
    bool sizeApisReady = false;
    bool sizeMenuHooksReady = false;
    QPointer<QObject> liveSizeController;
};

typedef bool (*ResolvePinnedAddress)(
    void* handle,
    char const* symbol,
    uintptr_t expectedVma,
    void** destination);
typedef bool (*PointerMatchesVma)(void* pointer, uintptr_t expectedVma);

struct InstallPins {
    char const* toolMenuConstructorSymbol;
    uintptr_t toolMenuConstructorVma;
    char const* createBrushSizeRowSymbol;
    uintptr_t createBrushSizeRowVma;
    char const* setBrushSizeIndexSymbol;
    uintptr_t setBrushSizeIndexVma;
    char const* setActiveToolSymbol;
    uintptr_t setActiveToolVma;
};

bool installHooks(
    FirmwareApi& firmware,
    RuntimeState& state,
    void* handle,
    InstallPins const& pins,
    ResolvePinnedAddress resolvePinnedAddress,
    PointerMatchesVma pointerMatchesVma);

} // namespace eraser_menu
} // namespace cnt
