#line 7204 "src/customnotebooktemplates.cc"

static bool installEraserSizeMenuHooks(void* handle) {
    if (!handle || !eraserState().sizeApisReady)
        return false;

    // Resolve every pass-through before mutating any GOT entry. A partial
    // installation remains inert because all wrappers also require the final
    // ready flag, which is set only after the constructor hook validates.
    ToolMenuConstructor resolvedConstructor = nullptr;
    CreateBrushSizeRow resolvedRow = nullptr;
    SetBrushSizeIndex resolvedSetter = nullptr;
    SetActiveTool resolvedActiveTool = nullptr;
    if (!resolvePinned(handle, kToolMenuConstructorSymbol,
            kToolMenuConstructorVma, &resolvedConstructor)
            || !resolvePinned(handle, kCreateBrushSizeRowSymbol,
                kCreateBrushSizeRowVma, &resolvedRow)
            || !resolvePinned(handle, kSetBrushSizeIndexSymbol,
                kSetBrushSizeIndexVma, &resolvedSetter)
            || !resolvePinned(handle, kSetActiveToolSymbol,
                kSetActiveToolVma, &resolvedActiveTool)) {
        trace("eraser-size: native menu symbols did not resolve; popup unchanged");
        return false;
    }
    firmwareApi().toolMenuConstructorOriginal = resolvedConstructor;
    firmwareApi().createBrushSizeRowOriginal = resolvedRow;
    firmwareApi().setBrushSizeIndexOriginal = resolvedSetter;
    firmwareApi().setActiveToolOriginal = resolvedActiveTool;

    SetBrushSizeIndexAddress setterReplacement;
    setterReplacement.function = _cnt_set_brush_size_index_hook;
    void* const originalSetter = nh_dlhook(
        handle, kSetBrushSizeIndexSymbol, setterReplacement.pointer);
    if (!pointerMatchesVma(originalSetter, kSetBrushSizeIndexVma)) {
        trace("eraser-size: native size callback hook mismatch; popup unchanged");
        return false;
    }
    firmwareApi().setBrushSizeIndexOriginal =
        reinterpret_cast<SetBrushSizeIndex>(originalSetter);

    CreateBrushSizeRowAddress rowReplacement;
    rowReplacement.function = _cnt_create_brush_size_row_hook;
    void* const originalRow = nh_dlhook(
        handle, kCreateBrushSizeRowSymbol, rowReplacement.pointer);
    if (!pointerMatchesVma(originalRow, kCreateBrushSizeRowVma)) {
        trace("eraser-size: native size-row hook mismatch; popup unchanged");
        return false;
    }
    firmwareApi().createBrushSizeRowOriginal =
        reinterpret_cast<CreateBrushSizeRow>(originalRow);

    // Hardware stylus inversion reaches setActiveTool through a unique PLT
    // call in stylusTouchBegin. The wrapper remains stock for every other
    // caller and only mirrors the already-reset native size into popup state.
    SetActiveToolAddress activeToolReplacement;
    activeToolReplacement.function = _cnt_set_active_tool_hook;
    void* const originalActiveTool = nh_dlhook(
        handle, kSetActiveToolSymbol, activeToolReplacement.pointer);
    if (!pointerMatchesVma(originalActiveTool, kSetActiveToolVma)) {
        trace("eraser-size: hardware UI synchronization hook mismatch; popup unchanged");
        return false;
    }
    firmwareApi().setActiveToolOriginal = reinterpret_cast<SetActiveTool>(originalActiveTool);

    // Install the constructor last. Until this exact hook succeeds no menu is
    // marked, so the earlier wrappers are strict stock pass-throughs.
    ToolMenuConstructorAddress constructorReplacement;
    constructorReplacement.function = _cnt_tool_menu_constructor_hook;
    void* const originalConstructor = nh_dlhook(
        handle, kToolMenuConstructorSymbol, constructorReplacement.pointer);
    if (!pointerMatchesVma(originalConstructor, kToolMenuConstructorVma)) {
        trace("eraser-size: native controller hook mismatch; popup unchanged");
        return false;
    }
    firmwareApi().toolMenuConstructorOriginal =
        reinterpret_cast<ToolMenuConstructor>(originalConstructor);
    eraserState().sizeMenuHooksReady = true;
    trace("eraser-size: stock five-button eraser row hooks installed");
    return true;
}
