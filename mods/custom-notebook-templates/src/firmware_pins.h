#pragma once

#include <cstdint>

// Binary Ninja image addresses minus its 0x10000 analysis base.
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
static char const kMenuSelectBackgroundSymbol[] = "_ZN18IInkMenuController16selectBackgroundEv";
static char const kSetBackgroundTypeSymbol[] = "_ZN17IInkNotePadWidget17setBackgroundTypeERK7QString";
static char const kWidgetSaveSymbol[] = "_ZN17IInkNotePadWidget4saveEv";
static char const kWidgetFilePathSymbol[] = "_ZNK17IInkNotePadWidget8filePathEv";
static char const kWidgetRefreshSymbol[] = "_ZN17IInkNotePadWidget7refreshEv";
static char const kShowErrorPopupSymbol[] = "_ZN17IInkNotePadWidget14showErrorPopupERK7QString";
static char const kBackgroundTypeSymbol[] = "_ZN16BackgroundWidget14backgroundTypeEv";
static char const kContentGetIdSymbol[] = "_ZNK7Content5getIdEv";

static uintptr_t const kCreateIInkMenuItemVma = 0x46bc4;
static uintptr_t const kToolMenuTapGestureVma = 0x75a4c;
static uintptr_t const kNickelTouchMenuConstructorVma = 0xed6788;
static uintptr_t const kNickelTouchMenuSetAlignmentVma = 0xed6a68;
static uintptr_t const kTouchMenuSetCustomPopupPositionOffsetVma = 0x11150a8;
static uintptr_t const kAbstractNickelMenuControllerPopupFromWidgetVma =
    0xeb4f94;
static uintptr_t const kNickelTouchMenuPopupPositionVma = 0xed7248;
static uintptr_t const kIInkToolMenuWidgetConstructorVma = 0x761ac;
static uintptr_t const kIInkToolMenuWidgetSetSelectedVma = 0x76580;
static uintptr_t const kAbstractMenuControllerGrabTapGestureVma = 0xb3cfac;
static uintptr_t const kAbstractMenuControllerTapGestureVma = 0xb3d00c;
static uintptr_t const kMenuSelectBackgroundVma = 0x49438;
static uintptr_t const kSetBackgroundTypeVma = 0x5b9fc;
static uintptr_t const kWidgetSaveVma = 0x5f780;
static uintptr_t const kWidgetFilePathVma = 0x58da8;
static uintptr_t const kWidgetRefreshVma = 0x57fec;
static uintptr_t const kShowErrorPopupVma = 0x58b64;
static uintptr_t const kBackgroundTypeVma = 0x41c64;
static uintptr_t const kContentGetIdVma = 0x953d84;

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
static char const kRendererGetViewTransformSymbol[] =
    "_ZNK8myscript4iink12RendererImpl16getViewTransformEv";
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
static uintptr_t const kRendererGetViewTransformVma = 0x49df8c;
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
static uintptr_t const kCoreEraserUpdateSelectionVma = 0xa75ce4;
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
