#pragma once

#line 60 "src/customnotebooktemplates.cc"
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
