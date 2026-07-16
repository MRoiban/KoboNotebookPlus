#include "eraser_menu.h"

#include "abi_types.h"
#include "firmware_api.h"
#include "notebook_widget.h"
#include "settings.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <NickelHook.h>

#include <dlfcn.h>

namespace cnt {
namespace eraser_menu {
namespace {

static char const kToolMenuConstructorSymbol[] =
    "_ZN22IInkToolMenuControllerC1EP7QWidgetRK7QVectorI8IInkToolERKS2_I13IInkToolBrushE13IInkToolTheme";
static char const kCreateBrushSizeRowSymbol[] =
    "_ZN22IInkToolMenuController18createBrushSizeRowEP15NickelTouchMenuRK7QString";
static char const kSetBrushSizeIndexSymbol[] =
    "_ZN22IInkToolMenuController17setBrushSizeIndexEi";
static char const kSetActiveToolSymbol[] =
    "_ZN17IInkNotePadWidget13setActiveToolE8IInkTool";

static uintptr_t const kToolMenuConstructorVma = 0x74760;
static uintptr_t const kCreateBrushSizeRowVma = 0x74fcc;
static uintptr_t const kSetBrushSizeIndexVma = 0x74ba4;
static uintptr_t const kSetBrushSizeIndexClickReturnVma = 0x74d7c;
static uintptr_t const kSetBrushSizeIndexLoadReturnVma = 0x75702;
static uintptr_t const kSetActiveToolVma = 0x62194;
// stylusTouchBegin's unique hardware-eraser call returns here after loading
// the saved eraser enum from IInkNotePadWidget+0xb8.
static uintptr_t const kHardwareEraserSetActiveToolReturnVma = 0x6244e;

template <typename Function>
bool resolvePinned(
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

bool pointerMatchesVma(void* pointer, uintptr_t expectedVma) {
    Dl_info image = {};
    if (!pointer || !dladdr(pointer, &image) || !image.dli_fbase)
        return false;
    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const address = reinterpret_cast<uintptr_t>(pointer) & ~uintptr_t(1);
    return address - base == expectedVma;
}

} // namespace

bool installHooks(
        FirmwareApi& firmware,
        RuntimeState& state,
        void* handle) {
    if (!handle || !state.sizeApisReady)
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
    firmware.toolMenuConstructorOriginal = resolvedConstructor;
    firmware.createBrushSizeRowOriginal = resolvedRow;
    firmware.setBrushSizeIndexOriginal = resolvedSetter;
    firmware.setActiveToolOriginal = resolvedActiveTool;

    SetBrushSizeIndexAddress setterReplacement;
    setterReplacement.function = _cnt_set_brush_size_index_hook;
    void* const originalSetter = nh_dlhook(
        handle, kSetBrushSizeIndexSymbol, setterReplacement.pointer);
    if (!pointerMatchesVma(originalSetter, kSetBrushSizeIndexVma)) {
        trace("eraser-size: native size callback hook mismatch; popup unchanged");
        return false;
    }
    firmware.setBrushSizeIndexOriginal =
        reinterpret_cast<SetBrushSizeIndex>(originalSetter);

    CreateBrushSizeRowAddress rowReplacement;
    rowReplacement.function = _cnt_create_brush_size_row_hook;
    void* const originalRow = nh_dlhook(
        handle, kCreateBrushSizeRowSymbol, rowReplacement.pointer);
    if (!pointerMatchesVma(originalRow, kCreateBrushSizeRowVma)) {
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
        handle, kSetActiveToolSymbol, activeToolReplacement.pointer);
    if (!pointerMatchesVma(originalActiveTool, kSetActiveToolVma)) {
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
        handle, kToolMenuConstructorSymbol, constructorReplacement.pointer);
    if (!pointerMatchesVma(originalConstructor, kToolMenuConstructorVma)) {
        trace("eraser-size: native controller hook mismatch; popup unchanged");
        return false;
    }
    firmware.toolMenuConstructorOriginal =
        reinterpret_cast<ToolMenuConstructor>(originalConstructor);
    state.sizeMenuHooksReady = true;
    trace("eraser-size: stock five-button eraser row hooks installed");
    return true;
}

void constructController(
        FirmwareApi& firmware,
        RuntimeState& state,
        SettingsStore& settings,
        void* controller,
        QWidget* parent,
        QVector<int> const* tools,
        QVector<int> const* brushSections,
        void* themeStorage) {
    if (!firmware.toolMenuConstructorOriginal)
        return;

    bool const exactEraserMenu = state.sizeMenuHooksReady
        && controller
        && tools
        && brushSections
        && tools->size() == 2
        && tools->at(0) == 1
        && tools->at(1) == 2
        && brushSections->isEmpty();
    if (!exactEraserMenu) {
        firmware.toolMenuConstructorOriginal(
            controller, parent, tools, brushSections, themeStorage);
        return;
    }

    // IInkToolBrush 0 is Kobo's stock five-button size row. The constructor
    // copies this QVector into controller+0x24, so the local lifetime ends
    // safely when the original returns.
    QVector<int> eraserSections;
    eraserSections.append(0);
    firmware.toolMenuConstructorOriginal(
        controller, parent, tools, &eraserSections, themeStorage);

    try {
        QObject* const object = reinterpret_cast<QObject*>(controller);
        object->setProperty("_cnt_eraser_size_controller", true);
        state.liveSizeController = object;
        int const index = settings.configuredEraserSizeIndex();
        // Seed before onToolButtonTapped invokes virtual loadView immediately
        // after this constructor returns. The empty button vector is handled
        // by stock setBrushSizeIndex.
        if (firmware.setBrushSizeIndexOriginal && validEraserSizeIndex(index))
            firmware.setBrushSizeIndexOriginal(controller, index);
        trace(QLatin1String(
            "eraser-size: native eraser controller augmented index=")
            + QString::number(index));
    } catch (...) {
        trace("eraser-size: controller metadata failed; stock controller preserved");
    }
}

void createBrushSizeRow(
        FirmwareApi& firmware,
        RuntimeState& state,
        void* controller,
        NickelTouchMenu* menu,
        QString const& title) {
    if (!firmware.createBrushSizeRowOriginal)
        return;
    bool marked = false;
    if (state.sizeMenuHooksReady && controller) {
        marked = reinterpret_cast<QObject*>(controller)
            ->property("_cnt_eraser_size_controller").toBool();
    }
    if (!marked || !menu) {
        firmware.createBrushSizeRowOriginal(controller, menu, title);
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
    firmware.createBrushSizeRowOriginal(controller, menu, eraserTitle);
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

void afterBrushSizeIndex(
        FirmwareApi& firmware,
        RuntimeState& state,
        SettingsStore& settings,
        uintptr_t caller,
        void* controller,
        int index,
        ApplyConfiguredSize applyConfiguredSize) {
    if (!state.sizeMenuHooksReady || !controller
            || !validEraserSizeIndex(index)
            || !reinterpret_cast<QObject*>(controller)
                ->property("_cnt_eraser_size_controller").toBool()
            || !firmware.iinknoteBase) {
        return;
    }

    bool const clicked =
        caller == firmware.iinknoteBase + kSetBrushSizeIndexClickReturnVma;
    bool const loaded =
        caller == firmware.iinknoteBase + kSetBrushSizeIndexLoadReturnVma;
    if (!clicked && !loaded)
        return;
    if (clicked)
        settings.persistEraserSizeIndex(index);

    QObject* const widget = notebook_widget::findNotebookWidget(
        reinterpret_cast<QObject*>(controller));
    if (!widget) {
        trace("eraser-size: notebook widget missing from native controller");
        return;
    }
    applyConfiguredSize(widget, clicked ? "menu-select" : "menu-load");
}

void afterActiveTool(
        FirmwareApi& firmware,
        RuntimeState& state,
        SettingsStore& settings,
        uintptr_t caller,
        void* widget,
        int tool,
        ApplyConfiguredSize applyConfiguredSize) {
    // stylusTouchBegin switches the engine to its saved eraser tool without
    // visiting IInkToolMenuController, and firmware resets the live radius in
    // that path. The user's configured size remains authoritative: replay it
    // after stock activation, then mirror the same index into any open popup.
    // Nothing here persists; only an explicit five-button tap writes JSON.
    if (!state.sizeMenuHooksReady || !widget || !firmware.iinknoteBase
            || caller != firmware.iinknoteBase
                + kHardwareEraserSetActiveToolReturnVma
            || (tool != 1 && tool != 2)
            || !notebook_widget::isNotebookWidget(
                reinterpret_cast<QObject*>(widget))) {
        return;
    }

    try {
        int const configuredIndex = settings.configuredEraserSizeIndex();
        bool const engineUpdated = applyConfiguredSize(
            reinterpret_cast<QObject*>(widget), "hardware-eraser");
        bool uiUpdated = false;
        QObject* const controller = state.liveSizeController.data();
        if (engineUpdated && controller && firmware.setBrushSizeIndexOriginal
                && notebook_widget::findNotebookWidget(controller)
                    == reinterpret_cast<QObject*>(widget)
                && controller->property(
                    "_cnt_eraser_size_controller").toBool()) {
            // The stock setter updates controller+0x48 and all five native
            // BrushButtons but emits no toolSelected signal, so this cannot
            // recurse into setToolTheme or mutate the engine a second time.
            firmware.setBrushSizeIndexOriginal(controller, configuredIndex);
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

} // namespace eraser_menu
} // namespace cnt
