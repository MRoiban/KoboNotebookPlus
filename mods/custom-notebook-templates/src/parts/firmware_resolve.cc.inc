
template <typename Function>
static bool resolvePinned(
    void* handle,
    char const* symbolName,
    uintptr_t expectedVma,
    Function* destination) {
    void* const symbol = dlsym(handle, symbolName);
    Dl_info image = {};
    if (!symbol || !dladdr(symbol, &image) || !image.dli_fbase) {
        nh_log("cover API symbol missing: %s", symbolName);
        return false;
    }

    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address = reinterpret_cast<uintptr_t>(symbol) & ~uintptr_t(1);
    if (address - base != expectedVma) {
        nh_log("cover API VMA mismatch for %s: 0x%lx, expected 0x%lx",
            symbolName,
            static_cast<unsigned long>(address - base),
            static_cast<unsigned long>(expectedVma));
        return false;
    }

    union {
        void* pointer;
        Function function;
    } converter;
    converter.pointer = symbol;
    *destination = converter.function;
    return true;
}

template <typename Function>
static bool resolvePinnedThumbVma(
    void* handle,
    char const* verifiedMarkerSymbol,
    uintptr_t expectedVma,
    Function* destination) {
    void* const marker = dlsym(handle, verifiedMarkerSymbol);
    Dl_info markerImage = {};
    if (!marker || !dladdr(marker, &markerImage) || !markerImage.dli_fbase) {
        nh_log("cover API image missing for internal VMA 0x%lx",
            static_cast<unsigned long>(expectedVma));
        return false;
    }

    uintptr_t const base = reinterpret_cast<uintptr_t>(markerImage.dli_fbase);
    void* const code = reinterpret_cast<void*>(base + expectedVma);
    Dl_info codeImage = {};
    if (!dladdr(code, &codeImage)
            || codeImage.dli_fbase != markerImage.dli_fbase) {
        nh_log("cover API internal VMA unmapped: 0x%lx",
            static_cast<unsigned long>(expectedVma));
        return false;
    }

    union {
        void* pointer;
        Function function;
    } converter;
    converter.pointer = reinterpret_cast<void*>(base + expectedVma + 1);
    *destination = converter.function;
    return true;
}

static bool resolveCoverApis(void* iinknoteHandle) {
    void* const iinkHandle = dlopen("libiink.so", RTLD_LAZY | RTLD_NOLOAD);
    void* const iinkUiRefHandle = dlopen(
        "libiinkuiref.so.1", RTLD_LAZY | RTLD_NOLOAD);
    void* const nickelHandle = dlopen(
        "libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!iinkHandle) {
        trace("covers: libiink not loaded");
        if (iinkUiRefHandle)
            dlclose(iinkUiRefHandle);
        if (nickelHandle)
            dlclose(nickelHandle);
        return false;
    }

    bool const noteSymbols =
        resolvePinned(iinknoteHandle, kCreateIInkMenuItemSymbol,
            kCreateIInkMenuItemVma, &firmwareApi().createIInkMenuItem)
        && resolvePinned(iinknoteHandle, kToolMenuTapGestureSymbol,
            kToolMenuTapGestureVma, &firmwareApi().toolMenuTapGesture)
        && resolvePinned(iinknoteHandle, kMenuSelectBackgroundSymbol,
            kMenuSelectBackgroundVma, &firmwareApi().menuSelectBackground)
        && resolvePinned(iinknoteHandle, kSetBackgroundTypeSymbol,
            kSetBackgroundTypeVma, &firmwareApi().setBackgroundTypeOriginal)
        && resolvePinned(iinknoteHandle, kWidgetSaveSymbol,
            kWidgetSaveVma, &firmwareApi().widgetSave)
        && resolvePinned(iinknoteHandle, kWidgetFilePathSymbol,
            kWidgetFilePathVma, &firmwareApi().widgetFilePath)
        && resolvePinned(iinknoteHandle, kWidgetRefreshSymbol,
            kWidgetRefreshVma, &firmwareApi().widgetRefresh)
        && resolvePinned(iinknoteHandle, kShowErrorPopupSymbol,
            kShowErrorPopupVma, &firmwareApi().showErrorPopup)
        && resolvePinned(iinknoteHandle, kBackgroundTypeSymbol,
            kBackgroundTypeVma, &firmwareApi().backgroundType)
        && resolvePinned(iinknoteHandle, kContentGetIdSymbol,
            kContentGetIdVma, &firmwareApi().contentGetId);

    bool const iinkSymbols =
        resolvePinned(iinkHandle, kEditorGetPartSymbol,
            kEditorGetPartVma, &firmwareApi().editorGetPart)
        && resolvePinned(iinkHandle, kEditorGetEngineSymbol,
            kEditorGetEngineVma, &firmwareApi().editorGetEngine)
        && resolvePinned(iinkHandle, kEditorSetPartSymbol,
            kEditorSetPartVma, &firmwareApi().editorSetPart)
        && resolvePinned(iinkHandle, kPartGetPackageSymbol,
            kPartGetPackageVma, &firmwareApi().partGetPackage)
        && resolvePinned(iinkHandle, kPartGetPageSymbol,
            kPartGetPageVma, &firmwareApi().partGetPage)
        && resolvePinned(iinkHandle, kPageDocumentSymbol,
            kPageDocumentVma, &firmwareApi().pageDocument)
        && resolvePinned(iinkHandle, kPageDestructorSymbol,
            kPageDestructorVma, &firmwareApi().pageDestructor)
        && resolvePinned(iinkHandle, kPackagePartCountSymbol,
            kPackagePartCountVma, &firmwareApi().packagePartCount)
        && resolvePinned(iinkHandle, kPackageGetPartSymbol,
            kPackageGetPartVma, &firmwareApi().packageGetPart)
        && resolvePinned(iinkHandle, kPackageIndexOfPartSymbol,
            kPackageIndexOfPartVma, &firmwareApi().packageIndexOfPart)
        && resolvePinned(iinkHandle, kPackageCreatePartSymbol,
            kPackageCreatePartVma, &firmwareApi().packageCreatePart)
        && resolvePinned(iinkHandle, kPackageClonePartSymbol,
            kPackageClonePartVma, &firmwareApi().packageClonePart)
        && resolvePinned(iinkHandle, kPackageRemovePartSymbol,
            kPackageRemovePartVma, &firmwareApi().packageRemovePart)
        && resolvePinned(iinkHandle, kPackageSaveSymbol,
            kPackageSaveVma, &firmwareApi().packageSave)
        && resolvePinned(iinkHandle, kEngineOpenPackageSymbol,
            kEngineOpenPackageVma, &firmwareApi().engineOpenPackage)
        && resolvePinned(iinkHandle, kDocumentPageCountSymbol,
            kDocumentPageCountVma, &firmwareApi().documentPageCount)
        && resolvePinned(iinkHandle, kDocumentMovePageSymbol,
            kDocumentMovePageVma, &firmwareApi().documentMovePage)
        && resolvePinned(iinkHandle, kIInkStringCtorSymbol,
            kIInkStringCtorVma, &firmwareApi().iinkStringCtor);

    bool const layerSymbols =
        resolvePinned(iinkHandle, kEditorGetRendererSymbol,
            kEditorGetRendererVma, &firmwareApi().editorGetRenderer)
        && resolvePinned(iinkHandle, kPartGetIdSymbol,
            kPartGetIdVma, &firmwareApi().partGetId)
        && resolvePinned(iinkHandle, kPageLayoutSymbol,
            kPageLayoutVma, &firmwareApi().pageLayout)
        && resolvePinned(iinkHandle, kLayoutDestructorSymbol,
            kLayoutDestructorVma, &firmwareApi().layoutDestructor)
        && resolvePinned(iinkHandle, kLayoutAppendLayerSymbol,
            kLayoutAppendLayerVma, &firmwareApi().layoutAppendLayer)
        && resolvePinned(iinkHandle, kLayoutRemoveLayerSymbol,
            kLayoutRemoveLayerVma, &firmwareApi().layoutRemoveLayer)
        && resolvePinned(iinkHandle, kAtkLayoutRawLayoutSymbol,
            kAtkLayoutRawLayoutVma, &firmwareApi().atkLayoutRawLayout)
        && resolvePinned(iinkHandle, kDocumentLayoutGetLayerSymbol,
            kDocumentLayoutGetLayerVma, &firmwareApi().documentLayoutGetLayer)
        && resolvePinned(iinkHandle, kLayerIteratorIsAtEndSymbol,
            kLayerIteratorIsAtEndVma, &firmwareApi().layerIteratorIsAtEnd)
        && resolvePinned(iinkHandle, kManagedObjectDestructorSymbol,
            kManagedObjectDestructorVma, &firmwareApi().managedObjectDestructor)
        && resolvePinned(iinkHandle, kRendererGetBackendSymbol,
            kRendererGetBackendVma, &firmwareApi().rendererGetBackend)
        && resolvePinned(iinkHandle, kRendererRestrictToLayersSymbol,
            kRendererRestrictToLayersVma, &firmwareApi().rendererRestrictToLayers)
        && resolvePinned(iinkHandle, kPageControllerInputDispatcherSymbol,
            kPageControllerInputDispatcherVma, &firmwareApi().pageControllerInputDispatcher)
        && resolvePinned(iinkHandle,
            kPlatformInputDispatcherGetCurrentToolSymbol,
            kPlatformInputDispatcherGetCurrentToolVma,
            &firmwareApi().platformInputDispatcherGetCurrentTool)
        && resolvePinned(iinkHandle, kDynamicToolDispatcherCastSymbol,
            kDynamicToolDispatcherCastVma, &firmwareApi().dynamicToolDispatcherCast)
        && resolvePinned(iinkHandle, kToolDispatcherRestrictToLayerSymbol,
            kToolDispatcherRestrictToLayerVma,
            &firmwareApi().toolDispatcherRestrictToLayer)
        && resolvePinned(iinkHandle, kIInkStringToStdStringSymbol,
            kIInkStringToStdStringVma, &firmwareApi().iinkStringToStdString)
        && resolvePinned(iinkHandle, kDocumentLayerNameSymbol,
            kDocumentLayerNameVma, &firmwareApi().documentLayerName)
        && resolvePinned(iinkHandle, kBackgroundObjectLayerNameSymbol,
            kBackgroundObjectLayerNameVma, &firmwareApi().backgroundObjectLayerName)
        && resolvePinned(iinkHandle, kNeboBackendVtableSymbol,
            kNeboBackendVtableVma, &firmwareApi().neboBackendVtable)
        && resolvePinned(iinkHandle, kCompositeBoxFactoryMainBackendSymbol,
            kCompositeBoxFactoryMainBackendVma, &firmwareApi().compositeBoxFactoryMainBackend)
        && resolvePinned(iinkHandle, kCompositeBoxFactoryBackendsSymbol,
            kCompositeBoxFactoryBackendsVma, &firmwareApi().compositeBoxFactoryBackends)
        && resolvePinned(iinkHandle, kSelectionSelectLayerSymbol,
            kSelectionSelectLayerVma, &firmwareApi().selectionSelectLayer)
        && resolvePinned(iinkHandle, kSelectionSelectPolygonSymbol,
            kSelectionSelectPolygonVma, &firmwareApi().selectionSelectPolygon)
        && resolvePinned(iinkHandle, kSelectionSelectNoneSymbol,
            kSelectionSelectNoneVma, &firmwareApi().selectionSelectNone)
        && resolvePinned(iinkHandle, kSelectionIsEmptySymbol,
            kSelectionIsEmptyVma, &firmwareApi().selectionIsEmpty)
        && resolvePinned(iinkHandle, kSelectionAdjustToStrokeBoundariesSymbol,
            kSelectionAdjustToStrokeBoundariesVma,
            &firmwareApi().selectionAdjustToStrokeBoundaries)
        && resolvePinned(iinkHandle, kEraserPolicySymbol,
            kEraserPolicyVma, &firmwareApi().eraserPolicy)
        && resolvePinned(iinkHandle, kEraserStrokerPolygonSymbol,
            kEraserStrokerPolygonVma, &firmwareApi().eraserStrokerPolygon)
        && resolvePinned(iinkHandle, kDrawingBackendVtableSymbol,
            kDrawingBackendVtableVma, &firmwareApi().drawingBackendVtable)
        && resolvePinned(iinkHandle, kDrawingEraserVtableSymbol,
            kDrawingEraserVtableVma, &firmwareApi().drawingEraserVtable)
        && resolvePinned(iinkHandle, kPlainDrawingEraserVtableSymbol,
            kPlainDrawingEraserVtableVma, &firmwareApi().plainDrawingEraserVtable)
        && resolvePinned(iinkHandle, kDiagramEraserVtableSymbol,
            kDiagramEraserVtableVma, &firmwareApi().diagramEraserVtable)
        && resolvePinned(iinkHandle, kDiagramPenVtableSymbol,
            kDiagramPenVtableVma, &firmwareApi().diagramPenVtable);

    // Eraser size uses the same live editor/backend path as layer routing, but
    // it remains a separate feature gate. The exact vtables prevent a radius
    // setter from ever being called on an arbitrary kind-4 Tool.
    bool const eraserSizeSymbols = layerSymbols
        && resolvePinned(iinkHandle, kEraserSetRadiusSymbol,
            kEraserSetRadiusVma, &firmwareApi().eraserSetRadius)
        && resolvePinned(iinkHandle, kEraserRadiusSymbol,
            kEraserRadiusVma, &firmwareApi().eraserRadius)
        && resolvePinned(iinkHandle, kEraserWidthFromThicknessRatioSymbol,
            kEraserWidthFromThicknessRatioVma,
            &firmwareApi().eraserWidthFromThicknessRatio)
        && resolvePinned(iinkHandle, kLayoutGridLineGapSymbol,
            kLayoutGridLineGapVma, &firmwareApi().layoutGridLineGap)
        && resolvePinned(iinkHandle, kCoreEraserVtableSymbol,
            kCoreEraserVtableVma, &firmwareApi().coreEraserVtable)
        && resolvePinned(iinkHandle, kTextEraserSntVtableSymbol,
            kTextEraserSntVtableVma, &firmwareApi().textEraserSntVtable)
        && resolvePinned(iinkHandle, kMathEraserVtableSymbol,
            kMathEraserVtableVma, &firmwareApi().mathEraserVtable)
        && resolvePinned(iinkHandle, kTextEraserVtableSymbol,
            kTextEraserVtableVma, &firmwareApi().textEraserVtable);
    eraserState().sizeApisReady = eraserSizeSymbols;

    bool const layerPreviewSymbols = iinkUiRefHandle
        && resolvePinned(iinkHandle, kEditorGetConfigurationSymbol,
            kEditorGetConfigurationVma, &firmwareApi().editorGetConfiguration)
        && resolvePinned(iinkHandle, kPageControllerExportToPngSymbol,
            kPageControllerExportToPngVma, &firmwareApi().pageControllerExportToPng)
        && resolvePinned(iinkHandle, kBackendImageDrawerMakeSharedSymbol,
            kBackendImageDrawerMakeSharedVma, &firmwareApi().backendImageDrawerMakeShared)
        && resolvePinnedThumbVma(
            iinkHandle,
            kPageControllerExportToPngSymbol,
            kStockBackendImageDrawerExportVma,
            &firmwareApi().stockBackendImageDrawerExport)
        && resolvePinned(iinkUiRefHandle, kImagePainterConstructorSymbol,
            kImagePainterConstructorVma, &firmwareApi().imagePainterConstructor)
        && resolvePinned(
            iinkUiRefHandle,
            kImagePainterDeletingDestructorSymbol,
            kImagePainterDeletingDestructorVma,
            &firmwareApi().imagePainterDeletingDestructor)
        && resolvePinned(
            iinkUiRefHandle,
            kImagePainterSetImageLoaderSymbol,
            kImagePainterSetImageLoaderVma,
            &firmwareApi().imagePainterSetImageLoader)
        && resolvePinned(
            iinkUiRefHandle,
            kUirefEditorWidgetVtableSymbol,
            kUirefEditorWidgetVtableVma,
            &firmwareApi().uirefEditorWidgetVtable);
    layerState().previewApisReady = iinkSymbols && layerSymbols && layerPreviewSymbols;

    // Best-effort: the base Eraser::updateSelection lets the diagram eraser
    // commit custom-layer ink. If it cannot be verified the eraser simply
    // keeps stock behavior; selection and pen routing remain unaffected.
    resolvePinned(iinkHandle, kCoreEraserUpdateSelectionSymbol,
        kCoreEraserUpdateSelectionVma, &firmwareApi().coreEraserUpdateSelection);

    bool const nativeLayerMenuSymbols = nickelHandle
        && resolvePinned(nickelHandle, kNickelTouchMenuConstructorSymbol,
            kNickelTouchMenuConstructorVma, &firmwareApi().nickelTouchMenuConstructor)
        && resolvePinned(nickelHandle, kNickelTouchMenuSetAlignmentSymbol,
            kNickelTouchMenuSetAlignmentVma, &firmwareApi().nickelTouchMenuSetAlignment)
        && resolvePinned(
            nickelHandle,
            kTouchMenuSetCustomPopupPositionOffsetSymbol,
            kTouchMenuSetCustomPopupPositionOffsetVma,
            &firmwareApi().touchMenuSetCustomPopupPositionOffset)
        && resolvePinned(
            nickelHandle,
            kAbstractNickelMenuControllerPopupFromWidgetSymbol,
            kAbstractNickelMenuControllerPopupFromWidgetVma,
            &firmwareApi().abstractNickelMenuControllerPopupFromWidget)
        && resolvePinned(
            nickelHandle,
            kNickelTouchMenuPopupPositionSymbol,
            kNickelTouchMenuPopupPositionVma,
            &firmwareApi().nickelTouchMenuPopupPosition);

    // Selectable layer rows use the exact native tool-row surface, but never
    // pass a synthetic layer value through IInkToolMenuWidget::setTool(). The
    // stock constructor gives us its QSS and named QLabel children; the
    // plugin supplies only their thumbnail/text and registers the same
    // GestureReceiver subobject used by IInkToolMenuController::loadView().
    bool const nativeLayerRowSymbols = nickelHandle
        && resolvePinned(
            iinknoteHandle,
            kIInkToolMenuWidgetConstructorSymbol,
            kIInkToolMenuWidgetConstructorVma,
            &firmwareApi().iInkToolMenuWidgetConstructor)
        && resolvePinned(
            iinknoteHandle,
            kIInkToolMenuWidgetSetSelectedSymbol,
            kIInkToolMenuWidgetSetSelectedVma,
            &firmwareApi().iInkToolMenuWidgetSetSelected)
        && resolvePinned(
            nickelHandle,
            kAbstractMenuControllerGrabTapGestureSymbol,
            kAbstractMenuControllerGrabTapGestureVma,
            &firmwareApi().abstractMenuControllerGrabTapGesture)
        && resolvePinned(
            nickelHandle,
            kAbstractMenuControllerTapGestureSymbol,
            kAbstractMenuControllerTapGestureVma,
            &firmwareApi().abstractMenuControllerTapGesture);

    layerState().hooksReady = noteSymbols && iinkSymbols && layerSymbols
        && nativeLayerMenuSymbols && nativeLayerRowSymbols;
    trace(layerState().hooksReady
        ? "layers: pinned layout, routing, and native menu APIs verified"
        : "layers: pinned API verification failed; feature disabled");
    trace(layerPreviewSymbols
        ? "layers: isolated preview APIs verified"
        : "layers: preview APIs unavailable; placeholder cards retained");
    trace(eraserState().sizeApisReady
        ? "eraser-size: pinned radius, line-grid, and exact eraser APIs verified"
        : "eraser-size: pinned engine APIs unavailable; feature disabled");

    dlclose(iinkHandle);
    if (iinkUiRefHandle)
        dlclose(iinkUiRefHandle);
    if (nickelHandle)
        dlclose(nickelHandle);
    if (!noteSymbols || !iinkSymbols) {
        trace("covers: pinned API resolution failed");
        return false;
    }

    trace("covers: pinned APIs verified");
    return true;
}

static bool pointerMatchesVma(void* pointer, uintptr_t expectedVma) {
    Dl_info image = {};
    if (!pointer || !dladdr(pointer, &image) || !image.dli_fbase)
        return false;
    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address = reinterpret_cast<uintptr_t>(pointer) & ~uintptr_t(1);
    return address - base == expectedVma;
}

// Read-only device diagnostic for polymorphic firmware objects. dladdr keeps
// this independent of ASLR while the reported VMA and nearest dynamic symbol
// let us identify an unexpected concrete backend/tool in the pinned binary.
// Function pointers may carry ARM's Thumb bit, so strip it before computing
// the image-relative address.
static QString pointerIdentity(void* pointer) {
    Dl_info image = {};
    if (!pointer || !dladdr(pointer, &image) || !image.dli_fbase)
        return QLatin1String("unmapped");
    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address = reinterpret_cast<uintptr_t>(pointer) & ~uintptr_t(1);
    QString identity = QLatin1String("0x")
        + QString::number(address - base, 16);
    if (image.dli_sname && image.dli_sname[0] != '\0')
        identity += QLatin1String(":") + QString::fromLatin1(image.dli_sname);
    return identity;
}

static QString toolIdentity(void* tool, bool includeSelectionSlot) {
    if (!tool)
        return QLatin1String("missing");
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return QLatin1String("null-vptr");
    QString identity = QLatin1String("vptr=") + pointerIdentity(vptr)
        + QLatin1String(" penDown=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + 0x3c))
        + QLatin1String(" restrict=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + kToolRestrictToLayerVtableSlot))
        + QLatin1String(" restricted=")
        + pointerIdentity(*reinterpret_cast<void**>(
            static_cast<char*>(vptr) + 0x58));
    if (includeSelectionSlot) {
        identity += QLatin1String(" selection=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + kDrawingEraserSelectionFromPointsVtableSlot))
            + QLatin1String(" update=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + kDiagramEraserUpdateSelectionVtableSlot))
            + QLatin1String(" erase=")
            + pointerIdentity(*reinterpret_cast<void**>(
                static_cast<char*>(vptr)
                    + kDiagramEraserEraseSelectionVtableSlot));
    }
    return identity;
}
