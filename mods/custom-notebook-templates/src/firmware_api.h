#pragma once

#include "abi_types.h"

#include <QMap>
#include <QString>

#include <cstdint>
#include <string>

// Resolved firmware entry points and image-derived addresses. This stays a
// plain, value-initialized aggregate: resolution remains explicit and feature
// gates continue to decide which independently optional groups are usable.
struct FirmwareApi {
    BackgroundOptions backgroundOptionsOriginal;
    CreateIInkMenuItem createIInkMenuItem;
    ToolMenuTapGesture toolMenuTapGesture;
    MenuSelectBackground menuSelectBackground;
    SetBackgroundType setBackgroundTypeOriginal;
    SetToolTheme setToolThemeOriginal;
    RenderVolume renderVolumeOriginal;
    ToolMenuConstructor toolMenuConstructorOriginal;
    CreateBrushSizeRow createBrushSizeRowOriginal;
    SetBrushSizeIndex setBrushSizeIndexOriginal;
    SetActiveTool setActiveToolOriginal;
    WidgetSave widgetSave;
    WidgetFilePath widgetFilePath;
    WidgetRefresh widgetRefresh;
    ShowErrorPopup showErrorPopup;
    BackgroundType backgroundType;
    IInkToolMenuWidgetConstructor iInkToolMenuWidgetConstructor;
    IInkToolMenuWidgetSetSelected iInkToolMenuWidgetSetSelected;

    uintptr_t iinknoteBase;
    QMap<QString, QString>* rendererMap;

    SetDialogTitle setDialogTitleOriginal;
    NickelTouchMenuConstructor nickelTouchMenuConstructor;
    NickelTouchMenuSetAlignment nickelTouchMenuSetAlignment;
    TouchMenuSetCustomPopupPositionOffset touchMenuSetCustomPopupPositionOffset;
    AbstractNickelMenuControllerPopupFromWidget
        abstractNickelMenuControllerPopupFromWidget;
    NickelTouchMenuPopupPosition nickelTouchMenuPopupPosition;
    AbstractMenuControllerGrabTapGesture abstractMenuControllerGrabTapGesture;
    ToolMenuTapGesture abstractMenuControllerTapGesture;
    AddWidgetAction addWidgetActionOriginal;
    ParserImageParsed parserImageParsedOriginal;
    VolumeLoadCover volumeLoadCoverOriginal;
    PixmapSetImage pixmapSetImage;
    ContentGetId contentGetId;
    ContentGetImageId contentGetImageId;
    ExcludeSyncFolders excludeSyncFoldersOriginal;
    RemoveCommonBookData removeCommonBookDataOriginal;

    EditorGetPart editorGetPart;
    EditorGetEngine editorGetEngine;
    EditorGetConfiguration editorGetConfiguration;
    EditorGetRenderer editorGetRenderer;
    EditorSetPart editorSetPart;
    PartGetPackage partGetPackage;
    PartGetId partGetId;
    PartGetPage partGetPage;
    PageDocument pageDocument;
    PageDestructor pageDestructor;
    PageLayout pageLayout;
    LayoutDestructor layoutDestructor;
    LayoutAppendLayer layoutAppendLayer;
    LayoutRemoveLayer layoutRemoveLayer;
    AtkLayoutRawLayout atkLayoutRawLayout;
    DocumentLayoutGetLayer documentLayoutGetLayer;
    LayerIteratorIsAtEnd layerIteratorIsAtEnd;
    ManagedObjectDestructor managedObjectDestructor;
    RendererGetBackend rendererGetBackend;
    RendererRestrictToLayers rendererRestrictToLayers;
    PageControllerExportToPng pageControllerExportToPng;
    BackendImageDrawerMakeShared backendImageDrawerMakeShared;
    RendererListenerExport stockBackendImageDrawerExport;
    PageControllerInputDispatcher pageControllerInputDispatcher;
    PlatformInputDispatcherGetCurrentTool platformInputDispatcherGetCurrentTool;
    DynamicToolDispatcherCast dynamicToolDispatcherCast;
    ToolDispatcherRestrictToLayer toolDispatcherRestrictToLayer;
    CompositeBoxFactoryMainBackend compositeBoxFactoryMainBackend;
    CompositeBoxFactoryBackends compositeBoxFactoryBackends;
    SelectionSelectLayer selectionSelectLayer;
    SelectionSelectPolygon selectionSelectPolygon;
    SelectionSelectNone selectionSelectNone;
    SelectionIsEmpty selectionIsEmpty;
    SelectionAdjustToStrokeBoundaries selectionAdjustToStrokeBoundaries;
    EraserSetPolicy eraserSetPolicy;
    EraserPolicy eraserPolicy;
    EraserSetRadius eraserSetRadius;
    EraserRadius eraserRadius;
    EraserWidthFromThicknessRatio eraserWidthFromThicknessRatio;
    LayoutGridLineGap layoutGridLineGap;
    EraserStrokerPolygon eraserStrokerPolygon;
    IInkStringToStdString iinkStringToStdString;
    // Base Eraser::updateSelection is best-effort and may remain null. It lets
    // the diagram eraser re-inject a custom-layer selection after the stock
    // diagram override narrows the committed selection.
    CoreEraserUpdateSelectionFn coreEraserUpdateSelection;
    PackagePartCount packagePartCount;
    PackageGetPart packageGetPart;
    PackageIndexOfPart packageIndexOfPart;
    PackageCreatePart packageCreatePart;
    PackageClonePart packageClonePart;
    PackageRemovePart packageRemovePart;
    PackageSave packageSave;
    EngineOpenPackage engineOpenPackage;
    DocumentPageCount documentPageCount;
    DocumentMovePage documentMovePage;
    IInkStringCtor iinkStringCtor;

    DrawingEraserSelectionFromPointsFn
        drawingEraserSelectionFromPointsOriginal;
    DrawingEraserSelectionFromPointsFn
        plainDrawingEraserSelectionFromPointsOriginal;
    DrawingEraserSelectionFromPointsFn
        diagramEraserSelectionFromPointsOriginal;
    DiagramEraserUpdateSelectionFn diagramEraserUpdateSelectionOriginal;
    DiagramEraserEraseSelectionFn diagramEraserEraseSelectionOriginal;
    DiagramPenPenDownFn diagramPenPenDownOriginal;

    std::string const* documentLayerName;
    std::string const* backgroundObjectLayerName;
    void* neboBackendVtable;
    void* drawingBackendVtable;
    void* drawingEraserVtable;
    void* plainDrawingEraserVtable;
    void* diagramEraserVtable;
    void* coreEraserVtable;
    void* textEraserSntVtable;
    void* mathEraserVtable;
    void* textEraserVtable;
    void* diagramPenVtable;

    ImagePainterConstructor imagePainterConstructor;
    ImagePainterDeletingDestructor imagePainterDeletingDestructor;
    ImagePainterSetImageLoader imagePainterSetImageLoader;
    void* uirefEditorWidgetVtable;
};
