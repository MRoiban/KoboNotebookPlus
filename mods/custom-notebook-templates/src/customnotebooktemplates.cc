#include <QAction>
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPoint>
#include <QPointer>
#include <QPixmap>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <map>
#include <new>
#include <string>
#include <stdexcept>
#include <vector>
#include <sys/statvfs.h>
#include <unistd.h>

#include <NickelHook.h>

#include "covermenureceiver.h"

namespace {

// Recovered from libnickel.so.1.0.0 on firmware 4.38.23697. The three
// accessors read QString fields at offsets 0, 4, and 8 respectively.
struct BackgroundOption {
    QString type;
    QString path;
    QString text;

    BackgroundOption() {}
    BackgroundOption(QString const& type_, QString const& path_, QString const& text_)
        : type(type_), path(path_), text(text_) {}
};

struct CustomTemplate {
    QString id;
    QString label;
    QString icon;
    QString backgroundBase;
};

// One archive-scan verdict per canonical onboard .nebo path. Negative results
// (no custom cover, or an ambiguous marker count) are cached too, so uncovered
// notebooks stop paying the ZIP scan on every tile reload. Entries are reused
// only while the notebook's modification time and size are both unchanged;
// busy or unreadable archives are never cached.
struct CoverScanEntry {
    qint64 modifiedMs;
    qint64 size;
    QString type;
    bool hasCover;
};

// Kobo's own rendered first-page preview, including the selected cover and
// live ink. The parser callback populates this cache; the notebook-card hook
// may reuse it only while the source .nebo has the same mtime and size.
struct RenderedCoverEntry {
    QImage image;
    qint64 notebookModifiedMs;
    qint64 notebookSize;
    QString coverType;
    quint64 sequence;
};

// Clean source covers are the immediate notebook-card fallback until Kobo's
// thumbnail service supplies an ink render. Revalidate against the source PNG
// so replacing a cover file never leaves a stale decoded image in memory.
struct CleanCoverEntry {
    QImage image;
    qint64 pngModifiedMs;
    qint64 pngSize;
    quint64 sequence;
};

// ABI-only declarations for MyScript objects. Most come from libiink-owned
// shared_ptr control blocks. Layer preview export additionally constructs the
// stock UI-reference ImagePainter and stock BackendImageDrawer through their
// pinned constructors/factory; their original destructors/control blocks still
// own all cleanup.
struct ContentPartOpaque;
struct ContentPackageOpaque;
struct DocumentOpaque;
struct EngineOpaque;
struct ConfigurationOpaque;
struct RendererOpaque;
struct RendererListenerOpaque;
struct ImagePainterOpaque;
struct ImageLoaderOpaque;
struct BackendImageDrawerOpaque;
struct BoxOpaque;
struct ToolOpaque;
struct ToolDispatcherOpaque;
struct PlatformInputDispatcherOpaque;
struct ActiveBackendOpaque;
struct ZipArchiveOpaque;
struct ZipFileOpaque;
class NickelTouchMenu;

typedef std::shared_ptr<ContentPartOpaque> SharedPart;
typedef std::shared_ptr<ContentPackageOpaque> SharedPackage;
typedef std::shared_ptr<DocumentOpaque> SharedDocument;
typedef std::shared_ptr<EngineOpaque> SharedEngine;
typedef std::shared_ptr<ConfigurationOpaque> SharedConfiguration;
typedef std::shared_ptr<RendererOpaque> SharedRenderer;
typedef std::shared_ptr<RendererListenerOpaque> SharedRendererListener;
typedef std::shared_ptr<ImagePainterOpaque> SharedImagePainter;
typedef std::shared_ptr<ImageLoaderOpaque> SharedImageLoader;
typedef std::shared_ptr<BackendImageDrawerOpaque> SharedBackendImageDrawer;
typedef std::shared_ptr<BoxOpaque> SharedBox;
typedef std::shared_ptr<ToolOpaque> SharedTool;
typedef std::shared_ptr<ToolDispatcherOpaque> SharedToolDispatcher;
typedef std::shared_ptr<PlatformInputDispatcherOpaque> SharedPlatformInputDispatcher;
typedef std::shared_ptr<ActiveBackendOpaque> SharedActiveBackend;

// atk::core::Extent is a four-float homogeneous aggregate under ARM's
// hard-float ABI. PageController::exportToPNG passes it in s0-s3 to its one
// RendererListener callback.
struct ExtentOpaque {
    float x;
    float y;
    float width;
    float height;
};
static_assert(sizeof(ExtentOpaque) == 16, "unexpected Extent ABI");

// atk::core::Point is two floats. The PointerInfo vector remains opaque: it
// is passed by reference to Kobo's own Eraser::strokerPolygon implementation,
// so the plugin never needs to construct or inspect a PointerInfo.
struct SelectionPointOpaque {
    float x;
    float y;
};
static_assert(sizeof(SelectionPointOpaque) == 8, "unexpected Point ABI");
struct PointerInfoVectorOpaque {};

// myscript::iink::String is one pointer to a polymorphic StringImpl on this
// SDK. Its public const-char constructor is resolved from the pinned libiink.
struct IInkStringImpl {
    virtual ~IInkStringImpl() {}
};

struct IInkStringStorage {
    IInkStringImpl* impl;
};

typedef QVector<BackgroundOption> (*BackgroundOptions)();
typedef void (*SetDialogTitle)(void* dialog, QString const& title);
typedef QWidget* (*CreateIInkMenuItem)(
    void* controller,
    QMenu* menu,
    QString const& text,
    QPixmap const& pixmap,
    bool pixmapOnLeft);
typedef void (*NickelTouchMenuConstructor)(
    void* menu,
    QWidget* parent,
    int decorationPosition);
typedef void (*NickelTouchMenuSetAlignment)(
    void* menu,
    Qt::Alignment alignment);
typedef void (*TouchMenuSetCustomPopupPositionOffset)(
    void* menu,
    QPoint const& offset);
typedef QWidget* (*AbstractNickelMenuControllerPopupFromWidget)(
    void const* controller);
typedef QPoint (*NickelTouchMenuPopupPosition)(
    void* menu,
    QWidget* anchor);
static_assert(sizeof(QPoint) == sizeof(int) * 2,
    "unexpected Qt 5 QPoint ABI");
typedef void (*IInkToolMenuWidgetConstructor)(
    void* widget,
    QWidget* parent);
typedef void (*IInkToolMenuWidgetSetSelected)(
    void* widget,
    bool selected);
typedef void (*AbstractMenuControllerGrabTapGesture)(
    void* controller,
    void* gestureReceiver);
// Bool semantics recovered from AbstractMenuController::createAction at
// libnickel VMA 0xb3cd80: the fifth argument wires the action to the menu's
// close path, the sixth is QAction::setEnabled, and the seventh appends a
// QMenu separator after the row, which is how the stock menu forms sections.
typedef void (*AddWidgetAction)(
    void* controller,
    QMenu* menu,
    QWidget* widget,
    QObject* receiver,
    char const* member,
    bool closesMenu,
    bool enabled,
    bool separatorAfter);
typedef void (*ToolMenuTapGesture)(void* controller, void* receiver, void* tap);
typedef void (*MenuSelectBackground)(void* controller);
typedef void (*SetBackgroundType)(void* widget, QString const& type);
typedef void (*SetToolTheme)(void* widget, void* theme);
// IInkToolMenuController's fifth constructor argument is a non-trivial
// IInkToolTheme passed indirectly by the ARM C++ ABI. Keep it opaque while
// preserving the exact machine-level r0-r3 + first-stack-word call shape.
typedef void (*ToolMenuConstructor)(
    void* controller,
    QWidget* parent,
    QVector<int> const* tools,
    QVector<int> const* brushSections,
    void* themeStorage);
typedef void (*CreateBrushSizeRow)(
    void* controller,
    NickelTouchMenu* menu,
    QString const& title);
typedef void (*SetBrushSizeIndex)(void* controller, int index);
typedef void (*SetActiveTool)(void* widget, int tool);
typedef void (*RenderVolume)(void* widget, void const* volume);
typedef void (*WidgetSave)(void* widget);
typedef QString (*WidgetFilePath)(void const* widget);
typedef void (*WidgetRefresh)(void* widget);
typedef void (*ShowErrorPopup)(void* widget, QString const& message);
typedef QString (*BackgroundType)(void* backgroundWidget);
typedef void (*ParserImageParsed)(
    void* parser,
    void const* volume,
    QImage const& image);
typedef void (*VolumeLoadCover)(void* view);
typedef void (*PixmapSetImage)(
    void* view,
    QImage const& image,
    QString const& imageId);
typedef QString (*ContentGetId)(void const* content);
typedef QString (*ContentGetImageId)(void const* content);
typedef QString (*ExcludeSyncFolders)(void* featureSettings);
typedef void (*RemoveCommonBookData)(
    void* manager,
    void const* device,
    void* volume,
    bool removeBackingFile);

typedef SharedPart (*EditorGetPart)(void* editor);
typedef SharedEngine (*EditorGetEngine)(void* editor);
typedef SharedConfiguration (*EditorGetConfiguration)(void* editor);
typedef SharedRenderer (*EditorGetRenderer)(void* editor);
typedef void (*EditorSetPart)(void* editor, SharedPart part);
typedef SharedPackage (*PartGetPackage)(void const* part);
typedef void (*PartGetId)(IInkStringStorage* result, void const* part);
typedef SharedDocument (*PageDocument)(void const* page);
typedef void (*PartGetPage)(void* result, void const* part);
typedef void (*PageDestructor)(void* page);
typedef void (*PageLayout)(void* result, void const* page);
typedef void (*LayoutDestructor)(void* layout);
typedef bool (*LayoutAppendLayer)(void* layout, std::string const& id);
typedef void (*LayoutRemoveLayer)(void* layout, std::string const& id);
typedef void (*AtkLayoutRawLayout)(void* result, void const* layout);
typedef void (*DocumentLayoutGetLayer)(
    void* result,
    void const* layout,
    std::string const& id);
typedef bool (*LayerIteratorIsAtEnd)(void const* iterator);
typedef void (*ManagedObjectDestructor)(void* object);
typedef void* (*RendererGetBackend)(void* renderer);
typedef void (*RendererRestrictToLayers)(
    void* renderer,
    std::vector<std::string> const& ids);
typedef bool (*PageControllerExportToPng)(
    void* controller,
    SharedBox box,
    std::string const& path,
    SharedRendererListener listener,
    unsigned int flags);
typedef void (*RendererListenerExport)(
    void* listener,
    SharedRenderer const& renderer,
    void const* selection,
    ExtentOpaque extent,
    unsigned int flags,
    std::string const& path);
typedef void (*ImagePainterConstructor)(void* painter);
typedef void (*ImagePainterDeletingDestructor)(void* painter);
typedef void (*ImagePainterSetImageLoader)(
    void* painter,
    SharedImageLoader loader);
// AAPCS shape of libiink's exported make_shared helper: r0 is the result,
// r1/r2 are empty tag/allocator placeholders, r3 is &engine, then painter and
// configuration references occupy the first two stack words.
typedef void (*BackendImageDrawerMakeShared)(
    SharedBackendImageDrawer* result,
    uintptr_t emptyTag,
    void const* allocator,
    SharedEngine* engine,
    SharedImagePainter* painter,
    SharedConfiguration* configuration);
typedef SharedPlatformInputDispatcher (*PageControllerInputDispatcher)(
    void const* controller);
typedef SharedTool (*PlatformInputDispatcherGetCurrentTool)(
    void const* dispatcher);
typedef SharedActiveBackend (*CompositeBoxFactoryMainBackend)(
    void const* factory);
typedef std::map<std::string, SharedActiveBackend> ActiveBackendMap;
typedef ActiveBackendMap (*CompositeBoxFactoryBackends)(
    void const* factory);
typedef void* (*SelectionSelectLayer)(
    void* selection,
    std::string const& id,
    int mode);
typedef void* (*SelectionSelectPolygon)(
    void* selection,
    SelectionPointOpaque const* points,
    int count,
    int mode);
typedef void* (*SelectionSelectNone)(void* selection);
typedef bool (*SelectionIsEmpty)(void const* selection);
typedef void* (*SelectionAdjustToStrokeBoundaries)(
    void* selection,
    float ratio);
typedef int (*EraserPolicy)(void const* eraser);
typedef void (*EraserSetRadius)(void* eraser, float radius);
typedef float (*EraserRadius)(void const* eraser);
typedef float (*EraserWidthFromThicknessRatio)(
    float thicknessRatio,
    float lineGap);
typedef float (*LayoutGridLineGap)(void const* layoutGrid);
typedef std::vector<SelectionPointOpaque> (*EraserStrokerPolygon)(
    float width,
    PointerInfoVectorOpaque const& points);
typedef SharedToolDispatcher (*DynamicToolDispatcherCast)(
    SharedTool const& tool);
typedef void (*ToolDispatcherRestrictToLayer)(
    void* dispatcher,
    std::string const& id);
typedef void (*IInkStringToStdString)(
    std::string* result,
    IInkStringStorage const* value);
typedef int (*PackagePartCount)(void const* package);
typedef SharedPart (*PackageGetPart)(void* package, int index);
typedef int (*PackageIndexOfPart)(void* package, SharedPart part);
typedef SharedPart (*PackageCreatePart)(void* package, IInkStringStorage const& type);
typedef SharedPart (*PackageClonePart)(void* package, SharedPart part);
typedef void (*PackageRemovePart)(void* package, SharedPart part);
typedef void (*PackageSave)(void* package);
typedef SharedPackage (*EngineOpenPackage)(
    void* engine,
    IInkStringStorage const& path,
    int openOption);
typedef int (*DocumentPageCount)(void const* document);
typedef void (*DocumentMovePage)(void* document, int from, int to);
typedef void (*IInkStringCtor)(IInkStringStorage* value, char const* utf8);

typedef ZipArchiveOpaque* (*ZipOpen)(char const* path, int flags, int* error);
typedef long long (*ZipGetNumEntries)(ZipArchiveOpaque* archive, unsigned int flags);
typedef char const* (*ZipGetName)(
    ZipArchiveOpaque* archive,
    unsigned long long index,
    unsigned int flags);
typedef ZipFileOpaque* (*ZipFopen)(
    ZipArchiveOpaque* archive,
    char const* name,
    unsigned int flags);
typedef long long (*ZipFread)(
    ZipFileOpaque* file,
    void* buffer,
    unsigned long long bytes);
typedef int (*ZipFclose)(ZipFileOpaque* file);
typedef void (*ZipDiscard)(ZipArchiveOpaque* archive);

union BackgroundOptionsAddress {
    BackgroundOptions function;
    void* pointer;
};

union AddWidgetActionAddress {
    AddWidgetAction function;
    void* pointer;
};

union SetDialogTitleAddress {
    SetDialogTitle function;
    void* pointer;
};

union ParserImageParsedAddress {
    ParserImageParsed function;
    void* pointer;
};

union VolumeLoadCoverAddress {
    VolumeLoadCover function;
    void* pointer;
};

union SetToolThemeAddress {
    SetToolTheme function;
    void* pointer;
};

union RenderVolumeAddress {
    RenderVolume function;
    void* pointer;
};

union ToolMenuConstructorAddress {
    ToolMenuConstructor function;
    void* pointer;
};

union CreateBrushSizeRowAddress {
    CreateBrushSizeRow function;
    void* pointer;
};

union SetBrushSizeIndexAddress {
    SetBrushSizeIndex function;
    void* pointer;
};

union SetActiveToolAddress {
    SetActiveTool function;
    void* pointer;
};

union ExcludeSyncFoldersAddress {
    ExcludeSyncFolders function;
    void* pointer;
};

union RemoveCommonBookDataAddress {
    RemoveCommonBookData function;
    void* pointer;
};

extern "C" __attribute__((visibility("hidden")))
void* nh_dlhook(void* handle, char const* symbol, void* replacement);

extern "C" __attribute__((visibility("default")))
QVector<BackgroundOption> _cnt_background_options_hook();

extern "C" __attribute__((visibility("default")))
void _cnt_add_widget_action_hook(
    void* controller,
    QMenu* menu,
    QWidget* widget,
    QObject* receiver,
    char const* member,
    bool closesMenu,
    bool enabled,
    bool separatorAfter);

extern "C" __attribute__((visibility("default")))
void _cnt_set_dialog_title_hook(void* dialog, QString const& title);

extern "C" __attribute__((visibility("default")))
void _cnt_parser_image_parsed_hook(
    void* parser,
    void const* volume,
    QImage const& image);

extern "C" __attribute__((visibility("default")))
void _cnt_volume_load_cover_hook(void* view);

extern "C" __attribute__((visibility("default")))
void _cnt_set_tool_theme_hook(void* widget, void* theme);

extern "C" __attribute__((visibility("default")))
void _cnt_render_volume_hook(void* widget, void const* volume);

extern "C" __attribute__((visibility("default")))
void _cnt_tool_menu_constructor_hook(
    void* controller,
    QWidget* parent,
    QVector<int> const* tools,
    QVector<int> const* brushSections,
    void* themeStorage);

extern "C" __attribute__((visibility("default")))
void _cnt_create_brush_size_row_hook(
    void* controller,
    NickelTouchMenu* menu,
    QString const& title);

extern "C" __attribute__((visibility("default")))
void _cnt_set_brush_size_index_hook(void* controller, int index);

extern "C" __attribute__((visibility("default")))
void _cnt_set_active_tool_hook(void* widget, int tool);

extern "C" __attribute__((visibility("default")))
QString _cnt_exclude_sync_folders_hook(void* featureSettings);

extern "C" __attribute__((visibility("default")))
void _cnt_remove_common_book_data_hook(
    void* manager,
    void const* device,
    void* volume,
    bool removeBackingFile);

static BackgroundOptions backgroundOptionsOriginal = nullptr;
static SetDialogTitle setDialogTitleOriginal = nullptr;
static CreateIInkMenuItem createIInkMenuItem = nullptr;
static NickelTouchMenuConstructor nickelTouchMenuConstructor = nullptr;
static NickelTouchMenuSetAlignment nickelTouchMenuSetAlignment = nullptr;
static TouchMenuSetCustomPopupPositionOffset
    touchMenuSetCustomPopupPositionOffset = nullptr;
static AbstractNickelMenuControllerPopupFromWidget
    abstractNickelMenuControllerPopupFromWidget = nullptr;
static NickelTouchMenuPopupPosition nickelTouchMenuPopupPosition = nullptr;
static IInkToolMenuWidgetConstructor iInkToolMenuWidgetConstructor = nullptr;
static IInkToolMenuWidgetSetSelected iInkToolMenuWidgetSetSelected = nullptr;
static AbstractMenuControllerGrabTapGesture
    abstractMenuControllerGrabTapGesture = nullptr;
static ToolMenuTapGesture abstractMenuControllerTapGesture = nullptr;
static AddWidgetAction addWidgetActionOriginal = nullptr;
static ToolMenuTapGesture toolMenuTapGesture = nullptr;
static MenuSelectBackground menuSelectBackground = nullptr;
static SetBackgroundType setBackgroundTypeOriginal = nullptr;
static SetToolTheme setToolThemeOriginal = nullptr;
static RenderVolume renderVolumeOriginal = nullptr;
static ToolMenuConstructor toolMenuConstructorOriginal = nullptr;
static CreateBrushSizeRow createBrushSizeRowOriginal = nullptr;
static SetBrushSizeIndex setBrushSizeIndexOriginal = nullptr;
static SetActiveTool setActiveToolOriginal = nullptr;
static WidgetSave widgetSave = nullptr;
static WidgetFilePath widgetFilePath = nullptr;
static WidgetRefresh widgetRefresh = nullptr;
static ShowErrorPopup showErrorPopup = nullptr;
static BackgroundType backgroundType = nullptr;
static ParserImageParsed parserImageParsedOriginal = nullptr;
static VolumeLoadCover volumeLoadCoverOriginal = nullptr;
static PixmapSetImage pixmapSetImage = nullptr;
static ContentGetId contentGetId = nullptr;
static ContentGetImageId contentGetImageId = nullptr;
static ExcludeSyncFolders excludeSyncFoldersOriginal = nullptr;
static RemoveCommonBookData removeCommonBookDataOriginal = nullptr;
static EditorGetPart editorGetPart = nullptr;
static EditorGetEngine editorGetEngine = nullptr;
static EditorGetConfiguration editorGetConfiguration = nullptr;
static EditorGetRenderer editorGetRenderer = nullptr;
static EditorSetPart editorSetPart = nullptr;
static PartGetPackage partGetPackage = nullptr;
static PartGetId partGetId = nullptr;
static PartGetPage partGetPage = nullptr;
static PageDocument pageDocument = nullptr;
static PageDestructor pageDestructor = nullptr;
static PageLayout pageLayout = nullptr;
static LayoutDestructor layoutDestructor = nullptr;
static LayoutAppendLayer layoutAppendLayer = nullptr;
static LayoutRemoveLayer layoutRemoveLayer = nullptr;
static AtkLayoutRawLayout atkLayoutRawLayout = nullptr;
static DocumentLayoutGetLayer documentLayoutGetLayer = nullptr;
static LayerIteratorIsAtEnd layerIteratorIsAtEnd = nullptr;
static ManagedObjectDestructor managedObjectDestructor = nullptr;
static RendererGetBackend rendererGetBackend = nullptr;
static RendererRestrictToLayers rendererRestrictToLayers = nullptr;
static PageControllerExportToPng pageControllerExportToPng = nullptr;
static ImagePainterConstructor imagePainterConstructor = nullptr;
static ImagePainterDeletingDestructor imagePainterDeletingDestructor = nullptr;
static ImagePainterSetImageLoader imagePainterSetImageLoader = nullptr;
static void* uirefEditorWidgetVtable = nullptr;
static BackendImageDrawerMakeShared backendImageDrawerMakeShared = nullptr;
static RendererListenerExport stockBackendImageDrawerExport = nullptr;
static PageControllerInputDispatcher pageControllerInputDispatcher = nullptr;
static PlatformInputDispatcherGetCurrentTool
    platformInputDispatcherGetCurrentTool = nullptr;
static DynamicToolDispatcherCast dynamicToolDispatcherCast = nullptr;
static ToolDispatcherRestrictToLayer toolDispatcherRestrictToLayer = nullptr;
static CompositeBoxFactoryMainBackend compositeBoxFactoryMainBackend = nullptr;
static CompositeBoxFactoryBackends compositeBoxFactoryBackends = nullptr;
static SelectionSelectLayer selectionSelectLayer = nullptr;
static SelectionSelectPolygon selectionSelectPolygon = nullptr;
static SelectionSelectNone selectionSelectNone = nullptr;
static SelectionIsEmpty selectionIsEmpty = nullptr;
static SelectionAdjustToStrokeBoundaries
    selectionAdjustToStrokeBoundaries = nullptr;
static EraserPolicy eraserPolicy = nullptr;
static EraserSetRadius eraserSetRadius = nullptr;
static EraserRadius eraserRadius = nullptr;
static EraserWidthFromThicknessRatio eraserWidthFromThicknessRatio = nullptr;
static LayoutGridLineGap layoutGridLineGap = nullptr;
static EraserStrokerPolygon eraserStrokerPolygon = nullptr;
static IInkStringToStdString iinkStringToStdString = nullptr;
static std::string const* documentLayerName = nullptr;
static std::string const* backgroundObjectLayerName = nullptr;
static void* neboBackendVtable = nullptr;
static void* drawingBackendVtable = nullptr;
static void* drawingEraserVtable = nullptr;
static void* plainDrawingEraserVtable = nullptr;
static void* diagramEraserVtable = nullptr;
static void* coreEraserVtable = nullptr;
static void* textEraserSntVtable = nullptr;
static void* mathEraserVtable = nullptr;
static void* textEraserVtable = nullptr;
static void* diagramPenVtable = nullptr;
// snt::DrawingEraser, the global DrawingEraser, and the raw-content
// atk::diagram::DiagramEraser each have 148-byte Itanium ABI vtables. Separate
// writable, process-lifetime clones preserve every class override while
// replacing selectionFromPoints. The two DrawingErasers filter their stock
// hit selection. DiagramEraser rebuilds that hit selection in firmware's
// layer-first order. Stock libiink text and code remain untouched.
static size_t const kDrawingEraserVtableWords = 148 / sizeof(void*);
static void* layerAwareDrawingEraserVtable[kDrawingEraserVtableWords];
static void* layerAwarePlainDrawingEraserVtable[kDrawingEraserVtableWords];
static void* layerAwareDiagramEraserVtable[kDrawingEraserVtableWords];
static bool layerAwareDrawingEraserVtableReady = false;
static bool layerAwarePlainDrawingEraserVtableReady = false;
static bool layerAwareDiagramEraserVtableReady = false;
static bool layerAwareDiagramEraserObserversReady = false;
static int layerAwareEraserTraceBudget = 0;
static int layerAwareDiagramEraserObserverTraceBudget = 0;
typedef void* (*DrawingEraserSelectionFromPointsFn)(
    void* result,
    void* eraser,
    void const* points);
typedef uint32_t (*DiagramEraserUpdateSelectionFn)(
    void* eraser,
    void const* selection,
    void const* points);
typedef int32_t (*DiagramEraserEraseSelectionFn)(void* eraser);
// atk::core::Eraser::updateSelection(this, Selection const&, vector const&)
// unions its Selection argument into the eraser's committed selection at
// Eraser+0xd0 (verified: isEmpty guard then combine(this+0xd0, sel, 1)). The
// diagram override discards our layer-scoped selection and rebuilds from the
// diagram layoutGroup, so we call this base method directly to re-inject it.
typedef uint32_t (*CoreEraserUpdateSelectionFn)(
    void* eraser,
    void const* selection,
    void const* points);
static DrawingEraserSelectionFromPointsFn
    drawingEraserSelectionFromPointsOriginal = nullptr;
static DrawingEraserSelectionFromPointsFn
    plainDrawingEraserSelectionFromPointsOriginal = nullptr;
static DrawingEraserSelectionFromPointsFn
    diagramEraserSelectionFromPointsOriginal = nullptr;
static DiagramEraserUpdateSelectionFn
    diagramEraserUpdateSelectionOriginal = nullptr;
static DiagramEraserEraseSelectionFn
    diagramEraserEraseSelectionOriginal = nullptr;
static CoreEraserUpdateSelectionFn
    coreEraserUpdateSelection = nullptr;
// Raw-content pages cache one 152-byte DiagramPen vtable. A writable clone
// replaces only penDown so the selected layer can be reasserted at the last
// possible point before core Pen tags the new stroke. This guards against a
// later native tool refresh silently restoring an older layer between popup
// activation and physical pen-down; every other DiagramPen slot remains stock.
static size_t const kDiagramPenVtableWords = 152 / sizeof(void*);
static void* layerGuardedDiagramPenVtable[kDiagramPenVtableWords];
static bool layerGuardedDiagramPenVtableReady = false;
static int layerGuardedDiagramPenTraceBudget = 0;
typedef bool (*DiagramPenPenDownFn)(void* pen, void const* pointerInfo);
static DiagramPenPenDownFn diagramPenPenDownOriginal = nullptr;
static std::map<void*, std::string> desiredDiagramPenLayers;
static QMutex desiredDiagramPenLayersMutex;
static PackagePartCount packagePartCount = nullptr;
static PackageGetPart packageGetPart = nullptr;
static PackageIndexOfPart packageIndexOfPart = nullptr;
static PackageCreatePart packageCreatePart = nullptr;
static PackageClonePart packageClonePart = nullptr;
static PackageRemovePart packageRemovePart = nullptr;
static PackageSave packageSave = nullptr;
static EngineOpenPackage engineOpenPackage = nullptr;
static DocumentPageCount documentPageCount = nullptr;
static DocumentMovePage documentMovePage = nullptr;
static IInkStringCtor iinkStringCtor = nullptr;
static ZipOpen zipOpen = nullptr;
static ZipGetNumEntries zipGetNumEntries = nullptr;
static ZipGetName zipGetName = nullptr;
static ZipFopen zipFopen = nullptr;
static ZipFread zipFread = nullptr;
static ZipFclose zipFclose = nullptr;
static ZipDiscard zipDiscard = nullptr;
static void* zipLibraryHandle = nullptr;
static QVector<CustomTemplate> customTemplates;
static QVector<CustomTemplate> customCovers;
// Both caches are shared between the UI-thread loadCover hook and the parser
// thumbnail callback, so every access happens under coverCacheMutex. The lock
// is only ever held for map operations, never across archive scans, PNG
// decoding, or calls back into Qt or firmware code.
static QHash<QString, CoverScanEntry> coverScanCache;
static QHash<QString, RenderedCoverEntry> renderedCoverCache;
static quint64 renderedCoverSequence = 0;
static QHash<QString, CleanCoverEntry> cleanCoverCache;
static quint64 cleanCoverSequence = 0;
static QMutex coverCacheMutex;
static QMap<QString, QString>* rendererMap = nullptr;
static QTimer* hookTimer = nullptr;
static bool coverPickerPending = false;
static bool coverTitlePending = false;
static bool coverHooksReady = false;
static bool coverGridHookReady = false;
static bool pageManagerHooksReady = false;
static bool layerHooksReady = false;
static bool layerPreviewApisReady = false;
static bool eraserSizeApisReady = false;
static bool eraserSizeMenuHooksReady = false;
static bool notebookLifecycleHooksReady = false;
static QMutex customAssetExclusionTraceMutex;
static bool customAssetExclusionObserved = false;
static bool customAssetBackingFilePreserved = false;
static QMutex eraserSizeSettingsMutex;
static bool eraserSizeSettingsLoaded = false;
static int eraserSizeIndex = 2;
// Native tool-menu controllers are QObjects on Nickel's UI thread. QPointer
// lets the hardware-eraser hook update an open popup without retaining it or
// touching a controller after the menu has been destroyed.
static QPointer<QObject> liveEraserSizeController;
static uintptr_t iinknoteBase = 0;

static char const kManifest[] = "/mnt/onboard/.kobo/custom/templates/templates.json";
static char const kTemplateRoot[] = "/mnt/onboard/.kobo/custom/templates/";
static char const kCoverRoot[] = "/mnt/onboard/.kobo/custom/covers/";
static char const kCoverBackupRoot[] = "/mnt/onboard/.kobo/custom/covers/backups/";
static char const kPageManagerRoot[] = "/mnt/onboard/.kobo/custom/page-manager/";
static char const kPageBackupRoot[] = "/mnt/onboard/.kobo/custom/page-manager/backups/";
static char const kPageTransactionRoot[] = "/mnt/onboard/.kobo/custom/page-manager/transactions/";
static char const kLayerRoot[] = "/mnt/onboard/.kobo/custom/layers/";
static char const kLayerBackupRoot[] = "/mnt/onboard/.kobo/custom/layers/backups/";
static char const kLayerPreviewRoot[] = "/mnt/onboard/.kobo/custom/layers/previews/";
static char const kRenderedPreviewRoot[] = "/mnt/onboard/.kobo/custom/previews/";
// SyncFileSystemCommand anchors each semicolon-separated expression beneath
// /mnt/(sd|onboard). Hide the plugin's entire support tree from content import
// while preserving its existing paths for direct QFile/MyScript access.
static char const kCustomAssetExcludePattern[] =
    "(\\.kobo|koboExtStorage)/custom";
static char const kCustomAssetOnboardContentPrefix[] =
    "file:///mnt/onboard/.kobo/custom/";
static char const kCustomAssetOnboardExternalContentPrefix[] =
    "file:///mnt/onboard/koboExtStorage/custom/";
static char const kCustomAssetSdContentPrefix[] =
    "file:///mnt/sd/.kobo/custom/";
static char const kCustomAssetSdExternalContentPrefix[] =
    "file:///mnt/sd/koboExtStorage/custom/";
static char const kCondorSuffix[] = "_condor.png";
static char const kTrace[] = "/mnt/onboard/.kobo/custom/templates/plugin-status.txt";
static char const kEraserSizeSettings[] =
    "/mnt/onboard/.kobo/custom/templates/eraser-size.json";
static int const kMaximumCustomTemplates = 32;
static int const kMaximumCustomCovers = 32;
static qint64 const kMaximumAutomaticPngSize = 32 * 1024 * 1024;
static int const kBackgroundWidth = 1404;
static int const kBackgroundHeight = 1872;
static int const kPickerIconSize = 280;
static int const kMaximumPageMetadataSize = 256 * 1024;
static int const kMaximumScanCacheEntries = 2048;
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
static qint64 const kMaximumImageCacheBytes = qint64(64) * 1024 * 1024;
static int const kMaximumPersistedPreviews = 128;

// Binary Ninja image addresses minus its 0x10000 analysis base.
static uintptr_t const kBackgroundOptionsVma = 0x78c9c;
static uintptr_t const kRendererMapVma = 0xa8b54;
static int const kExpectedBuiltinMapSize = 36;
static char const kBackgroundOptionsSymbol[] = "_ZN23BackgroundOptionsWidget17backgroundOptionsEv";
static char const kAddWidgetActionSymbol[] =
    "_ZN22AbstractMenuController15addWidgetActionEP5QMenuP7QWidgetP7QObjectPKcbbb";
static char const kCreateIInkMenuItemSymbol[] =
    "_ZN26IInkDropdownMenuController18createIInkMenuItemEP5QMenuRK7QStringRK7QPixmapb";
static char const kToolMenuTapGestureSymbol[] =
    "_ZN22IInkToolMenuController10tapGestureEP15GestureReceiverP10TapGesture";
static char const kToolMenuConstructorSymbol[] =
    "_ZN22IInkToolMenuControllerC1EP7QWidgetRK7QVectorI8IInkToolERKS2_I13IInkToolBrushE13IInkToolTheme";
static char const kCreateBrushSizeRowSymbol[] =
    "_ZN22IInkToolMenuController18createBrushSizeRowEP15NickelTouchMenuRK7QString";
static char const kSetBrushSizeIndexSymbol[] =
    "_ZN22IInkToolMenuController17setBrushSizeIndexEi";
static char const kSetActiveToolSymbol[] =
    "_ZN17IInkNotePadWidget13setActiveToolE8IInkTool";
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
static uintptr_t const kToolMenuConstructorVma = 0x74760;
static uintptr_t const kCreateBrushSizeRowVma = 0x74fcc;
static uintptr_t const kSetBrushSizeIndexVma = 0x74ba4;
static uintptr_t const kSetBrushSizeIndexClickReturnVma = 0x74d7c;
static uintptr_t const kSetBrushSizeIndexLoadReturnVma = 0x75702;
static uintptr_t const kSetActiveToolVma = 0x62194;
// stylusTouchBegin's unique hardware-eraser call returns here after loading
// the saved eraser enum from IInkNotePadWidget+0xb8.
static uintptr_t const kHardwareEraserSetActiveToolReturnVma = 0x6244e;
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

static void trace(char const* message, bool truncate = false) {
    int const flags = O_WRONLY | O_CREAT | (truncate ? O_TRUNC : O_APPEND);
    int const fd = open(kTrace, flags, 0644);
    if (fd < 0)
        return;
    write(fd, message, strlen(message));
    write(fd, "\n", 1);
    fsync(fd);
    close(fd);
}

static void trace(QString const& message) {
    QByteArray const utf8 = message.left(1024).toUtf8();
    trace(utf8.constData());
}

static bool validEraserSizeIndex(int index) {
    return index >= 0 && index < 5;
}

static void rememberEraserSizeIndex(int index) {
    if (!validEraserSizeIndex(index))
        return;
    QMutexLocker locker(&eraserSizeSettingsMutex);
    eraserSizeIndex = index;
    eraserSizeSettingsLoaded = true;
}

static int configuredEraserSizeIndex() {
    {
        QMutexLocker locker(&eraserSizeSettingsMutex);
        if (eraserSizeSettingsLoaded)
            return eraserSizeIndex;
    }

    int loadedIndex = 2;
    bool validFile = false;
    QString const settingsPath = QLatin1String(kEraserSizeSettings);
    QFile file(settingsPath);
    QFileInfo const info(file);
    if (!info.exists()) {
        trace("eraser-size: no saved setting; using stock-compatible middle size");
    } else if (!info.isFile() || info.size() < 1 || info.size() > 4096) {
        trace("eraser-size: saved setting has invalid size; preserving file and using default");
    } else if (!file.open(QIODevice::ReadOnly)) {
        trace("eraser-size: saved setting unreadable; preserving file and using default");
    } else {
        QJsonParseError parseError;
        QJsonDocument const document = QJsonDocument::fromJson(
            file.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError
                && document.isObject()) {
            QJsonValue const value = document.object().value(
                QLatin1String("index"));
            double const number = value.toDouble(-1.0);
            int const candidate = static_cast<int>(number);
            if (number == static_cast<double>(candidate)
                    && validEraserSizeIndex(candidate)) {
                loadedIndex = candidate;
                validFile = true;
            }
        }
        if (!validFile) {
            trace("eraser-size: saved setting malformed; preserving file and using default");
        }
    }

    {
        QMutexLocker locker(&eraserSizeSettingsMutex);
        if (!eraserSizeSettingsLoaded) {
            eraserSizeIndex = loadedIndex;
            eraserSizeSettingsLoaded = true;
        }
        loadedIndex = eraserSizeIndex;
    }
    trace(QLatin1String("eraser-size: configured index=")
        + QString::number(loadedIndex)
        + (validFile
            ? QLatin1String(" restored") : QLatin1String(" default")));
    return loadedIndex;
}

static bool persistEraserSizeIndex(int index) {
    if (!validEraserSizeIndex(index))
        return false;
    rememberEraserSizeIndex(index);

    QDir root;
    if (!root.mkpath(QLatin1String(kTemplateRoot))) {
        trace("eraser-size: settings directory unavailable; selection kept in memory");
        return false;
    }
    QString const settingsPath = QLatin1String(kEraserSizeSettings);
    QSaveFile file(settingsPath);
    if (!file.open(QIODevice::WriteOnly)) {
        trace("eraser-size: setting could not be opened; selection kept in memory");
        return false;
    }
    QJsonObject rootObject;
    rootObject.insert(QLatin1String("index"), index);
    QByteArray bytes = QJsonDocument(rootObject).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        trace("eraser-size: setting commit failed; selection kept in memory");
        return false;
    }
    trace(QLatin1String("eraser-size: selected index persisted=")
        + QString::number(index));
    return true;
}

__attribute__((constructor(101))) static void traceLibraryLoad() {
    trace("plugin library loaded", true);
}

static double elapsedMs(QElapsedTimer const& timer) {
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

static bool safeId(QString const& id) {
    if (!id.startsWith(QLatin1String("Custom_")) || id.size() > 63)
        return false;

    for (int i = 0; i < id.size(); ++i) {
        ushort const c = id.at(i).unicode();
        bool const asciiLetter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        bool const asciiDigit = c >= '0' && c <= '9';
        if (!(asciiLetter || asciiDigit || c == '_'))
            return false;
    }
    return true;
}

static bool safeTemplatePath(QString const& path) {
    return path.startsWith(QLatin1String(kTemplateRoot))
        && !path.contains(QLatin1String(".."))
        && QFileInfo(path).isFile();
}

static bool hasPngSignature(QString const& path) {
    static unsigned char const signature[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray const header = file.read(static_cast<qint64>(sizeof(signature)));
    return header.size() == static_cast<int>(sizeof(signature))
        && memcmp(header.constData(), signature, sizeof(signature)) == 0;
}

static bool filesEqual(QString const& firstPath, QString const& secondPath) {
    QFileInfo const firstInfo(firstPath);
    QFileInfo const secondInfo(secondPath);
    if (!firstInfo.isFile() || !secondInfo.isFile() || firstInfo.size() != secondInfo.size())
        return false;

    QFile first(firstPath);
    QFile second(secondPath);
    if (!first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly))
        return false;

    while (!first.atEnd()) {
        QByteArray const firstBlock = first.read(64 * 1024);
        QByteArray const secondBlock = second.read(64 * 1024);
        if (firstBlock != secondBlock)
            return false;
    }
    return second.atEnd();
}

static bool syncCondorVariant(QString const& sourcePath, QString* condorPath) {
    QString destination = sourcePath;
    destination.chop(4);
    destination.append(QLatin1String(kCondorSuffix));
    *condorPath = destination;

    if (filesEqual(sourcePath, destination))
        return true;

    QString const temporary = destination + QLatin1String(".tmp");
    QFile::remove(temporary);
    if (!QFile::copy(sourcePath, temporary))
        return false;
    if (QFile::exists(destination) && !QFile::remove(destination)) {
        QFile::remove(temporary);
        return false;
    }
    if (!QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        return false;
    }
    return true;
}

static bool writeBytesIfChanged(QString const& destination, QByteArray const& contents) {
    QFile existing(destination);
    if (existing.open(QIODevice::ReadOnly)) {
        bool const unchanged = existing.readAll() == contents;
        existing.close();
        if (unchanged)
            return true;
    }

    QString const temporary = destination + QLatin1String(".tmp");
    QFile::remove(temporary);
    QFile output(temporary);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (output.write(contents) != contents.size()) {
        output.close();
        QFile::remove(temporary);
        return false;
    }
    output.close();

    if (QFile::exists(destination) && !QFile::remove(destination)) {
        QFile::remove(temporary);
        return false;
    }
    if (!QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        return false;
    }
    return true;
}

static bool createPickerIcon(QString const& sourcePath, QString* iconPath) {
    QString destination = sourcePath;
    destination.chop(4);
    destination.append(QLatin1String("-icon.png"));
    *iconPath = destination;

    QImage page;
    if (!page.load(sourcePath, "PNG"))
        return false;
    if (page.width() != kBackgroundWidth || page.height() != kBackgroundHeight)
        return false;

    QImage const fitted = page.scaled(
        kPickerIconSize,
        kPickerIconSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    QImage icon(kPickerIconSize, kPickerIconSize, QImage::Format_RGB32);
    icon.fill(qRgb(255, 255, 255));

    QPainter painter(&icon);
    painter.drawImage(
        (kPickerIconSize - fitted.width()) / 2,
        (kPickerIconSize - fitted.height()) / 2,
        fitted);
    painter.end();

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !icon.save(&buffer, "PNG"))
        return false;
    buffer.close();
    return writeBytesIfChanged(destination, encoded);
}

// Kobo composites the page template above the live pen layer: every built-in
// *_condor.png paper is transparent except for its line work, so fresh ink
// stays visible through the paper while writing. A fully opaque user PNG
// (white background baked in) hides new strokes until the next full page
// render. Recreate the built-in contract for automatic templates: a source
// with no real transparency is converted so white becomes transparent paper
// and darkness becomes black at matching opacity. Composited over the white
// page this is pixel-identical to the source. A source that already carries
// transparency is copied verbatim, exactly as before.
static bool syncTemplateOverlay(QString const& sourcePath, QString* condorPath) {
    QString destination = sourcePath;
    destination.chop(4);
    destination.append(QLatin1String(kCondorSuffix));
    *condorPath = destination;

    QImage page;
    if (!page.load(sourcePath, "PNG"))
        return false;
    if (page.width() != kBackgroundWidth || page.height() != kBackgroundHeight)
        return false;

    QImage overlay = page.convertToFormat(QImage::Format_ARGB32);
    if (overlay.isNull())
        return false;

    if (page.hasAlphaChannel()) {
        for (int y = 0; y < overlay.height(); ++y) {
            QRgb const* const row =
                reinterpret_cast<QRgb const*>(overlay.constScanLine(y));
            for (int x = 0; x < overlay.width(); ++x) {
                if (qAlpha(row[x]) != 255)
                    return syncCondorVariant(sourcePath, condorPath);
            }
        }
    }

    for (int y = 0; y < overlay.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        for (int x = 0; x < overlay.width(); ++x)
            row[x] = qRgba(0, 0, 0, 255 - qGray(row[x]));
    }

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !overlay.save(&buffer, "PNG"))
        return false;
    buffer.close();
    if (!writeBytesIfChanged(destination, encoded))
        return false;
    trace("automatic: opaque paper converted to transparent overlay");
    return true;
}

static uint32_t stableFilenameHash(QByteArray const& value) {
    uint32_t hash = UINT32_C(2166136261);
    for (int i = 0; i < value.size(); ++i) {
        hash ^= static_cast<unsigned char>(value.at(i));
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool automaticSource(QFileInfo const& info) {
    QString const name = info.fileName();
    QString const lower = name.toLower();
    return info.isFile()
        && info.size() >= 8
        && info.size() <= kMaximumAutomaticPngSize
        && name.endsWith(QLatin1String(".png"))
        && !lower.endsWith(QLatin1String(kCondorSuffix))
        && !lower.endsWith(QLatin1String("-icon.png"))
        && !lower.endsWith(QLatin1String("_icon.png"));
}

static bool locateRendererMap() {
    void* const original = nh_symptr(backgroundOptionsOriginal);
    Dl_info image = {};

    if (!original || !dladdr(original, &image) || !image.dli_fbase) {
        trace("renderer map: could not locate libiinknote");
        nh_log("could not locate libiinknote image base");
        return false;
    }

    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const function = reinterpret_cast<uintptr_t>(original) & ~uintptr_t(1);
    if (function - base != kBackgroundOptionsVma) {
        trace("renderer map: backgroundOptions VMA mismatch");
        nh_log("unsupported libiinknote: backgroundOptions offset is 0x%lx, expected 0x%lx",
            static_cast<unsigned long>(function - base),
            static_cast<unsigned long>(kBackgroundOptionsVma));
        return false;
    }

    rendererMap = reinterpret_cast<QMap<QString, QString>*>(base + kRendererMapVma);
    if (rendererMap->size() != kExpectedBuiltinMapSize) {
        trace("renderer map: built-in count mismatch");
        nh_log("unsupported libiinknote: renderer map has %d entries, expected %d",
            rendererMap->size(), kExpectedBuiltinMapSize);
        rendererMap = nullptr;
        return false;
    }
    iinknoteBase = base;
    trace("renderer map: verified");
    return true;
}

static bool loadManifest() {
    QFile file;
    file.setFileName(QLatin1String(kManifest));
    if (!file.exists()) {
        trace("manifest: not found");
        nh_log("manifest not found at %s; no custom templates loaded", kManifest);
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        trace("manifest: open failed");
        nh_log("could not open manifest at %s", kManifest);
        return false;
    }

    QByteArray const raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument const document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        trace("manifest: invalid JSON");
        nh_log("invalid manifest JSON: %s", qPrintable(parseError.errorString()));
        return false;
    }

    QJsonArray const entries = document.object().value(QLatin1String("templates")).toArray();
    if (entries.size() > kMaximumCustomTemplates) {
        trace("manifest: too many entries");
        nh_log("manifest has %d entries; maximum is %d",
            entries.size(), kMaximumCustomTemplates);
        return false;
    }

    QVector<CustomTemplate> parsed;
    QMap<QString, bool> seen;
    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject const item = entries.at(i).toObject();
        QString const id = item.value(QLatin1String("id")).toString();
        QString const label = item.value(QLatin1String("label")).toString();
        QString const icon = item.value(QLatin1String("icon")).toString();
        QString const background = item.value(QLatin1String("background")).toString();

        if (!safeId(id) || label.isEmpty() || label.size() > 64) {
            trace("manifest: invalid id or label");
            nh_log("manifest entry %d has an invalid id or label", i);
            return false;
        }
        if (seen.contains(id) || rendererMap->contains(id)) {
            trace("manifest: identifier collision");
            nh_log("manifest entry %d collides with template id '%s'", i, qPrintable(id));
            return false;
        }
        if (!safeTemplatePath(icon) || !icon.endsWith(QLatin1String(".png"))) {
            trace("manifest: invalid icon");
            nh_log("manifest entry %d has an invalid or missing icon", i);
            return false;
        }
        if (!safeTemplatePath(background) || !background.endsWith(QLatin1String(kCondorSuffix))) {
            trace("manifest: invalid background");
            nh_log("manifest entry %d needs an existing *_condor.png background", i);
            return false;
        }

        QString backgroundBase = background;
        backgroundBase.chop(static_cast<int>(sizeof(kCondorSuffix) - 1));
        backgroundBase.append(QLatin1String(".png"));

        CustomTemplate value = {id, label, icon, backgroundBase};
        parsed.append(value);
        seen.insert(id, true);
    }

    for (int i = 0; i < parsed.size(); ++i) {
        CustomTemplate const& value = parsed.at(i);
        rendererMap->insert(value.id, value.backgroundBase);
        customTemplates.append(value);
        nh_log("loaded custom notebook template '%s' as '%s'",
            qPrintable(value.id), qPrintable(value.label));
    }
    trace("manifest: templates inserted into renderer map");
    return true;
}

static void loadAutomaticTemplates() {
    QDir directory(QString::fromLatin1(kTemplateRoot));
    QFileInfoList const files = directory.entryInfoList(
        QDir::Files | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);

    for (int i = 0; i < files.size(); ++i) {
        if (customTemplates.size() >= kMaximumCustomTemplates) {
            trace("automatic: template limit reached");
            break;
        }

        QFileInfo const& info = files.at(i);
        if (!automaticSource(info))
            continue;

        QString const sourcePath = info.absoluteFilePath();
        if (!hasPngSignature(sourcePath)) {
            trace("automatic: invalid PNG skipped");
            nh_log("automatic template '%s' does not have a PNG signature",
                qPrintable(info.fileName()));
            continue;
        }

        QString label = info.completeBaseName();
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        label.replace(QLatin1Char('-'), QLatin1Char(' '));
        label = label.simplified().left(64);
        if (label.isEmpty())
            continue;

        uint32_t const hash = stableFilenameHash(info.fileName().toUtf8());
        QString const id = QString::fromLatin1("Custom_Auto_%1")
            .arg(hash, 8, 16, QLatin1Char('0'));
        if (rendererMap->contains(id)) {
            trace("automatic: identifier collision skipped");
            continue;
        }

        QString iconPath;
        if (!createPickerIcon(sourcePath, &iconPath)) {
            trace("automatic: could not generate picker icon");
            nh_log("automatic template '%s' must be a readable %d x %d PNG",
                qPrintable(info.fileName()), kBackgroundWidth, kBackgroundHeight);
            continue;
        }

        QString condorPath;
        if (!syncTemplateOverlay(sourcePath, &condorPath)) {
            trace("automatic: could not prepare renderer copy");
            nh_log("could not create renderer copy '%s'", qPrintable(condorPath));
            continue;
        }

        rendererMap->insert(id, sourcePath);
        customTemplates.append(CustomTemplate{id, label, iconPath, sourcePath});
        trace("automatic: PNG template loaded");
        nh_log("automatically loaded notebook template '%s' as '%s'",
            qPrintable(info.fileName()), qPrintable(label));
    }
}

static void loadAutomaticCovers() {
    QDir directory(QString::fromLatin1(kCoverRoot));
    QFileInfoList const files = directory.entryInfoList(
        QDir::Files | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);

    for (int i = 0; i < files.size(); ++i) {
        if (customCovers.size() >= kMaximumCustomCovers) {
            trace("covers: cover limit reached");
            break;
        }

        QFileInfo const& info = files.at(i);
        if (!automaticSource(info))
            continue;

        QString const sourcePath = info.absoluteFilePath();
        if (!hasPngSignature(sourcePath)) {
            trace("covers: invalid PNG skipped");
            continue;
        }

        QString label = info.completeBaseName();
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        label.replace(QLatin1Char('-'), QLatin1Char(' '));
        label = label.simplified().left(64);
        if (label.isEmpty())
            continue;

        uint32_t const hash = stableFilenameHash(info.fileName().toUtf8());
        QString const id = QString::fromLatin1("Custom_Cover_%1")
            .arg(hash, 8, 16, QLatin1Char('0'));
        if (rendererMap->contains(id)) {
            trace("covers: identifier collision skipped");
            continue;
        }

        QString iconPath;
        if (!createPickerIcon(sourcePath, &iconPath)) {
            trace("covers: could not generate picker icon");
            nh_log("cover '%s' must be a readable %d x %d PNG",
                qPrintable(info.fileName()), kBackgroundWidth, kBackgroundHeight);
            continue;
        }

        QString condorPath;
        if (!syncCondorVariant(sourcePath, &condorPath)) {
            trace("covers: could not prepare renderer copy");
            continue;
        }

        rendererMap->insert(id, sourcePath);
        customCovers.append(CustomTemplate{id, label, iconPath, sourcePath});
        trace("covers: PNG cover loaded");
        nh_log("automatically loaded notebook cover '%s' as '%s'",
            qPrintable(info.fileName()), qPrintable(label));
    }
}

template <typename Function>
static bool resolveDynamicSymbol(
    void* handle,
    char const* symbolName,
    Function* destination) {
    union {
        void* pointer;
        Function function;
    } converter = {};
    converter.pointer = dlsym(handle, symbolName);
    if (!converter.pointer)
        return false;
    *destination = converter.function;
    return true;
}

static bool loadZipApis() {
    if (zipLibraryHandle)
        return true;

    zipLibraryHandle = dlopen("libzip.so.5", RTLD_LAZY | RTLD_LOCAL);
    if (!zipLibraryHandle)
        return false;

    bool const ready =
        resolveDynamicSymbol(zipLibraryHandle, "zip_open", &zipOpen)
        && resolveDynamicSymbol(
            zipLibraryHandle, "zip_get_num_entries", &zipGetNumEntries)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_get_name", &zipGetName)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_fopen", &zipFopen)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_fread", &zipFread)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_fclose", &zipFclose)
        && resolveDynamicSymbol(zipLibraryHandle, "zip_discard", &zipDiscard);
    if (!ready) {
        dlclose(zipLibraryHandle);
        zipLibraryHandle = nullptr;
        zipOpen = nullptr;
        zipGetNumEntries = nullptr;
        zipGetName = nullptr;
        zipFopen = nullptr;
        zipFread = nullptr;
        zipFclose = nullptr;
        zipDiscard = nullptr;
        return false;
    }

    trace("archive: read-only notebook APIs resolved");
    return true;
}

static bool readZipEntry(
    ZipArchiveOpaque* archive,
    char const* entryName,
    QByteArray* contents) {
    contents->clear();
    ZipFileOpaque* const file = zipFopen(archive, entryName, 0);
    if (!file)
        return false;

    bool valid = true;
    char block[4096];
    while (true) {
        long long const count = zipFread(file, block, sizeof(block));
        if (count < 0) {
            valid = false;
            break;
        }
        if (count == 0)
            break;
        if (contents->size() + count > kMaximumPageMetadataSize) {
            valid = false;
            break;
        }
        contents->append(block, static_cast<int>(count));
    }
    if (zipFclose(file) != 0)
        valid = false;
    if (!valid)
        contents->clear();
    return valid;
}

// Return 1 when the serialized page.bdom contains the exact native layer ID,
// 0 when the entry was read completely without it, and -1 when the archive
// could not be inspected. This is a read-only persistence probe; it never
// participates in the mutation decision.
static int notebookArchiveContainsLayerId(
    QString const& notebookPath,
    QString const& partId,
    QString const& layerId) {
    if (!zipOpen || !zipFopen || !zipFread || !zipFclose || !zipDiscard
            || partId.isEmpty() || layerId.isEmpty()) {
        return -1;
    }

    QByteArray const encodedPath = QFile::encodeName(notebookPath);
    int openError = 0;
    ZipArchiveOpaque* const archive = zipOpen(
        encodedPath.constData(), 16, &openError);  // ZIP_RDONLY
    if (!archive)
        return -1;

    QByteArray const entryName = QByteArray("pages/")
        + partId.toUtf8() + QByteArray("/page.bdom");
    ZipFileOpaque* const file = zipFopen(archive, entryName.constData(), 0);
    if (!file) {
        zipDiscard(archive);
        return -1;
    }

    QByteArray const needle = layerId.toUtf8();
    QByteArray carry;
    bool found = false;
    bool complete = true;
    qint64 scanned = 0;
    char block[4096];
    while (true) {
        long long const count = zipFread(file, block, sizeof(block));
        if (count < 0) {
            complete = false;
            break;
        }
        if (count == 0)
            break;
        scanned += count;
        if (scanned > qint64(32) * 1024 * 1024) {
            complete = false;
            break;
        }
        QByteArray window = carry;
        window.append(block, static_cast<int>(count));
        if (window.contains(needle)) {
            found = true;
            break;
        }
        int const retained = std::min(needle.size() - 1, window.size());
        carry = retained > 0 ? window.right(retained) : QByteArray();
    }
    if (zipFclose(file) != 0)
        complete = false;
    zipDiscard(archive);
    return found ? 1 : (complete ? 0 : -1);
}

static QString coverTypeFromMetadata(QByteArray const& contents) {
    QJsonParseError parseError;
    QJsonDocument const document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QString();

    QJsonObject const metadata = document.object()
        .value(QLatin1String("iink-user-metadata")).toObject();
    QJsonObject const kobo = metadata.value(QLatin1String("kobo")).toObject();
    QString const type = kobo.value(QLatin1String("backgroundType")).toString();
    return type.startsWith(QLatin1String("Custom_Cover_"))
        ? type
        : QString();
}

struct ScanTiming {
    double zipOpenMs;
    double scanMs;
    int metadataEntries;

    ScanTiming() : zipOpenMs(0.0), scanMs(0.0), metadataEntries(0) {}
};

static bool scanNotebookCoverType(
    QString const& notebookPath,
    QString* coverType,
    bool* determinate,
    ScanTiming* timing) {
    coverType->clear();
    *determinate = false;
    QByteArray const encodedPath = QFile::encodeName(notebookPath);
    int openError = 0;
    QElapsedTimer timer;
    timer.start();
    // ZIP_RDONLY. A notebook being saved or otherwise unreadable simply uses
    // Kobo's original preview for this pass, and the verdict is not cached.
    ZipArchiveOpaque* const archive = zipOpen(encodedPath.constData(), 16, &openError);
    timing->zipOpenMs = elapsedMs(timer);
    if (!archive)
        return false;

    int matches = 0;
    bool readFailure = false;
    QString found;
    timer.restart();
    long long const entryCount = zipGetNumEntries(archive, 0);
    if (entryCount >= 0 && entryCount <= 65536) {
        *determinate = true;
        for (long long i = 0; i < entryCount; ++i) {
            char const* const entryName = zipGetName(
                archive, static_cast<unsigned long long>(i), 0);
            if (!entryName)
                continue;
            QByteArray const name(entryName);
            if (!name.startsWith("pages/") || !name.endsWith("/meta.json"))
                continue;

            ++timing->metadataEntries;
            QByteArray contents;
            if (!readZipEntry(archive, entryName, &contents)) {
                readFailure = true;
                continue;
            }
            QString const type = coverTypeFromMetadata(contents);
            if (type.isEmpty())
                continue;
            found = type;
            ++matches;
            if (matches > 1)
                break;
        }
    }
    zipDiscard(archive);
    timing->scanMs = elapsedMs(timer);

    // An unreadable page entry may be a save in progress; keep this pass's
    // verdict out of the cache so the next pass rescans.
    if (readFailure)
        *determinate = false;

    // A cover inserted by this plugin is the sole Custom_Cover page. Multiple
    // candidates are ambiguous, so preserve Kobo's original preview.
    if (matches != 1)
        return false;
    *coverType = found;
    return true;
}

static int countNotebookPageEntries(QString const& notebookPath) {
    if (!zipOpen || !zipGetNumEntries || !zipGetName || !zipDiscard)
        return -1;

    QByteArray const encodedPath = QFile::encodeName(notebookPath);
    int openError = 0;
    ZipArchiveOpaque* const archive = zipOpen(
        encodedPath.constData(), 16, &openError);
    if (!archive)
        return -1;

    int pages = 0;
    bool valid = true;
    long long const entryCount = zipGetNumEntries(archive, 0);
    if (entryCount < 0 || entryCount > 65536) {
        valid = false;
    } else {
        for (long long i = 0; i < entryCount; ++i) {
            char const* const entryName = zipGetName(
                archive, static_cast<unsigned long long>(i), 0);
            if (!entryName) {
                valid = false;
                break;
            }
            QByteArray const name(entryName);
            if (name.startsWith("pages/") && name.endsWith("/meta.json"))
                ++pages;
        }
    }
    zipDiscard(archive);
    return valid ? pages : -1;
}

static bool openNotebookHasPluginCover(void* widget, bool* hasCover) {
    if (!zipOpen
            || !zipGetNumEntries
            || !zipGetName
            || !zipFopen
            || !zipFread
            || !zipFclose
            || !zipDiscard) {
        *hasCover = false;
        return false;
    }

    QString const notebookPath = QDir::cleanPath(widgetFilePath(widget));
    QString coverType;
    bool determinate = false;
    ScanTiming timing;
    *hasCover = scanNotebookCoverType(
        notebookPath, &coverType, &determinate, &timing);
    return determinate;
}

static bool cachedNotebookCoverType(
    QString const& notebookPath,
    QString* coverType,
    double pathResolveMs) {
    coverType->clear();
    QFileInfo const before(notebookPath);
    qint64 const modifiedMs = before.lastModified().toMSecsSinceEpoch();
    qint64 const size = before.size();
    if (!before.isFile() || size <= 0)
        return false;

    char line[224];
    {
        QMutexLocker locker(&coverCacheMutex);
        QHash<QString, CoverScanEntry>::const_iterator const found =
            coverScanCache.constFind(notebookPath);
        if (found != coverScanCache.constEnd()) {
            if (found.value().modifiedMs == modifiedMs
                    && found.value().size == size) {
                *coverType = found.value().type;
                bool const hasCover = found.value().hasCover;
                locker.unlock();
                snprintf(line, sizeof(line),
                    "cover-cache: hit (%s) path-resolve=%.1fms",
                    hasCover ? "cover" : "no cover", pathResolveMs);
                trace(line);
                return hasCover;
            }
            coverScanCache.remove(notebookPath);
            locker.unlock();
            trace("cover-cache: entry invalidated (notebook changed)");
        }
    }

    ScanTiming timing;
    bool determinate = false;
    QString type;
    bool const hasCover = scanNotebookCoverType(
        notebookPath, &type, &determinate, &timing);

    // Only cache a verdict that describes the file we originally measured.
    QFileInfo const after(notebookPath);
    bool const stable = after.isFile()
        && after.size() == size
        && after.lastModified().toMSecsSinceEpoch() == modifiedMs;

    snprintf(line, sizeof(line),
        "cover-cache: miss path-resolve=%.1fms zip-open=%.1fms "
        "metadata-scan=%.1fms pages=%d verdict=%s%s",
        pathResolveMs, timing.zipOpenMs, timing.scanMs,
        timing.metadataEntries,
        hasCover ? "cover" : "no cover",
        (determinate && stable) ? " (cached)" : " (not cached)");
    trace(line);

    if (determinate && stable) {
        QMutexLocker locker(&coverCacheMutex);
        if (coverScanCache.size() >= kMaximumScanCacheEntries) {
            coverScanCache.clear();
            trace("cover-cache: scan cache cleared (entry limit)");
        }
        CoverScanEntry entry;
        entry.modifiedMs = modifiedMs;
        entry.size = size;
        entry.type = hasCover ? type : QString();
        entry.hasCover = hasCover;
        coverScanCache.insert(notebookPath, entry);
    }
    if (hasCover)
        *coverType = type;
    return hasCover;
}

// Composed cover previews are persisted to plugin-owned storage so notebook
// cards can show cover and ink immediately after a restart. The in-memory
// cache dies with the process, while Kobo's thumbnailer considers its own
// cache fresh and does not call the parser again, so without the file the
// card would fall back to the clean cover after every boot.
static QString renderedPreviewFilePath(QString const& notebookPath) {
    QByteArray const digest = QCryptographicHash::hash(
        QDir::cleanPath(notebookPath).toUtf8(), QCryptographicHash::Sha1);
    QString path = QString::fromLatin1(kRenderedPreviewRoot);
    path += QString::fromLatin1(digest.toHex().constData());
    path += QLatin1String(".png");
    return path;
}

static void pruneRenderedPreviewFiles() {
    QDir const directory(QString::fromLatin1(kRenderedPreviewRoot));
    QFileInfoList files = directory.entryInfoList(
        QStringList() << QLatin1String("*.png"), QDir::Files, QDir::Time);
    while (files.size() > kMaximumPersistedPreviews) {
        QFileInfo const oldest = files.takeLast();
        if (!QFile::remove(oldest.absoluteFilePath()))
            break;
        trace("cover-cache: persisted preview pruned");
    }
}

static void persistRenderedCoverImage(
    QString const& notebookPath,
    QString const& coverType,
    QImage const& image) {
    if (!QDir().mkpath(QString::fromLatin1(kRenderedPreviewRoot))) {
        trace("cover-cache: preview directory unavailable");
        return;
    }
    QImage annotated = image;
    annotated.setText(QLatin1String("cnt-cover-type"), coverType);
    QSaveFile file(renderedPreviewFilePath(notebookPath));
    if (!file.open(QIODevice::WriteOnly)
            || !annotated.save(&file, "PNG")
            || !file.commit()) {
        trace("cover-cache: preview persist failed");
        return;
    }
    pruneRenderedPreviewFiles();
    trace("cover-cache: composed preview persisted");
}

static QImage loadPersistedCoverImage(
    QString const& notebookPath,
    QString const& coverType) {
    QString const path = renderedPreviewFilePath(notebookPath);
    if (!QFileInfo(path).isFile())
        return QImage();
    QImage const image(path);
    if (image.isNull()
            || image.width() != kBackgroundWidth
            || image.height() != kBackgroundHeight
            || image.text(QLatin1String("cnt-cover-type")) != coverType) {
        QFile::remove(path);
        trace("cover-cache: persisted preview rejected");
        return QImage();
    }
    return image;
}

static void invalidateNotebookScanEntry(QString const& notebookPath) {
    QString const key = QDir::cleanPath(notebookPath);
    if (QFile::remove(renderedPreviewFilePath(key)))
        trace("cover-cache: persisted preview removed after cover change");
    QMutexLocker locker(&coverCacheMutex);
    if (coverScanCache.remove(key) > 0)
        trace("cover-cache: entry invalidated after plugin cover change");
    renderedCoverCache.remove(key);
}

static CustomTemplate const* customCoverForType(QString const& type) {
    for (int i = 0; i < customCovers.size(); ++i) {
        if (customCovers.at(i).id == type)
            return &customCovers.at(i);
    }
    return nullptr;
}

// Kobo's thumbnail service renders notebook ink on white but does not include
// a plugin-defined background. Treat darkness in that render as an ink mask
// and multiply it over the clean cover. Transparent pixels remain untouched,
// while black handwriting remains black. The result is deliberately opaque,
// matching the full-page cover images accepted by the existing callback.
static QImage composeCoverWithRenderedInk(
    QString const& type,
    QImage const& renderedInk) {
    CustomTemplate const* const cover = customCoverForType(type);
    if (!cover || renderedInk.isNull())
        return QImage();

    QImage composed(cover->backgroundBase);
    if (composed.isNull()
            || composed.width() != kBackgroundWidth
            || composed.height() != kBackgroundHeight) {
        return QImage();
    }
    composed = composed.convertToFormat(QImage::Format_RGB32);

    QImage ink = renderedInk.convertToFormat(QImage::Format_ARGB32);
    if (ink.size() != composed.size()) {
        ink = ink.scaled(
            composed.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (ink.isNull())
        return QImage();

    for (int y = 0; y < composed.height(); ++y) {
        QRgb* const destination =
            reinterpret_cast<QRgb*>(composed.scanLine(y));
        QRgb const* const source =
            reinterpret_cast<QRgb const*>(ink.constScanLine(y));
        for (int x = 0; x < composed.width(); ++x) {
            int const darkness =
                ((255 - qGray(source[x])) * qAlpha(source[x]) + 127) / 255;
            if (darkness == 0)
                continue;
            int const keep = 255 - darkness;
            QRgb const base = destination[x];
            destination[x] = qRgb(
                (qRed(base) * keep + 127) / 255,
                (qGreen(base) * keep + 127) / 255,
                (qBlue(base) * keep + 127) / 255);
        }
    }
    return composed;
}

// Caller must hold coverCacheMutex.
static void evictCleanCoverImagesLocked() {
    qint64 total = 0;
    QHash<QString, CleanCoverEntry>::const_iterator it;
    for (it = cleanCoverCache.constBegin();
            it != cleanCoverCache.constEnd(); ++it) {
        total += it.value().image.byteCount();
    }
    while (total > kMaximumImageCacheBytes && cleanCoverCache.size() > 1) {
        QHash<QString, CleanCoverEntry>::iterator oldest =
            cleanCoverCache.begin();
        for (QHash<QString, CleanCoverEntry>::iterator candidate =
                cleanCoverCache.begin();
                candidate != cleanCoverCache.end(); ++candidate) {
            if (candidate.value().sequence < oldest.value().sequence)
                oldest = candidate;
        }
        total -= oldest.value().image.byteCount();
        cleanCoverCache.erase(oldest);
        trace("cover-cache: clean cover evicted (memory bound)");
    }
}

static QImage cleanCustomCoverImage(QString const& type) {
    CustomTemplate const* const cover = customCoverForType(type);
    if (!cover)
        return QImage();

    QFileInfo const info(cover->backgroundBase);
    qint64 const modifiedMs = info.lastModified().toMSecsSinceEpoch();
    qint64 const size = info.size();
    if (!info.isFile() || size <= 0)
        return QImage();

    {
        QMutexLocker locker(&coverCacheMutex);
        QHash<QString, CleanCoverEntry>::iterator const found =
            cleanCoverCache.find(type);
        if (found != cleanCoverCache.end()) {
            if (found.value().pngModifiedMs == modifiedMs
                    && found.value().pngSize == size) {
                found.value().sequence = ++cleanCoverSequence;
                return found.value().image;
            }
            cleanCoverCache.erase(found);
        }
    }

    QImage const image(cover->backgroundBase);
    if (image.isNull()
            || image.width() != kBackgroundWidth
            || image.height() != kBackgroundHeight) {
        return QImage();
    }

    QMutexLocker locker(&coverCacheMutex);
    CleanCoverEntry entry;
    entry.image = image;
    entry.pngModifiedMs = modifiedMs;
    entry.pngSize = size;
    entry.sequence = ++cleanCoverSequence;
    cleanCoverCache.insert(type, entry);
    evictCleanCoverImagesLocked();
    return image;
}

// Caller must hold coverCacheMutex.
static void evictRenderedCoverImagesLocked() {
    qint64 total = 0;
    QHash<QString, RenderedCoverEntry>::const_iterator it;
    for (it = renderedCoverCache.constBegin();
            it != renderedCoverCache.constEnd(); ++it) {
        total += it.value().image.byteCount();
    }
    while (total > kMaximumImageCacheBytes && renderedCoverCache.size() > 1) {
        QHash<QString, RenderedCoverEntry>::iterator oldest =
            renderedCoverCache.begin();
        for (QHash<QString, RenderedCoverEntry>::iterator candidate =
                renderedCoverCache.begin();
                candidate != renderedCoverCache.end(); ++candidate) {
            if (candidate.value().sequence < oldest.value().sequence)
                oldest = candidate;
        }
        total -= oldest.value().image.byteCount();
        renderedCoverCache.erase(oldest);
        trace("cover-cache: rendered preview evicted (memory bound)");
    }
}

static void cacheRenderedCoverImage(
    QString const& notebookPath,
    QString const& coverType,
    QImage const& image) {
    if (image.isNull())
        return;
    QFileInfo const info(notebookPath);
    if (!info.isFile() || info.size() <= 0)
        return;

    persistRenderedCoverImage(notebookPath, coverType, image);

    RenderedCoverEntry entry;
    entry.image = image;
    entry.notebookModifiedMs = info.lastModified().toMSecsSinceEpoch();
    entry.notebookSize = info.size();
    entry.coverType = coverType;
    QMutexLocker locker(&coverCacheMutex);
    entry.sequence = ++renderedCoverSequence;
    renderedCoverCache.insert(QDir::cleanPath(notebookPath), entry);
    evictRenderedCoverImagesLocked();
}

static QImage cachedRenderedCoverImage(
    QString const& notebookPath,
    QString const& coverType) {
    QString const key = QDir::cleanPath(notebookPath);
    QFileInfo const info(key);
    if (!info.isFile() || info.size() <= 0)
        return QImage();

    {
        QMutexLocker locker(&coverCacheMutex);
        QHash<QString, RenderedCoverEntry>::iterator const found =
            renderedCoverCache.find(key);
        if (found != renderedCoverCache.end()) {
            if (found.value().coverType != coverType) {
                renderedCoverCache.erase(found);
            } else {
                // MyScript can emit the completed ink render before
                // IInkNotePadWidget's final save updates the .nebo mtime and
                // size. Do not throw that valid preview away merely because
                // the file settles afterward. It remains the best available
                // thumbnail until the next renderer callback replaces it.
                // A cover change through this plugin explicitly clears the
                // entry in invalidateNotebookScanEntry(), and a type mismatch
                // above also clears it.
                if (found.value().notebookModifiedMs
                            != info.lastModified().toMSecsSinceEpoch()
                        || found.value().notebookSize != info.size()) {
                    trace("cover-cache: retaining rendered ink across final "
                          "notebook save");
                }
                found.value().sequence = ++renderedCoverSequence;
                return found.value().image;
            }
        }
    }

    QImage const persisted = loadPersistedCoverImage(key, coverType);
    if (persisted.isNull())
        return QImage();

    RenderedCoverEntry entry;
    entry.image = persisted;
    entry.notebookModifiedMs = info.lastModified().toMSecsSinceEpoch();
    entry.notebookSize = info.size();
    entry.coverType = coverType;
    QMutexLocker locker(&coverCacheMutex);
    entry.sequence = ++renderedCoverSequence;
    renderedCoverCache.insert(key, entry);
    evictRenderedCoverImagesLocked();
    trace("cover-cache: persisted preview restored");
    return persisted;
}

static QString notebookPathFromVolume(void const* volume) {
    QString path = contentGetId(volume);
    if (path.startsWith(QLatin1String("file:")))
        path = QUrl(path).toLocalFile();
    if (!path.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive))
        path.append(QLatin1String(".nebo"));
    path = QDir::cleanPath(path);
    if (!path.startsWith(QLatin1String("/mnt/onboard/"))
            || !QFileInfo(path).isFile()) {
        return QString();
    }
    return path;
}

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
            kCreateIInkMenuItemVma, &createIInkMenuItem)
        && resolvePinned(iinknoteHandle, kToolMenuTapGestureSymbol,
            kToolMenuTapGestureVma, &toolMenuTapGesture)
        && resolvePinned(iinknoteHandle, kMenuSelectBackgroundSymbol,
            kMenuSelectBackgroundVma, &menuSelectBackground)
        && resolvePinned(iinknoteHandle, kSetBackgroundTypeSymbol,
            kSetBackgroundTypeVma, &setBackgroundTypeOriginal)
        && resolvePinned(iinknoteHandle, kWidgetSaveSymbol,
            kWidgetSaveVma, &widgetSave)
        && resolvePinned(iinknoteHandle, kWidgetFilePathSymbol,
            kWidgetFilePathVma, &widgetFilePath)
        && resolvePinned(iinknoteHandle, kWidgetRefreshSymbol,
            kWidgetRefreshVma, &widgetRefresh)
        && resolvePinned(iinknoteHandle, kShowErrorPopupSymbol,
            kShowErrorPopupVma, &showErrorPopup)
        && resolvePinned(iinknoteHandle, kBackgroundTypeSymbol,
            kBackgroundTypeVma, &backgroundType)
        && resolvePinned(iinknoteHandle, kContentGetIdSymbol,
            kContentGetIdVma, &contentGetId);

    bool const iinkSymbols =
        resolvePinned(iinkHandle, kEditorGetPartSymbol,
            kEditorGetPartVma, &editorGetPart)
        && resolvePinned(iinkHandle, kEditorGetEngineSymbol,
            kEditorGetEngineVma, &editorGetEngine)
        && resolvePinned(iinkHandle, kEditorSetPartSymbol,
            kEditorSetPartVma, &editorSetPart)
        && resolvePinned(iinkHandle, kPartGetPackageSymbol,
            kPartGetPackageVma, &partGetPackage)
        && resolvePinned(iinkHandle, kPartGetPageSymbol,
            kPartGetPageVma, &partGetPage)
        && resolvePinned(iinkHandle, kPageDocumentSymbol,
            kPageDocumentVma, &pageDocument)
        && resolvePinned(iinkHandle, kPageDestructorSymbol,
            kPageDestructorVma, &pageDestructor)
        && resolvePinned(iinkHandle, kPackagePartCountSymbol,
            kPackagePartCountVma, &packagePartCount)
        && resolvePinned(iinkHandle, kPackageGetPartSymbol,
            kPackageGetPartVma, &packageGetPart)
        && resolvePinned(iinkHandle, kPackageIndexOfPartSymbol,
            kPackageIndexOfPartVma, &packageIndexOfPart)
        && resolvePinned(iinkHandle, kPackageCreatePartSymbol,
            kPackageCreatePartVma, &packageCreatePart)
        && resolvePinned(iinkHandle, kPackageClonePartSymbol,
            kPackageClonePartVma, &packageClonePart)
        && resolvePinned(iinkHandle, kPackageRemovePartSymbol,
            kPackageRemovePartVma, &packageRemovePart)
        && resolvePinned(iinkHandle, kPackageSaveSymbol,
            kPackageSaveVma, &packageSave)
        && resolvePinned(iinkHandle, kEngineOpenPackageSymbol,
            kEngineOpenPackageVma, &engineOpenPackage)
        && resolvePinned(iinkHandle, kDocumentPageCountSymbol,
            kDocumentPageCountVma, &documentPageCount)
        && resolvePinned(iinkHandle, kDocumentMovePageSymbol,
            kDocumentMovePageVma, &documentMovePage)
        && resolvePinned(iinkHandle, kIInkStringCtorSymbol,
            kIInkStringCtorVma, &iinkStringCtor);

    bool const layerSymbols =
        resolvePinned(iinkHandle, kEditorGetRendererSymbol,
            kEditorGetRendererVma, &editorGetRenderer)
        && resolvePinned(iinkHandle, kPartGetIdSymbol,
            kPartGetIdVma, &partGetId)
        && resolvePinned(iinkHandle, kPageLayoutSymbol,
            kPageLayoutVma, &pageLayout)
        && resolvePinned(iinkHandle, kLayoutDestructorSymbol,
            kLayoutDestructorVma, &layoutDestructor)
        && resolvePinned(iinkHandle, kLayoutAppendLayerSymbol,
            kLayoutAppendLayerVma, &layoutAppendLayer)
        && resolvePinned(iinkHandle, kLayoutRemoveLayerSymbol,
            kLayoutRemoveLayerVma, &layoutRemoveLayer)
        && resolvePinned(iinkHandle, kAtkLayoutRawLayoutSymbol,
            kAtkLayoutRawLayoutVma, &atkLayoutRawLayout)
        && resolvePinned(iinkHandle, kDocumentLayoutGetLayerSymbol,
            kDocumentLayoutGetLayerVma, &documentLayoutGetLayer)
        && resolvePinned(iinkHandle, kLayerIteratorIsAtEndSymbol,
            kLayerIteratorIsAtEndVma, &layerIteratorIsAtEnd)
        && resolvePinned(iinkHandle, kManagedObjectDestructorSymbol,
            kManagedObjectDestructorVma, &managedObjectDestructor)
        && resolvePinned(iinkHandle, kRendererGetBackendSymbol,
            kRendererGetBackendVma, &rendererGetBackend)
        && resolvePinned(iinkHandle, kRendererRestrictToLayersSymbol,
            kRendererRestrictToLayersVma, &rendererRestrictToLayers)
        && resolvePinned(iinkHandle, kPageControllerInputDispatcherSymbol,
            kPageControllerInputDispatcherVma, &pageControllerInputDispatcher)
        && resolvePinned(iinkHandle,
            kPlatformInputDispatcherGetCurrentToolSymbol,
            kPlatformInputDispatcherGetCurrentToolVma,
            &platformInputDispatcherGetCurrentTool)
        && resolvePinned(iinkHandle, kDynamicToolDispatcherCastSymbol,
            kDynamicToolDispatcherCastVma, &dynamicToolDispatcherCast)
        && resolvePinned(iinkHandle, kToolDispatcherRestrictToLayerSymbol,
            kToolDispatcherRestrictToLayerVma,
            &toolDispatcherRestrictToLayer)
        && resolvePinned(iinkHandle, kIInkStringToStdStringSymbol,
            kIInkStringToStdStringVma, &iinkStringToStdString)
        && resolvePinned(iinkHandle, kDocumentLayerNameSymbol,
            kDocumentLayerNameVma, &documentLayerName)
        && resolvePinned(iinkHandle, kBackgroundObjectLayerNameSymbol,
            kBackgroundObjectLayerNameVma, &backgroundObjectLayerName)
        && resolvePinned(iinkHandle, kNeboBackendVtableSymbol,
            kNeboBackendVtableVma, &neboBackendVtable)
        && resolvePinned(iinkHandle, kCompositeBoxFactoryMainBackendSymbol,
            kCompositeBoxFactoryMainBackendVma, &compositeBoxFactoryMainBackend)
        && resolvePinned(iinkHandle, kCompositeBoxFactoryBackendsSymbol,
            kCompositeBoxFactoryBackendsVma, &compositeBoxFactoryBackends)
        && resolvePinned(iinkHandle, kSelectionSelectLayerSymbol,
            kSelectionSelectLayerVma, &selectionSelectLayer)
        && resolvePinned(iinkHandle, kSelectionSelectPolygonSymbol,
            kSelectionSelectPolygonVma, &selectionSelectPolygon)
        && resolvePinned(iinkHandle, kSelectionSelectNoneSymbol,
            kSelectionSelectNoneVma, &selectionSelectNone)
        && resolvePinned(iinkHandle, kSelectionIsEmptySymbol,
            kSelectionIsEmptyVma, &selectionIsEmpty)
        && resolvePinned(iinkHandle, kSelectionAdjustToStrokeBoundariesSymbol,
            kSelectionAdjustToStrokeBoundariesVma,
            &selectionAdjustToStrokeBoundaries)
        && resolvePinned(iinkHandle, kEraserPolicySymbol,
            kEraserPolicyVma, &eraserPolicy)
        && resolvePinned(iinkHandle, kEraserStrokerPolygonSymbol,
            kEraserStrokerPolygonVma, &eraserStrokerPolygon)
        && resolvePinned(iinkHandle, kDrawingBackendVtableSymbol,
            kDrawingBackendVtableVma, &drawingBackendVtable)
        && resolvePinned(iinkHandle, kDrawingEraserVtableSymbol,
            kDrawingEraserVtableVma, &drawingEraserVtable)
        && resolvePinned(iinkHandle, kPlainDrawingEraserVtableSymbol,
            kPlainDrawingEraserVtableVma, &plainDrawingEraserVtable)
        && resolvePinned(iinkHandle, kDiagramEraserVtableSymbol,
            kDiagramEraserVtableVma, &diagramEraserVtable)
        && resolvePinned(iinkHandle, kDiagramPenVtableSymbol,
            kDiagramPenVtableVma, &diagramPenVtable);

    // Eraser size uses the same live editor/backend path as layer routing, but
    // it remains a separate feature gate. The exact vtables prevent a radius
    // setter from ever being called on an arbitrary kind-4 Tool.
    bool const eraserSizeSymbols = layerSymbols
        && resolvePinned(iinkHandle, kEraserSetRadiusSymbol,
            kEraserSetRadiusVma, &eraserSetRadius)
        && resolvePinned(iinkHandle, kEraserRadiusSymbol,
            kEraserRadiusVma, &eraserRadius)
        && resolvePinned(iinkHandle, kEraserWidthFromThicknessRatioSymbol,
            kEraserWidthFromThicknessRatioVma,
            &eraserWidthFromThicknessRatio)
        && resolvePinned(iinkHandle, kLayoutGridLineGapSymbol,
            kLayoutGridLineGapVma, &layoutGridLineGap)
        && resolvePinned(iinkHandle, kCoreEraserVtableSymbol,
            kCoreEraserVtableVma, &coreEraserVtable)
        && resolvePinned(iinkHandle, kTextEraserSntVtableSymbol,
            kTextEraserSntVtableVma, &textEraserSntVtable)
        && resolvePinned(iinkHandle, kMathEraserVtableSymbol,
            kMathEraserVtableVma, &mathEraserVtable)
        && resolvePinned(iinkHandle, kTextEraserVtableSymbol,
            kTextEraserVtableVma, &textEraserVtable);
    eraserSizeApisReady = eraserSizeSymbols;

    bool const layerPreviewSymbols = iinkUiRefHandle
        && resolvePinned(iinkHandle, kEditorGetConfigurationSymbol,
            kEditorGetConfigurationVma, &editorGetConfiguration)
        && resolvePinned(iinkHandle, kPageControllerExportToPngSymbol,
            kPageControllerExportToPngVma, &pageControllerExportToPng)
        && resolvePinned(iinkHandle, kBackendImageDrawerMakeSharedSymbol,
            kBackendImageDrawerMakeSharedVma, &backendImageDrawerMakeShared)
        && resolvePinnedThumbVma(
            iinkHandle,
            kPageControllerExportToPngSymbol,
            kStockBackendImageDrawerExportVma,
            &stockBackendImageDrawerExport)
        && resolvePinned(iinkUiRefHandle, kImagePainterConstructorSymbol,
            kImagePainterConstructorVma, &imagePainterConstructor)
        && resolvePinned(
            iinkUiRefHandle,
            kImagePainterDeletingDestructorSymbol,
            kImagePainterDeletingDestructorVma,
            &imagePainterDeletingDestructor)
        && resolvePinned(
            iinkUiRefHandle,
            kImagePainterSetImageLoaderSymbol,
            kImagePainterSetImageLoaderVma,
            &imagePainterSetImageLoader)
        && resolvePinned(
            iinkUiRefHandle,
            kUirefEditorWidgetVtableSymbol,
            kUirefEditorWidgetVtableVma,
            &uirefEditorWidgetVtable);
    layerPreviewApisReady = iinkSymbols && layerSymbols && layerPreviewSymbols;

    // Best-effort: the base Eraser::updateSelection lets the diagram eraser
    // commit custom-layer ink. If it cannot be verified the eraser simply
    // keeps stock behavior; selection and pen routing remain unaffected.
    resolvePinned(iinkHandle, kCoreEraserUpdateSelectionSymbol,
        kCoreEraserUpdateSelectionVma, &coreEraserUpdateSelection);

    bool const nativeLayerMenuSymbols = nickelHandle
        && resolvePinned(nickelHandle, kNickelTouchMenuConstructorSymbol,
            kNickelTouchMenuConstructorVma, &nickelTouchMenuConstructor)
        && resolvePinned(nickelHandle, kNickelTouchMenuSetAlignmentSymbol,
            kNickelTouchMenuSetAlignmentVma, &nickelTouchMenuSetAlignment)
        && resolvePinned(
            nickelHandle,
            kTouchMenuSetCustomPopupPositionOffsetSymbol,
            kTouchMenuSetCustomPopupPositionOffsetVma,
            &touchMenuSetCustomPopupPositionOffset)
        && resolvePinned(
            nickelHandle,
            kAbstractNickelMenuControllerPopupFromWidgetSymbol,
            kAbstractNickelMenuControllerPopupFromWidgetVma,
            &abstractNickelMenuControllerPopupFromWidget)
        && resolvePinned(
            nickelHandle,
            kNickelTouchMenuPopupPositionSymbol,
            kNickelTouchMenuPopupPositionVma,
            &nickelTouchMenuPopupPosition);

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
            &iInkToolMenuWidgetConstructor)
        && resolvePinned(
            iinknoteHandle,
            kIInkToolMenuWidgetSetSelectedSymbol,
            kIInkToolMenuWidgetSetSelectedVma,
            &iInkToolMenuWidgetSetSelected)
        && resolvePinned(
            nickelHandle,
            kAbstractMenuControllerGrabTapGestureSymbol,
            kAbstractMenuControllerGrabTapGestureVma,
            &abstractMenuControllerGrabTapGesture)
        && resolvePinned(
            nickelHandle,
            kAbstractMenuControllerTapGestureSymbol,
            kAbstractMenuControllerTapGestureVma,
            &abstractMenuControllerTapGesture);

    layerHooksReady = noteSymbols && iinkSymbols && layerSymbols
        && nativeLayerMenuSymbols && nativeLayerRowSymbols;
    trace(layerHooksReady
        ? "layers: pinned layout, routing, and native menu APIs verified"
        : "layers: pinned API verification failed; feature disabled");
    trace(layerPreviewSymbols
        ? "layers: isolated preview APIs verified"
        : "layers: preview APIs unavailable; placeholder cards retained");
    trace(eraserSizeApisReady
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

static bool backupNotebookPath(
    QString const& sourcePath,
    QString const& backupRoot,
    QString const& operation,
    QString* backupPath,
    QString* error) {
    QFileInfo const source(sourcePath);
    if (!sourcePath.startsWith(QLatin1String("/mnt/onboard/"))
            || !sourcePath.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive)
            || !source.isFile()
            || source.size() <= 0) {
        *error = operation
            + QLatin1String(": notebook path could not be verified.");
        return false;
    }

    QDir root;
    if (!root.mkpath(backupRoot)) {
        *error = operation
            + QLatin1String(": backup folder could not be created.");
        return false;
    }

    struct statvfs storage = {};
    if (statvfs(qPrintable(backupRoot), &storage) != 0) {
        *error = operation
            + QLatin1String(": free space could not be checked.");
        return false;
    }

    quint64 const available = static_cast<quint64>(storage.f_bavail)
        * static_cast<quint64>(storage.f_frsize);
    quint64 const required = static_cast<quint64>(source.size())
        + UINT64_C(16) * 1024 * 1024;
    if (available < required) {
        *error = operation
            + QLatin1String(": not enough space for a safety backup.");
        return false;
    }

    QString const stamp = QDateTime::currentDateTimeUtc()
        .toString(QLatin1String("yyyyMMdd-HHmmsszzz"));
    QString destination = backupRoot
        + stamp + QLatin1Char('-') + source.fileName()
        + QLatin1String(".backup");
    for (int suffix = 1; QFileInfo(destination).exists(); ++suffix) {
        destination = backupRoot
            + stamp + QLatin1Char('-') + QString::number(suffix)
            + QLatin1Char('-') + source.fileName()
            + QLatin1String(".backup");
    }

    QString const temporary = destination + QLatin1String(".tmp");
    QFile::remove(temporary);
    if (!QFile::copy(sourcePath, temporary)) {
        QFile::remove(temporary);
        *error = operation + QLatin1String(": notebook backup failed.");
        return false;
    }

    int const backupFd = open(qPrintable(temporary), O_RDONLY);
    bool const backupSynced = backupFd >= 0 && fsync(backupFd) == 0;
    if (backupFd >= 0)
        close(backupFd);
    if (!backupSynced || !QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        *error = operation + QLatin1String(": notebook backup failed.");
        return false;
    }

    *backupPath = destination;
    trace("notebook safety backup complete");
    return true;
}

static bool backupNotebook(void* widget, QString* backupPath, QString* error) {
    return backupNotebookPath(
        widgetFilePath(widget),
        QLatin1String(kCoverBackupRoot),
        QLatin1String("Cover not changed"),
        backupPath,
        error);
}

struct PageStorage {
    alignas(8) unsigned char bytes[64];
    bool constructed;

    PageStorage() : constructed(false) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~PageStorage() {
        if (constructed)
            pageDestructor(bytes);
    }
};

struct ScopedIInkString {
    IInkStringStorage value;

    explicit ScopedIInkString(char const* utf8) {
        value.impl = nullptr;
        iinkStringCtor(&value, utf8);
    }

    ~ScopedIInkString() {
        delete value.impl;
    }
};

struct LayoutStorage {
    // atk::core::Layout contains a shared document-layout pointer followed by
    // an atk::core::Page. The pinned destructor accesses the Page at +0x8.
    // Keep generous aligned storage so the firmware owns every construction
    // and destruction detail.
    alignas(8) unsigned char bytes[128];
    bool constructed;

    LayoutStorage() : constructed(false) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~LayoutStorage() {
        if (constructed)
            layoutDestructor(bytes);
    }
};

struct ManagedObjectStorage {
    // myscript::engine::ManagedObject and its document wrappers are one
    // acquired _voReference pointer on this 32-bit SDK.
    alignas(4) unsigned char bytes[4];
    bool constructed;

    ManagedObjectStorage() : constructed(false) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~ManagedObjectStorage() {
        if (constructed && managedObjectDestructor)
            managedObjectDestructor(bytes);
    }

    bool hasObject() const {
        void* object = nullptr;
        memcpy(&object, bytes, sizeof(object));
        return object != nullptr;
    }
};
static_assert(sizeof(void*) == 4,
    "MyScript managed-object ABI requires the 32-bit Kobo target");

static QString iinkStringValue(IInkStringStorage* value) {
    if (!value || !value->impl || !iinkStringToStdString)
        return QString();
    std::string utf8;
    iinkStringToStdString(&utf8, value);
    return QString::fromUtf8(utf8.data(), static_cast<int>(utf8.size()));
}

static QString partStableId(SharedPart const& part) {
    if (!part || !partGetId)
        return QString();
    IInkStringStorage value = {nullptr};
    partGetId(&value, part.get());
    QString const id = iinkStringValue(&value);
    delete value.impl;
    return id;
}

static bool layoutForPart(
    SharedPart const& part,
    LayoutStorage* layout,
    QString* error) {
    if (!part || !layout || !partGetPage || !pageLayout || !layoutDestructor) {
        if (error)
            *error = QLatin1String("Layers unavailable: page layout API is not ready.");
        return false;
    }

    PageStorage page;
    partGetPage(page.bytes, part.get());
    page.constructed = true;
    pageLayout(layout->bytes, page.bytes);
    layout->constructed = true;
    return true;
}

static SharedDocument documentForPart(SharedPart const& part) {
    PageStorage page;
    partGetPage(page.bytes, part.get());
    page.constructed = true;
    return pageDocument(page.bytes);
}

// Verified IInkNotePadWidget offsets on firmware 4.38.23697: the guarded
// MyScript Editor object/control identity is mirrored at 0x3c/0x40, and
// BackgroundWidget is at 0x90. The plugin does not manufacture ownership from
// these raw words; live firmware owners remain in scope for every call.
static void* notePadEditor(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + 0x3c);
}

static void* notePadEditorControl(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + 0x40);
}

static void* notePadBackgroundWidget(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + 0x90);
}

static bool applyNotebookCover(void* widget, QString const& type, QString* error) {
    void* const editor = notePadEditor(widget);
    void* const editorControl = notePadEditorControl(widget);
    if (!editor || !editorControl) {
        *error = QLatin1String("Cover not changed: notebook editor is not ready.");
        return false;
    }

    widgetSave(widget);
    SharedPart const originalPart = editorGetPart(editor);
    if (!originalPart) {
        *error = QLatin1String("Cover not changed: current page was not found.");
        return false;
    }

    SharedPackage const package = partGetPackage(originalPart.get());
    if (!package) {
        *error = QLatin1String("Cover not changed: notebook package was not found.");
        return false;
    }

    int const oldCount = packagePartCount(package.get());
    if (oldCount < 1 || oldCount > 4096) {
        *error = QLatin1String("Cover not changed: notebook page count is invalid.");
        return false;
    }

    SharedPart const firstPart = packageGetPart(package.get(), 0);
    if (!firstPart) {
        *error = QLatin1String("Cover not changed: first page was not found.");
        return false;
    }

    editorSetPart(editor, firstPart);
    void* const backgroundWidget = notePadBackgroundWidget(widget);
    if (!backgroundWidget) {
        editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: page background is not ready.");
        return false;
    }

    QString const firstBackground = backgroundType(backgroundWidget);
    bool const alreadyHasCover = firstBackground.startsWith(
        QLatin1String("Custom_Cover_"));

    QString backupPath;
    if (!backupNotebook(widget, &backupPath, error)) {
        editorSetPart(editor, originalPart);
        return false;
    }

    if (alreadyHasCover) {
        setBackgroundTypeOriginal(widget, type);
        widgetSave(widget);
        packageSave(package.get());
        widgetRefresh(widget);
        invalidateNotebookScanEntry(widgetFilePath(widget));
        trace("covers: existing cover background changed");
        return true;
    }

    ScopedIInkString rawContent("Raw Content");
    if (!rawContent.value.impl) {
        editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: page type could not be created.");
        return false;
    }

    SharedPart const coverPart = packageCreatePart(package.get(), rawContent.value);
    if (!coverPart || packagePartCount(package.get()) != oldCount + 1) {
        editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: a new page could not be created.");
        return false;
    }

    editorSetPart(editor, coverPart);
    setBackgroundTypeOriginal(widget, type);
    widgetSave(widget);
    packageSave(package.get());
    // The archive is mutated from here on; drop any cached verdict even if a
    // later validation step fails.
    invalidateNotebookScanEntry(widgetFilePath(widget));

    SharedDocument const document = documentForPart(coverPart);
    int const appendedIndex = packageIndexOfPart(package.get(), coverPart);
    if (!document
            || documentPageCount(document.get()) != oldCount + 1
            || appendedIndex != oldCount) {
        *error = QLatin1String(
            "Cover page was added at the end, but could not be moved safely. "
            "The original notebook backup is in the covers/backups folder.");
        trace("covers: pre-move validation failed");
        return false;
    }

    documentMovePage(document.get(), appendedIndex, 0);
    packageSave(package.get());
    if (packageIndexOfPart(package.get(), coverPart) != 0
            || packagePartCount(package.get()) != oldCount + 1) {
        *error = QLatin1String(
            "Cover move could not be verified. The original notebook backup "
            "is in the covers/backups folder.");
        trace("covers: post-move validation failed");
        return false;
    }

    editorSetPart(editor, coverPart);
    widgetRefresh(widget);
    invalidateNotebookScanEntry(widgetFilePath(widget));
    trace("covers: new writable cover inserted as page zero");
    return true;
}

struct PageContext {
    void* widget;
    void* editor;
    SharedPart part;
    SharedPackage package;
    SharedDocument document;
    int index;
    int count;

    PageContext()
        : widget(nullptr), editor(nullptr), index(-1), count(0) {}
};

static bool loadPageContext(void* widget, PageContext* context, QString* error) {
    if (!widget || !context) {
        *error = QLatin1String("Page operation failed: notebook is not ready.");
        return false;
    }

    void* const editor = notePadEditor(widget);
    void* const editorControl = notePadEditorControl(widget);
    if (!editor || !editorControl) {
        *error = QLatin1String("Page operation failed: notebook editor is not ready.");
        return false;
    }

    SharedPart const part = editorGetPart(editor);
    SharedPackage const package = part ? partGetPackage(part.get()) : SharedPackage();
    int const count = package ? packagePartCount(package.get()) : 0;
    int const index = package ? packageIndexOfPart(package.get(), part) : -1;
    SharedDocument const document = part ? documentForPart(part) : SharedDocument();
    if (!part
            || !package
            || !document
            || count < 1
            || count > kMaximumNotebookPages
            || index < 0
            || index >= count
            || documentPageCount(document.get()) != count) {
        *error = QLatin1String("Page operation failed: current page could not be verified.");
        return false;
    }

    context->widget = widget;
    context->editor = editor;
    context->part = part;
    context->package = package;
    context->document = document;
    context->index = index;
    context->count = count;
    return true;
}

static bool writePageTransaction(
    QString const& path,
    QString const& stage,
    QString const& source,
    QString const& destination,
    QString const& sourceBackup,
    QString const& destinationBackup) {
    QJsonObject object;
    object.insert(QLatin1String("version"), 1);
    object.insert(QLatin1String("operation"), QLatin1String("move-page"));
    object.insert(QLatin1String("stage"), stage);
    object.insert(QLatin1String("source"), source);
    object.insert(QLatin1String("destination"), destination);
    object.insert(QLatin1String("sourceBackup"), sourceBackup);
    object.insert(QLatin1String("destinationBackup"), destinationBackup);
    object.insert(
        QLatin1String("updatedUtc"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QByteArray const bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

static bool reorderCurrentPage(void* widget, int target, QString* error) {
    try {
        widgetSave(widget);
        PageContext context;
        if (!loadPageContext(widget, &context, error))
            return false;
        if (target < 0 || target >= context.count) {
            *error = QLatin1String("Page not reordered: destination position is invalid.");
            return false;
        }
        bool hasCover = false;
        if (!openNotebookHasPluginCover(widget, &hasCover)) {
            *error = QLatin1String(
                "Page not reordered: the notebook cover could not be checked.");
            return false;
        }
        if (hasCover && (context.index == 0 || target == 0)) {
            *error = QLatin1String(
                "Page not reordered: the notebook cover must stay the first page.");
            return false;
        }
        if (target == context.index)
            return true;

        QString backupPath;
        if (!backupNotebookPath(
                widgetFilePath(widget),
                QLatin1String(kPageBackupRoot),
                QLatin1String("Page not reordered"),
                &backupPath,
                error)) {
            return false;
        }

        documentMovePage(context.document.get(), context.index, target);
        packageSave(context.package.get());
        if (packagePartCount(context.package.get()) != context.count
                || packageIndexOfPart(context.package.get(), context.part) != target) {
            // Best-effort in-memory rollback. The hidden archive backup is the
            // authoritative recovery copy if this validation itself failed.
            int const actual = packageIndexOfPart(context.package.get(), context.part);
            if (actual >= 0 && actual < context.count) {
                documentMovePage(context.document.get(), actual, context.index);
                packageSave(context.package.get());
            }
            *error = QLatin1String(
                "Page reorder could not be verified. The original notebook "
                "is preserved in the page-manager backup folder.");
            return false;
        }

        widgetSave(widget);
        invalidateNotebookScanEntry(widgetFilePath(widget));
        editorSetPart(context.editor, context.part);
        widgetRefresh(widget);
        trace("pages: current page reordered");
        return true;
    } catch (std::exception const&) {
        *error = QLatin1String(
            "Page not reordered: the notebook engine rejected the operation.");
    } catch (...) {
        *error = QLatin1String("Page not reordered: unexpected notebook error.");
    }
    trace("pages: reorder threw and was contained");
    return false;
}

static bool duplicateCurrentPage(void* widget, QString* error) {
    SharedPackage package;
    SharedPart clone;
    int oldCount = 0;
    bool cloneAttached = false;
    try {
        widgetSave(widget);
        PageContext context;
        if (!loadPageContext(widget, &context, error))
            return false;
        bool hasCover = false;
        if (!openNotebookHasPluginCover(widget, &hasCover)) {
            *error = QLatin1String(
                "Page not duplicated: the notebook cover could not be checked.");
            return false;
        }
        if (hasCover && context.index == 0) {
            *error = QLatin1String(
                "Page not duplicated: the notebook cover cannot be duplicated.");
            return false;
        }
        if (context.count >= kMaximumNotebookPages) {
            *error = QLatin1String("Page not duplicated: notebook has too many pages.");
            return false;
        }

        QString backupPath;
        if (!backupNotebookPath(
                widgetFilePath(widget),
                QLatin1String(kPageBackupRoot),
                QLatin1String("Page not duplicated"),
                &backupPath,
                error)) {
            return false;
        }

        package = context.package;
        oldCount = context.count;
        clone = packageClonePart(package.get(), context.part);
        cloneAttached = bool(clone);
        if (!clone
                || packagePartCount(package.get()) != oldCount + 1
                || packageIndexOfPart(package.get(), clone) != oldCount) {
            *error = QLatin1String("Page not duplicated: cloned page could not be verified.");
            throw std::runtime_error("clone validation failed");
        }

        SharedDocument const document = documentForPart(clone);
        int const target = context.index + 1;
        if (!document || documentPageCount(document.get()) != oldCount + 1) {
            *error = QLatin1String("Page not duplicated: cloned document could not be verified.");
            throw std::runtime_error("clone document validation failed");
        }
        if (target != oldCount)
            documentMovePage(document.get(), oldCount, target);
        packageSave(package.get());

        if (packagePartCount(package.get()) != oldCount + 1
                || packageIndexOfPart(package.get(), clone) != target) {
            *error = QLatin1String(
                "Duplicate could not be verified. The original notebook is "
                "preserved in the page-manager backup folder.");
            throw std::runtime_error("clone commit validation failed");
        }

        widgetSave(widget);
        invalidateNotebookScanEntry(widgetFilePath(widget));
        editorSetPart(context.editor, clone);
        widgetRefresh(widget);
        trace("pages: current page duplicated");
        return true;
    } catch (...) {
        if (package && clone && cloneAttached) {
            try {
                packageRemovePart(package.get(), clone);
                packageSave(package.get());
                if (packagePartCount(package.get()) == oldCount) {
                    trace("pages: failed duplicate rolled back");
                } else {
                    trace("pages: failed duplicate rollback also failed; backup retained");
                }
            } catch (...) {
                trace("pages: failed duplicate rollback also failed; backup retained");
            }
        }
        if (error->isEmpty()) {
            *error = QLatin1String(
                "Page not duplicated: the notebook engine rejected the operation.");
        }
        return false;
    }
}

struct NotebookDestination {
    QString label;
    QString path;
};

static QVector<NotebookDestination> notebookDestinations(QString const& sourcePath) {
    QVector<NotebookDestination> values;
    QDirIterator iterator(
        QLatin1String("/mnt/onboard"),
        QStringList() << QLatin1String("*.nebo"),
        QDir::Files | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (iterator.hasNext() && values.size() < kMaximumDestinationNotebooks) {
        QString const path = QDir::cleanPath(iterator.next());
        QFileInfo const info(path);
        if (path == sourcePath
                || path.startsWith(QLatin1String("/mnt/onboard/.kobo/"))
                || !info.isFile()
                || info.isSymLink()
                || info.size() <= 0) {
            continue;
        }
        QString label = QDir(QLatin1String("/mnt/onboard"))
            .relativeFilePath(path);
        if (label.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive))
            label.chop(5);
        NotebookDestination destination;
        destination.label = label;
        destination.path = path;
        values.append(destination);
    }
    std::sort(values.begin(), values.end(), [](NotebookDestination const& a,
                                                NotebookDestination const& b) {
        return QString::localeAwareCompare(a.label, b.label) < 0;
    });
    return values;
}

static bool moveCurrentPageToNotebook(
    void* widget,
    QString const& destinationPath,
    QString* error) {
    bool destinationVerified = false;
    bool sourceRemoved = false;
    PageContext source;
    try {
        widgetSave(widget);
        if (!loadPageContext(widget, &source, error))
            return false;
        if (source.count <= 1) {
            *error = QLatin1String(
                "Page not moved: a notebook must keep at least one page.");
            return false;
        }

        QString const sourcePath = QDir::cleanPath(widgetFilePath(widget));
        QString const destination = QDir::cleanPath(destinationPath);
        QFileInfo const destinationInfo(destination);
        if (destination == sourcePath
                || !destination.startsWith(QLatin1String("/mnt/onboard/"))
                || destination.startsWith(QLatin1String("/mnt/onboard/.kobo/"))
                || !destination.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive)
                || !destinationInfo.isFile()
                || destinationInfo.isSymLink()
                || destinationInfo.size() <= 0) {
            *error = QLatin1String("Page not moved: destination notebook is invalid.");
            return false;
        }

        QString sourceBackup;
        QString destinationBackup;
        if (!backupNotebookPath(
                sourcePath,
                QLatin1String(kPageBackupRoot),
                QLatin1String("Page not moved"),
                &sourceBackup,
                error)
                || !backupNotebookPath(
                    destination,
                    QLatin1String(kPageBackupRoot),
                    QLatin1String("Page not moved"),
                    &destinationBackup,
                    error)) {
            return false;
        }

        QDir root;
        if (!root.mkpath(QLatin1String(kPageTransactionRoot))) {
            *error = QLatin1String("Page not moved: transaction folder could not be created.");
            return false;
        }
        QString const transactionPath = QLatin1String(kPageTransactionRoot)
            + QDateTime::currentDateTimeUtc().toString(
                QLatin1String("yyyyMMdd-HHmmsszzz"))
            + QLatin1String(".json");
        if (!writePageTransaction(
                transactionPath,
                QLatin1String("prepared"),
                sourcePath,
                destination,
                sourceBackup,
                destinationBackup)) {
            *error = QLatin1String("Page not moved: safety journal could not be written.");
            return false;
        }

        SharedEngine const engine = editorGetEngine(source.editor);
        if (!engine) {
            *error = QLatin1String("Page not moved: notebook engine is unavailable.");
            return false;
        }
        QByteArray const destinationUtf8 = destination.toUtf8();
        ScopedIInkString destinationString(destinationUtf8.constData());
        if (!destinationString.value.impl) {
            *error = QLatin1String("Page not moved: destination path could not be opened.");
            return false;
        }

        // Binary Ninja verified that option zero is EXISTING. It cannot create
        // or truncate a package if the destination path is wrong.
        SharedPackage destinationPackage = engineOpenPackage(
            engine.get(), destinationString.value, 0);
        int const destinationCount = destinationPackage
            ? packagePartCount(destinationPackage.get()) : 0;
        if (!destinationPackage
                || destinationCount < 1
                || destinationCount >= kMaximumNotebookPages) {
            *error = QLatin1String("Page not moved: destination page count is invalid.");
            return false;
        }

        SharedPart destinationPart = packageClonePart(
            destinationPackage.get(), source.part);
        if (!destinationPart
                || packagePartCount(destinationPackage.get()) != destinationCount + 1
                || packageIndexOfPart(destinationPackage.get(), destinationPart)
                    != destinationCount) {
            *error = QLatin1String("Page not moved: destination copy could not be verified.");
            return false;
        }
        packageSave(destinationPackage.get());
        invalidateNotebookScanEntry(destination);
        destinationPart.reset();
        destinationPackage.reset();

        // Reopening is insufficient because MyScript's process-global path
        // registry may return a new package wrapper over the same cached
        // Document. Count serialized page metadata directly in the ZIP.
        if (countNotebookPageEntries(destination) != destinationCount + 1) {
            *error = QLatin1String(
                "Page was not removed from its source because the destination "
                "copy could not be verified. Both backups were retained.");
            return false;
        }
        destinationVerified = true;
        if (!writePageTransaction(
                transactionPath,
                QLatin1String("destination-verified"),
                sourcePath,
                destination,
                sourceBackup,
                destinationBackup)) {
            *error = QLatin1String(
                "The page was safely copied, but not removed because the "
                "transaction journal could not be updated.");
            return false;
        }

        int const oldIndex = source.index;
        int const oldCount = source.count;
        packageRemovePart(source.package.get(), source.part);
        sourceRemoved = true;
        if (packagePartCount(source.package.get()) != oldCount - 1) {
            *error = QLatin1String(
                "The page was copied, but removal from the source could not be "
                "verified. Both backups were retained.");
            return false;
        }

        int const replacementIndex = oldIndex == oldCount - 1
            ? oldIndex - 1 : oldIndex;
        SharedPart const replacement = packageGetPart(
            source.package.get(), replacementIndex);
        if (!replacement) {
            *error = QLatin1String(
                "The page was safely copied, but no replacement source page "
                "could be selected.");
            return false;
        }

        editorSetPart(source.editor, replacement);
        packageSave(source.package.get());
        invalidateNotebookScanEntry(sourcePath);
        if (packagePartCount(source.package.get()) != oldCount - 1
                || countNotebookPageEntries(sourcePath) != oldCount - 1) {
            *error = QLatin1String(
                "The page was copied, but removal from the source could not be "
                "verified. Both backups were retained.");
            return false;
        }

        widgetSave(widget);
        invalidateNotebookScanEntry(sourcePath);
        writePageTransaction(
            transactionPath,
            QLatin1String("complete"),
            sourcePath,
            destination,
            sourceBackup,
            destinationBackup);
        widgetRefresh(widget);
        trace("pages: current page moved between notebooks");
        return true;
    } catch (std::exception const&) {
        *error = destinationVerified
            ? QLatin1String(
                "The destination copy is safe, but the notebook engine stopped "
                "before source removal completed. Review both notebooks.")
            : QLatin1String(
                "Page not moved: the notebook engine rejected the destination copy.");
    } catch (...) {
        *error = destinationVerified
            ? QLatin1String(
                "The destination copy is safe, but the move did not finish. "
                "Review both notebooks.")
            : QLatin1String("Page not moved: unexpected notebook error.");
    }

    if (!sourceRemoved && source.editor && source.part) {
        try {
            if (packageIndexOfPart(source.package.get(), source.part) >= 0)
                editorSetPart(source.editor, source.part);
        } catch (...) {
            trace("pages: source editor could not be restored after failed move");
        }
    }
    widgetRefresh(widget);
    trace("pages: cross-notebook move stopped safely");
    return false;
}

static bool isNotebookWidget(QObject* object) {
    return object
        && object->metaObject()
        && strcmp(object->metaObject()->className(), "IInkNotePadWidget") == 0;
}

static QObject* findNotebookWidget(QObject* controller) {
    QObject* root = controller;
    for (QObject* current = controller; current; current = current->parent()) {
        if (isNotebookWidget(current))
            return current;
        root = current;
    }

    // The menu controller is normally parented directly to the note-pad
    // widget. Keep a guarded descendant fallback for this pinned firmware's
    // alternate ownership path.
    QObjectList const descendants = root->findChildren<QObject*>();
    for (int i = 0; i < descendants.size(); ++i) {
        if (isNotebookWidget(descendants.at(i)))
            return descendants.at(i);
    }
    return nullptr;
}

struct LayerRecord {
    QString id;
    QString name;
};

struct LayerState {
    QString notebookPath;
    QString partId;
    QString activeId;
    QVector<LayerRecord> customLayers;
};

struct LayerPreviewCardCacheEntry {
    QImage image;
    qint64 modifiedMs;
    qint64 size;
    quint64 sequence;
};

// Layer popup construction is GUI-thread-only. Cache the already scaled card
// images so repeatedly opening the native menu does not decode every PNG again.
static QHash<QString, LayerPreviewCardCacheEntry> layerPreviewCardCache;
static quint64 layerPreviewCardCacheSequence = 0;

struct LayerContext {
    QPointer<QObject> widgetObject;
    void* widget;
    void* editor;
    SharedPart part;
    SharedPackage package;
    LayerState state;

    LayerContext() : widget(nullptr), editor(nullptr) {}
};

enum LayerOperation {
    ActivateLayerOperation,
    AddLayerOperation,
    DeleteActiveLayerOperation,
    RefreshLayerPreviewsOperation
};

static QString nativeDocumentLayerId() {
    if (!documentLayerName || documentLayerName->empty())
        return QString();
    return QString::fromUtf8(
        documentLayerName->data(),
        static_cast<int>(documentLayerName->size()));
}

static bool safeLayerId(QString const& id) {
    if (!id.startsWith(QLatin1String("cnt.layer.")) || id.size() > 96)
        return false;
    for (int i = 0; i < id.size(); ++i) {
        ushort const value = id.at(i).unicode();
        if (!((value >= 'a' && value <= 'z')
                || (value >= '0' && value <= '9')
                || value == '.' || value == '-')) {
            return false;
        }
    }
    return true;
}

static QString canonicalNotebookPath(QString const& notebookPath) {
    QString const canonical = QFileInfo(notebookPath).canonicalFilePath();
    return canonical.isEmpty()
        ? QDir::cleanPath(notebookPath)
        : canonical;
}

static QString layerStatePath(QString const& notebookPath, QString const& partId) {
    QByteArray key = canonicalNotebookPath(notebookPath).toUtf8();
    key.append('\n');
    key.append(partId.toUtf8());
    QByteArray const digest = QCryptographicHash::hash(
        key, QCryptographicHash::Sha256).toHex();
    return QDir(QLatin1String(kLayerRoot)).filePath(
        QString::fromLatin1(digest) + QLatin1String(".json"));
}

static void traceSerializedLayerProbe(
    LayerState const& state,
    QString const& id,
    char const* phase) {
    int const marker = notebookArchiveContainsLayerId(
        state.notebookPath, state.partId, id);
    QString markerText = QLatin1String("indeterminate");
    if (marker > 0)
        markerText = QLatin1String("present");
    else if (marker == 0)
        markerText = QLatin1String("absent");
    QFileInfo const notebook(state.notebookPath);
    trace(QLatin1String("layers: archive probe phase=")
        + QLatin1String(phase)
        + QLatin1String(" part=") + state.partId
        + QLatin1String(" id=") + id
        + QLatin1String(" marker=") + markerText
        + QLatin1String(" bytes=") + QString::number(notebook.size())
        + QLatin1String(" mtime-ms=")
        + QString::number(notebook.lastModified().toMSecsSinceEpoch()));
}

static bool saveLayerState(LayerState const& state, QString* error) {
    if (state.notebookPath.isEmpty() || state.partId.isEmpty()) {
        if (error)
            *error = QLatin1String("Layer state not saved: page identity is missing.");
        return false;
    }
    if (!QDir().mkpath(QLatin1String(kLayerRoot))) {
        if (error)
            *error = QLatin1String("Layer state not saved: storage directory failed.");
        return false;
    }

    QJsonArray layers;
    for (int i = 0; i < state.customLayers.size(); ++i) {
        QJsonObject item;
        item.insert(QLatin1String("id"), state.customLayers.at(i).id);
        item.insert(QLatin1String("name"), state.customLayers.at(i).name);
        layers.append(item);
    }

    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("notebook"), state.notebookPath);
    root.insert(QLatin1String("part"), state.partId);
    root.insert(QLatin1String("active"), state.activeId);
    root.insert(QLatin1String("layers"), layers);

    QSaveFile file(layerStatePath(state.notebookPath, state.partId));
    if (!file.open(QIODevice::WriteOnly)
            || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
            || !file.commit()) {
        if (error)
            *error = QLatin1String("Layer state not saved: metadata write failed.");
        return false;
    }
    trace(QLatin1String("layers: sidecar saved file=")
        + QFileInfo(layerStatePath(state.notebookPath, state.partId)).fileName()
        + QLatin1String(" part=") + state.partId
        + QLatin1String(" active=") + state.activeId
        + QLatin1String(" rows=")
        + QString::number(state.customLayers.size()));
    return true;
}

static LayerState loadLayerState(
    QString const& notebookPath,
    QString const& partId) {
    LayerState state;
    state.notebookPath = canonicalNotebookPath(notebookPath);
    state.partId = partId;
    state.activeId = nativeDocumentLayerId();

    QString const sidecarPath = layerStatePath(state.notebookPath, partId);
    QFile file(sidecarPath);
    if (!file.exists()) {
        trace(QLatin1String("layers: sidecar absent file=")
            + QFileInfo(sidecarPath).fileName()
            + QLatin1String(" part=") + partId);
        return state;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
        trace(QLatin1String("layers: sidecar unreadable file=")
            + QFileInfo(sidecarPath).fileName());
        return state;
    }
    QJsonParseError parseError;
    QJsonDocument const document = QJsonDocument::fromJson(
        file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        trace(QLatin1String("layers: sidecar JSON rejected file=")
            + QFileInfo(sidecarPath).fileName());
        return state;
    }

    QJsonObject const root = document.object();
    QString const storedNotebook = root.value(QLatin1String("notebook")).toString();
    QString const storedPart = root.value(QLatin1String("part")).toString();
    if (root.value(QLatin1String("version")).toInt() != 1
            || canonicalNotebookPath(storedNotebook) != state.notebookPath
            || storedPart != partId) {
        trace(QLatin1String("layers: sidecar identity rejected file=")
            + QFileInfo(sidecarPath).fileName()
            + QLatin1String(" expected-part=") + partId
            + QLatin1String(" stored-part=") + storedPart);
        return state;
    }

    QStringList ids;
    QJsonArray const layers = root.value(QLatin1String("layers")).toArray();
    for (int i = 0;
            i < layers.size() && state.customLayers.size() < kMaximumNotebookLayers - 1;
            ++i) {
        QJsonObject const item = layers.at(i).toObject();
        LayerRecord record;
        record.id = item.value(QLatin1String("id")).toString();
        record.name = item.value(QLatin1String("name")).toString().trimmed();
        if (!safeLayerId(record.id) || ids.contains(record.id))
            continue;
        if (record.name.isEmpty() || record.name.size() > 64)
            record.name = QLatin1String("Layer ")
                + QString::number(state.customLayers.size() + 2);
        ids.append(record.id);
        state.customLayers.append(record);
    }

    QString const active = root.value(QLatin1String("active")).toString();
    if (active == nativeDocumentLayerId() || ids.contains(active))
        state.activeId = active;
    trace(QLatin1String("layers: sidecar loaded file=")
        + QFileInfo(sidecarPath).fileName()
        + QLatin1String(" part=") + partId
        + QLatin1String(" active=") + state.activeId
        + QLatin1String(" rows=")
        + QString::number(state.customLayers.size()));
    return state;
}

static bool nativeDocumentLayout(
    LayoutStorage const& layout,
    ManagedObjectStorage* documentLayout,
    QString* error) {
    if (!documentLayout || !atkLayoutRawLayout || !managedObjectDestructor) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layout API is missing.");
        return false;
    }
    try {
        atkLayoutRawLayout(documentLayout->bytes, layout.bytes);
        documentLayout->constructed = true;
    } catch (...) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layout lookup failed.");
        return false;
    }
    if (!documentLayout->hasObject()) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layout is empty.");
        return false;
    }
    return true;
}

static bool nativeLayerExists(
    ManagedObjectStorage const& documentLayout,
    QString const& id,
    bool* exists,
    QString* error) {
    if (!exists || !documentLayoutGetLayer || !layerIteratorIsAtEnd
            || id.isEmpty()) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layer API is missing.");
        return false;
    }
    ManagedObjectStorage iterator;
    try {
        std::string const nativeId = id.toUtf8().constData();
        documentLayoutGetLayer(iterator.bytes, documentLayout.bytes, nativeId);
        iterator.constructed = true;
        *exists = iterator.hasObject() && !layerIteratorIsAtEnd(iterator.bytes);
        return true;
    } catch (...) {
        if (error)
            *error = QLatin1String("Layers unavailable: native layer validation failed.");
        return false;
    }
}

static bool nativeLayerExists(
    LayoutStorage const& layout,
    QString const& id,
    bool* exists,
    QString* error) {
    ManagedObjectStorage documentLayout;
    return nativeDocumentLayout(layout, &documentLayout, error)
        && nativeLayerExists(documentLayout, id, exists, error);
}

static bool reconcileLayerStateWithNativeLayout(
    SharedPart const& part,
    LayerState* state,
    QString* error) {
    if (!state || !atkLayoutRawLayout || !documentLayoutGetLayer
            || !layerIteratorIsAtEnd || !managedObjectDestructor) {
        if (error)
            *error = QLatin1String("Layers unavailable: reconciliation API is missing.");
        return false;
    }

    LayoutStorage layout;
    if (!layoutForPart(part, &layout, error))
        return false;

    ManagedObjectStorage documentLayout;
    if (!nativeDocumentLayout(layout, &documentLayout, error))
        return false;

    bool baseExists = false;
    if (!nativeLayerExists(
            documentLayout, nativeDocumentLayerId(), &baseExists, error)
            || !baseExists) {
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layers unavailable: the native document layer is missing.");
        }
        return false;
    }

    for (int i = 0; i < state->customLayers.size(); ++i) {
        LayerRecord const record = state->customLayers.at(i);
        bool exists = false;
        if (!nativeLayerExists(documentLayout, record.id, &exists, error))
            return false;
        trace(QLatin1String("layers: native layer probe id=") + record.id
            + QLatin1String(" exists=")
            + (exists ? QLatin1String("yes") : QLatin1String("no")));
        if (!exists) {
            // A passive lifecycle callback can observe a page while its
            // native layout is still being reconstructed. Never turn one
            // transient negative lookup into permanent metadata loss. Explicit
            // add/delete paths already update the sidecar transactionally.
            trace(QLatin1String(
                "layers: native layer missing; sidecar metadata preserved id=")
                + record.id);
            if (error) {
                *error = QLatin1String(
                    "Layers are temporarily unavailable: a saved native layer "
                    "is not loaded. Its metadata was preserved.");
            }
            return false;
        }
    }

    trace(QLatin1String("layers: native reconciliation verified rows=")
        + QString::number(state->customLayers.size())
        + QLatin1String(" active=") + state->activeId);
    return true;
}

static bool loadLayerContext(
    QObject* controller,
    LayerContext* context,
    QString* error) {
    if (!layerHooksReady || !context) {
        if (error)
            *error = QLatin1String("Layers unavailable: firmware APIs are not ready.");
        return false;
    }
    QObject* const widgetObject = findNotebookWidget(controller);
    if (!widgetObject) {
        if (error)
            *error = QLatin1String("Layers unavailable: notebook widget was not found.");
        return false;
    }

    context->widgetObject = widgetObject;
    context->widget = widgetObject;
    context->editor = notePadEditor(context->widget);
    if (!context->editor) {
        if (error)
            *error = QLatin1String("Layers unavailable: notebook editor is not ready.");
        return false;
    }
    context->part = editorGetPart(context->editor);
    context->package = context->part
        ? partGetPackage(context->part.get()) : SharedPackage();
    QString const rawNotebookPath = QDir::cleanPath(widgetFilePath(context->widget));
    QString const notebookPath = canonicalNotebookPath(rawNotebookPath);
    QString const partId = partStableId(context->part);
    if (!context->part || !context->package
            || !rawNotebookPath.startsWith(QLatin1String("/mnt/onboard/"))
            || partId.isEmpty()
            || nativeDocumentLayerId().isEmpty()) {
        if (error)
            *error = QLatin1String("Layers unavailable: page identity is incomplete.");
        return false;
    }
    trace(QLatin1String("layers: context raw=") + rawNotebookPath
        + QLatin1String(" canonical=") + notebookPath
        + QLatin1String(" part=") + partId
        + QLatin1String(" sidecar=")
        + QFileInfo(layerStatePath(notebookPath, partId)).fileName());
    context->state = loadLayerState(notebookPath, partId);
    return reconcileLayerStateWithNativeLayout(
        context->part, &context->state, error);
}

// Virtual atk::core::Tool::restrictToLayer(std::string const&) via vtable slot
// +0x54 on the concrete tool. For a Pen it stores the string penDown reads; for
// a ToolDispatcher (diagram backends) it runs the override. Returns false only
// when the tool or its vtable entry is missing.
static bool restrictToolToLayer(void* tool, std::string const& layerId) {
    if (!tool)
        return false;
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return false;
    typedef void (*ToolRestrictToLayerFn)(void*, std::string const&);
    ToolRestrictToLayerFn const restrictToLayer =
        *reinterpret_cast<ToolRestrictToLayerFn*>(
            static_cast<char*>(vptr) + kToolRestrictToLayerVtableSlot);
    if (!restrictToLayer)
        return false;
    restrictToLayer(tool, layerId);
    return true;
}

static std::string restrictedToolLayer(void* tool) {
    if (!tool)
        return std::string();
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (!vptr)
        return std::string();
    typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
    ToolRestrictedLayerFn const restrictedLayer =
        *reinterpret_cast<ToolRestrictedLayerFn*>(
            static_cast<char*>(vptr) + kToolRestrictedLayerVtableSlot);
    return restrictedLayer ? restrictedLayer(tool) : std::string();
}

// DiagramPen::penDown (BN 0x7bf1e4) calls SmartPen::penDown, which reaches
// core Pen::penDown at BN 0xa93cf8. Core Pen then reads this same virtual
// restrictedLayer() value before setStrokeLayer at BN 0xa894be. Reassert the
// per-tool desired ID immediately before the stock DiagramPen implementation;
// this is both the narrow fix and the decisive on-device trace point.
static bool layerGuardedDiagramPenDown(
    void* pen,
    void const* pointerInfo) {
    if (!diagramPenPenDownOriginal)
        return false;

    std::string desired;
    {
        QMutexLocker locker(&desiredDiagramPenLayersMutex);
        std::map<void*, std::string>::const_iterator const it =
            desiredDiagramPenLayers.find(pen);
        if (it != desiredDiagramPenLayers.end())
            desired = it->second;
    }

    std::string before;
    std::string after;
    bool corrected = false;
    try {
        before = restrictedToolLayer(pen);
        if (!desired.empty() && before != desired) {
            corrected = restrictToolToLayer(pen, desired);
        }
        after = restrictedToolLayer(pen);
    } catch (...) {
        trace("layers: diagram pen-down layer guard threw; stock pen-down preserved");
    }

    if (layerGuardedDiagramPenTraceBudget > 0) {
        --layerGuardedDiagramPenTraceBudget;
        trace(QLatin1String("layers: diagram pen-down layer guard desired=")
            + QString::fromUtf8(
                desired.data(), static_cast<int>(desired.size()))
            + QLatin1String(" before=")
            + QString::fromUtf8(before.data(), static_cast<int>(before.size()))
            + QLatin1String(" after=")
            + QString::fromUtf8(after.data(), static_cast<int>(after.size()))
            + QLatin1String(" corrected=")
            + (corrected ? QLatin1String("yes") : QLatin1String("no")));
    }
    return diagramPenPenDownOriginal(pen, pointerInfo);
}

static bool initializeLayerGuardedDiagramPenVtable() {
    if (layerGuardedDiagramPenVtableReady)
        return true;
    if (!diagramPenVtable)
        return false;

    memcpy(
        layerGuardedDiagramPenVtable,
        diagramPenVtable,
        kDiagramPenVtableWords * sizeof(void*));
    size_t const slot = 2
        + kDiagramPenPenDownVtableSlot / sizeof(void*);
    if (slot >= kDiagramPenVtableWords)
        return false;

    union {
        void* pointer;
        DiagramPenPenDownFn function;
    } original;
    original.pointer = layerGuardedDiagramPenVtable[slot];
    if (!pointerMatchesVma(original.pointer, kDiagramPenPenDownVma)) {
        trace("layers: diagram pen vtable clone rejected unexpected penDown slot");
        return false;
    }
    diagramPenPenDownOriginal = original.function;

    union {
        void* pointer;
        DiagramPenPenDownFn function;
    } replacement;
    replacement.function = layerGuardedDiagramPenDown;
    layerGuardedDiagramPenVtable[slot] = replacement.pointer;
    layerGuardedDiagramPenVtableReady = true;
    trace("layers: diagram pen-down layer guard vtable clone verified");
    return true;
}

static bool isExactOrLayerGuardedDiagramPen(void* tool) {
    if (!tool || !diagramPenVtable)
        return false;
    void* const vptr = *reinterpret_cast<void**>(tool);
    return vptr == static_cast<char*>(diagramPenVtable) + 8
        || (layerGuardedDiagramPenVtableReady
            && vptr == static_cast<void*>(
                layerGuardedDiagramPenVtable + 2));
}

static bool armLayerGuardedDiagramPen(
    void* pen,
    std::string const& layerId) {
    if (!isExactOrLayerGuardedDiagramPen(pen)
            || !initializeLayerGuardedDiagramPenVtable()) {
        return false;
    }
    void* const stockVptr = static_cast<char*>(diagramPenVtable) + 8;
    if (*reinterpret_cast<void**>(pen) == stockVptr) {
        *reinterpret_cast<void**>(pen) = static_cast<void*>(
            layerGuardedDiagramPenVtable + 2);
    }
    {
        QMutexLocker locker(&desiredDiagramPenLayersMutex);
        desiredDiagramPenLayers[pen] = layerId;
    }
    layerGuardedDiagramPenTraceBudget = 8;
    return restrictToolToLayer(pen, layerId);
}

static void clearLayerAwareEraserSelection(void* selection) {
    if (!selection || !selectionSelectNone)
        return;
    try {
        selectionSelectNone(selection);
    } catch (...) {
        // The original firmware selection remains valid. Avoid allowing an
        // exception to cross the virtual callback boundary during pen input.
    }
}

// Apply the same named-layer/Intersection operation used by
// Selector::computeSelection after a concrete eraser has performed its stock
// polygon hit test and stroke-boundary adjustment.
static void filterLayerAwareEraserSelection(
    void* result,
    void* eraser,
    char const* implementation) {
    try {
        if (!eraser || !result || !selectionSelectLayer || !selectionIsEmpty) {
            clearLayerAwareEraserSelection(result);
            return;
        }
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (!vptr) {
            clearLayerAwareEraserSelection(result);
            return;
        }
        typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
        ToolRestrictedLayerFn const restrictedLayer =
            *reinterpret_cast<ToolRestrictedLayerFn*>(
                static_cast<char*>(vptr) + 0x58);
        if (!restrictedLayer) {
            clearLayerAwareEraserSelection(result);
            return;
        }
        std::string const& activeLayer = restrictedLayer(eraser);
        if (activeLayer.empty()) {
            clearLayerAwareEraserSelection(result);
            return;
        }
        bool const emptyBefore = selectionIsEmpty(result);
        selectionSelectLayer(result, activeLayer, kSelectionModeIntersect);
        bool const emptyAfter = selectionIsEmpty(result);
        if (layerAwareEraserTraceBudget > 0) {
            --layerAwareEraserTraceBudget;
            trace(QLatin1String(
                "layers: eraser selection intersected active layer id=")
                + QString::fromUtf8(
                    activeLayer.data(), static_cast<int>(activeLayer.size()))
                + QLatin1String(" mode=2 implementation=")
                + QLatin1String(implementation)
                + QLatin1String(" before=")
                + (emptyBefore ? QLatin1String("empty")
                               : QLatin1String("nonempty"))
                + QLatin1String(" after=")
                + (emptyAfter ? QLatin1String("empty")
                              : QLatin1String("nonempty")));
        }
    } catch (...) {
        clearLayerAwareEraserSelection(result);
        if (layerAwareEraserTraceBudget > 0) {
            --layerAwareEraserTraceBudget;
            trace(QLatin1String(
                "layers: eraser layer intersection failed closed implementation=")
                + QLatin1String(implementation));
        }
    }
}

// ARM returns atk::core::Selection by hidden result pointer. Preserve that
// exact ABI for both concrete implementations: call the corresponding Kobo
// original first, then filter the returned selection in place.
static void* layerAwareDrawingEraserSelectionFromPoints(
    void* result,
    void* eraser,
    void const* points) {
    if (!drawingEraserSelectionFromPointsOriginal)
        return result;
    drawingEraserSelectionFromPointsOriginal(result, eraser, points);
    filterLayerAwareEraserSelection(result, eraser, "snt");
    return result;
}

static void* layerAwarePlainDrawingEraserSelectionFromPoints(
    void* result,
    void* eraser,
    void const* points) {
    if (!plainDrawingEraserSelectionFromPointsOriginal)
        return result;
    plainDrawingEraserSelectionFromPointsOriginal(result, eraser, points);
    filterLayerAwareEraserSelection(result, eraser, "global");
    return result;
}

// Core Eraser::selectionFromPoints constructs a fresh Selection and applies
// its polygon with Replace. On raw-content Diagram pages that Replace step is
// already scoped to the document layer, so intersecting a custom layer after
// the fact cannot recover its strokes. Reuse the stock call to construct the
// correctly bound Selection, then deliberately discard its contents and
// reproduce Selector::computeSelection's verified order: layer/Replace first,
// polygon/Intersection second. Kobo's own strokerPolygon supplies the exact
// eraser geometry from the opaque PointerInfo vector. The custom-layer +0x7c
// path deliberately bypasses DiagramEraser's semantic transform, so restore
// its policy-0 whole-stroke behavior here while the mutable, layer-scoped
// Selection is still available. Policy 1 remains precise/brush erasing.
static void* layerAwareDiagramEraserSelectionFromPoints(
    void* result,
    void* eraser,
    void const* points) {
    if (!diagramEraserSelectionFromPointsOriginal)
        return result;
    diagramEraserSelectionFromPointsOriginal(result, eraser, points);
    try {
        if (!result || !eraser || !points || !selectionSelectLayer
                || !selectionSelectPolygon || !selectionSelectNone
                || !selectionIsEmpty || !selectionAdjustToStrokeBoundaries
                || !eraserPolicy || !eraserStrokerPolygon) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (!vptr) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
        ToolRestrictedLayerFn const restrictedLayer =
            *reinterpret_cast<ToolRestrictedLayerFn*>(
                static_cast<char*>(vptr) + 0x58);
        if (!restrictedLayer) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        std::string const& activeLayer = restrictedLayer(eraser);
        if (activeLayer.empty()) {
            clearLayerAwareEraserSelection(result);
            return result;
        }

        bool const stockEmpty = selectionIsEmpty(result);
        selectionSelectLayer(result, activeLayer, kSelectionModeReplace);
        bool const layerEmpty = selectionIsEmpty(result);

        float const width = *reinterpret_cast<float const*>(
            static_cast<char const*>(eraser) + kEraserWidthOffset)
            * *reinterpret_cast<float const*>(
                static_cast<char const*>(eraser) + kEraserViewScaleOffset);
        if (!(width > 0.0f && width < 10000.0f)) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        PointerInfoVectorOpaque const& pointVector =
            *static_cast<PointerInfoVectorOpaque const*>(points);
        std::vector<SelectionPointOpaque> const polygon =
            eraserStrokerPolygon(width, pointVector);
        if (polygon.empty()) {
            clearLayerAwareEraserSelection(result);
            return result;
        }
        selectionSelectPolygon(
            result,
            &polygon[0],
            static_cast<int>(polygon.size()),
            kSelectionModeIntersect);
        bool geometryEmpty = selectionIsEmpty(result);

        size_t const customPrefixLength = sizeof(kCustomLayerIdPrefix) - 1;
        bool const customLayer = activeLayer.compare(
            0, customPrefixLength, kCustomLayerIdPrefix) == 0;
        int const policy = eraserPolicy(eraser);
        bool boundaryAdjusted = false;
        if (customLayer && policy == kEraserPolicyStroke && !geometryEmpty) {
            // Stock DrawingEraser and DiagramEraser use exactly 0.0f for this
            // first whole-stroke expansion. It expands the already layer-
            // scoped Selection; it does not perform a fresh page hit-test.
            selectionAdjustToStrokeBoundaries(result, 0.0f);
            boundaryAdjusted = true;
            geometryEmpty = selectionIsEmpty(result);
        }

        if (layerAwareEraserTraceBudget > 0) {
            --layerAwareEraserTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser layer-first selection id=")
                + QString::fromUtf8(
                    activeLayer.data(), static_cast<int>(activeLayer.size()))
                + QLatin1String(" layer-mode=0 polygon-mode=2 stock=")
                + (stockEmpty ? QLatin1String("empty")
                              : QLatin1String("nonempty"))
                + QLatin1String(" layer=")
                + (layerEmpty ? QLatin1String("empty")
                              : QLatin1String("nonempty"))
                + QLatin1String(" geometry=")
                + (geometryEmpty ? QLatin1String("empty")
                                 : QLatin1String("nonempty"))
                + QLatin1String(" policy=")
                + QString::number(policy)
                + QLatin1String(" boundary-adjusted=")
                + (boundaryAdjusted ? QLatin1String("yes")
                                    : QLatin1String("no"))
                + QLatin1String(" polygon-points=")
                + QString::number(static_cast<int>(polygon.size())));
        }
    } catch (...) {
        clearLayerAwareEraserSelection(result);
        if (layerAwareEraserTraceBudget > 0) {
            --layerAwareEraserTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser layer-first selection failed closed"));
        }
    }
    return result;
}

// DiagramEraser::updateSelection (+0x7c) receives our +0x78 layer-scoped hit
// selection as `selection`, but its stock diagram override rebuilds the erase
// set from layoutGroup/selectByType/itemsIntersecting. Hardware traces show
// that override returns for Layer 2 but does not reach the wrapper's post-call
// trace for Layer 3, despite both +0x78 selections becoming non-empty. For only
// plugin-created cnt.layer.* IDs, bypass that diagram-only transform and call
// core Eraser::updateSelection directly. It clones/unions `selection` into the
// final Eraser+0xd0 member and returns; core Eraser::penUp then invokes vslot
// +0x80, whose DiagramEraser implementation removes that same selection. The
// document/base layer continues through the exact stock override.
static QLatin1String emptinessLabel(int state) {
    if (state == 0)
        return QLatin1String("nonempty");
    if (state == 1)
        return QLatin1String("empty");
    return QLatin1String("unknown");
}

static uint32_t layerAwareDiagramEraserUpdateSelection(
    void* eraser,
    void const* selection,
    void const* points) {
    if (!diagramEraserUpdateSelectionOriginal)
        return 0;

    int incomingEmpty = -1;
    int committedBeforeEmpty = -1;
    int committedAfterEmpty = -1;
    bool haveRestrict = false;
    std::string activeLayer;
    try {
        if (selectionIsEmpty && selection)
            incomingEmpty = selectionIsEmpty(selection) ? 1 : 0;
        void* const vptr = *reinterpret_cast<void**>(eraser);
        if (vptr) {
            typedef std::string const& (*ToolRestrictedLayerFn)(void const*);
            ToolRestrictedLayerFn const restrictedLayer =
                *reinterpret_cast<ToolRestrictedLayerFn*>(
                    static_cast<char*>(vptr) + 0x58);
            if (restrictedLayer) {
                activeLayer = restrictedLayer(eraser);
                haveRestrict = true;
            }
        }
    } catch (...) {
        if (layerAwareDiagramEraserObserverTraceBudget > 0) {
            --layerAwareDiagramEraserObserverTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser routing inspection failed; stock update preserved"));
        }
        return diagramEraserUpdateSelectionOriginal(
            eraser, selection, points);
    }

    size_t const customPrefixLength = sizeof(kCustomLayerIdPrefix) - 1;
    bool const customLayer = haveRestrict
        && activeLayer.compare(
            0, customPrefixLength, kCustomLayerIdPrefix) == 0;
    if (!customLayer || !coreEraserUpdateSelection) {
        return diagramEraserUpdateSelectionOriginal(
            eraser, selection, points);
    }

    if (selectionIsEmpty && eraser) {
        try {
            committedBeforeEmpty = selectionIsEmpty(
                static_cast<char*>(eraser)
                    + kEraserFinalSelectionOffset) ? 1 : 0;
        } catch (...) {
            committedBeforeEmpty = -1;
        }
    }
    if (layerAwareDiagramEraserObserverTraceBudget > 0) {
        --layerAwareDiagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser core-only enter id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" incoming=")
            + emptinessLabel(incomingEmpty)
            + QLatin1String(" committed-before=")
            + emptinessLabel(committedBeforeEmpty));
    }

    uint32_t result = 0;
    try {
        result = coreEraserUpdateSelection(eraser, selection, points);
        if (selectionIsEmpty && eraser) {
            committedAfterEmpty = selectionIsEmpty(
                static_cast<char*>(eraser)
                    + kEraserFinalSelectionOffset) ? 1 : 0;
        }
    } catch (...) {
        if (layerAwareDiagramEraserObserverTraceBudget > 0) {
            --layerAwareDiagramEraserObserverTraceBudget;
            trace(QLatin1String(
                "layers: diagram eraser core-only update threw; failed closed"));
        }
        return 0;
    }

    if (layerAwareDiagramEraserObserverTraceBudget > 0) {
        --layerAwareDiagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser core-only exit id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" committed-after=")
            + emptinessLabel(committedAfterEmpty)
            + QLatin1String(" changed=")
            + QLatin1String(result ? "yes" : "no"));
    }
    return result;
}

// Core Eraser::penUp loads vslot +0x80 at BN 0xa86f0a and calls it at
// 0xa86f0e. Observe the exact final Selection which DiagramEraser removes,
// while preserving the stock function's single call, return value, and
// exception behavior.
static int32_t layerAwareDiagramEraserEraseSelection(void* eraser) {
    if (!diagramEraserEraseSelectionOriginal)
        return 0;

    int committedBeforeEmpty = -1;
    std::string activeLayer;
    try {
        if (selectionIsEmpty && eraser) {
            committedBeforeEmpty = selectionIsEmpty(
                static_cast<char*>(eraser)
                    + kEraserFinalSelectionOffset) ? 1 : 0;
        }
        activeLayer = restrictedToolLayer(eraser);
    } catch (...) {}

    if (layerAwareDiagramEraserObserverTraceBudget > 0) {
        --layerAwareDiagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser remove enter id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" committed=")
            + emptinessLabel(committedBeforeEmpty));
    }

    int32_t const result = diagramEraserEraseSelectionOriginal(eraser);

    int committedAfterEmpty = -1;
    try {
        if (selectionIsEmpty && eraser) {
            committedAfterEmpty = selectionIsEmpty(
                static_cast<char*>(eraser)
                    + kEraserFinalSelectionOffset) ? 1 : 0;
        }
    } catch (...) {}
    if (layerAwareDiagramEraserObserverTraceBudget > 0) {
        --layerAwareDiagramEraserObserverTraceBudget;
        trace(QLatin1String("layers: diagram eraser remove exit id=")
            + QString::fromUtf8(
                activeLayer.data(), static_cast<int>(activeLayer.size()))
            + QLatin1String(" committed-after=")
            + emptinessLabel(committedAfterEmpty)
            + QLatin1String(" removed=")
            + QLatin1String(result ? "yes" : "no"));
    }
    return result;
}

// Installs the +0x7c custom-layer update and +0x80 removal observer into the
// already-cloned DiagramEraser vtable. Requires the base +0x78 clone to be
// verified first. Both stock slots are validated before either is replaced,
// so the object vptr is never published with a partial downstream clone.
static bool installLayerAwareDiagramEraserUpdateHook() {
    if (layerAwareDiagramEraserObserversReady)
        return true;
    if (!layerAwareDiagramEraserVtableReady || !coreEraserUpdateSelection)
        return false;

    size_t const updateSlot = 2
        + kDiagramEraserUpdateSelectionVtableSlot / sizeof(void*);
    size_t const eraseSlot = 2
        + kDiagramEraserEraseSelectionVtableSlot / sizeof(void*);
    if (updateSlot >= kDrawingEraserVtableWords
            || eraseSlot >= kDrawingEraserVtableWords)
        return false;

    union {
        void* pointer;
        DiagramEraserUpdateSelectionFn function;
    } originalUpdate;
    originalUpdate.pointer = layerAwareDiagramEraserVtable[updateSlot];
    if (!pointerMatchesVma(
            originalUpdate.pointer, kDiagramEraserUpdateSelectionVma)) {
        trace(QLatin1String(
            "layers: diagram eraser update hook rejected unexpected slot"));
        return false;
    }

    union {
        void* pointer;
        DiagramEraserEraseSelectionFn function;
    } originalErase;
    originalErase.pointer = layerAwareDiagramEraserVtable[eraseSlot];
    if (!pointerMatchesVma(
            originalErase.pointer, kDiagramEraserEraseSelectionVma)) {
        trace(QLatin1String(
            "layers: diagram eraser remove observer rejected unexpected slot"));
        return false;
    }

    union {
        void* pointer;
        DiagramEraserUpdateSelectionFn function;
    } replacementUpdate;
    replacementUpdate.function = layerAwareDiagramEraserUpdateSelection;
    union {
        void* pointer;
        DiagramEraserEraseSelectionFn function;
    } replacementErase;
    replacementErase.function = layerAwareDiagramEraserEraseSelection;

    diagramEraserUpdateSelectionOriginal = originalUpdate.function;
    diagramEraserEraseSelectionOriginal = originalErase.function;
    layerAwareDiagramEraserVtable[updateSlot] = replacementUpdate.pointer;
    layerAwareDiagramEraserVtable[eraseSlot] = replacementErase.pointer;
    layerAwareDiagramEraserObserversReady = true;
    trace(QLatin1String(
        "layers: diagram eraser core-only update and remove observer installed"));
    return true;
}

static bool initializeLayerAwareEraserVtable(
    void* stockVtable,
    void** layerVtable,
    bool* ready,
    DrawingEraserSelectionFromPointsFn* originalOut,
    DrawingEraserSelectionFromPointsFn replacement,
    uintptr_t expectedSelectionVma,
    char const* implementation) {
    if (*ready)
        return true;
    if (!stockVtable || !selectionSelectLayer || !selectionSelectNone)
        return false;

    memcpy(
        layerVtable,
        stockVtable,
        kDrawingEraserVtableWords * sizeof(void*));
    size_t const slot = 2
        + kDrawingEraserSelectionFromPointsVtableSlot / sizeof(void*);
    if (slot >= kDrawingEraserVtableWords)
        return false;

    union {
        void* pointer;
        DrawingEraserSelectionFromPointsFn function;
    } original;
    original.pointer = layerVtable[slot];
    if (!pointerMatchesVma(original.pointer, expectedSelectionVma)) {
        trace(QLatin1String(
            "layers: eraser vtable clone rejected unexpected selection slot implementation=")
            + QLatin1String(implementation));
        return false;
    }
    *originalOut = original.function;

    union {
        void* pointer;
        DrawingEraserSelectionFromPointsFn function;
    } replacementPointer;
    replacementPointer.function = replacement;
    layerVtable[slot] = replacementPointer.pointer;
    *ready = true;
    trace(QLatin1String(
        "layers: layer-aware eraser vtable clone verified implementation=")
        + QLatin1String(implementation));
    return true;
}

enum ConcreteDrawingEraserKind {
    kNotDrawingEraser,
    kSntDrawingEraser,
    kPlainDrawingEraser,
    kDiagramEraser
};

static ConcreteDrawingEraserKind concreteDrawingEraserKind(void* tool) {
    if (!tool)
        return kNotDrawingEraser;
    void* const vptr = *reinterpret_cast<void**>(tool);
    if (drawingEraserVtable
            && (vptr == static_cast<char*>(drawingEraserVtable) + 8
                || (layerAwareDrawingEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerAwareDrawingEraserVtable + 2)))) {
        return kSntDrawingEraser;
    }
    if (plainDrawingEraserVtable
            && (vptr == static_cast<char*>(plainDrawingEraserVtable) + 8
                || (layerAwarePlainDrawingEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerAwarePlainDrawingEraserVtable + 2)))) {
        return kPlainDrawingEraser;
    }
    if (diagramEraserVtable
            && (vptr == static_cast<char*>(diagramEraserVtable) + 8
                || (layerAwareDiagramEraserVtableReady
                    && vptr == static_cast<void*>(
                        layerAwareDiagramEraserVtable + 2)))) {
        return kDiagramEraser;
    }
    return kNotDrawingEraser;
}

static bool isLayerAwareDrawingEraser(void* tool) {
    return concreteDrawingEraserKind(tool) != kNotDrawingEraser;
}

static char const* exactEraserSizeClass(void* tool) {
    if (!tool)
        return nullptr;
    ConcreteDrawingEraserKind const drawingKind =
        concreteDrawingEraserKind(tool);
    if (drawingKind == kSntDrawingEraser)
        return "snt-drawing";
    if (drawingKind == kPlainDrawingEraser)
        return "global-drawing";
    if (drawingKind == kDiagramEraser)
        return "diagram";

    void* const vptr = *reinterpret_cast<void**>(tool);
    if (coreEraserVtable
            && vptr == static_cast<char*>(coreEraserVtable) + 8) {
        return "core";
    }
    if (textEraserSntVtable
            && vptr == static_cast<char*>(textEraserSntVtable) + 8) {
        return "snt-text";
    }
    if (mathEraserVtable
            && vptr == static_cast<char*>(mathEraserVtable) + 8) {
        return "math";
    }
    if (textEraserVtable
            && vptr == static_cast<char*>(textEraserVtable) + 8) {
        return "text";
    }
    return nullptr;
}

static bool applyRadiusToExactEraser(
    void* tool,
    float desiredRadius,
    char const* reason,
    char const* source) {
    char const* const eraserClass = exactEraserSizeClass(tool);
    if (!eraserClass || !eraserSetRadius || !eraserRadius)
        return false;

    try {
        float const beforeRadius = eraserRadius(tool);
        int const beforePolicy = eraserPolicy ? eraserPolicy(tool) : -1;
        std::string const beforeLayer = restrictedToolLayer(tool);
        eraserSetRadius(tool, desiredRadius);
        float const afterRadius = eraserRadius(tool);
        int const afterPolicy = eraserPolicy ? eraserPolicy(tool) : -1;
        std::string const afterLayer = restrictedToolLayer(tool);
        float difference = afterRadius - desiredRadius;
        if (difference < 0.0f)
            difference = -difference;
        bool const preserved = beforePolicy == afterPolicy
            && beforeLayer == afterLayer;
        bool const radiusApplied = difference < 0.001f;
        trace(QLatin1String("eraser-size: apply reason=")
            + QLatin1String(reason)
            + QLatin1String(" source=") + QLatin1String(source)
            + QLatin1String(" class=") + QLatin1String(eraserClass)
            + QLatin1String(" before=")
            + QString::number(beforeRadius, 'f', 3)
            + QLatin1String(" requested=")
            + QString::number(desiredRadius, 'f', 3)
            + QLatin1String(" after=")
            + QString::number(afterRadius, 'f', 3)
            + QLatin1String(" policy=") + QString::number(afterPolicy)
            + QLatin1String(" layer=")
            + QString::fromUtf8(
                afterLayer.data(), static_cast<int>(afterLayer.size()))
            + QLatin1String(" policy-layer-preserved=")
            + (preserved ? QLatin1String("yes") : QLatin1String("no")));
        if (!preserved)
            trace("eraser-size: setter changed policy/layer unexpectedly");
        return radiusApplied && preserved;
    } catch (...) {
        trace(QLatin1String("eraser-size: exact eraser rejected radius reason=")
            + QLatin1String(reason));
        return false;
    }
}

// Apply one plugin-owned size to every exact live core-Eraser subclass. This
// deliberately duplicates the already-proven backend map traversal instead of
// changing the working layer adapters. All shared_ptr owners remain in scope
// while their raw firmware objects are inspected or called.
static bool applyConfiguredEraserSizeForWidget(
    QObject* widgetObject,
    char const* reason) {
    if (!eraserSizeApisReady || !widgetObject
            || !isNotebookWidget(widgetObject)) {
        return false;
    }

    int const index = configuredEraserSizeIndex();
    if (!validEraserSizeIndex(index))
        return false;
    void* const editor = notePadEditor(widgetObject);
    if (!editor || !editorGetRenderer || !rendererGetBackend)
        return false;

    try {
        SharedRenderer const renderer = editorGetRenderer(editor);
        void* const backend = renderer
            ? rendererGetBackend(renderer.get()) : nullptr;
        void* const expectedNeboVptr = neboBackendVtable
            ? static_cast<char*>(neboBackendVtable) + 8 : nullptr;
        if (!backend || *reinterpret_cast<void**>(backend) != expectedNeboVptr) {
            trace(QLatin1String("eraser-size: unsupported backend reason=")
                + QLatin1String(reason));
            return false;
        }

        void* const pageController = *reinterpret_cast<void**>(
            static_cast<char*>(backend) + kNeboBackendPageControllerOffset);
        void* const layoutGrid = pageController
            ? *reinterpret_cast<void**>(static_cast<char*>(pageController)
                + kPageControllerLayoutGridOffset)
            : nullptr;
        if (!pageController || !layoutGrid || !layoutGridLineGap
                || !eraserWidthFromThicknessRatio) {
            trace(QLatin1String("eraser-size: page grid unavailable reason=")
                + QLatin1String(reason));
            return false;
        }
        float const lineGap = layoutGridLineGap(layoutGrid);
        if (!(lineGap > 0.0f) || lineGap > 10000.0f) {
            trace(QLatin1String("eraser-size: invalid line gap reason=")
                + QLatin1String(reason));
            return false;
        }
        float const width = eraserWidthFromThicknessRatio(
            kEraserSizeRatios[index], lineGap);
        float const radius = width * 0.5f;
        if (!(radius > 0.0f) || radius > 10000.0f) {
            trace(QLatin1String("eraser-size: invalid converted radius reason=")
                + QLatin1String(reason));
            return false;
        }

        SharedPlatformInputDispatcher const inputDispatcher =
            pageControllerInputDispatcher(pageController);
        if (!inputDispatcher)
            return false;
        SharedTool const currentTool =
            platformInputDispatcherGetCurrentTool(inputDispatcher.get());
        std::vector<void*> seen;
        int exactCount = 0;
        int appliedCount = 0;

        void* const currentRaw = currentTool.get();
        if (exactEraserSizeClass(currentRaw)) {
            seen.push_back(currentRaw);
            ++exactCount;
            if (applyRadiusToExactEraser(
                    currentRaw, radius, reason, "current")) {
                ++appliedCount;
            }
        }

        void* const boxFactory = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(inputDispatcher.get())
                + kPidBoxFactoryOffset);
        if (boxFactory && compositeBoxFactoryBackends) {
            ActiveBackendMap const backends =
                compositeBoxFactoryBackends(boxFactory);
            std::string const emptyId;
            for (ActiveBackendMap::const_iterator it = backends.begin();
                    it != backends.end(); ++it) {
                void* const activeBackend = it->second.get();
                if (!activeBackend)
                    continue;
                void* const vptr = *reinterpret_cast<void**>(activeBackend);
                if (!vptr)
                    continue;
                typedef SharedTool (*ActiveBackendGetToolFn)(
                    void*, int, std::string const&);
                ActiveBackendGetToolFn const getTool =
                    *reinterpret_cast<ActiveBackendGetToolFn*>(
                        static_cast<char*>(vptr)
                            + kActiveBackendGetToolVtableSlot);
                if (!getTool)
                    continue;
                SharedTool const cached = getTool(activeBackend, 4, emptyId);
                void* const raw = cached.get();
                if (!exactEraserSizeClass(raw)
                        || std::find(seen.begin(), seen.end(), raw)
                            != seen.end()) {
                    continue;
                }
                seen.push_back(raw);
                ++exactCount;
                QByteArray const source = QByteArray("backend:")
                    + QByteArray(
                        it->first.data(), static_cast<int>(it->first.size()));
                if (applyRadiusToExactEraser(
                        raw, radius, reason, source.constData())) {
                    ++appliedCount;
                }
            }
        }

        // DrawingBackend keeps the kind-4 shared_ptr at +0x4c. This fallback
        // covers a transient main backend not yet represented in backends().
        if (boxFactory && compositeBoxFactoryMainBackend
                && drawingBackendVtable) {
            SharedActiveBackend const mainBackend =
                compositeBoxFactoryMainBackend(boxFactory);
            void* const rawBackend = mainBackend.get();
            void* const expectedDrawingVptr =
                static_cast<char*>(drawingBackendVtable) + 8;
            if (rawBackend
                    && *reinterpret_cast<void**>(rawBackend)
                        == expectedDrawingVptr) {
                void* const rawEraser = *reinterpret_cast<void**>(
                    static_cast<char*>(rawBackend)
                        + kDrawingBackendEraserToolOffset);
                if (exactEraserSizeClass(rawEraser)
                        && std::find(seen.begin(), seen.end(), rawEraser)
                            == seen.end()) {
                    seen.push_back(rawEraser);
                    ++exactCount;
                    if (applyRadiusToExactEraser(
                            rawEraser, radius, reason, "main-fallback")) {
                        ++appliedCount;
                    }
                }
            }
        }

        trace(QLatin1String("eraser-size: fanout reason=")
            + QLatin1String(reason)
            + QLatin1String(" index=") + QString::number(index)
            + QLatin1String(" ratio=")
            + QString::number(kEraserSizeRatios[index], 'f', 2)
            + QLatin1String(" line-gap=")
            + QString::number(lineGap, 'f', 3)
            + QLatin1String(" radius=")
            + QString::number(radius, 'f', 3)
            + QLatin1String(" exact=") + QString::number(exactCount)
            + QLatin1String(" applied=") + QString::number(appliedCount));
        return exactCount > 0 && appliedCount == exactCount;
    } catch (...) {
        trace(QLatin1String("eraser-size: live fanout threw reason=")
            + QLatin1String(reason));
        return false;
    }
}

static bool armLayerAwareDrawingEraser(
    void* tool,
    std::string const& layerId) {
    ConcreteDrawingEraserKind const kind = concreteDrawingEraserKind(tool);
    if (kind == kNotDrawingEraser)
        return false;

    void* stockVtable = nullptr;
    void** layerVtable = nullptr;
    bool* ready = nullptr;
    DrawingEraserSelectionFromPointsFn* original = nullptr;
    DrawingEraserSelectionFromPointsFn replacement = nullptr;
    uintptr_t expectedSelectionVma = 0;
    char const* implementation = nullptr;
    if (kind == kSntDrawingEraser) {
        stockVtable = drawingEraserVtable;
        layerVtable = layerAwareDrawingEraserVtable;
        ready = &layerAwareDrawingEraserVtableReady;
        original = &drawingEraserSelectionFromPointsOriginal;
        replacement = layerAwareDrawingEraserSelectionFromPoints;
        expectedSelectionVma = kDrawingEraserSelectionFromPointsVma;
        implementation = "snt";
    } else if (kind == kPlainDrawingEraser) {
        stockVtable = plainDrawingEraserVtable;
        layerVtable = layerAwarePlainDrawingEraserVtable;
        ready = &layerAwarePlainDrawingEraserVtableReady;
        original = &plainDrawingEraserSelectionFromPointsOriginal;
        replacement = layerAwarePlainDrawingEraserSelectionFromPoints;
        expectedSelectionVma = kPlainDrawingEraserSelectionFromPointsVma;
        implementation = "global";
    } else if (kind == kDiagramEraser) {
        if (!selectionSelectPolygon || !eraserStrokerPolygon)
            return false;
        stockVtable = diagramEraserVtable;
        layerVtable = layerAwareDiagramEraserVtable;
        ready = &layerAwareDiagramEraserVtableReady;
        original = &diagramEraserSelectionFromPointsOriginal;
        replacement = layerAwareDiagramEraserSelectionFromPoints;
        expectedSelectionVma = kCoreEraserSelectionFromPointsVma;
        implementation = "diagram-layer-first";
    }

    void* const stockVptr = static_cast<char*>(stockVtable) + 8;
    void* const layerVptr = static_cast<void*>(layerVtable + 2);
    void* const currentVptr = *reinterpret_cast<void**>(tool);
    if (currentVptr != stockVptr
            && (!*ready
                || currentVptr != layerVptr)) {
        return false;
    }
    if (!initializeLayerAwareEraserVtable(
            stockVtable,
            layerVtable,
            ready,
            original,
            replacement,
            expectedSelectionVma,
            implementation)) {
        return false;
    }
    if (kind == kDiagramEraser) {
        if (!installLayerAwareDiagramEraserUpdateHook()) {
            trace(QLatin1String(
                "layers: diagram eraser custom-layer core-only hook unavailable"));
            return false;
        }
        layerAwareDiagramEraserObserverTraceBudget = 60;
    }
    if (currentVptr == stockVptr)
        *reinterpret_cast<void**>(tool) = layerVptr;
    return restrictToolToLayer(tool, layerId);
}

// ToolDispatcher::restrictToLayer uses this exact CompositeBoxFactory::backends
// map traversal and ActiveBackend vslot +0x2c to reach every cached concrete
// tool. Enumerating kind 4 here arms cached DrawingErasers before a later tool
// switch, including pages whose CompositeBoxFactory::mainBackend is a grid
// backend rather than DrawingBackend.
static bool armLayerAwareDrawingErasers(
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId,
    bool* currentIsEraser) {
    bool const currentEraser = isLayerAwareDrawingEraser(currentTool);
    trace(QLatin1String("layers: eraser probe current ")
        + toolIdentity(currentTool, false));
    if (currentIsEraser)
        *currentIsEraser = currentEraser;

    std::vector<void*> seen;
    int backendCount = 0;
    int drawingEraserCount = 0;
    int sntEraserCount = 0;
    int plainEraserCount = 0;
    int diagramEraserCount = 0;
    int armedCount = 0;
    int diagramPenCount = 0;
    int diagramPenRestrictedCount = 0;
    if (currentEraser) {
        seen.push_back(currentTool);
        ++drawingEraserCount;
        ConcreteDrawingEraserKind const kind =
            concreteDrawingEraserKind(currentTool);
        if (kind == kSntDrawingEraser)
            ++sntEraserCount;
        else if (kind == kPlainDrawingEraser)
            ++plainEraserCount;
        else
            ++diagramEraserCount;
        if (armLayerAwareDrawingEraser(currentTool, layerId))
            ++armedCount;
    }

    if (!inputDispatcher || !compositeBoxFactoryBackends) {
        trace("layers: eraser adapter backend enumeration unavailable");
        return armedCount > 0 && diagramEraserCount == 0;
    }
    void* const boxFactory = *reinterpret_cast<void**>(
        static_cast<char*>(inputDispatcher) + kPidBoxFactoryOffset);
    if (!boxFactory) {
        trace("layers: eraser adapter box factory missing");
        return armedCount > 0 && diagramEraserCount == 0;
    }

    try {
        ActiveBackendMap const backends =
            compositeBoxFactoryBackends(boxFactory);
        std::string const emptyId;
        for (ActiveBackendMap::const_iterator it = backends.begin();
                it != backends.end(); ++it) {
            ++backendCount;
            void* const backend = it->second.get();
            if (!backend)
                continue;
            void* const vptr = *reinterpret_cast<void**>(backend);
            if (!vptr)
                continue;
            typedef SharedTool (*ActiveBackendGetToolFn)(
                void*, int, std::string const&);
            ActiveBackendGetToolFn const getTool =
                *reinterpret_cast<ActiveBackendGetToolFn*>(
                    static_cast<char*>(vptr)
                        + kActiveBackendGetToolVtableSlot);
            if (!getTool)
                continue;

            SharedTool const eraser = getTool(backend, 4, emptyId);
            void* const raw = eraser.get();
            trace(QLatin1String("layers: eraser probe backend=")
                + QString::fromUtf8(
                    it->first.data(), static_cast<int>(it->first.size()))
                + QLatin1String(" backend-vptr=") + pointerIdentity(vptr)
                + QLatin1String(" getTool=")
                + pointerIdentity(*reinterpret_cast<void**>(
                    static_cast<char*>(vptr)
                        + kActiveBackendGetToolVtableSlot))
                + QLatin1String(" kind4 ") + toolIdentity(raw, true));
            ConcreteDrawingEraserKind const rawKind =
                concreteDrawingEraserKind(raw);

            // DiagramActiveBackend::getTool maps kind 4 to its cached
            // DiagramEraser at backend+0x64 (BN 0x738c4e) and pen kinds 0-2
            // to its cached DiagramPen at +0x5c (BN 0x738c76). Only after the
            // kind-4 result proves this is the exact Diagram backend do we
            // fetch kind 0. DiagramPen::restrictToLayer assigns the selected
            // ID both to Tool state and locked Diagram data+0x60 (BN
            // 0x7c0e76..0x7c0e8c). The pen-down guard below independently
            // reasserts that ID at the exact point core Pen consumes it.
            if (rawKind == kDiagramEraser) {
                SharedTool const pen = getTool(backend, 0, emptyId);
                void* const penRaw = pen.get();
                bool const exactDiagramPen =
                    isExactOrLayerGuardedDiagramPen(penRaw);
                bool const penRestricted = exactDiagramPen
                    && armLayerGuardedDiagramPen(penRaw, layerId);
                if (exactDiagramPen)
                    ++diagramPenCount;
                if (penRestricted)
                    ++diagramPenRestrictedCount;
                trace(QLatin1String(
                    "layers: diagram cached pen(kind0) backend=")
                    + QString::fromUtf8(
                        it->first.data(), static_cast<int>(it->first.size()))
                    + QLatin1String(" ") + toolIdentity(penRaw, false)
                    + QLatin1String(" exact-or-guarded=")
                    + (exactDiagramPen
                        ? QLatin1String("yes") : QLatin1String("no"))
                    + QLatin1String(" restricted=")
                    + (penRestricted
                        ? QLatin1String("yes") : QLatin1String("no")));
            }
            if (!isLayerAwareDrawingEraser(raw)
                    || std::find(seen.begin(), seen.end(), raw) != seen.end()) {
                continue;
            }
            seen.push_back(raw);
            ++drawingEraserCount;
            ConcreteDrawingEraserKind const kind = rawKind;
            if (kind == kSntDrawingEraser)
                ++sntEraserCount;
            else if (kind == kPlainDrawingEraser)
                ++plainEraserCount;
            else
                ++diagramEraserCount;
            if (armLayerAwareDrawingEraser(raw, layerId))
                ++armedCount;
        }
    } catch (...) {
        trace("layers: eraser adapter backend enumeration threw");
    }

    if (armedCount > 0)
        layerAwareEraserTraceBudget = 8;
    bool const diagramRoutingReady = diagramEraserCount == 0
        || diagramPenRestrictedCount > 0;
    trace(QLatin1String("layers: eraser adapters backends=")
        + QString::number(backendCount)
        + QLatin1String(" drawing=")
        + QString::number(drawingEraserCount)
        + QLatin1String(" snt=") + QString::number(sntEraserCount)
        + QLatin1String(" global=") + QString::number(plainEraserCount)
        + QLatin1String(" diagram=") + QString::number(diagramEraserCount)
        + QLatin1String(" armed=") + QString::number(armedCount)
        + QLatin1String(" diagram-pens=") + QString::number(diagramPenCount)
        + QLatin1String(" pen-restricted=")
        + QString::number(diagramPenRestrictedCount)
        + QLatin1String(" diagram-pen-cache=")
        + (diagramRoutingReady
            ? QLatin1String("ready") : QLatin1String("missing"))
        + QLatin1String(" current=")
        + (currentEraser
            ? QLatin1String("yes") : QLatin1String("no")));
    return armedCount > 0 && diagramRoutingReady;
}

// Mirror DrawingBackend's writing caches. Pen kinds 0-2 share +0x44 and kind
// 6's DrawingBrush also uses atk::core::Pen::penDown, so both writing paths
// retain the layer across category changes. All exact eraser classes are
// armed separately with narrow selection adapters; Diagram uses layer-first
// geometry while the other two intersect their stock selections.
static bool restrictBackendWritingToolsToLayer(
    void* inputDispatcher,
    void* currentTool,
    std::string const& layerId) {
    if (!inputDispatcher || !compositeBoxFactoryMainBackend
            || !drawingBackendVtable) {
        trace("layers: cached writing-tool routing APIs unavailable");
        return false;
    }
    void* const boxFactory = *reinterpret_cast<void**>(
        static_cast<char*>(inputDispatcher) + kPidBoxFactoryOffset);
    if (!boxFactory) {
        trace("layers: cached writing-tool routing box factory missing");
        return false;
    }
    SharedActiveBackend const backend =
        compositeBoxFactoryMainBackend(boxFactory);
    void* const raw = backend.get();
    if (!raw) {
        trace("layers: cached writing-tool routing main backend missing");
        return false;
    }
    void* const expectedVptr = static_cast<char*>(drawingBackendVtable) + 8;
    if (*reinterpret_cast<void**>(raw) != expectedVptr) {
        trace("layers: cached writing-tool routing backend is not DrawingBackend");
        return false;
    }

    void* const eraser = *reinterpret_cast<void**>(
        static_cast<char*>(raw) + kDrawingBackendEraserToolOffset);
    bool const eraserArmed = eraser
        && armLayerAwareDrawingEraser(eraser, layerId);
    trace(QLatin1String("layers: cached eraser(kind4) present=")
        + (eraser ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" current=")
        + (eraser && eraser == currentTool
            ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" adapter=")
        + (eraserArmed
            ? QLatin1String("armed-to-active-layer")
            : QLatin1String("arm-failed")));

    size_t const routedCount = sizeof(kDrawingBackendRestrictedToolOffsets)
        / sizeof(kDrawingBackendRestrictedToolOffsets[0]);
    int restricted = 0;
    for (size_t i = 0; i < routedCount; ++i) {
        void* const tool = *reinterpret_cast<void**>(
            static_cast<char*>(raw) + kDrawingBackendRestrictedToolOffsets[i]);
        bool const applied = tool && restrictToolToLayer(tool, layerId);
        if (applied)
            ++restricted;
        trace(QLatin1String("layers: cached ")
            + QLatin1String(kDrawingBackendRestrictedToolNames[i])
            + QLatin1String(" present=")
            + (tool ? QLatin1String("yes") : QLatin1String("no"))
            + QLatin1String(" current=")
            + (tool && tool == currentTool
                ? QLatin1String("yes") : QLatin1String("no"))
            + QLatin1String(" restricted=")
            + (applied ? QLatin1String("yes") : QLatin1String("no")));
    }

    // Best-effort. The pen is offset[0], attempted first, so restricted > 0 means
    // at least the primary writing tool now carries the layer. Eraser arming and
    // full completeness are reported for the trace but do not gate pen routing.
    if (restricted == static_cast<int>(routedCount) && eraserArmed)
        trace("layers: pen, selector, brush restricted; eraser layer adapter armed");
    else if (restricted > 0)
        trace("layers: cached writing-tool restriction partial");
    else
        trace("layers: cached writing-tool restriction found no writing tool");
    return restricted > 0;
}

static bool applyActiveLayer(
    LayerContext const& context,
    QString const& layerId,
    QString* error) {
    if (!editorGetRenderer || !rendererGetBackend
            || !neboBackendVtable || !pageControllerInputDispatcher
            || !platformInputDispatcherGetCurrentTool) {
        if (error)
            *error = QLatin1String("Layer not selected: routing API is unavailable.");
        return false;
    }

    SharedRenderer const renderer = editorGetRenderer(context.editor);
    void* const backend = renderer ? rendererGetBackend(renderer.get()) : nullptr;
    void* const expectedVptr = static_cast<char*>(neboBackendVtable) + 8;
    if (!backend || *reinterpret_cast<void**>(backend) != expectedVptr) {
        if (error) {
            *error = QLatin1String(
                "Layer not selected: this notebook backend is not supported yet.");
        }
        return false;
    }

    // NeboBackend::selectTool on this firmware reads its PageController
    // shared_ptr object at +0x14/+0x18. The raw object is sufficient while the
    // renderer/backend shared_ptrs above remain alive.
    void* const pageController = *reinterpret_cast<void**>(
        static_cast<char*>(backend) + kNeboBackendPageControllerOffset);
    if (!pageController) {
        if (error)
            *error = QLatin1String("Layer not selected: page controller is missing.");
        return false;
    }

    trace("layers: routing input dispatcher query");
    SharedPlatformInputDispatcher const inputDispatcher =
        pageControllerInputDispatcher(pageController);
    if (!inputDispatcher) {
        if (error)
            *error = QLatin1String(
                "Layer not selected: input dispatcher is unavailable.");
        return false;
    }
    trace("layers: routing input dispatcher acquired");

    SharedTool const currentTool =
        platformInputDispatcherGetCurrentTool(inputDispatcher.get());
    void* const tool = currentTool.get();
    if (!tool) {
        if (error)
            *error = QLatin1String("Layer not selected: main tool is unavailable.");
        return false;
    }
    trace("layers: routing main tool acquired");

    // The current tool is what the very next stroke uses (PID+0x0c), and
    // atk::core::Pen::penDown reads its own restrictedLayer(). Restrict it
    // directly: this is the reliable path that works on ANY backend, not only a
    // DrawingBackend. Every exact eraser adapter consumes the same string;
    // DiagramEraser uses it to build layer-first polygon geometry. Its cached
    // pen is also synchronized for a later toolbar switch back to writing.
    std::string const nativeId = layerId.toUtf8().constData();
    bool currentIsEraser = false;
    bool applied = false;
    try {
        bool const erasersArmed = armLayerAwareDrawingErasers(
            inputDispatcher.get(), tool, nativeId, &currentIsEraser);
        if (currentIsEraser) {
            if (!erasersArmed) {
                if (error) {
                    *error = QLatin1String(
                        "Layer not selected: eraser layer routing is unavailable.");
                }
                trace("layers: current eraser routing incomplete; selection rejected");
                return false;
            }
            applied = true;
            if (concreteDrawingEraserKind(tool) == kDiagramEraser) {
                trace(QLatin1String(
                    "layers: current diagram eraser layer-first geometry armed and cached pen synchronized"));
            } else {
                trace(QLatin1String(
                    "layers: current eraser restricted for selection intersection"));
            }
        } else if (restrictToolToLayer(tool, nativeId)) {
            applied = true;
            trace("layers: routing current tool restriction complete");
        } else {
            trace("layers: routing current tool restriction unavailable");
        }
        // Best-effort: also pin the DrawingBackend's cached pen/selector/brush so
        // the layer survives pen/eraser/selector switches. snt/global cached
        // erasers use selection adapters; the Diagram traversal also updates
        // its cached pen for the next writing-tool switch. Never fatal — a page
        // whose main backend is not a DrawingBackend still routes through the
        // backends() traversal above.
        if (restrictBackendWritingToolsToLayer(
                inputDispatcher.get(), tool, nativeId)) {
            applied = true;
        }
    } catch (...) {
        if (error) {
            *error = QLatin1String(
                "Layer not selected: a notebook writing tool rejected it.");
        }
        trace("layers: routing tool restriction threw");
        return false;
    }
    // Success once any writing tool carries the layer, or current eraser
    // routing is complete. A current DiagramEraser is accepted only after its
    // layer-first adapter is armed and cached DiagramPen is synchronized.
    if (!applied && !currentIsEraser) {
        if (error) {
            *error = QLatin1String(
                "Layer not selected: no writing tool accepted the layer.");
        }
        trace("layers: routing produced no restricted tool");
        return false;
    }
    return true;
}

// The sidecar's active ID is authoritative, but every newly opened page owns
// a fresh set of concrete cached tools. Reassert the exact string ID after
// Kobo has constructed those tools and again before showing an "active" row.
// This is deliberately idempotent and never rewrites sidecar metadata.
static bool synchronizeSavedActiveLayer(
    LayerContext const& context,
    char const* reason,
    QString* error) {
    trace(QLatin1String("layers: active synchronization begin reason=")
        + QLatin1String(reason)
        + QLatin1String(" part=") + context.state.partId
        + QLatin1String(" id=") + context.state.activeId);
    if (!applyActiveLayer(context, context.state.activeId, error)) {
        trace(QLatin1String("layers: active synchronization failed reason=")
            + QLatin1String(reason)
            + QLatin1String(" part=") + context.state.partId
            + QLatin1String(" id=") + context.state.activeId
            + QLatin1String(" error=")
            + (error ? *error : QString()));
        return false;
    }
    trace(QLatin1String("layers: active synchronization complete reason=")
        + QLatin1String(reason)
        + QLatin1String(" part=") + context.state.partId
        + QLatin1String(" id=") + context.state.activeId);
    return true;
}

struct ImagePainterDeletingDeleter {
    void operator()(ImagePainterOpaque* painter) const {
        if (painter && imagePainterDeletingDestructor)
            imagePainterDeletingDestructor(painter);
    }
};

static bool copyLiveLayerPreviewImageLoader(
    LayerContext const& context,
    SharedImageLoader* loaderResult,
    QString* error) {
    if (!loaderResult || !context.widget || !context.widgetObject
            || context.widgetObject.data()
                != reinterpret_cast<QObject*>(context.widget)
            || !uirefEditorWidgetVtable) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook UI is no longer live.");
        }
        return false;
    }

    char* const widgetBytes = static_cast<char*>(context.widget);
    void* const editorWidgetGuard = *reinterpret_cast<void**>(
        widgetBytes + kNotePadEditorWidgetGuardOffset);
    // Match IInkNotePadWidget's own QPointer check before following +0x48:
    // a non-null ExternalRefCountData whose strongref word at +4 is non-zero.
    if (!editorWidgetGuard
            || *reinterpret_cast<volatile int const*>(
                static_cast<char*>(editorWidgetGuard) + 4) == 0) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook canvas is closing.");
        }
        return false;
    }

    void* const editorWidget = *reinterpret_cast<void**>(
        widgetBytes + kNotePadEditorWidgetObjectOffset);
    void* const expectedEditorWidgetVptr =
        static_cast<char*>(uirefEditorWidgetVtable) + 8;
    if (!editorWidget
            || *reinterpret_cast<void**>(editorWidget)
                != expectedEditorWidgetVptr) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook canvas type changed.");
        }
        return false;
    }

    char* const editorWidgetBytes = static_cast<char*>(editorWidget);
    void* const liveEditor = *reinterpret_cast<void**>(
        editorWidgetBytes + kEditorWidgetEditorObjectOffset);
    void* const liveEditorControl = *reinterpret_cast<void**>(
        editorWidgetBytes + kEditorWidgetEditorControlOffset);
    if (!liveEditorControl || liveEditor != context.editor
            || liveEditorControl != notePadEditorControl(context.widget)) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook canvas is out of sync.");
        }
        return false;
    }

    void* const loaderObject = *reinterpret_cast<void**>(
        editorWidgetBytes + kEditorWidgetImageLoaderObjectOffset);
    void* const loaderControl = *reinterpret_cast<void**>(
        editorWidgetBytes + kEditorWidgetImageLoaderControlOffset);
    if (!loaderObject || !loaderControl) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook image loader is missing.");
        }
        return false;
    }

    try {
        // Copy the real strong owner; never synthesize ImageLoader's private
        // layout and never retain only its raw object pointer.
        SharedImageLoader const& liveLoader =
            *reinterpret_cast<SharedImageLoader const*>(
                editorWidgetBytes + kEditorWidgetImageLoaderObjectOffset);
        SharedImageLoader const loader = liveLoader;
        if (!loader || loader.get() != loaderObject) {
            if (error) {
                *error = QLatin1String(
                    "Layer preview unavailable: notebook image loader changed.");
            }
            return false;
        }
        *loaderResult = loader;
    } catch (...) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: notebook image loader copy failed.");
        }
        return false;
    }

    trace("layers: preview live image loader acquired");
    return true;
}

static bool createLayerPreviewDrawer(
    LayerContext const& context,
    SharedImagePainter* painterResult,
    SharedBackendImageDrawer* drawerResult,
    QString* error) {
    if (!painterResult || !drawerResult || !editorGetEngine
            || !editorGetConfiguration || !imagePainterConstructor
            || !imagePainterDeletingDestructor
            || !imagePainterSetImageLoader
            || !backendImageDrawerMakeShared) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: stock image writer APIs are missing.");
        }
        return false;
    }

    SharedEngine const engine = editorGetEngine(context.editor);
    SharedConfiguration const configuration =
        editorGetConfiguration(context.editor);
    if (!engine || !configuration) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: editor services are missing.");
        }
        return false;
    }

    SharedImageLoader imageLoader;
    if (!copyLiveLayerPreviewImageLoader(context, &imageLoader, error))
        return false;

    void* painterMemory = nullptr;
    try {
        painterMemory = ::operator new(kImagePainterObjectBytes);
        memset(painterMemory, 0, kImagePainterObjectBytes);
        imagePainterConstructor(painterMemory);
    } catch (...) {
        if (painterMemory)
            ::operator delete(painterMemory);
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: image painter construction failed.");
        }
        return false;
    }

    SharedImagePainter painter;
    SharedBackendImageDrawer drawer;
    try {
        // IImagePainter is ImagePainter's primary base at +0. The custom
        // owner calls Kobo's deleting destructor exactly once; the drawer
        // copies this shared owner before export starts.
        painter.reset(
            reinterpret_cast<ImagePainterOpaque*>(painterMemory),
            ImagePainterDeletingDeleter());
        imagePainterSetImageLoader(painter.get(), imageLoader);
        SharedEngine engineArgument = engine;
        SharedConfiguration configurationArgument = configuration;
        backendImageDrawerMakeShared(
            &drawer,
            0,
            nullptr,
            &engineArgument,
            &painter,
            &configurationArgument);
    } catch (...) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: image drawer construction failed.");
        }
        return false;
    }
    if (!drawer) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: image drawer is missing.");
        }
        return false;
    }

    *painterResult = painter;
    *drawerResult = drawer;
    return true;
}

struct StockPreviewCallbackContext {
    alignas(4) unsigned char bytes[kStockPreviewContextBytes];

    StockPreviewCallbackContext(void* drawer, void* backend) {
        memset(bytes, 0, sizeof(bytes));
        memcpy(bytes + kStockPreviewDrawerOffset, &drawer, sizeof(drawer));
        memcpy(bytes + kStockPreviewBackendOffset, &backend, sizeof(backend));
    }
};

class LayerPreviewRendererListener {
public:
    LayerPreviewRendererListener(
        SharedImagePainter const& painter,
        SharedBackendImageDrawer const& drawer,
        SharedRenderer const& liveRenderer,
        void* backend,
        std::string const& layerId,
        bool* restrictionApplied,
        bool* writerInvoked)
        : painter_(painter),
          drawer_(drawer),
          liveRenderer_(liveRenderer),
          backend_(backend),
          layerId_(layerId),
          restrictionApplied_(restrictionApplied),
          writerInvoked_(writerInvoked) {}

    virtual ~LayerPreviewRendererListener() {}

    // RendererListener has one pure virtual callback after its two destructor
    // slots. Its hard-float call shape is recovered from exportToPNG at
    // 0x513392..0x5133d0. After restricting only the fresh export renderer,
    // call the exact anonymous writer callback used by EditorImpl::export_.
    // That firmware callback owns every private Page/Layout/Selection
    // temporary and invokes BackendImageDrawer::drawImage with stock ABI.
    virtual void exportImage(
        SharedRenderer const& renderer,
        void const* selection,
        ExtentOpaque extent,
        unsigned int flags,
        std::string const& path) {
        if (restrictionApplied_)
            *restrictionApplied_ = false;
        if (writerInvoked_)
            *writerInvoked_ = false;
        try {
            if (renderer && rendererRestrictToLayers) {
                std::vector<std::string> ids(1, layerId_);
                if (backgroundObjectLayerName
                        && !backgroundObjectLayerName->empty()
                        && *backgroundObjectLayerName != layerId_) {
                    ids.push_back(*backgroundObjectLayerName);
                }
                rendererRestrictToLayers(renderer.get(), ids);
                if (restrictionApplied_)
                    *restrictionApplied_ = true;
            }
        } catch (...) {
            trace("layers: preview renderer restriction failed");
        }

        if (!stockBackendImageDrawerExport || !drawer_ || !backend_)
            return;
        StockPreviewCallbackContext callback(drawer_.get(), backend_);
        if (writerInvoked_)
            *writerInvoked_ = true;
        try {
            stockBackendImageDrawerExport(
                callback.bytes, renderer, selection, extent, flags, path);
        } catch (...) {
            trace("layers: stock preview image writer threw");
        }
    }

private:
    SharedImagePainter painter_;
    SharedBackendImageDrawer drawer_;
    SharedRenderer liveRenderer_;
    void* backend_;
    std::string layerId_;
    bool* restrictionApplied_;
    bool* writerInvoked_;
};

static bool liveNeboControllerAndBackend(
    LayerContext const& context,
    SharedRenderer* rendererKeepAlive,
    void** pageController,
    void** backendResult,
    QString* error) {
    if (!rendererKeepAlive || !pageController || !backendResult
            || !editorGetRenderer || !rendererGetBackend || !neboBackendVtable) {
        if (error)
            *error = QLatin1String("Layer preview unavailable: renderer APIs are missing.");
        return false;
    }

    *rendererKeepAlive = editorGetRenderer(context.editor);
    void* const backend = *rendererKeepAlive
        ? rendererGetBackend(rendererKeepAlive->get()) : nullptr;
    void* const expectedVptr = static_cast<char*>(neboBackendVtable) + 8;
    if (!backend || *reinterpret_cast<void**>(backend) != expectedVptr) {
        if (error)
            *error = QLatin1String("Layer preview unavailable for this notebook backend.");
        return false;
    }

    *pageController = *reinterpret_cast<void**>(
        static_cast<char*>(backend) + kNeboBackendPageControllerOffset);
    if (!*pageController) {
        if (error)
            *error = QLatin1String("Layer preview unavailable: page controller is missing.");
        return false;
    }
    *backendResult = backend;
    return true;
}

static QString layerPreviewPath(LayerState const& state, QString const& id) {
    QByteArray key = state.notebookPath.toUtf8();
    key.append('\n');
    key.append(state.partId.toUtf8());
    key.append('\n');
    key.append(id.toUtf8());
    QByteArray const digest = QCryptographicHash::hash(
        key, QCryptographicHash::Sha256).toHex();
    return QDir(QLatin1String(kLayerPreviewRoot)).filePath(
        QString::fromLatin1(digest) + QLatin1String(".png"));
}

static bool layerPreviewNeedsRefresh(
    LayerState const& state,
    QString const& id) {
    QFileInfo const preview(layerPreviewPath(state, id));
    QFileInfo const notebook(state.notebookPath);
    return !preview.isFile()
        || preview.size() <= 0
        || preview.size() > kMaximumLayerPreviewBytes
        || (notebook.isFile() && preview.lastModified() < notebook.lastModified());
}

static bool layerPreviewCacheUsable(
    LayerState const& state,
    QString const& id) {
    QFileInfo const preview(layerPreviewPath(state, id));
    return preview.isFile()
        && preview.size() > 0
        && preview.size() <= kMaximumLayerPreviewBytes;
}

class LayerPreviewActiveGuard {
public:
    explicit LayerPreviewActiveGuard(QObject* widgetObject)
        : widgetObject_(widgetObject) {
        if (widgetObject_)
            widgetObject_->setProperty("_cnt_layer_preview_active", true);
    }

    ~LayerPreviewActiveGuard() {
        if (widgetObject_)
            widgetObject_->setProperty("_cnt_layer_preview_active", false);
    }

private:
    QPointer<QObject> widgetObject_;
};

static bool generateLayerPreview(
    LayerContext const& context,
    QString const& id,
    QString* error) {
    if (!layerPreviewApisReady || !rendererRestrictToLayers
            || !pageControllerExportToPng || !stockBackendImageDrawerExport) {
        if (error) {
            *error = QLatin1String(
                "Layer preview unavailable: firmware export APIs are not ready.");
        }
        return false;
    }
    if (!QDir().mkpath(QLatin1String(kLayerPreviewRoot))) {
        if (error)
            *error = QLatin1String("Layer preview unavailable: cache directory failed.");
        return false;
    }
    if (!context.widgetObject
            || context.widgetObject->property("_cnt_layer_preview_active").toBool()) {
        if (error)
            *error = QLatin1String("Layer preview deferred: renderer is busy.");
        return false;
    }
    LayerPreviewActiveGuard const previewActive(context.widgetObject);

    SharedRenderer rendererKeepAlive;
    void* pageController = nullptr;
    void* backend = nullptr;
    if (!liveNeboControllerAndBackend(
            context, &rendererKeepAlive, &pageController, &backend, error)) {
        return false;
    }
    SharedImagePainter painter;
    SharedBackendImageDrawer drawer;
    if (!createLayerPreviewDrawer(
            context, &painter, &drawer, error)) {
        return false;
    }

    QString const finalPath = layerPreviewPath(context.state, id);
    QString const uniqueSuffix = QString::number(getpid())
        + QLatin1String("-")
        + QUuid::createUuid().toString().remove('{').remove('}');
    QString const temporaryPath = finalPath
        + QLatin1String(".tmp-") + uniqueSuffix
        + QLatin1String(".png");
    QFile::remove(temporaryPath);
    bool restrictionApplied = false;
    bool writerInvoked = false;
    std::string const nativeId = id.toUtf8().constData();
    std::shared_ptr<LayerPreviewRendererListener> const proxy =
        std::make_shared<LayerPreviewRendererListener>(
            painter,
            drawer,
            rendererKeepAlive,
            backend,
            nativeId,
            &restrictionApplied,
            &writerInvoked);
    SharedRendererListener const opaqueProxy(
        proxy,
        reinterpret_cast<RendererListenerOpaque*>(proxy.get()));

    bool exported = false;
    trace(QLatin1String("layers: isolated preview export begin id=") + id);
    try {
        std::string const output = temporaryPath.toUtf8().constData();
        exported = pageControllerExportToPng(
            pageController, SharedBox(), output, opaqueProxy, 0);
    } catch (...) {
        exported = false;
    }

    QFileInfo const generated(temporaryPath);
    trace(QLatin1String("layers: isolated preview export result id=") + id
        + QLatin1String(" exported=")
        + (exported ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" restricted=")
        + (restrictionApplied ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" writer=")
        + (writerInvoked ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" file=")
        + (generated.isFile() ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" bytes=") + QString::number(generated.size()));
    if (!exported || !restrictionApplied || !writerInvoked
            || !generated.isFile()
            || generated.size() <= 0
            || generated.size() > kMaximumLayerPreviewBytes) {
        QFile::remove(temporaryPath);
        if (error) {
            *error = QLatin1String(
                "Layer preview failed: the isolated renderer produced no valid PNG.");
        }
        return false;
    }
    QImageReader validationReader(temporaryPath);
    QSize const decodedSize = validationReader.size();
    if (!decodedSize.isValid() || decodedSize.width() > 4096
            || decodedSize.height() > 4096) {
        QFile::remove(temporaryPath);
        if (error)
            *error = QLatin1String("Layer preview failed: exported image is invalid.");
        return false;
    }
    QImage const validation = validationReader.read();
    if (validation.isNull()) {
        QFile::remove(temporaryPath);
        if (error)
            *error = QLatin1String("Layer preview failed: exported image is unreadable.");
        return false;
    }

    // The menu never needs the 1024x1024 export after validation. Persist the
    // exact page-shaped payload it displays so future popup opens read only a
    // few kilobytes. Keep the full export as a safe fallback if Qt cannot
    // encode the derivative for any reason.
    QString const cardTemporaryPath = finalPath
        + QLatin1String(".card-") + uniqueSuffix + QLatin1String(".png");
    QFile::remove(cardTemporaryPath);
    QImage const card = validation.scaled(
        QSize(kLayerPreviewCardWidth - 6, kLayerPreviewCardHeight - 6),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    QString cacheCandidatePath = temporaryPath;
    bool cardSaved = !card.isNull()
        && card.save(cardTemporaryPath, "PNG");
    QFileInfo const cardFile(cardTemporaryPath);
    if (cardSaved && cardFile.isFile() && cardFile.size() > 0
            && cardFile.size() <= kMaximumLayerPreviewBytes) {
        cacheCandidatePath = cardTemporaryPath;
    } else {
        cardSaved = false;
        QFile::remove(cardTemporaryPath);
    }
    trace(QLatin1String("layers: preview card cache optimized id=") + id
        + QLatin1String(" optimized=")
        + (cardSaved ? QLatin1String("yes") : QLatin1String("no")));

    QString const previousPath = finalPath + QLatin1String(".previous");
    QFile::remove(previousPath);
    if (QFileInfo(finalPath).exists() && !QFile::rename(finalPath, previousPath)) {
        QFile::remove(temporaryPath);
        QFile::remove(cardTemporaryPath);
        if (error)
            *error = QLatin1String("Layer preview failed: old cache is busy.");
        return false;
    }
    if (!QFile::rename(cacheCandidatePath, finalPath)) {
        QFile::rename(previousPath, finalPath);
        QFile::remove(temporaryPath);
        QFile::remove(cardTemporaryPath);
        if (error)
            *error = QLatin1String("Layer preview failed: cache update was not atomic.");
        return false;
    }
    QFile::remove(temporaryPath);
    QFile::remove(cardTemporaryPath);
    QFile::remove(previousPath);
    // FAT timestamps are coarse and two tiny PNGs can occasionally have the
    // same byte count. Explicit eviction guarantees the just-written card is
    // decoded once instead of reusing an indistinguishable old memory entry.
    layerPreviewCardCache.remove(finalPath);
    return true;
}

static bool readLayerPreviewCard(
    LayerState const& state,
    QString const& id,
    QImage* result) {
    if (!result || !layerPreviewCacheUsable(state, id))
        return false;
    QString const path = layerPreviewPath(state, id);
    QFileInfo const cached(path);
    qint64 const modifiedMs = cached.lastModified().toMSecsSinceEpoch();
    QHash<QString, LayerPreviewCardCacheEntry>::iterator found =
        layerPreviewCardCache.find(path);
    if (found != layerPreviewCardCache.end()
            && found->modifiedMs == modifiedMs
            && found->size == cached.size()
            && !found->image.isNull()) {
        found->sequence = ++layerPreviewCardCacheSequence;
        *result = found->image;
        return true;
    }
    if (found != layerPreviewCardCache.end())
        layerPreviewCardCache.erase(found);

    QImageReader reader(path);
    QSize const decodedSize = reader.size();
    if (!decodedSize.isValid() || decodedSize.width() > 4096
            || decodedSize.height() > 4096) {
        return false;
    }
    QSize const target = decodedSize.scaled(
        QSize(kLayerPreviewCardWidth - 6, kLayerPreviewCardHeight - 6),
        Qt::KeepAspectRatio);
    if (target.isValid() && target != decodedSize)
        reader.setScaledSize(target);
    QImage image = reader.read();
    if (image.isNull())
        return false;
    if (image.width() > kLayerPreviewCardWidth - 6
            || image.height() > kLayerPreviewCardHeight - 6) {
        image = image.scaled(
            QSize(kLayerPreviewCardWidth - 6, kLayerPreviewCardHeight - 6),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }
    if (image.isNull())
        return false;

    if (layerPreviewCardCache.size() >= kMaximumLayerPreviewCardCacheEntries) {
        QHash<QString, LayerPreviewCardCacheEntry>::iterator oldest =
            layerPreviewCardCache.begin();
        for (QHash<QString, LayerPreviewCardCacheEntry>::iterator it =
                 layerPreviewCardCache.begin();
                it != layerPreviewCardCache.end(); ++it) {
            if (it->sequence < oldest->sequence)
                oldest = it;
        }
        if (oldest != layerPreviewCardCache.end())
            layerPreviewCardCache.erase(oldest);
    }
    LayerPreviewCardCacheEntry entry;
    entry.image = image;
    entry.modifiedMs = modifiedMs;
    entry.size = cached.size();
    entry.sequence = ++layerPreviewCardCacheSequence;
    layerPreviewCardCache.insert(path, entry);
    *result = image;
    return true;
}

static QPixmap layerPreview(
    LayerState const& state,
    QString const& id,
    QString const& name,
    bool active,
    bool* cacheDrawnResult = nullptr) {
    QPixmap preview(kLayerPreviewCardWidth, kLayerPreviewCardHeight);
    preview.fill(Qt::white);
    QPainter painter(&preview);
    bool cacheDrawn = false;
    // A stale but valid cache is still a better first frame than a text
    // placeholder. The popup queues its replacement only after it is visible.
    QImage image;
    if (readLayerPreviewCard(state, id, &image)) {
        QPoint const origin(
            (preview.width() - image.width()) / 2,
            (preview.height() - image.height()) / 2);
        painter.drawImage(origin, image);
        cacheDrawn = true;
    }
    painter.setPen(QPen(active ? Qt::black : QColor(110, 110, 110), active ? 3 : 1));
    painter.drawRect(2, 2, preview.width() - 5, preview.height() - 5);
    if (!cacheDrawn) {
        painter.setPen(Qt::black);
        QFont font = painter.font();
        font.setBold(active);
        font.setPointSize(11);
        painter.setFont(font);
        painter.drawText(preview.rect().adjusted(6, 6, -6, -6),
            Qt::AlignCenter | Qt::TextWordWrap, name);
    }
    if (cacheDrawnResult)
        *cacheDrawnResult = cacheDrawn;
    return preview;
}

static void showLayerError(void* widget, QString const& error) {
    trace("layers: operation failed safely");
    if (showErrorPopup)
        showErrorPopup(widget, error);
}

static bool addNotebookLayer(LayerContext* context, QString* error) {
    if (!context || context->state.customLayers.size() + 1 >= kMaximumNotebookLayers) {
        if (error)
            *error = QLatin1String("Layer not added: this notebook reached the layer limit.");
        return false;
    }

    widgetSave(context->widget);
    QString backupPath;
    if (!backupNotebookPath(
            context->state.notebookPath,
            QLatin1String(kLayerBackupRoot),
            QLatin1String("Add layer"),
            &backupPath,
            error)) {
        return false;
    }

    LayoutStorage layout;
    if (!layoutForPart(context->part, &layout, error))
        return false;
    LayerRecord record;
    record.id = QLatin1String("cnt.layer.")
        + QUuid::createUuid().toString().remove('{').remove('}');
    record.name = QLatin1String("Layer ")
        + QString::number(context->state.customLayers.size() + 2);
    std::string const nativeId = record.id.toUtf8().constData();
    if (!layoutAppendLayer(layout.bytes, nativeId)) {
        if (error)
            *error = QLatin1String("Layer not added: document rejected the new layer.");
        return false;
    }
    bool addedExists = false;
    if (!nativeLayerExists(layout, record.id, &addedExists, error)
            || !addedExists) {
        try {
            layoutRemoveLayer(layout.bytes, nativeId);
        } catch (...) {
            trace("layers: failed add could not be rolled back in memory");
        }
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layer not added: the native layer could not be verified. "
                "The notebook backup was retained.");
        }
        return false;
    }

    LayerState const previous = context->state;
    context->state.customLayers.append(record);
    context->state.activeId = record.id;
    if (!applyActiveLayer(*context, record.id, error)
            || !saveLayerState(context->state, error)) {
        layoutRemoveLayer(layout.bytes, nativeId);
        context->state = previous;
        saveLayerState(previous, nullptr);
        applyActiveLayer(*context, previous.activeId, nullptr);
        return false;
    }

    trace("layers: package save begin after native add");
    packageSave(context->package.get());
    trace("layers: package save complete after native add");
    widgetSave(context->widget);
    trace("layers: widget save complete after native add");
    traceSerializedLayerProbe(context->state, record.id, "after-add-save");
    widgetRefresh(context->widget);
    trace("layers: native layer added and selected");
    return true;
}

static bool activateNotebookLayer(
    LayerContext* context,
    QString const& id,
    QString* error) {
    if (!context)
        return false;
    bool known = id == nativeDocumentLayerId();
    for (int i = 0; !known && i < context->state.customLayers.size(); ++i)
        known = context->state.customLayers.at(i).id == id;
    if (!known) {
        if (error)
            *error = QLatin1String("Layer not selected: layer metadata is stale.");
        return false;
    }

    QString const previous = context->state.activeId;
    if (!applyActiveLayer(*context, id, error))
        return false;
    context->state.activeId = id;
    if (!saveLayerState(context->state, error)) {
        context->state.activeId = previous;
        applyActiveLayer(*context, previous, nullptr);
        return false;
    }
    // Keep the live renderer unrestricted. Firmware uses restrictToLayers()
    // only on isolated export/thumbnail renderers; mutating the live
    // RenderPad vector from this menu callback races its render lifecycle.
    // The active ToolDispatcher still routes all future strokes natively.
    widgetRefresh(context->widget);
    trace("layers: active layer changed");
    return true;
}

static bool deleteActiveNotebookLayer(LayerContext* context, QString* error) {
    if (!context || context->state.activeId == nativeDocumentLayerId()) {
        if (error)
            *error = QLatin1String("Layer not deleted: the base layer is protected.");
        return false;
    }

    int index = -1;
    for (int i = 0; i < context->state.customLayers.size(); ++i) {
        if (context->state.customLayers.at(i).id == context->state.activeId) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (error)
            *error = QLatin1String("Layer not deleted: layer metadata is stale.");
        return false;
    }

    widgetSave(context->widget);
    QString backupPath;
    if (!backupNotebookPath(
            context->state.notebookPath,
            QLatin1String(kLayerBackupRoot),
            QLatin1String("Delete layer"),
            &backupPath,
            error)) {
        return false;
    }

    LayoutStorage layout;
    if (!layoutForPart(context->part, &layout, error))
        return false;
    QString const deletedId = context->state.activeId;
    std::string const nativeId = deletedId.toUtf8().constData();
    QString const previewPath = layerPreviewPath(
        context->state, context->state.activeId);
    layoutRemoveLayer(layout.bytes, nativeId);
    bool stillExists = true;
    if (!nativeLayerExists(
            layout, context->state.activeId, &stillExists, error)) {
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layer removal could not be verified. The notebook backup "
                "was retained.");
        }
        return false;
    }
    if (stillExists) {
        if (error) {
            *error = QLatin1String(
                "Layer not deleted: the native document kept the layer. "
                "The notebook backup was retained.");
        }
        return false;
    }
    context->state.customLayers.remove(index);
    context->state.activeId = nativeDocumentLayerId();
    if (!applyActiveLayer(*context, context->state.activeId, error)
            || !saveLayerState(context->state, error)) {
        if (error && error->isEmpty()) {
            *error = QLatin1String(
                "Layer was removed, but its UI state could not be finalized. "
                "A notebook backup is available.");
        }
        return false;
    }
    trace("layers: package save begin after native delete");
    packageSave(context->package.get());
    trace("layers: package save complete after native delete");
    widgetSave(context->widget);
    trace("layers: widget save complete after native delete");
    traceSerializedLayerProbe(context->state, deletedId, "after-delete-save");
    QFile::remove(previewPath);
    layerPreviewCardCache.remove(previewPath);
    widgetRefresh(context->widget);
    trace("layers: active native layer deleted");
    return true;
}

static bool refreshLayerPreviews(LayerContext* context, QString* error) {
    if (!context)
        return false;
    widgetSave(context->widget);

    QVector<QString> ids;
    ids.append(nativeDocumentLayerId());
    for (int i = 0; i < context->state.customLayers.size(); ++i)
        ids.append(context->state.customLayers.at(i).id);

    int generated = 0;
    QString firstError;
    for (int i = 0; i < ids.size(); ++i) {
        QString itemError;
        if (generateLayerPreview(*context, ids.at(i), &itemError))
            ++generated;
        else if (firstError.isEmpty())
            firstError = itemError;
    }
    if (generated != ids.size()) {
        if (error) {
            *error = QLatin1String("Generated ") + QString::number(generated)
                + QLatin1String(" of ") + QString::number(ids.size())
                + QLatin1String(" layer previews. ") + firstError;
        }
        trace("layers: isolated preview refresh incomplete");
        return false;
    }
    trace("layers: isolated preview cache refreshed");
    return true;
}

static QString layerPopupRowLabel(
    QString const& name,
    bool active,
    bool previewPending) {
    QString label = name;
    if (active)
        label += QLatin1String("  (active)");
    if (previewPending)
        label += QLatin1String("  (preview pending)");
    return label;
}

struct DeferredLayerPreviewRow {
    QString id;
    QString name;
    bool active;
    QPointer<QLabel> previewLabel;
    QPointer<QLabel> textLabel;
};

struct DeferredLayerPreviewRefresh {
    QPointer<QObject> controller;
    LayerState expectedState;
    QVector<DeferredLayerPreviewRow> rows;
    QPointer<QTimer> timer;
    int next;
    int attempted;
    int generated;
    QElapsedTimer elapsed;

    DeferredLayerPreviewRefresh()
        : next(0), attempted(0), generated(0) {}
};

// MyScript renderers are GUI-thread-affine, so this deliberately does not use
// a worker. The native popup is constructed from cached images first; a
// parented timer then exports one stale row per turn of QMenu's nested event
// loop. Closing the popup destroys the timer and cancels all remaining work.
static void continueDeferredLayerPreviewRefresh(
    std::shared_ptr<DeferredLayerPreviewRefresh> const& state) {
    if (!state || !state->timer || !state->controller)
        return;
    if (state->next >= state->rows.size()
            || state->attempted >= kLayerPreviewDeferredBudget) {
        trace(QLatin1String("layers: deferred popup preview refresh complete attempted=")
            + QString::number(state->attempted)
            + QLatin1String(" generated=") + QString::number(state->generated)
            + QLatin1String(" remaining=")
            + QString::number(state->rows.size() - state->next)
            + QLatin1String(" ms=") + QString::number(state->elapsed.elapsed()));
        return;
    }

    DeferredLayerPreviewRow const row = state->rows.at(state->next++);
    ++state->attempted;
    QElapsedTimer itemTimer;
    itemTimer.start();

    // Do not retain raw Editor/PageController pointers across event-loop
    // turns. Reload the live notebook context and prove that it is still the
    // exact page for which this work was queued before touching MyScript.
    LayerContext context;
    QString contextError;
    if (!loadLayerContext(state->controller, &context, &contextError)
            || context.state.notebookPath != state->expectedState.notebookPath
            || context.state.partId != state->expectedState.partId) {
        trace(QLatin1String("layers: deferred popup preview cancelled context=")
            + contextError);
        return;
    }
    if (context.widgetObject->property("_cnt_layer_operation_active").toBool()) {
        --state->next;
        --state->attempted;
        state->timer->start(kLayerPreviewDeferredNextMs);
        return;
    }
    bool knownLayer = row.id == nativeDocumentLayerId();
    for (int i = 0; !knownLayer && i < context.state.customLayers.size(); ++i)
        knownLayer = context.state.customLayers.at(i).id == row.id;
    if (!knownLayer) {
        trace(QLatin1String("layers: deferred popup preview cancelled missing id=")
            + row.id);
        return;
    }

    QString itemError;
    bool const wasStale = layerPreviewNeedsRefresh(context.state, row.id);
    bool refreshed = !wasStale;
    if (wasStale)
        refreshed = generateLayerPreview(context, row.id, &itemError);
    if (refreshed) {
        if (wasStale)
            ++state->generated;
        if (row.previewLabel) {
            row.previewLabel->setPixmap(layerPreview(
                context.state, row.id, row.name, row.active));
        }
        if (row.textLabel)
            row.textLabel->setText(layerPopupRowLabel(row.name, row.active, false));
    } else {
        trace(QLatin1String("layers: deferred popup preview retained cache id=")
            + row.id + QLatin1String(" error=") + itemError);
    }
    trace(QLatin1String("layers: deferred popup preview row id=") + row.id
        + QLatin1String(" generated=")
        + (refreshed ? QLatin1String("yes") : QLatin1String("no"))
        + QLatin1String(" ms=") + QString::number(itemTimer.elapsed()));

    if (state->next < state->rows.size()
            && state->attempted < kLayerPreviewDeferredBudget) {
        state->timer->start(kLayerPreviewDeferredNextMs);
    } else {
        trace(QLatin1String("layers: deferred popup preview refresh complete attempted=")
            + QString::number(state->attempted)
            + QLatin1String(" generated=") + QString::number(state->generated)
            + QLatin1String(" remaining=")
            + QString::number(state->rows.size() - state->next)
            + QLatin1String(" ms=") + QString::number(state->elapsed.elapsed()));
    }
}

static void startDeferredLayerPreviewRefresh(
    QObject* controller,
    LayerContext const& context,
    QMenu* popup,
    QVector<DeferredLayerPreviewRow> const& rows) {
    if (!popup || rows.isEmpty() || !layerPreviewApisReady) {
        trace(QLatin1String("layers: deferred popup preview refresh queued=0"));
        return;
    }

    std::shared_ptr<DeferredLayerPreviewRefresh> const state(
        new DeferredLayerPreviewRefresh);
    state->controller = controller;
    state->expectedState = context.state;
    state->rows = rows;
    state->elapsed.start();
    QTimer* const timer = new QTimer(popup);
    timer->setSingleShot(true);
    state->timer = timer;
    QObject::connect(timer, &QTimer::timeout, [state]() {
        continueDeferredLayerPreviewRefresh(state);
    });
    timer->start(kLayerPreviewDeferredStartMs);
    trace(QLatin1String("layers: deferred popup preview refresh queued=")
        + QString::number(rows.size()));
}

class LayerOperationActiveGuard {
public:
    explicit LayerOperationActiveGuard(QObject* widgetObject)
        : widgetObject_(widgetObject) {
        if (widgetObject_)
            widgetObject_->setProperty("_cnt_layer_operation_active", true);
    }

    ~LayerOperationActiveGuard() {
        if (widgetObject_)
            widgetObject_->setProperty("_cnt_layer_operation_active", false);
    }

private:
    QPointer<QObject> widgetObject_;
};

static void runLayerOperation(
    QObject* controller,
    LayerOperation operation,
    QString const& id = QString()) {
    if (!controller)
        return;
    QObject* const widgetObject = findNotebookWidget(controller);
    if (!widgetObject
            || widgetObject->property("_cnt_layer_operation_active").toBool()) {
        return;
    }
    LayerOperationActiveGuard const activeGuard(widgetObject);

    char const* operationName = "activate";
    if (operation == AddLayerOperation)
        operationName = "add";
    else if (operation == DeleteActiveLayerOperation)
        operationName = "delete";
    else if (operation == RefreshLayerPreviewsOperation)
        operationName = "refresh-previews";
    trace(QLatin1String("layers: operation begin type=")
        + QLatin1String(operationName));

    LayerContext context;
    QString error;
    if (!loadLayerContext(controller, &context, &error)) {
        QObject* const widgetObject = findNotebookWidget(controller);
        showLayerError(widgetObject, error);
        return;
    }

    if (operation == DeleteActiveLayerOperation) {
        QWidget* const parent = qobject_cast<QWidget*>(context.widgetObject.data());
        if (QMessageBox::question(
                parent,
                QLatin1String("Delete layer"),
                QLatin1String(
                    "Delete the active layer and all ink on it? A notebook "
                    "backup will be created first."),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes) {
            return;
        }
    }

    bool succeeded = false;
    try {
        if (operation == AddLayerOperation)
            succeeded = addNotebookLayer(&context, &error);
        else if (operation == DeleteActiveLayerOperation)
            succeeded = deleteActiveNotebookLayer(&context, &error);
        else if (operation == RefreshLayerPreviewsOperation)
            succeeded = refreshLayerPreviews(&context, &error);
        else
            succeeded = activateNotebookLayer(&context, id, &error);
    } catch (std::exception const&) {
        error = QLatin1String("Layer operation failed: notebook engine rejected it.");
    } catch (...) {
        error = QLatin1String("Layer operation failed: unexpected notebook error.");
    }
    if (!succeeded)
        showLayerError(context.widget, error);
}

static QMenu* createNativeLayerPopup(QWidget* anchor, QString* error) {
    if (!anchor || !nickelTouchMenuConstructor || !nickelTouchMenuSetAlignment
            || !touchMenuSetCustomPopupPositionOffset) {
        if (error)
            *error = QLatin1String("Layers unavailable: native menu API is missing.");
        return nullptr;
    }

    void* storage = nullptr;
    bool constructed = false;
    try {
        storage = ::operator new(kNickelTouchMenuSize);
        // Exact constructor arguments recovered from
        // IInkToolMenuController::loadView on firmware 4.38.23697.
        // Stock loadView parents the menu to popupFromWidget(), which is the
        // toolbar button used to open the controller. That relationship is
        // also consumed by NickelTouchMenu's decoration/placement code.
        nickelTouchMenuConstructor(storage, anchor, 0);
        constructed = true;
        nickelTouchMenuSetAlignment(
            storage, Qt::AlignHCenter | Qt::AlignBottom);
        QPoint const offset(0, 14);
        touchMenuSetCustomPopupPositionOffset(storage, offset);
        QMenu* const popup = reinterpret_cast<QMenu*>(storage);
        popup->setAttribute(static_cast<Qt::WidgetAttribute>(55), true);
        return popup;
    } catch (...) {
        if (constructed)
            delete reinterpret_cast<QMenu*>(storage);
        else if (storage)
            ::operator delete(storage);
        if (error)
            *error = QLatin1String("Layers unavailable: native menu creation failed.");
        return nullptr;
    }
}

static QWidget* createNativeLayerToolRow(
    void* controller,
    QMenu* popup,
    QString const& text,
    QPixmap const& preview,
    bool active,
    QString* error) {
    if (!controller || !popup || !iInkToolMenuWidgetConstructor
            || !iInkToolMenuWidgetSetSelected
            || !abstractMenuControllerGrabTapGesture) {
        if (error)
            *error = QLatin1String("Layers unavailable: native row API is missing.");
        return nullptr;
    }

    void* storage = nullptr;
    bool constructed = false;
    try {
        storage = ::operator new(kIInkToolMenuWidgetSize);
        iInkToolMenuWidgetConstructor(storage, popup);
        constructed = true;

        QWidget* const row = reinterpret_cast<QWidget*>(storage);
        QLabel* const button = row->findChild<QLabel*>(
            QLatin1String("toolButton"));
        QLabel* const label = row->findChild<QLabel*>(
            QLatin1String("toolText"));
        if (!button || !label) {
            delete row;
            if (error) {
                *error = QLatin1String(
                    "Layers unavailable: native row children changed.");
            }
            return nullptr;
        }

        // The firmware setter only records the selected flag in ToolButton.
        // Do not call setTool(): its enum switch is exclusively for Kobo's
        // pen/eraser icons and labels. Our active thumbnail already carries
        // the visible dark border, while the native row supplies its exact
        // typography and reversible press/release treatment.
        iInkToolMenuWidgetSetSelected(storage, active);
        button->setAlignment(Qt::AlignCenter);
        button->setPixmap(preview);
        label->setText(text);

        // IInkToolMenuWidget inherits GestureReceiver through its secondary
        // base at +0x44 on this exact firmware. Stock loadView registers that
        // adjusted pointer, not the QWidget address, before creating the
        // QAction. The ABI verifier pins both the adjustment and call site.
        abstractMenuControllerGrabTapGesture(
            controller,
            reinterpret_cast<char*>(storage)
                + kIInkToolMenuWidgetGestureReceiverOffset);
        return row;
    } catch (...) {
        if (constructed)
            delete reinterpret_cast<QWidget*>(storage);
        else if (storage)
            ::operator delete(storage);
        if (error)
            *error = QLatin1String("Layers unavailable: native row creation failed.");
        return nullptr;
    }
}

// AbstractMenuController::tapGesture() does not resolve an action from the
// GestureReceiver which emitted the tap. On the pinned firmware it instead
// reads AbstractController's QPointer<QWidget> view at controller + 0x10,
// maps the global tap into that menu, and calls QMenu::actionAt(). A menu made
// with the stock helpers therefore also has to be the controller's active
// view while it is open; otherwise the tap is dispatched to a row in the
// (possibly reloaded) notebook tool menu underneath it.
//
// The offset and two-word QPointer layout are firmware-private ABI. Both were
// recovered from IInkToolMenuController::loadView() and
// AbstractMenuController::tapGesture() for 4.38.23697 and are protected by the
// same exact-library ABI gate as the native menu symbols.
class ControllerMenuViewGuard {
public:
    ControllerMenuViewGuard(QObject* controller, QMenu* menu)
        : controller_(controller), original_(), view_(nullptr), active_(false) {
        static_assert(sizeof(QPointer<QObject>) == sizeof(void*) * 2,
            "unexpected Qt 5 QPointer ABI");
        if (!controller_ || !menu)
            return;
        view_ = reinterpret_cast<QPointer<QObject>*>(
            reinterpret_cast<char*>(controller) + 0x10);
        original_ = *view_;
        *view_ = menu;
        active_ = true;
    }

    ~ControllerMenuViewGuard() {
        if (active_ && controller_ && view_)
            *view_ = original_;
    }

private:
    QPointer<QObject> controller_;
    QPointer<QObject> original_;
    QPointer<QObject>* view_;
    bool active_;
};

static void showLayerPopup(QObject* controller) {
    QElapsedTimer popupOpenTimer;
    popupOpenTimer.start();
    LayerContext context;
    QString error;
    if (!loadLayerContext(controller, &context, &error)) {
        QObject* const widgetObject = findNotebookWidget(controller);
        showLayerError(widgetObject, error);
        return;
    }
    // A reopened notebook can restore the sidecar before any tool-theme
    // callback occurs. Synchronize first so the row marked "active" always
    // matches the concrete pen/eraser restriction used by the next stroke.
    if (!synchronizeSavedActiveLayer(context, "popup-open", &error)) {
        showLayerError(context.widget, error);
        return;
    }
    QWidget* const parent = qobject_cast<QWidget*>(context.widgetObject.data());
    if (!parent || !createIInkMenuItem || !addWidgetActionOriginal
            || !abstractNickelMenuControllerPopupFromWidget
            || !nickelTouchMenuPopupPosition) {
        showLayerError(
            context.widget,
            QLatin1String("Layers unavailable: native menu actions are missing."));
        return;
    }

    // IInkMenuController is opened from the notebook toolbar's overflow
    // button. Recover that exact stock anchor instead of guessing a screen
    // position from the notebook rectangle.
    QWidget* const anchor = abstractNickelMenuControllerPopupFromWidget(
        controller);
    if (!anchor) {
        showLayerError(
            context.widget,
            QLatin1String("Layers unavailable: toolbar anchor was not found."));
        return;
    }

    QMenu* const popup = createNativeLayerPopup(anchor, &error);
    if (!popup) {
        showLayerError(context.widget, error);
        return;
    }

    bool menuComplete = true;
    QVector<DeferredLayerPreviewRow> deferredPreviewRows;
    auto addLayerRow = [&](QString const& id,
                           QString const& name,
                           bool active,
                           bool separatorAfter) {
        bool cacheDrawn = false;
        QPixmap const preview = layerPreview(
            context.state, id, name, active, &cacheDrawn);
        bool const previewPending = !cacheDrawn
            || layerPreviewNeedsRefresh(context.state, id);
        QString const label = layerPopupRowLabel(name, active, previewPending);
        LayerMenuReceiver* const receiver = new LayerMenuReceiver(
            controller, id, popup);
        QObject::connect(
            receiver,
            &LayerMenuReceiver::activateRequested,
            [](QObject* target, QString const& layerId) {
                runLayerOperation(target, ActivateLayerOperation, layerId);
            });
        QWidget* const item = createNativeLayerToolRow(
            controller,
            popup,
            label,
            preview,
            active,
            &error);
        if (!item) {
            receiver->deleteLater();
            menuComplete = false;
            return;
        }
        if (previewPending) {
            DeferredLayerPreviewRow row;
            row.id = id;
            row.name = name;
            row.active = active;
            row.previewLabel = item->findChild<QLabel*>(
                QLatin1String("toolButton"));
            row.textLabel = item->findChild<QLabel*>(
                QLatin1String("toolText"));
            // Refresh the selected layer first. Other rows are deliberately
            // carried over to later opens by the one-export safety budget.
            if (active)
                deferredPreviewRows.prepend(row);
            else
                deferredPreviewRows.append(row);
        }
        addWidgetActionOriginal(
            controller,
            popup,
            item,
            receiver,
            SLOT(activateLayer()),
            true,
            true,
            separatorAfter);
    };

    for (int i = context.state.customLayers.size() - 1; i >= 0; --i) {
        LayerRecord const record = context.state.customLayers.at(i);
        bool const active = record.id == context.state.activeId;
        addLayerRow(record.id, record.name, active, false);
    }

    QString const baseId = nativeDocumentLayerId();
    bool const baseActive = context.state.activeId == baseId;
    addLayerRow(baseId, QLatin1String("Layer 1"), baseActive, true);

    LayerMenuReceiver* const commands = new LayerMenuReceiver(
        controller, QString(), popup);
    QObject::connect(commands, &LayerMenuReceiver::addRequested,
        [](QObject* target) { runLayerOperation(target, AddLayerOperation); });
    QObject::connect(commands, &LayerMenuReceiver::deleteRequested,
        [](QObject* target) {
            runLayerOperation(target, DeleteActiveLayerOperation);
        });
    QObject::connect(commands, &LayerMenuReceiver::refreshRequested,
        [](QObject* target) {
            runLayerOperation(target, RefreshLayerPreviewsOperation);
        });

    QPixmap noIcon;
    QWidget* const add = createIInkMenuItem(
        controller, popup, QLatin1String("Add layer"), noIcon, false);
    QWidget* const remove = createIInkMenuItem(
        controller, popup, QLatin1String("Delete active layer"), noIcon, false);
    QWidget* const refresh = createIInkMenuItem(
        controller,
        popup,
        QLatin1String("Refresh layer previews"),
        noIcon,
        false);
    if (!add || !remove || !refresh) {
        menuComplete = false;
    } else {
        addWidgetActionOriginal(
            controller,
            popup,
            add,
            commands,
            SLOT(addLayer()),
            true,
            context.state.customLayers.size() + 1 < kMaximumNotebookLayers,
            false);
        addWidgetActionOriginal(
            controller,
            popup,
            remove,
            commands,
            SLOT(deleteLayer()),
            true,
            !baseActive,
            false);
        addWidgetActionOriginal(
            controller,
            popup,
            refresh,
            commands,
            SLOT(refreshPreviews()),
            true,
            layerPreviewApisReady,
            true);
    }

    if (!menuComplete) {
        delete popup;
        showLayerError(
            context.widget,
            QLatin1String("Layers unavailable: native menu row creation failed."));
        return;
    }

    trace(QLatin1String("layers: native popup ready ms=")
        + QString::number(popupOpenTimer.elapsed())
        + QLatin1String(" deferred=")
        + QString::number(deferredPreviewRows.size()));
    trace("layers: native tool-style NickelTouchMenu popup opened");
    popup->ensurePolished();
    QPoint const position = nickelTouchMenuPopupPosition(popup, anchor);
    trace("layers: native popup anchored to notebook toolbar");
    ControllerMenuViewGuard const activeMenu(controller, popup);
    // Stock uses QMenu::popup(position). Keep exec(position) here because the
    // injected rows still delegate taps to the existing overflow controller:
    // the nested loop keeps that controller alive until this menu closes.
    // Geometry and the triangular decoration are nevertheless computed by
    // the same NickelTouchMenu::popupPosition(anchor) path as pen/eraser.
    // WA_DeleteOnClose (55), matching the stock popup, owns teardown.
    startDeferredLayerPreviewRefresh(
        controller, context, popup, deferredPreviewRows);
    popup->exec(position);
}

static void showPageOperationError(void* widget, QString const& error) {
    trace("pages: operation failed safely");
    if (showErrorPopup)
        showErrorPopup(widget, error);
}

enum PageOperation {
    DuplicatePageOperation,
    MovePageEarlierOperation,
    MovePageLaterOperation
};

class PageOperationActiveGuard {
public:
    explicit PageOperationActiveGuard(QObject* widgetObject)
        : widgetObject_(widgetObject) {
        widgetObject_->setProperty("_cnt_page_operation_active", true);
    }

    ~PageOperationActiveGuard() {
        if (widgetObject_)
            widgetObject_->setProperty("_cnt_page_operation_active", false);
    }

private:
    QPointer<QObject> widgetObject_;
};

static void runPageOperation(QObject* controller, PageOperation operation) {
    if (!controller)
        return;
    QObject* const widgetObject = findNotebookWidget(controller);
    if (!widgetObject)
        return;
    if (widgetObject->property("_cnt_page_operation_active").toBool())
        return;

    PageOperationActiveGuard const activeGuard(widgetObject);
    void* const widget = widgetObject;
    if (kEnableCrossNotebookMove) {
        // A native N3Dialog-based destination picker is required before this
        // can be enabled; plugin-created top-level picker windows are unsafe.
        QVector<NotebookDestination> (*const listDestinations)(QString const&) =
            &notebookDestinations;
        bool (*const movePage)(void*, QString const&, QString*) =
            &moveCurrentPageToNotebook;
        (void)listDestinations;
        (void)movePage;
    }

    QString error;
    try {
        bool succeeded = false;
        if (operation == DuplicatePageOperation) {
            succeeded = duplicateCurrentPage(widget, &error);
        } else {
            widgetSave(widget);
            PageContext context;
            if (!loadPageContext(widget, &context, &error)) {
                showPageOperationError(widget, error);
                return;
            }

            int const target = context.index
                + (operation == MovePageEarlierOperation ? -1 : 1);
            if (target < 0 || target >= context.count) {
                if (showErrorPopup) {
                    showErrorPopup(
                        widget,
                        operation == MovePageEarlierOperation
                            ? QLatin1String("This is already the first page.")
                            : QLatin1String("This is already the last page."));
                }
                return;
            }
            succeeded = reorderCurrentPage(widget, target, &error);
        }

        if (!succeeded)
            showPageOperationError(widget, error);
    } catch (std::exception const&) {
        showPageOperationError(
            widget,
            QLatin1String("Page operation failed: the notebook engine rejected it."));
    } catch (...) {
        showPageOperationError(
            widget,
            QLatin1String("Page operation failed: unexpected notebook error."));
    }
}

struct CoverPickerState {
    QPointer<QObject> widgetObject;
    void* widget;
    void* editor;
    SharedPart originalPart;
    QString originalFirstBackground;
    QPointer<QTimer> timer;
    int polls;

    CoverPickerState()
        : widget(nullptr), editor(nullptr), polls(0) {}
};

static void beginCoverPicker(QObject* controller) {
    QObject* const widgetObject = findNotebookWidget(controller);
    if (!widgetObject) {
        trace("covers: notebook widget not found from menu controller");
        return;
    }
    if (widgetObject->property("_cnt_cover_picker_active").toBool())
        return;

    void* const widget = widgetObject;
    void* const editor = notePadEditor(widget);
    void* const editorControl = notePadEditorControl(widget);
    void* const backgroundWidget = notePadBackgroundWidget(widget);
    if (!editor || !editorControl || !backgroundWidget) {
        trace("covers: notebook editor not ready for picker");
        return;
    }

    SharedPart const originalPart = editorGetPart(editor);
    if (!originalPart) {
        trace("covers: current page missing before picker");
        return;
    }
    SharedPackage const package = partGetPackage(originalPart.get());
    if (!package || packagePartCount(package.get()) < 1) {
        trace("covers: notebook package missing before picker");
        return;
    }
    SharedPart const firstPart = packageGetPart(package.get(), 0);
    if (!firstPart) {
        trace("covers: first page missing before picker");
        return;
    }

    editorSetPart(editor, firstPart);
    std::shared_ptr<CoverPickerState> const state(new CoverPickerState);
    state->widgetObject = widgetObject;
    state->widget = widget;
    state->editor = editor;
    state->originalPart = originalPart;
    state->originalFirstBackground = backgroundType(backgroundWidget);

    QTimer* const timer = new QTimer(widgetObject);
    state->timer = timer;
    timer->setInterval(100);
    widgetObject->setProperty("_cnt_cover_picker_active", true);
    QObject::connect(timer, &QTimer::timeout, [state]() {
        if (!state->widgetObject || !state->timer)
            return;

        QObject* const object = state->widgetObject.data();
        void* const backgroundWidget = notePadBackgroundWidget(state->widget);
        QString const selected = backgroundWidget
            ? backgroundType(backgroundWidget) : QString();

        if (selected.startsWith(QLatin1String("Custom_Cover_"))
                && selected != state->originalFirstBackground) {
            state->timer->stop();
            object->setProperty("_cnt_cover_picker_active", false);
            coverTitlePending = false;

            // Nickel has applied the picker result to page zero. Restore that
            // page first, then let the backed-up insertion routine decide
            // whether to update an existing cover or create a new one.
            setBackgroundTypeOriginal(
                state->widget, state->originalFirstBackground);
            widgetSave(state->widget);

            QString error;
            if (!applyNotebookCover(state->widget, selected, &error)) {
                editorSetPart(state->editor, state->originalPart);
                widgetRefresh(state->widget);
                trace("covers: cover operation failed safely");
                if (showErrorPopup)
                    showErrorPopup(state->widget, error);
            }
            state->timer->deleteLater();
            return;
        }

        // Treat a closed picker as a harmless cancel. The long timeout avoids
        // relying on private picker-controller lifetime or dialog signals.
        if (++state->polls >= 300) {
            state->timer->stop();
            object->setProperty("_cnt_cover_picker_active", false);
            coverTitlePending = false;
            editorSetPart(state->editor, state->originalPart);
            widgetRefresh(state->widget);
            state->timer->deleteLater();
            trace("covers: picker cancelled or unchanged");
        }
    });

    coverPickerPending = true;
    coverTitlePending = true;
    menuSelectBackground(controller);
    // backgroundOptions() is invoked synchronously by this signal on Nickel's
    // UI thread. A leftover flag means controller creation failed.
    coverPickerPending = false;
    timer->start();
    trace("covers: cover picker opened from notebook menu");
}

static void stopHookTimer() {
    if (!hookTimer)
        return;
    hookTimer->stop();
    hookTimer->deleteLater();
    hookTimer = nullptr;
}

static bool installCustomAssetVisibilityHooks() {
    // This image plugin is loaded by Nickel itself, so libnickel must already
    // be present. Install before waiting for libiinknote: filesystem sync can
    // begin before the notebook UI library is naturally loaded. Excluding a
    // previously indexed directory makes its rows stale; stock Nickel passes
    // removeBackingFile=true while pruning those rows. The preservation hook
    // must therefore validate before the exclusion hook is allowed to run.
    void* const handle = dlopen(
        "libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) {
        trace("asset-visibility: libnickel unavailable; scanner unchanged");
        return false;
    }

    ContentGetId resolvedContentGetId = nullptr;
    RemoveCommonBookData resolvedRemoveCommonBookData = nullptr;
    ExcludeSyncFolders resolvedExcludeSyncFolders = nullptr;
    if (!resolvePinned(handle, kExcludeSyncFoldersSymbol,
            kExcludeSyncFoldersVma, &resolvedExcludeSyncFolders)
            || !resolvePinned(handle, kContentGetIdSymbol,
                kContentGetIdVma, &resolvedContentGetId)
            || !resolvePinned(handle, kRemoveCommonBookDataSymbol,
                kRemoveCommonBookDataVma, &resolvedRemoveCommonBookData)) {
        trace("asset-visibility: preservation symbols mismatch; scanner unchanged");
        dlclose(handle);
        return false;
    }

    contentGetId = resolvedContentGetId;
    removeCommonBookDataOriginal = resolvedRemoveCommonBookData;
    RemoveCommonBookDataAddress preservationReplacement;
    preservationReplacement.function = _cnt_remove_common_book_data_hook;
    void* const originalPreservation = nh_dlhook(
        handle, kRemoveCommonBookDataSymbol, preservationReplacement.pointer);
    if (!pointerMatchesVma(
            originalPreservation, kRemoveCommonBookDataVma)) {
        // nh_dlhook returns dlsym(stock), not the displaced GOT entry. If it
        // returned a usable stock address after patching an unexpected image,
        // write that stock target back; otherwise exclusion stays disabled.
        bool restored = false;
        if (originalPreservation) {
            void* const rollbackResult = nh_dlhook(
                handle, kRemoveCommonBookDataSymbol, originalPreservation);
            restored = pointerMatchesVma(
                rollbackResult, kRemoveCommonBookDataVma);
        }
        trace(restored
            ? "asset-visibility: preservation hook mismatch; stock target restored"
            : "asset-visibility: preservation hook mismatch; scanner unchanged");
        dlclose(handle);
        return false;
    }
    removeCommonBookDataOriginal =
        reinterpret_cast<RemoveCommonBookData>(originalPreservation);

    // Set the pass-through before mutating the second GOT entry. Even if
    // validation rejects it, the wrapper preserves the complete stock/user
    // setting rather than replacing it.
    excludeSyncFoldersOriginal = resolvedExcludeSyncFolders;
    ExcludeSyncFoldersAddress exclusionReplacement;
    exclusionReplacement.function = _cnt_exclude_sync_folders_hook;
    void* const originalExclusion = nh_dlhook(
        handle, kExcludeSyncFoldersSymbol, exclusionReplacement.pointer);
    if (!pointerMatchesVma(originalExclusion, kExcludeSyncFoldersVma)) {
        bool exclusionRestored = false;
        if (originalExclusion) {
            void* const rollbackResult = nh_dlhook(
                handle, kExcludeSyncFoldersSymbol, originalExclusion);
            exclusionRestored = pointerMatchesVma(
                rollbackResult, kExcludeSyncFoldersVma);
        }
        void* const preservationRollback = nh_dlhook(
            handle, kRemoveCommonBookDataSymbol, originalPreservation);
        bool const preservationRestored =
            pointerMatchesVma(
                preservationRollback, kRemoveCommonBookDataVma);
        trace(exclusionRestored && preservationRestored
            ? "asset-visibility: exclusion mismatch; both stock targets restored"
            : "asset-visibility: exclusion mismatch; scanner unchanged, rollback unverified");
        dlclose(handle);
        return false;
    }
    excludeSyncFoldersOriginal =
        reinterpret_cast<ExcludeSyncFolders>(originalExclusion);

    dlclose(handle);
    trace("asset-visibility: backing-file guard and scanner exclusion installed");
    return true;
}

static void hideLegacyNotebookBackups() {
    QDir directory = QDir(QLatin1String(kCoverBackupRoot));
    QFileInfoList const files = directory.entryInfoList(
        QDir::Files | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);
    for (int i = 0; i < files.size(); ++i) {
        QFileInfo const& info = files.at(i);
        if (!info.fileName().endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive))
            continue;
        QString const hiddenPath = info.absoluteFilePath()
            + QLatin1String(".backup");
        if (QFileInfo(hiddenPath).exists())
            continue;
        if (QFile::rename(info.absoluteFilePath(), hiddenPath))
            trace("covers: legacy notebook backup hidden from library scanner");
    }
}

static bool installEraserSizeMenuHooks(void* handle) {
    if (!handle || !eraserSizeApisReady)
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
    toolMenuConstructorOriginal = resolvedConstructor;
    createBrushSizeRowOriginal = resolvedRow;
    setBrushSizeIndexOriginal = resolvedSetter;
    setActiveToolOriginal = resolvedActiveTool;

    SetBrushSizeIndexAddress setterReplacement;
    setterReplacement.function = _cnt_set_brush_size_index_hook;
    void* const originalSetter = nh_dlhook(
        handle, kSetBrushSizeIndexSymbol, setterReplacement.pointer);
    if (!pointerMatchesVma(originalSetter, kSetBrushSizeIndexVma)) {
        trace("eraser-size: native size callback hook mismatch; popup unchanged");
        return false;
    }
    setBrushSizeIndexOriginal =
        reinterpret_cast<SetBrushSizeIndex>(originalSetter);

    CreateBrushSizeRowAddress rowReplacement;
    rowReplacement.function = _cnt_create_brush_size_row_hook;
    void* const originalRow = nh_dlhook(
        handle, kCreateBrushSizeRowSymbol, rowReplacement.pointer);
    if (!pointerMatchesVma(originalRow, kCreateBrushSizeRowVma)) {
        trace("eraser-size: native size-row hook mismatch; popup unchanged");
        return false;
    }
    createBrushSizeRowOriginal =
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
    setActiveToolOriginal = reinterpret_cast<SetActiveTool>(originalActiveTool);

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
    toolMenuConstructorOriginal =
        reinterpret_cast<ToolMenuConstructor>(originalConstructor);
    eraserSizeMenuHooksReady = true;
    trace("eraser-size: stock five-button eraser row hooks installed");
    return true;
}

static void installHookAfterNaturalLoad() {
    void* const handle = dlopen("libiinknote.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!handle)
        return;

    trace("libiinknote observed after natural load");
    BackgroundOptionsAddress replacement;
    replacement.function = _cnt_background_options_hook;
    void* const original = nh_dlhook(
        handle,
        kBackgroundOptionsSymbol,
        replacement.pointer);
    if (!original) {
        trace("delayed backgroundOptions hook failed");
        stopHookTimer();
        return;
    }

    backgroundOptionsOriginal = reinterpret_cast<BackgroundOptions>(original);
    trace("delayed backgroundOptions hook installed");

    if (!locateRendererMap()) {
        stopHookTimer();
        return;
    }

    // A malformed user manifest is non-fatal: Nickel continues with every
    // built-in template and no custom rows.
    if (!loadManifest())
        nh_log("custom templates disabled because the manifest is invalid");
    loadAutomaticTemplates();
    loadAutomaticCovers();

    if (resolveCoverApis(handle)) {
        AddWidgetActionAddress replacement;
        replacement.function = _cnt_add_widget_action_hook;
        void* const original = nh_dlhook(
            handle,
            kAddWidgetActionSymbol,
            replacement.pointer);
        addWidgetActionOriginal = reinterpret_cast<AddWidgetAction>(original);
        if (pointerMatchesVma(original, kAddWidgetActionVma)) {
            pageManagerHooksReady = true;
            trace("pages: guarded notebook-menu hook installed");
        } else {
            layerHooksReady = false;
            trace("pages: notebook-menu hook validation failed");
            trace("layers: native action hook unavailable; feature disabled");
        }

        if (layerHooksReady || eraserSizeApisReady) {
            SetToolTheme resolvedTheme = nullptr;
            RenderVolume resolvedRender = nullptr;
            bool const lifecycleSymbols =
                resolvePinned(handle, kSetToolThemeSymbol,
                    kSetToolThemeVma, &resolvedTheme)
                && resolvePinned(handle, kRenderVolumeSymbol,
                    kRenderVolumeVma, &resolvedRender);
            setToolThemeOriginal = resolvedTheme;
            renderVolumeOriginal = resolvedRender;

            bool themeHookValid = false;
            bool renderHookValid = false;
            if (lifecycleSymbols) {
                SetToolThemeAddress themeReplacement;
                themeReplacement.function = _cnt_set_tool_theme_hook;
                void* const originalTheme = nh_dlhook(
                    handle,
                    kSetToolThemeSymbol,
                    themeReplacement.pointer);
                if (pointerMatchesVma(originalTheme, kSetToolThemeVma)) {
                    setToolThemeOriginal =
                        reinterpret_cast<SetToolTheme>(originalTheme);
                    themeHookValid = true;
                }

                RenderVolumeAddress renderReplacement;
                renderReplacement.function = _cnt_render_volume_hook;
                void* const originalRender = nh_dlhook(
                    handle,
                    kRenderVolumeSymbol,
                    renderReplacement.pointer);
                if (pointerMatchesVma(originalRender, kRenderVolumeVma)) {
                    renderVolumeOriginal =
                        reinterpret_cast<RenderVolume>(originalRender);
                    renderHookValid = true;
                }
            }
            notebookLifecycleHooksReady =
                lifecycleSymbols && themeHookValid && renderHookValid;
            if (notebookLifecycleHooksReady) {
                trace("layers: active-layer lifecycle hooks installed");
                trace("eraser-size: notebook lifecycle reapply hooks installed");
            } else {
                if (layerHooksReady)
                    trace("layers: lifecycle hook validation failed; feature disabled");
                if (eraserSizeApisReady) {
                    trace("eraser-size: lifecycle hook validation failed; feature disabled");
                }
                layerHooksReady = false;
                eraserSizeApisReady = false;
            }
        }

        if (notebookLifecycleHooksReady && eraserSizeApisReady
                && !installEraserSizeMenuHooks(handle)) {
            eraserSizeApisReady = false;
            trace("eraser-size: popup augmentation unavailable; feature disabled");
        }

        if (layerHooksReady && !loadZipApis()) {
            trace("layers: read-only archive persistence probe unavailable");
        }

        if (!customCovers.isEmpty() && loadZipApis()) {
            SetDialogTitleAddress titleReplacement;
        titleReplacement.function = _cnt_set_dialog_title_hook;
        void* const originalTitle = nh_dlhook(
            handle,
            kSetDialogTitleSymbol,
            titleReplacement.pointer);
        setDialogTitleOriginal = reinterpret_cast<SetDialogTitle>(originalTitle);

        ParserImageParsedAddress imageReplacement;
        imageReplacement.function = _cnt_parser_image_parsed_hook;
        void* const originalImageParsed = nh_dlhook(
            handle,
            kParserImageParsedSymbol,
            imageReplacement.pointer);
        parserImageParsedOriginal = reinterpret_cast<ParserImageParsed>(
            originalImageParsed);

        void* const nickelHandle = dlopen(
            "libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
        void* originalLoadCover = nullptr;
        if (nickelHandle) {
            bool const gridSymbols =
                resolvePinned(nickelHandle, kContentGetIdSymbol,
                    kContentGetIdVma, &contentGetId)
                && resolvePinned(nickelHandle, kContentGetImageIdSymbol,
                    kContentGetImageIdVma, &contentGetImageId)
                && resolvePinned(nickelHandle, kPixmapSetImageSymbol,
                    kPixmapSetImageVma, &pixmapSetImage);
            if (gridSymbols) {
                VolumeLoadCoverAddress loadCoverReplacement;
                loadCoverReplacement.function = _cnt_volume_load_cover_hook;
                originalLoadCover = nh_dlhook(
                    nickelHandle,
                    kVolumeLoadCoverSymbol,
                    loadCoverReplacement.pointer);
                volumeLoadCoverOriginal = reinterpret_cast<VolumeLoadCover>(
                    originalLoadCover);
                if (pointerMatchesVma(
                        originalLoadCover, kVolumeLoadCoverVma)) {
                    coverGridHookReady = true;
                    trace("covers: live notebook-card load hook installed");
                } else {
                    trace("covers: notebook-card load hook validation failed");
                }
            }
            dlclose(nickelHandle);
        }

        // All imported functions are pinned in libnickel. Menu calls are
        // additionally guarded to IInkMenuController::loadView below, while
        // the title hook requires the one-shot cover-picker flag and exact
        // stock title text. Thumbnail replacement is restricted to the sole
        // imageParsed call in the verified parser callback. The notebook-grid
        // load hook independently checks its exact libnickel VMA, lets Kobo
        // load first, then reads only the view's own Volume and .nebo path
        // (including notebook subfolders).
        if (pointerMatchesVma(original, kAddWidgetActionVma)
                && pointerMatchesVma(originalTitle, kSetDialogTitleVma)
                && pointerMatchesVma(
                    originalImageParsed, kParserImageParsedVma)) {
            coverHooksReady = true;
            trace("covers: guarded menu, picker-title, and parser-thumbnail hooks installed");
        } else {
            trace("covers: hook validation failed");
            nh_log("custom notebook covers disabled: firmware hook mismatch");
        }
        } else {
            trace("covers: no cover PNGs found; cover hooks not installed");
        }
    } else {
        trace("pages: pinned notebook APIs did not resolve");
    }
    trace("delayed hook initialization complete");
    stopHookTimer();
}

static int initialize() {
    trace("NickelHook init entered");
    installCustomAssetVisibilityHooks();
    hideLegacyNotebookBackups();
    hookTimer = new QTimer(QCoreApplication::instance());
    hookTimer->setInterval(100);
    QObject::connect(hookTimer, &QTimer::timeout, installHookAfterNaturalLoad);
    hookTimer->start();
    trace("NickelHook init complete; waiting for natural libiinknote load");
    return 0;
}

static struct nh_info info = {
    "CustomNotebookTemplates",
    "Adds notebook papers, covers, page tools, and experimental native layers.",
    "/mnt/onboard/.kobo/custom/templates/uninstall-plugin",
    nullptr,
    // The insertion and thumbnail hooks have passed their disposable-notebook
    // test. Keep a short startup-crash window without leaving the plugin
    // renamed for ten minutes and accidentally disabling it on a normal
    // follow-up restart.
    30,
};

} // namespace

NickelHook(initialize, &info, nullptr, nullptr, nullptr)

extern "C" __attribute__((visibility("default")))
void _cnt_remove_common_book_data_hook(
    void* manager,
    void const* device,
    void* volume,
    bool removeBackingFile) {
    if (!removeCommonBookDataOriginal)
        return;

    bool preserveBackingFile = false;
    if (removeBackingFile && contentGetId && volume) {
        QString const id = contentGetId(volume);
        preserveBackingFile =
            id.startsWith(QLatin1String(kCustomAssetOnboardContentPrefix))
            || id.startsWith(QLatin1String(
                kCustomAssetOnboardExternalContentPrefix))
            || id.startsWith(QLatin1String(kCustomAssetSdContentPrefix))
            || id.startsWith(QLatin1String(
                kCustomAssetSdExternalContentPrefix));
    }

    if (preserveBackingFile) {
        removeBackingFile = false;
        bool shouldTrace = false;
        {
            QMutexLocker locker(&customAssetExclusionTraceMutex);
            if (!customAssetBackingFilePreserved) {
                customAssetBackingFilePreserved = true;
                shouldTrace = true;
            }
        }
        if (shouldTrace) {
            trace("asset-visibility: stale support row removed; backing file preserved");
        }
    }

    removeCommonBookDataOriginal(
        manager, device, volume, removeBackingFile);
}

extern "C" __attribute__((visibility("default")))
QString _cnt_exclude_sync_folders_hook(void* featureSettings) {
    if (!excludeSyncFoldersOriginal)
        return QString();

    QString exclusions = excludeSyncFoldersOriginal(featureSettings);
    QString const pattern = QLatin1String(kCustomAssetExcludePattern);
    QStringList const entries = exclusions.split(
        QLatin1Char(';'), QString::SkipEmptyParts);
    if (!entries.contains(pattern)) {
        if (!exclusions.isEmpty() && !exclusions.endsWith(QLatin1Char(';')))
            exclusions.append(QLatin1Char(';'));
        exclusions.append(pattern);
    }

    // One observation proves that the live scanner reached the interposed
    // PLT/GOT seam. Avoid logging every recursive directory visit.
    bool shouldTrace = false;
    {
        QMutexLocker locker(&customAssetExclusionTraceMutex);
        if (!customAssetExclusionObserved) {
            customAssetExclusionObserved = true;
            shouldTrace = true;
        }
    }
    if (shouldTrace) {
        trace("asset-visibility: .kobo/custom excluded from live library scan");
    }
    return exclusions;
}

extern "C" __attribute__((visibility("default")))
QVector<BackgroundOption> _cnt_background_options_hook() {
    trace("backgroundOptions hook invoked");
    QVector<BackgroundOption> options = backgroundOptionsOriginal();

    if (coverPickerPending) {
        coverPickerPending = false;
        options.clear();
        for (int i = 0; i < customCovers.size(); ++i) {
            CustomTemplate const& value = customCovers.at(i);
            options.append(BackgroundOption(value.id, value.icon, value.label));
        }
        trace("backgroundOptions hook returned cover-only rows");
        return options;
    }

    for (int i = 0; i < customTemplates.size(); ++i) {
        CustomTemplate const& value = customTemplates.at(i);
        options.append(BackgroundOption(value.id, value.icon, value.label));
    }
    trace("backgroundOptions hook appended custom rows");
    return options;
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_dialog_title_hook(void* dialog, QString const& title) {
    if (!setDialogTitleOriginal)
        return;
    if (coverHooksReady
            && coverTitlePending
            && title == QLatin1String("Notebook Templates")) {
        coverTitlePending = false;
        setDialogTitleOriginal(dialog, QLatin1String("Notebook Covers"));
        trace("covers: picker title changed to Notebook Covers");
        return;
    }
    setDialogTitleOriginal(dialog, title);
}

extern "C" __attribute__((visibility("default")))
void _cnt_parser_image_parsed_hook(
    void* parser,
    void const* volume,
    QImage const& image) {
    if (!parserImageParsedOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    if (!coverHooksReady
            || !contentGetId
            || !zipOpen
            || !iinknoteBase
            || caller != iinknoteBase + kThumbnailCallbackReturnVma) {
        parserImageParsedOriginal(parser, volume, image);
        return;
    }

    QElapsedTimer timer;
    timer.start();
    QString const notebookPath = notebookPathFromVolume(volume);
    double const pathResolveMs = elapsedMs(timer);
    QString type;
    if (notebookPath.isEmpty()
            || !cachedNotebookCoverType(notebookPath, &type, pathResolveMs)) {
        parserImageParsedOriginal(parser, volume, image);
        return;
    }

    QImage const composed = composeCoverWithRenderedInk(type, image);
    if (composed.isNull()) {
        parserImageParsedOriginal(parser, volume, image);
        trace("covers: cover-and-ink composition failed; stock preview preserved");
        return;
    }

    cacheRenderedCoverImage(notebookPath, type, composed);
    parserImageParsedOriginal(parser, volume, composed);
    trace("covers: custom cover composed with rendered page ink");
}

extern "C" __attribute__((visibility("default")))
void _cnt_volume_load_cover_hook(void* view) {
    if (!volumeLoadCoverOriginal)
        return;

    // Preserve Kobo's normal cache/download/default-cover behavior first.
    volumeLoadCoverOriginal(view);
    if (!coverGridHookReady
            || !view
            || !contentGetId
            || !contentGetImageId
            || !pixmapSetImage
            || !zipOpen) {
        return;
    }

    void const* const volume = static_cast<char const*>(view)
        + kVolumeInPixmapViewOffset;
    QElapsedTimer timer;
    timer.start();
    QString const notebookPath = notebookPathFromVolume(volume);
    double const pathResolveMs = elapsedMs(timer);
    QString type;
    if (notebookPath.isEmpty()
            || !cachedNotebookCoverType(notebookPath, &type, pathResolveMs)) {
        return;
    }

    QImage preview = cachedRenderedCoverImage(notebookPath, type);
    if (preview.isNull()) {
        preview = cleanCustomCoverImage(type);
        if (!preview.isNull())
            trace("covers: clean cover used until ink preview is rendered");
    }
    if (preview.isNull()) {
        trace("covers: no rendered or clean preview; stock card preserved");
        return;
    }

    QString const imageId = contentGetImageId(volume);
    timer.restart();
    pixmapSetImage(view, preview, imageId);
    char line[128];
    snprintf(line, sizeof(line),
        "covers: live notebook card replaced (set-image=%.1fms)",
        elapsedMs(timer));
    trace(line);
}

extern "C" __attribute__((visibility("default")))
void _cnt_tool_menu_constructor_hook(
    void* controller,
    QWidget* parent,
    QVector<int> const* tools,
    QVector<int> const* brushSections,
    void* themeStorage) {
    if (!toolMenuConstructorOriginal)
        return;

    bool const exactEraserMenu = eraserSizeMenuHooksReady
        && controller
        && tools
        && brushSections
        && tools->size() == 2
        && tools->at(0) == 1
        && tools->at(1) == 2
        && brushSections->isEmpty();
    if (!exactEraserMenu) {
        toolMenuConstructorOriginal(
            controller, parent, tools, brushSections, themeStorage);
        return;
    }

    // IInkToolBrush 0 is Kobo's stock five-button size row. The constructor
    // copies this QVector into controller+0x24, so the local lifetime ends
    // safely when the original returns.
    QVector<int> eraserSections;
    eraserSections.append(0);
    toolMenuConstructorOriginal(
        controller, parent, tools, &eraserSections, themeStorage);

    try {
        QObject* const object = reinterpret_cast<QObject*>(controller);
        object->setProperty("_cnt_eraser_size_controller", true);
        liveEraserSizeController = object;
        int const index = configuredEraserSizeIndex();
        // Seed before onToolButtonTapped invokes virtual loadView immediately
        // after this constructor returns. The empty button vector is handled
        // by stock setBrushSizeIndex.
        if (setBrushSizeIndexOriginal && validEraserSizeIndex(index))
            setBrushSizeIndexOriginal(controller, index);
        trace(QLatin1String(
            "eraser-size: native eraser controller augmented index=")
            + QString::number(index));
    } catch (...) {
        trace("eraser-size: controller metadata failed; stock controller preserved");
    }
}

extern "C" __attribute__((visibility("default")))
void _cnt_create_brush_size_row_hook(
    void* controller,
    NickelTouchMenu* menu,
    QString const& title) {
    if (!createBrushSizeRowOriginal)
        return;
    bool marked = false;
    if (eraserSizeMenuHooksReady && controller) {
        marked = reinterpret_cast<QObject*>(controller)
            ->property("_cnt_eraser_size_controller").toBool();
    }
    if (!marked || !menu) {
        createBrushSizeRowOriginal(controller, menu, title);
        return;
    }

    // Stock createBrushSizeRow uses `title` both for visible/action text and
    // as the GenericContainerWidget objectName. The latter is a private style
    // identity: changing it makes the header inherit the active-action
    // highlight. Keep the desired text, then restore only the newly-created
    // row widget's objectName to the stock translated title.
    QObject* const menuObject = reinterpret_cast<QObject*>(menu);
    QString const eraserTitle = QLatin1String("Eraser Size");
    QList<QWidget*> const rowsBefore = menuObject->findChildren<QWidget*>(
        eraserTitle);
    createBrushSizeRowOriginal(controller, menu, eraserTitle);
    QList<QWidget*> const rowsAfter = menuObject->findChildren<QWidget*>(
        eraserTitle);
    bool identityRestored = false;
    for (int i = 0; i < rowsAfter.size(); ++i) {
        QWidget* const row = rowsAfter.at(i);
        if (!row || rowsBefore.contains(row))
            continue;
        row->setObjectName(title);
        identityRestored = true;
        break;
    }
    trace(identityRestored
        ? "eraser-size: native five-button row created with stock style identity"
        : "eraser-size: native row created; stock style identity was not restored");
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_brush_size_index_hook(void* controller, int index) {
    if (!setBrushSizeIndexOriginal)
        return;
    // Preserve every stock theme/button update first, including unknown future
    // callers. Only the two pinned firmware callsites below have plugin work.
    setBrushSizeIndexOriginal(controller, index);
    if (!eraserSizeMenuHooksReady || !controller
            || !validEraserSizeIndex(index)
            || !reinterpret_cast<QObject*>(controller)
                ->property("_cnt_eraser_size_controller").toBool()
            || !iinknoteBase) {
        return;
    }

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    bool const clicked =
        caller == iinknoteBase + kSetBrushSizeIndexClickReturnVma;
    bool const loaded =
        caller == iinknoteBase + kSetBrushSizeIndexLoadReturnVma;
    if (!clicked && !loaded)
        return;
    if (clicked)
        persistEraserSizeIndex(index);

    QObject* const widget = findNotebookWidget(
        reinterpret_cast<QObject*>(controller));
    if (!widget) {
        trace("eraser-size: notebook widget missing from native controller");
        return;
    }
    applyConfiguredEraserSizeForWidget(
        widget, clicked ? "menu-select" : "menu-load");
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_active_tool_hook(void* widget, int tool) {
    if (!setActiveToolOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    setActiveToolOriginal(widget, tool);

    // stylusTouchBegin switches the engine to its saved eraser tool without
    // visiting IInkToolMenuController, and firmware resets the live radius in
    // that path. The user's configured size remains authoritative: replay it
    // after stock activation, then mirror the same index into any open popup.
    // Nothing here persists; only an explicit five-button tap writes JSON.
    if (!eraserSizeMenuHooksReady || !widget || !iinknoteBase
            || caller != iinknoteBase
                + kHardwareEraserSetActiveToolReturnVma
            || (tool != 1 && tool != 2)
            || !isNotebookWidget(reinterpret_cast<QObject*>(widget))) {
        return;
    }

    try {
        int const configuredIndex = configuredEraserSizeIndex();
        bool const engineUpdated = applyConfiguredEraserSizeForWidget(
            reinterpret_cast<QObject*>(widget), "hardware-eraser");
        bool uiUpdated = false;
        QObject* const controller = liveEraserSizeController.data();
        if (engineUpdated && controller && setBrushSizeIndexOriginal
                && findNotebookWidget(controller)
                    == reinterpret_cast<QObject*>(widget)
                && controller->property(
                    "_cnt_eraser_size_controller").toBool()) {
            // The stock setter updates controller+0x48 and all five native
            // BrushButtons but emits no toolSelected signal, so this cannot
            // recurse into setToolTheme or mutate the engine a second time.
            setBrushSizeIndexOriginal(controller, configuredIndex);
            uiUpdated = true;
        }
        trace(QLatin1String(
            "eraser-size: hardware activation synchronized index=")
            + QString::number(configuredIndex)
            + QLatin1String(" engine=")
            + (engineUpdated ? QLatin1String("applied")
                             : QLatin1String("unavailable"))
            + QLatin1String(" popup=")
            + (uiUpdated ? QLatin1String("updated")
                         : QLatin1String("not-open")));
    } catch (...) {
        trace("eraser-size: hardware activation synchronization threw; last successful state preserved");
    }
}

extern "C" __attribute__((visibility("default")))
void _cnt_render_volume_hook(void* widget, void const* volume) {
    if (!renderVolumeOriginal)
        return;

    // Stock renderVolume synchronously constructs the editor/backend/tool and,
    // on its saved-page path, installs the final part after setupVolume. The
    // caller reaches this symbol through the PLT, so running after the original
    // is the first hookable point where all three identities are coherent.
    renderVolumeOriginal(widget, volume);
    if (!notebookLifecycleHooksReady || !widget)
        return;

    QObject* const object = reinterpret_cast<QObject*>(widget);
    if (!isNotebookWidget(object))
        return;
    if (eraserSizeApisReady) {
        try {
            applyConfiguredEraserSizeForWidget(object, "notebook-open");
        } catch (...) {
            trace("eraser-size: notebook-open replay threw; stock radius preserved");
        }
    }
    if (!layerHooksReady)
        return;

    try {
        trace("layers: notebook render complete; restoring saved active layer");
        LayerContext context;
        QString error;
        if (!loadLayerContext(object, &context, &error)) {
            trace(QLatin1String(
                "layers: notebook-open context unavailable; metadata preserved: ")
                + error);
            return;
        }
        if (!synchronizeSavedActiveLayer(
                context, "notebook-open", &error)) {
            trace(QLatin1String(
                "layers: notebook-open active layer not restored; metadata preserved: ")
                + error);
        }
    } catch (...) {
        // Opening the stock notebook must remain safe even if a passive restore
        // observes a transient engine state. No metadata is changed here.
        trace("layers: notebook-open active synchronization threw; metadata preserved");
    }
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_tool_theme_hook(void* widget, void* theme) {
    if (!setToolThemeOriginal)
        return;
    setToolThemeOriginal(widget, theme);
    if (!notebookLifecycleHooksReady || !widget)
        return;

    QObject* const object = reinterpret_cast<QObject*>(widget);
    if (!isNotebookWidget(object))
        return;
    if (eraserSizeApisReady) {
        try {
            applyConfiguredEraserSizeForWidget(object, "tool-theme");
        } catch (...) {
            trace("eraser-size: tool-theme replay threw; stock radius preserved");
        }
    }
    if (!layerHooksReady)
        return;
    trace("layers: setToolTheme hook invoked for notebook");
    LayerContext context;
    QString error;
    if (!loadLayerContext(object, &context, &error)) {
        trace(QLatin1String("layers: setToolTheme context unavailable: ") + error);
        return;
    }
    if (!applyActiveLayer(context, context.state.activeId, &error)) {
        trace(QLatin1String(
            "layers: active layer could not be reapplied after tool change: ")
            + error);
        return;
    }
    if (context.state.activeId != nativeDocumentLayerId())
        trace("layers: custom active layer reapplied after tool change");
    else
        trace("layers: base layer reapplied after tool change");
}

extern "C" __attribute__((visibility("default")))
void _cnt_add_widget_action_hook(
    void* controller,
    QMenu* menu,
    QWidget* widget,
    QObject* receiver,
    char const* member,
    bool closesMenu,
    bool enabled,
    bool separatorAfter) {
    if (!addWidgetActionOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    addWidgetActionOriginal(
        controller, menu, widget, receiver, member,
        closesMenu, enabled, separatorAfter);

    if ((!coverHooksReady && !pageManagerHooksReady && !layerHooksReady)
            || !controller
            || !menu
            || !iinknoteBase
            || caller < iinknoteBase + kMenuLoadViewVma
            || caller >= iinknoteBase + kMenuLoadViewVma + kMenuLoadViewSize
            || menu->property("_cnt_notebook_actions_added").toBool()) {
        return;
    }

    menu->setProperty("_cnt_notebook_actions_added", true);
    QObject* const controllerObject = reinterpret_cast<QObject*>(controller);
    QPixmap noIcon;

    if (coverHooksReady && !customCovers.isEmpty()) {
        CoverMenuReceiver* const coverReceiver = new CoverMenuReceiver(
            controllerObject, menu);
        QObject::connect(
            coverReceiver,
            &CoverMenuReceiver::coverRequested,
            [](QObject* target) {
                if (target)
                    beginCoverPicker(target);
            });

        QWidget* const coverItem = createIInkMenuItem(
            controller,
            menu,
            QLatin1String("Change notebook cover"),
            noIcon,
            false);
        if (coverItem) {
            addWidgetActionOriginal(
                controller,
                menu,
                coverItem,
                coverReceiver,
                SLOT(activate()),
                true,
                true,
                true);
            trace("covers: native notebook menu item added");
        } else {
            coverReceiver->deleteLater();
            trace("covers: native menu item creation failed");
        }
    }

    if (layerHooksReady) {
        CoverMenuReceiver* const layerReceiver = new CoverMenuReceiver(
            controllerObject, menu);
        QObject::connect(
            layerReceiver,
            &CoverMenuReceiver::layersRequested,
            [](QObject* target) {
                if (target)
                    showLayerPopup(target);
            });
        QWidget* const layerItem = createIInkMenuItem(
            controller,
            menu,
            QLatin1String("Layers"),
            noIcon,
            false);
        if (layerItem) {
            addWidgetActionOriginal(
                controller,
                menu,
                layerItem,
                layerReceiver,
                SLOT(activateLayers()),
                true,
                true,
                true);
            trace("layers: native notebook menu item added");
        } else {
            layerReceiver->deleteLater();
            trace("layers: native menu item creation failed");
        }
    }

    if (pageManagerHooksReady) {
        CoverMenuReceiver* const pageReceiver = new CoverMenuReceiver(
            controllerObject, menu);
        QObject::connect(
            pageReceiver,
            &CoverMenuReceiver::duplicatePageRequested,
            [](QObject* target) {
                if (target)
                    runPageOperation(target, DuplicatePageOperation);
            });
        QObject::connect(
            pageReceiver,
            &CoverMenuReceiver::movePageEarlierRequested,
            [](QObject* target) {
                if (target)
                    runPageOperation(target, MovePageEarlierOperation);
            });
        QObject::connect(
            pageReceiver,
            &CoverMenuReceiver::movePageLaterRequested,
            [](QObject* target) {
                if (target)
                    runPageOperation(target, MovePageLaterOperation);
            });

        QWidget* const duplicateItem = createIInkMenuItem(
            controller,
            menu,
            QLatin1String("Duplicate page"),
            noIcon,
            false);
        if (duplicateItem) {
            addWidgetActionOriginal(
                controller,
                menu,
                duplicateItem,
                pageReceiver,
                SLOT(activateDuplicatePage()),
                true,
                true,
                false);
            trace("pages: native duplicate menu item added");
        } else {
            trace("pages: native duplicate menu item creation failed");
        }

        QWidget* const earlierItem = createIInkMenuItem(
            controller,
            menu,
            QLatin1String("Move page earlier"),
            noIcon,
            false);
        if (earlierItem) {
            addWidgetActionOriginal(
                controller,
                menu,
                earlierItem,
                pageReceiver,
                SLOT(activateMovePageEarlier()),
                true,
                true,
                false);
            trace("pages: native move-earlier menu item added");
        } else {
            trace("pages: native move-earlier menu item creation failed");
        }

        QWidget* const laterItem = createIInkMenuItem(
            controller,
            menu,
            QLatin1String("Move page later"),
            noIcon,
            false);
        if (laterItem) {
            addWidgetActionOriginal(
                controller,
                menu,
                laterItem,
                pageReceiver,
                SLOT(activateMovePageLater()),
                true,
                true,
                true);
            trace("pages: native move-later menu item added");
        } else {
            trace("pages: native move-later menu item creation failed");
        }
    }
}
