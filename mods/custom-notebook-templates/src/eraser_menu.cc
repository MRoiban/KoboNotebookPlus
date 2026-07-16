#include "eraser_menu.h"

#include "abi_types.h"
#include "firmware_api.h"
#include "settings.h"

namespace cnt {
namespace eraser_menu {

bool installHooks(
        FirmwareApi& firmware,
        RuntimeState& state,
        void* handle,
        InstallPins const& pins,
        ResolvePinnedAddress resolvePinnedAddress,
        PointerMatchesVma pointerMatchesVma) {
    if (!handle || !state.sizeApisReady)
        return false;

    // Resolve every pass-through before mutating any GOT entry. A partial
    // installation remains inert because all wrappers also require the final
    // ready flag, which is set only after the constructor hook validates.
    ToolMenuConstructorAddress resolvedConstructor = {};
    CreateBrushSizeRowAddress resolvedRow = {};
    SetBrushSizeIndexAddress resolvedSetter = {};
    SetActiveToolAddress resolvedActiveTool = {};
    if (!resolvePinnedAddress(handle, pins.toolMenuConstructorSymbol,
            pins.toolMenuConstructorVma, &resolvedConstructor.pointer)
            || !resolvePinnedAddress(handle, pins.createBrushSizeRowSymbol,
                pins.createBrushSizeRowVma, &resolvedRow.pointer)
            || !resolvePinnedAddress(handle, pins.setBrushSizeIndexSymbol,
                pins.setBrushSizeIndexVma, &resolvedSetter.pointer)
            || !resolvePinnedAddress(handle, pins.setActiveToolSymbol,
                pins.setActiveToolVma, &resolvedActiveTool.pointer)) {
        trace("eraser-size: native menu symbols did not resolve; popup unchanged");
        return false;
    }
    firmware.toolMenuConstructorOriginal = resolvedConstructor.function;
    firmware.createBrushSizeRowOriginal = resolvedRow.function;
    firmware.setBrushSizeIndexOriginal = resolvedSetter.function;
    firmware.setActiveToolOriginal = resolvedActiveTool.function;

    SetBrushSizeIndexAddress setterReplacement;
    setterReplacement.function = _cnt_set_brush_size_index_hook;
    void* const originalSetter = nh_dlhook(
        handle, pins.setBrushSizeIndexSymbol, setterReplacement.pointer);
    if (!pointerMatchesVma(originalSetter, pins.setBrushSizeIndexVma)) {
        trace("eraser-size: native size callback hook mismatch; popup unchanged");
        return false;
    }
    firmware.setBrushSizeIndexOriginal =
        reinterpret_cast<SetBrushSizeIndex>(originalSetter);

    CreateBrushSizeRowAddress rowReplacement;
    rowReplacement.function = _cnt_create_brush_size_row_hook;
    void* const originalRow = nh_dlhook(
        handle, pins.createBrushSizeRowSymbol, rowReplacement.pointer);
    if (!pointerMatchesVma(originalRow, pins.createBrushSizeRowVma)) {
        trace("eraser-size: native size-row hook mismatch; popup unchanged");
        return false;
    }
    firmware.createBrushSizeRowOriginal =
        reinterpret_cast<CreateBrushSizeRow>(originalRow);

    // Hardware stylus inversion reaches setActiveTool through a unique PLT
    // call in stylusTouchBegin. The wrapper remains stock for every other
    // caller and only mirrors the already-reset native size into popup state.
    SetActiveToolAddress activeToolReplacement;
    activeToolReplacement.function = _cnt_set_active_tool_hook;
    void* const originalActiveTool = nh_dlhook(
        handle, pins.setActiveToolSymbol, activeToolReplacement.pointer);
    if (!pointerMatchesVma(originalActiveTool, pins.setActiveToolVma)) {
        trace("eraser-size: hardware UI synchronization hook mismatch; popup unchanged");
        return false;
    }
    firmware.setActiveToolOriginal =
        reinterpret_cast<SetActiveTool>(originalActiveTool);

    // Install the constructor last. Until this exact hook succeeds no menu is
    // marked, so the earlier wrappers are strict stock pass-throughs.
    ToolMenuConstructorAddress constructorReplacement;
    constructorReplacement.function = _cnt_tool_menu_constructor_hook;
    void* const originalConstructor = nh_dlhook(
        handle, pins.toolMenuConstructorSymbol, constructorReplacement.pointer);
    if (!pointerMatchesVma(originalConstructor, pins.toolMenuConstructorVma)) {
        trace("eraser-size: native controller hook mismatch; popup unchanged");
        return false;
    }
    firmware.toolMenuConstructorOriginal =
        reinterpret_cast<ToolMenuConstructor>(originalConstructor);
    state.sizeMenuHooksReady = true;
    trace("eraser-size: stock five-button eraser row hooks installed");
    return true;
}

} // namespace eraser_menu
} // namespace cnt
