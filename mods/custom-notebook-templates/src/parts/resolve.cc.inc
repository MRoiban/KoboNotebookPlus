#line 528 "src/customnotebooktemplates.cc"

static PluginState* gPluginState = nullptr;

static PluginState& pluginState() {
    return *gPluginState;
}

static FirmwareApi& firmwareApi() {
    return pluginState().firmware;
}

static cnt::templates::TemplateRuntimeState& templateState() {
    return pluginState().templates;
}

static cnt::cover_cache::State& coverState() {
    return pluginState().covers;
}

static PageRuntimeState& pageState() {
    return pluginState().pages;
}

static LayerRuntimeState& layerState() {
    return pluginState().layers;
}

static cnt::eraser_menu::RuntimeState& eraserState() {
    return pluginState().eraser;
}

static cnt::SettingsStore& settingsStore() {
    return pluginState().settings;
}

static cnt::visibility::RuntimeState& visibilityState() {
    return pluginState().visibility;
}

static HookRuntimeState& hookState() {
    return pluginState().hooks;
}

// The alpha's file-scope diagram-pen map emitted two weak std::map destructor
// symbols. Preserve that dynamic ABI surface with an immutable empty anchor;
// all live routing data remains owned by PluginState.
static std::map<void*, std::string> const kMapDestructorParityAnchor;
// snt::DrawingEraser, the global DrawingEraser, and the raw-content
// atk::diagram::DiagramEraser each have 148-byte Itanium ABI vtables. Separate
// writable, process-lifetime clones preserve every class override while
// replacing selectionFromPoints. The two DrawingErasers filter their stock
// hit selection. DiagramEraser rebuilds that hit selection in firmware's
// layer-first order. Stock libiink text and code remain untouched.
static size_t const kDrawingEraserVtableWords = 148 / sizeof(void*);
// Raw-content pages cache one 152-byte DiagramPen vtable. A writable clone
// replaces only penDown so the selected layer can be reasserted at the last
// possible point before core Pen tags the new stroke. This guards against a
// later native tool refresh silently restoring an older layer between popup
// activation and physical pen-down; every other DiagramPen slot remains stock.
static size_t const kDiagramPenVtableWords = 152 / sizeof(void*);
// Both caches are shared between the UI-thread loadCover hook and the parser
// thumbnail callback, so every access happens under coverCacheMutex. The lock
// is only ever held for map operations, never across archive scans, PNG
// decoding, or calls back into Qt or firmware code.
static char const kCoverBackupRoot[] = "/mnt/onboard/.kobo/custom/covers/backups/";
static char const kPageManagerRoot[] = "/mnt/onboard/.kobo/custom/page-manager/";
static char const kPageBackupRoot[] = "/mnt/onboard/.kobo/custom/page-manager/backups/";
static char const kPageTransactionRoot[] = "/mnt/onboard/.kobo/custom/page-manager/transactions/";
static char const kLayerRoot[] = "/mnt/onboard/.kobo/custom/layers/";
static char const kLayerBackupRoot[] = "/mnt/onboard/.kobo/custom/layers/backups/";
static char const kLayerPreviewRoot[] = "/mnt/onboard/.kobo/custom/layers/previews/";
static int const kMaximumNotebookPages = 4096;
static int const kMaximumDestinationNotebooks = 512;
static int const kMaximumNotebookLayers = 16;
static qint64 const kMaximumLayerPreviewBytes = qint64(8) * 1024 * 1024;
// Match the physical notebook page/background ratio (1404 x 1872 = 3:4)
// while preserving the 72-pixel height already proven inside Kobo's native
// tool-menu row. A taller card would risk clipping the stock row QSS.
static int const kLayerPreviewCardWidth = 54;
static int const kLayerPreviewCardHeight = 72;
// Let Nickel paint the popup before the first thread-affine MyScript export.
// Further rows are yielded back to the nested menu event loop one at a time.
static int const kLayerPreviewDeferredStartMs = 350;
static int const kLayerPreviewDeferredNextMs = 50;
// A renderer export is synchronous and cannot be preempted safely. Keep the
// automatic budget deliberately small so opening the menu never turns into an
// unbounded sequence of full-page renders. Further stale rows are refreshed
// on later opens or by the explicit "Refresh layer previews" command.
static int const kLayerPreviewDeferredBudget = 1;
static int const kMaximumLayerPreviewCardCacheEntries = 64;
// Blocked until the clonePart resource-independence runtime matrix passes on
// a disposable device test.
static bool const kEnableCrossNotebookMove = false;
// Rendered previews are normally card-sized, but keep a hard byte bound in
// case this firmware callback supplies full-resolution images.

// Binary Ninja image addresses minus its 0x10000 analysis base.
static uintptr_t const kBackgroundOptionsVma = 0x78c9c;
static char const kBackgroundOptionsSymbol[] = "_ZN23BackgroundOptionsWidget17backgroundOptionsEv";
static char const kAddWidgetActionSymbol[] =
    "_ZN22AbstractMenuController15addWidgetActionEP5QMenuP7QWidgetP7QObjectPKcbbb";
static char const kCreateIInkMenuItemSymbol[] =
    "_ZN26IInkDropdownMenuController18createIInkMenuItemEP5QMenuRK7QStringRK7QPixmapb";
static char const kToolMenuTapGestureSymbol[] =
    "_ZN22IInkToolMenuController10tapGestureEP15GestureReceiverP10TapGesture";
static char const kNickelTouchMenuConstructorSymbol[] =
    "_ZN15NickelTouchMenuC1EP7QWidget18DecorationPosition";
static char const kNickelTouchMenuSetAlignmentSymbol[] =
    "_ZN15NickelTouchMenu12setAlignmentE6QFlagsIN2Qt13AlignmentFlagEE";
static char const kTouchMenuSetCustomPopupPositionOffsetSymbol[] =
    "_ZN9TouchMenu28setCustomPopupPositionOffsetERK6QPoint";
static char const kAbstractNickelMenuControllerPopupFromWidgetSymbol[] =
    "_ZNK28AbstractNickelMenuController15popupFromWidgetEv";
static char const kNickelTouchMenuPopupPositionSymbol[] =
    "_ZN15NickelTouchMenu13popupPositionEP7QWidget";
static char const kIInkToolMenuWidgetConstructorSymbol[] =
    "_ZN18IInkToolMenuWidgetC1EP7QWidget";
static char const kIInkToolMenuWidgetSetSelectedSymbol[] =
    "_ZN18IInkToolMenuWidget11setSelectedEb";
static char const kAbstractMenuControllerGrabTapGestureSymbol[] =
    "_ZN22AbstractMenuController14grabTapGestureEP15GestureReceiver";
static char const kAbstractMenuControllerTapGestureSymbol[] =
    "_ZN22AbstractMenuController10tapGestureEP15GestureReceiverP10TapGesture";
static char const kSetDialogTitleSymbol[] =
    "_ZN8N3Dialog8setTitleERK7QString";
static char const kMenuSelectBackgroundSymbol[] = "_ZN18IInkMenuController16selectBackgroundEv";
static char const kSetBackgroundTypeSymbol[] = "_ZN17IInkNotePadWidget17setBackgroundTypeERK7QString";
static char const kSetToolThemeSymbol[] =
    "_ZN17IInkNotePadWidget12setToolThemeER13IInkToolTheme";
static char const kRenderVolumeSymbol[] =
    "_ZN17IInkNotePadWidget12renderVolumeERK6Volume";
static char const kWidgetSaveSymbol[] = "_ZN17IInkNotePadWidget4saveEv";
static char const kWidgetFilePathSymbol[] = "_ZNK17IInkNotePadWidget8filePathEv";
static char const kWidgetRefreshSymbol[] = "_ZN17IInkNotePadWidget7refreshEv";
static char const kShowErrorPopupSymbol[] = "_ZN17IInkNotePadWidget14showErrorPopupERK7QString";
static char const kBackgroundTypeSymbol[] = "_ZN16BackgroundWidget14backgroundTypeEv";
static char const kParserImageParsedSymbol[] =
    "_ZN15ParserInterface11imageParsedERK6VolumeRK6QImage";
static char const kVolumeLoadCoverSymbol[] =
    "_ZN16VolumePixmapView9loadCoverEv";
static char const kPixmapSetImageSymbol[] =
    "_ZN10PixmapView8setImageERK6QImageRK7QString";
static char const kContentGetIdSymbol[] = "_ZNK7Content5getIdEv";
static char const kContentGetImageIdSymbol[] = "_ZNK7Content10getImageIdEv";

static uintptr_t const kMenuLoadViewVma = 0x4984c;
static uintptr_t const kMenuLoadViewSize = 0xe5e;
static uintptr_t const kAddWidgetActionVma = 0xb3ce80;
static uintptr_t const kCreateIInkMenuItemVma = 0x46bc4;
static uintptr_t const kToolMenuTapGestureVma = 0x75a4c;
static uintptr_t const kNickelTouchMenuConstructorVma = 0xed6788;
static uintptr_t const kNickelTouchMenuSetAlignmentVma = 0xed6a68;
static uintptr_t const kTouchMenuSetCustomPopupPositionOffsetVma = 0x11150a8;
static uintptr_t const kAbstractNickelMenuControllerPopupFromWidgetVma =
    0xeb4f94;
static uintptr_t const kNickelTouchMenuPopupPositionVma = 0xed7248;
static size_t const kNickelTouchMenuSize = 0x90;
static uintptr_t const kIInkToolMenuWidgetConstructorVma = 0x761ac;
static uintptr_t const kIInkToolMenuWidgetSetSelectedVma = 0x76580;
static uintptr_t const kAbstractMenuControllerGrabTapGestureVma = 0xb3cfac;
static uintptr_t const kAbstractMenuControllerTapGestureVma = 0xb3d00c;
static size_t const kIInkToolMenuWidgetSize = 0x58;
static uintptr_t const kIInkToolMenuWidgetGestureReceiverOffset = 0x44;
static uintptr_t const kSetDialogTitleVma = 0x10e4168;
static uintptr_t const kMenuSelectBackgroundVma = 0x49438;
static uintptr_t const kSetBackgroundTypeVma = 0x5b9fc;
static uintptr_t const kSetToolThemeVma = 0x62200;
static uintptr_t const kRenderVolumeVma = 0x66720;
static uintptr_t const kWidgetSaveVma = 0x5f780;
static uintptr_t const kWidgetFilePathVma = 0x58da8;
static uintptr_t const kWidgetRefreshVma = 0x57fec;
static uintptr_t const kShowErrorPopupVma = 0x58b64;
static uintptr_t const kBackgroundTypeVma = 0x41c64;
static uintptr_t const kParserImageParsedVma = 0x118f714;
static uintptr_t const kVolumeLoadCoverVma = 0xc73388;
static char const kExcludeSyncFoldersSymbol[] =
    "_ZN15FeatureSettings18excludeSyncFoldersEv";
static uintptr_t const kExcludeSyncFoldersVma = 0xa04650;
static char const kRemoveCommonBookDataSymbol[] =
    "_ZN13VolumeManager20removeCommonBookDataERK6DeviceR6Volumeb";
static uintptr_t const kRemoveCommonBookDataVma = 0xa6ec74;
static uintptr_t const kPixmapSetImageVma = 0x10fa084;
static uintptr_t const kContentGetIdVma = 0x953d84;
static uintptr_t const kContentGetImageIdVma = 0x957628;
static uintptr_t const kVolumeInPixmapViewOffset = 0xac;
static uintptr_t const kThumbnailCallbackReturnVma = 0x5433e;

static char const kEditorGetPartSymbol[] = "_ZN8myscript4iink10EditorImpl7getPartEv";
static char const kEditorGetEngineSymbol[] = "_ZN8myscript4iink10EditorImpl9getEngineEv";
static char const kEditorGetConfigurationSymbol[] =
    "_ZN8myscript4iink10EditorImpl16getConfigurationEv";
static char const kEditorGetRendererSymbol[] =
    "_ZN8myscript4iink10EditorImpl11getRendererEv";
static char const kEditorSetPartSymbol[] = "_ZN8myscript4iink10EditorImpl7setPartESt10shared_ptrINS0_11ContentPartEE";
static char const kPartGetPackageSymbol[] = "_ZNK8myscript4iink15ContentPartImpl10getPackageEv";
static char const kPartGetIdSymbol[] =
    "_ZNK8myscript4iink15ContentPartImpl5getIdEv";
static char const kPartGetPageSymbol[] = "_ZNK8myscript4iink15ContentPartImpl7getPageEv";
static char const kPageDocumentSymbol[] = "_ZNK3atk4core4Page8documentEv";
static char const kPageDestructorSymbol[] = "_ZN3atk4core4PageD1Ev";
static char const kPageLayoutSymbol[] = "_ZNK3atk4core4Page6layoutEv";
static char const kLayoutDestructorSymbol[] = "_ZN3atk4core6LayoutD1Ev";
static char const kLayoutAppendLayerSymbol[] =
    "_ZN3atk4core6Layout19appendLayerWithNameERKSs";
static char const kLayoutRemoveLayerSymbol[] =
    "_ZN3atk4core6Layout11removeLayerERKSs";
static char const kAtkLayoutRawLayoutSymbol[] =
    "_ZNK3atk4core6Layout7_layoutEv";
static char const kDocumentLayoutGetLayerSymbol[] =
    "_ZNK8myscript8document6Layout8getLayerERKSs";
static char const kLayerIteratorIsAtEndSymbol[] =
    "_ZNK8myscript8document13LayerIterator7isAtEndEv";
static char const kManagedObjectDestructorSymbol[] =
    "_ZN8myscript6engine13ManagedObjectD1Ev";
static char const kRendererGetBackendSymbol[] =
    "_ZN8myscript4iink12RendererImpl10getBackendEv";
static char const kRendererRestrictToLayersSymbol[] =
    "_ZN3atk4core8Renderer16restrictToLayersERKSt6vectorISsSaISsEE";
static char const kPageControllerExportToPngSymbol[] =
    "_ZN3snt14PageController11exportToPNGESt10shared_ptrIN3atk4core3BoxEERKSsS1_INS3_16RendererListenerEEj";
static char const kBackendImageDrawerMakeSharedSymbol[] =
    "_ZNSt12__shared_ptrIN8myscript4iink18BackendImageDrawerELN9__gnu_cxx12_Lock_policyE2EEC2ISaIS2_EIRSt10shared_ptrINS1_10EngineImplEERS8_INS1_13IImagePainterEERS8_INS1_17ConfigurationImplEEEEESt19_Sp_make_shared_tagRKT_DpOT0_";
static char const kImagePainterConstructorSymbol[] =
    "_ZN8myscript4iink25uireferenceimplementation12ImagePainterC1Ev";
static char const kImagePainterDeletingDestructorSymbol[] =
    "_ZN8myscript4iink25uireferenceimplementation12ImagePainterD0Ev";
static char const kImagePainterSetImageLoaderSymbol[] =
    "_ZN8myscript4iink25uireferenceimplementation12ImagePainter14setImageLoaderESt10shared_ptrINS1_11ImageLoaderEE";
static char const kUirefEditorWidgetVtableSymbol[] =
    "_ZTVN8myscript4iink25uireferenceimplementation12EditorWidgetE";
static char const kPageControllerInputDispatcherSymbol[] =
    "_ZNK3snt14PageController15inputDispatcherEv";
static char const kPlatformInputDispatcherGetCurrentToolSymbol[] =
    "_ZNK3snt23PlatformInputDispatcher14getCurrentToolEv";
static char const kDynamicToolDispatcherCastSymbol[] =
    "_ZSt20dynamic_pointer_castIN3snt14ToolDispatcherEN3atk4core4ToolEESt10shared_ptrIT_ERKS5_IT0_E";
static char const kToolDispatcherRestrictToLayerSymbol[] =
    "_ZN3snt14ToolDispatcher15restrictToLayerERKSs";
static char const kCompositeBoxFactoryMainBackendSymbol[] =
    "_ZNK3snt19CompositeBoxFactory11mainBackendEv";
static char const kCompositeBoxFactoryBackendsSymbol[] =
    "_ZNK3snt19CompositeBoxFactory8backendsEv";
static char const kSelectionSelectLayerSymbol[] =
    "_ZN3atk4core9Selection11selectLayerERKSsNS1_4ModeE";
static char const kSelectionSelectPolygonSymbol[] =
    "_ZN3atk4core9Selection13selectPolygonEPKNS0_5PointEiNS1_4ModeE";
static char const kSelectionSelectNoneSymbol[] =
    "_ZN3atk4core9Selection10selectNoneEv";
static char const kSelectionIsEmptySymbol[] =
    "_ZNK3atk4core9Selection7isEmptyEv";
static char const kSelectionAdjustToStrokeBoundariesSymbol[] =
    "_ZN3atk4core9Selection24adjustToStrokeBoundariesEf";
static char const kEraserPolicySymbol[] =
    "_ZNK3atk4core6Eraser12eraserPolicyEv";
static char const kEraserSetRadiusSymbol[] =
    "_ZN3atk4core6Eraser9setRadiusEf";
static char const kEraserRadiusSymbol[] =
    "_ZNK3atk4core6Eraser6radiusEv";
static char const kEraserWidthFromThicknessRatioSymbol[] =
    "_ZN3snt13PenStyleUtils29eraserWidthFromThicknessRatioEff";
static char const kLayoutGridLineGapSymbol[] =
    "_ZNK3snt10LayoutGrid7lineGapEv";
static char const kEraserStrokerPolygonSymbol[] =
    "_ZN3atk4core6Eraser14strokerPolygonEfRKSt6vectorINS0_11PointerInfoESaIS3_EE";
static char const kCoreEraserUpdateSelectionSymbol[] =
    "_ZN3atk4core6Eraser15updateSelectionERKNS0_9SelectionERKSt6vectorINS0_11PointerInfoESaIS6_EE";
static char const kDrawingBackendVtableSymbol[] =
    "_ZTVN3snt14DrawingBackendE";
static char const kDrawingEraserVtableSymbol[] =
    "_ZTVN3snt13DrawingEraserE";
static char const kPlainDrawingEraserVtableSymbol[] =
    "_ZTV13DrawingEraser";
static char const kDiagramEraserVtableSymbol[] =
    "_ZTVN3atk7diagram13DiagramEraserE";
static char const kCoreEraserVtableSymbol[] =
    "_ZTVN3atk4core6EraserE";
static char const kTextEraserSntVtableSymbol[] =
    "_ZTVN3snt13TextEraserSNTE";
static char const kMathEraserVtableSymbol[] =
    "_ZTVN3atk4math10MathEraserE";
static char const kTextEraserVtableSymbol[] =
    "_ZTVN3atk4text10TextEraserE";
static char const kDiagramPenVtableSymbol[] =
    "_ZTVN3atk7diagram10DiagramPenE";
static char const kIInkStringToStdStringSymbol[] =
    "_ZNK8myscript4iink6StringcvSsEv";
static char const kDocumentLayerNameSymbol[] =
    "_ZN3atk4core6Layout19DOCUMENT_LAYER_NAMEE";
static char const kBackgroundObjectLayerNameSymbol[] =
    "_ZN3atk4core6Layout28BACKGROUND_OBJECT_LAYER_NAMEE";
static char const kNeboBackendVtableSymbol[] =
    "_ZTVN8myscript4iink11NeboBackendE";
static char const kPackagePartCountSymbol[] = "_ZNK8myscript4iink18ContentPackageImpl12getPartCountEv";
static char const kPackageGetPartSymbol[] = "_ZN8myscript4iink18ContentPackageImpl7getPartEi";
static char const kPackageIndexOfPartSymbol[] = "_ZN8myscript4iink18ContentPackageImpl11indexOfPartESt10shared_ptrINS0_11ContentPartEE";
static char const kPackageCreatePartSymbol[] = "_ZN8myscript4iink18ContentPackageImpl10createPartERKNS0_6StringE";
static char const kPackageClonePartSymbol[] = "_ZN8myscript4iink18ContentPackageImpl9clonePartESt10shared_ptrINS0_11ContentPartEE";
static char const kPackageRemovePartSymbol[] = "_ZN8myscript4iink18ContentPackageImpl10removePartESt10shared_ptrINS0_11ContentPartEE";
static char const kPackageSaveSymbol[] = "_ZN8myscript4iink18ContentPackageImpl4saveEv";
static char const kEngineOpenPackageSymbol[] = "_ZN8myscript4iink10EngineImpl11openPackageERKNS0_6StringENS0_17PackageOpenOptionE";
static char const kDocumentPageCountSymbol[] = "_ZNK3atk4core8Document13numberOfPagesEv";
static char const kDocumentMovePageSymbol[] = "_ZN3atk4core8Document8movePageEii";
static char const kIInkStringCtorSymbol[] = "_ZN8myscript4iink6StringC1EPKc";

static uintptr_t const kEditorGetPartVma = 0x425ca0;
static uintptr_t const kEditorGetEngineVma = 0x425bb0;
static uintptr_t const kEditorGetConfigurationVma = 0x425cf0;
static uintptr_t const kEditorGetRendererVma = 0x425c00;
static uintptr_t const kEditorSetPartVma = 0x438154;
static uintptr_t const kPartGetPackageVma = 0x3f5bac;
static uintptr_t const kPartGetIdVma = 0x3f5bfc;
static uintptr_t const kPartGetPageVma = 0x3f5b34;
static uintptr_t const kPageDocumentVma = 0x9f65d8;
static uintptr_t const kPageDestructorVma = 0x9f6234;
static uintptr_t const kPageLayoutVma = 0x9f990c;
static uintptr_t const kLayoutDestructorVma = 0x3e6be4;
static uintptr_t const kLayoutAppendLayerVma = 0x9da780;
static uintptr_t const kLayoutRemoveLayerVma = 0x9d9be8;
static uintptr_t const kAtkLayoutRawLayoutVma = 0x9da8c8;
static uintptr_t const kDocumentLayoutGetLayerVma = 0x4b3d00;
static uintptr_t const kLayerIteratorIsAtEndVma = 0x4b3e88;
static uintptr_t const kManagedObjectDestructorVma = 0x3e5390;
static uintptr_t const kRendererGetBackendVma = 0x49e318;
static uintptr_t const kRendererRestrictToLayersVma = 0xa57714;
static uintptr_t const kPageControllerExportToPngVma = 0x5130c4;
static uintptr_t const kBackendImageDrawerMakeSharedVma = 0x443eac;
// Anonymous callback used by EditorImpl::export_. It owns Kobo's exact
// Page/Layout/Selection temporary construction and destruction sequence, then
// calls BackendImageDrawer::drawImage. It is Thumb code and is reached through
// the verified libiink image base rather than dlsym (the symbol is local).
static uintptr_t const kStockBackendImageDrawerExportVma = 0x47b090;
static uintptr_t const kImagePainterConstructorVma = 0x28b48;
static uintptr_t const kImagePainterDeletingDestructorVma = 0x29948;
static uintptr_t const kImagePainterSetImageLoaderVma = 0x28a60;
static uintptr_t const kUirefEditorWidgetVtableVma = 0x50000;
static uintptr_t const kPageControllerInputDispatcherVma = 0x501960;
static uintptr_t const kPlatformInputDispatcherGetCurrentToolVma = 0x5bc80c;
static uintptr_t const kDynamicToolDispatcherCastVma = 0x522d44;
static uintptr_t const kToolDispatcherRestrictToLayerVma = 0x5c4c90;
static uintptr_t const kCompositeBoxFactoryMainBackendVma = 0x5b81b8;
static uintptr_t const kCompositeBoxFactoryBackendsVma = 0x5b83e0;
static uintptr_t const kSelectionSelectLayerVma = 0x9e1ec4;
static uintptr_t const kSelectionSelectPolygonVma = 0x9e27b8;
static uintptr_t const kSelectionSelectNoneVma = 0x9e1c18;
static uintptr_t const kSelectionIsEmptyVma = 0x9e30f0;
static uintptr_t const kSelectionAdjustToStrokeBoundariesVma = 0x9e2d70;
static uintptr_t const kEraserPolicyVma = 0xa75aa4;
static uintptr_t const kEraserSetRadiusVma = 0xa75b2c;
static uintptr_t const kEraserRadiusVma = 0xa75adc;
static uintptr_t const kEraserWidthFromThicknessRatioVma = 0x69c278;
static uintptr_t const kLayoutGridLineGapVma = 0x6051fc;
static uintptr_t const kEraserStrokerPolygonVma = 0xa75fb0;
static uintptr_t const kDrawingBackendVtableVma = 0xd8a190;
static uintptr_t const kDrawingEraserVtableVma = 0xd8a478;
static uintptr_t const kPlainDrawingEraserVtableVma = 0xd856e0;
static uintptr_t const kDiagramEraserVtableVma = 0xd8eb30;
static uintptr_t const kCoreEraserVtableVma = 0xd97370;
static uintptr_t const kTextEraserSntVtableVma = 0xd8b470;
static uintptr_t const kMathEraserVtableVma = 0xd94470;
static uintptr_t const kTextEraserVtableVma = 0xd95c10;
static uintptr_t const kDiagramPenVtableVma = 0xd8ffb0;
static uintptr_t const kIInkStringToStdStringVma = 0x3e52a8;
static uintptr_t const kDocumentLayerNameVma = 0x21cb994;
static uintptr_t const kBackgroundObjectLayerNameVma = 0x21cb998;
static uintptr_t const kNeboBackendVtableVma = 0xd871a0;
static uintptr_t const kNeboBackendPageControllerOffset = 0x14;
static uintptr_t const kStockPreviewDrawerOffset = 0x4;
static uintptr_t const kStockPreviewBackendOffset = 0x24;
static size_t const kStockPreviewContextBytes = 0x28;
static size_t const kImagePainterObjectBytes = 64;
// IInkNotePadWidget owns a guarded uireference EditorWidget. That live
// EditorWidget owns the configured ImageLoader used by every stock canvas.
// These offsets are pinned against both libraries before the preview feature
// gate is enabled; the guarded object and its live editor identity are checked
// again before copying the shared loader owner.
static uintptr_t const kNotePadEditorWidgetGuardOffset = 0x44;
static uintptr_t const kNotePadEditorWidgetObjectOffset = 0x48;
static uintptr_t const kEditorWidgetEditorObjectOffset = 0x18;
static uintptr_t const kEditorWidgetEditorControlOffset = 0x1c;
static uintptr_t const kEditorWidgetImageLoaderObjectOffset = 0x20;
static uintptr_t const kEditorWidgetImageLoaderControlOffset = 0x24;
static uintptr_t const kPageControllerLayoutGridOffset = 0xe4;
// Five native BrushButton swatches, mapped onto firmware's own eraser-width
// buckets. Index 2 deliberately preserves the common 3px radius at a 12px
// line gap; the two widest buckets remain clearly distinguishable on e-ink.
static float const kEraserSizeRatios[] = {
    0.0f, 0.25f, 0.35f, 0.70f, 1.0f
};
// atk::core::Tool::restrictToLayer(std::string const&) is virtual vtable slot
// +0x54 (restrictedLayer() is +0x58). Binary Ninja / objdump confirmed slot
// +0x54 resolves to Tool::restrictToLayer (0xa84358) in the base Tool vtable
// (0xd977a8) and to snt::ToolDispatcher::restrictToLayer (0x5c4c90) in the
// dispatcher override vtable (0xd8b538). Calling it virtually on the concrete
// tool that PlatformInputDispatcher::getCurrentTool() returns routes the next
// stroke into the active layer and honors any tool-specific override, without
// the wrong-type cast that previously failed (a concrete Pen is not a
// ToolDispatcher). scripts/verify-layer-abi.py pins this slot against both
// vtables.
static uintptr_t const kToolRestrictToLayerVtableSlot = 0x54;
static uintptr_t const kToolRestrictedLayerVtableSlot = 0x58;
// snt::DrawingBackend caches one persistent tool per kind as members. From
// DrawingBackend::getTool (0x5589cc): pen kinds -> +0x44, kind 4 (eraser) ->
// +0x4c, kind 3 -> +0x54, kind 6 -> +0x5c (each a shared_ptr; the raw Tool* is
// the low word). DrawingBackend::init applies DOCUMENT_LAYER_NAME to the Pen,
// Selector, and DrawingBrush through virtual slot +0x54, but deliberately
// skips DrawingEraser. Mirror that stock policy: DrawingBrush inherits the Pen
// penDown path and consumes restrictedLayer(), while DrawingEraser inherits
// core Eraser selection over the composite Layout and never reads it.
static uintptr_t const kDrawingBackendRestrictedToolOffsets[] = {
    0x44, 0x54, 0x5c
};
static char const* const kDrawingBackendRestrictedToolNames[] = {
    "pen(kinds0-2)", "selector(kind3)", "brush(kind6)"
};
static uintptr_t const kDrawingBackendEraserToolOffset = 0x4c;
// DrawingEraser::selectionFromPoints is vslot +0x78 and returns Selection by
// hidden result pointer on ARM. The replacement calls the original first, then
// intersects that polygon hit selection with the eraser's restrictedLayer().
// Mode 2 is Intersection, proven by Selector::computeSelection at BN
// 0xa8ced8 (selectLayer/Replace) and 0xa8d082 (selectPolygon/Intersection).
static uintptr_t const kDrawingEraserSelectionFromPointsVtableSlot = 0x78;
static uintptr_t const kDiagramPenPenDownVtableSlot = 0x3c;
static uintptr_t const kDiagramPenPenDownVma = 0x7af1e4;
static uintptr_t const kDrawingEraserSelectionFromPointsVma = 0x562838;
static uintptr_t const kPlainDrawingEraserSelectionFromPointsVma = 0x421314;
static uintptr_t const kCoreEraserSelectionFromPointsVma = 0xa77470;
// Core Eraser::selectionFromPoints multiplies these exact fields before
// calling strokerPolygon (BN 0xa8748c, 0xa87492, 0xa874a8).
static uintptr_t const kEraserWidthOffset = 0xcc;
static uintptr_t const kEraserViewScaleOffset = 0x110;
// DiagramEraser's inherited +0x78 selectionFromPoints slot is replaced on the
// exact DiagramEraser clone to enforce layer-first polygon selection. Its +0x7c
// updateSelection slot (BN 0x76b73c) is also replaced: the stock diagram
// override rebuilds the erase set from Diagram::layoutGroup ->
// selectByType(0xcc4) -> itemsIntersecting (BN 0x76b784/0x76b7ae/0x76b7da),
// which omits custom named-layer ink, then commits the empty set through core
// Eraser::updateSelection. Our +0x7c wrapper runs the stock override unchanged
// for the document/base layer. For plugin-created cnt.layer.* layers it calls
// the base Eraser::updateSelection directly with the +0x78 layer-scoped
// selection, avoiding the diagram-only narrowing which has been observed not
// to return for one valid custom layer. The same removal sink (Eraser+0xd0) is
// drained by DiagramEraser::eraseSelection from core Eraser::penUp.
static uintptr_t const kDiagramEraserUpdateSelectionVtableSlot = 0x7c;
static uintptr_t const kDiagramEraserEraseSelectionVtableSlot = 0x80;
static uintptr_t const kDiagramEraserUpdateSelectionVma = 0x75b73c;
static uintptr_t const kDiagramEraserEraseSelectionVma = 0x75c224;
static uintptr_t const kCoreEraserUpdateSelectionVma = 0xa75ce4;
static uintptr_t const kEraserFinalSelectionOffset = 0xd0;
static char const kCustomLayerIdPrefix[] = "cnt.layer.";
static int const kSelectionModeReplace = 0;
static int const kSelectionModeIntersect = 2;
// ToolDispatcher maps eraser.policy="stroke" to 0 and "precise" to 1
// (BN 0x5d3998/0x5d3b1e). Only policy 0 receives whole-stroke expansion.
static int const kEraserPolicyStroke = 0;
// snt::PlatformInputDispatcher stores the GridBoxFactory (a CompositeBoxFactory)
// shared_ptr at +0x24; CompositeBoxFactory::mainBackend() yields the active
// DrawingBackend. Recovered from activatePenCategoryForMainBackend (0x5bcd2c).
static uintptr_t const kPidBoxFactoryOffset = 0x24;
static uintptr_t const kActiveBackendGetToolVtableSlot = 0x2c;
static uintptr_t const kPackagePartCountVma = 0x3eeb40;
static uintptr_t const kPackageGetPartVma = 0x3f4850;
static uintptr_t const kPackageIndexOfPartVma = 0x3f2238;
static uintptr_t const kPackageCreatePartVma = 0x3f2ecc;
static uintptr_t const kPackageClonePartVma = 0x3f3d5c;
static uintptr_t const kPackageRemovePartVma = 0x3f27b0;
static uintptr_t const kPackageSaveVma = 0x3f1960;
static uintptr_t const kEngineOpenPackageVma = 0x4470fc;
static uintptr_t const kDocumentPageCountVma = 0x9f17cc;
static uintptr_t const kDocumentMovePageVma = 0x9f09e4;
static uintptr_t const kIInkStringCtorVma = 0x411c3c;
