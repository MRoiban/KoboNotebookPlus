#include "abi_types.h"
#include "cover_cache.h"
#include "covers.h"
#include "eraser_menu.h"
#include "firmware_api.h"
#include "firmware_resolver.h"
#include "notebook_sleep.h"
#include "plugin_runtime.h"
#include "plugin_state.h"
#include "settings.h"
#include "templates.h"
#include "visibility.h"
#include "visibility_hooks.h"

#include <NickelHook.h>

#include <QCoreApplication>
#include <QObject>
#include <QTimer>

#include <cstdint>
#include <dlfcn.h>

using cnt::trace;

namespace {

char const kBackgroundOptionsSymbol[] =
    "_ZN23BackgroundOptionsWidget17backgroundOptionsEv";
uintptr_t const kBackgroundOptionsVma = 0x78c9c;
char const kAddWidgetActionSymbol[] =
    "_ZN22AbstractMenuController15addWidgetActionEP5QMenuP7QWidgetP7QObjectPKcbbb";
uintptr_t const kAddWidgetActionVma = 0xb3ce80;
char const kSetToolThemeSymbol[] =
    "_ZN17IInkNotePadWidget12setToolThemeER13IInkToolTheme";
uintptr_t const kSetToolThemeVma = 0x62200;
char const kRenderVolumeSymbol[] =
    "_ZN17IInkNotePadWidget12renderVolumeERK6Volume";
uintptr_t const kRenderVolumeVma = 0x66720;
char const kSetDialogTitleSymbol[] = "_ZN8N3Dialog8setTitleERK7QString";
uintptr_t const kSetDialogTitleVma = 0x10e4168;
char const kParserImageParsedSymbol[] =
    "_ZN15ParserInterface11imageParsedERK6VolumeRK6QImage";
uintptr_t const kParserImageParsedVma = 0x118f714;
char const kContentGetIdSymbol[] = "_ZNK7Content5getIdEv";
uintptr_t const kContentGetIdVma = 0x953d84;
char const kContentGetImageIdSymbol[] = "_ZNK7Content10getImageIdEv";
uintptr_t const kContentGetImageIdVma = 0x957628;
char const kPixmapSetImageSymbol[] =
    "_ZN10PixmapView8setImageERK6QImageRK7QString";
uintptr_t const kPixmapSetImageVma = 0x10fa084;
char const kVolumeLoadCoverSymbol[] = "_ZN16VolumePixmapView9loadCoverEv";
uintptr_t const kVolumeLoadCoverVma = 0xc73388;

static void installHookAfterNaturalLoad() {
    void* const handle = dlopen("libiinknote.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!handle)
        return;

    trace("libiinknote observed after natural load");
    BackgroundOptions resolvedBackgroundOptions = nullptr;
    if (!resolvePinned(handle, kBackgroundOptionsSymbol,
            kBackgroundOptionsVma, &resolvedBackgroundOptions)) {
        trace("delayed backgroundOptions symbol validation failed");
        hookState().stopTimer();
        return;
    }
    firmwareApi().backgroundOptionsOriginal = resolvedBackgroundOptions;

    if (!cnt::templates::locateRendererMap(
            nh_symptr(firmwareApi().backgroundOptionsOriginal),
            firmwareApi().rendererMap,
            firmwareApi().iinknoteBase)) {
        hookState().stopTimer();
        return;
    }

    // A malformed user manifest is non-fatal: Nickel continues with every
    // built-in template and no custom rows.
    if (!cnt::templates::loadManifest(
            *firmwareApi().rendererMap, pluginState().templates))
        nh_log("custom templates disabled because the manifest is invalid");
    cnt::templates::loadAutomaticTemplates(
        *firmwareApi().rendererMap, pluginState().templates);
    cnt::covers::loadAutomaticCovers(
        *firmwareApi().rendererMap, coverState().customCovers);

    BackgroundOptionsAddress replacement;
    replacement.function = _cnt_background_options_hook;
    void* const original = nh_dlhook(
        handle,
        kBackgroundOptionsSymbol,
        replacement.pointer);
    if (!pointerMatchesVma(original, kBackgroundOptionsVma)) {
        trace("delayed backgroundOptions hook validation failed");
        hookState().stopTimer();
        return;
    }
    firmwareApi().backgroundOptionsOriginal = reinterpret_cast<BackgroundOptions>(original);
    trace("delayed backgroundOptions hook installed");

    if (resolveFirmwareApis(handle)) {
        AddWidgetAction resolvedAction = nullptr;
        void* original = nullptr;
        if (firmwareApi().createIInkMenuItem && firmwareApi().iinknoteBase
                && resolvePinned(handle, kAddWidgetActionSymbol,
                    kAddWidgetActionVma, &resolvedAction)) {
            firmwareApi().addWidgetActionOriginal = resolvedAction;
            AddWidgetActionAddress actionReplacement;
            actionReplacement.function = _cnt_add_widget_action_hook;
            original = nh_dlhook(
                handle,
                kAddWidgetActionSymbol,
                actionReplacement.pointer);
            if (pointerMatchesVma(original, kAddWidgetActionVma)) {
                firmwareApi().addWidgetActionOriginal =
                    reinterpret_cast<AddWidgetAction>(original);
            }
        }
        if (pointerMatchesVma(original, kAddWidgetActionVma)) {
            pluginState().pages.hooksReady = true;
            trace("pages: guarded notebook-menu hook installed");
        } else {
            layerState().hooksReady = false;
            trace("pages: notebook-menu hook validation failed");
            trace("layers: native action hook unavailable; feature disabled");
        }

        if (layerState().hooksReady || eraserState().sizeApisReady
                || pluginState().notebookSleep.coreHooksReady) {
            SetToolTheme resolvedTheme = nullptr;
            RenderVolume resolvedRender = nullptr;
            bool const lifecycleSymbols =
                resolvePinned(handle, kSetToolThemeSymbol,
                    kSetToolThemeVma, &resolvedTheme)
                && resolvePinned(handle, kRenderVolumeSymbol,
                    kRenderVolumeVma, &resolvedRender);
            firmwareApi().setToolThemeOriginal = resolvedTheme;
            firmwareApi().renderVolumeOriginal = resolvedRender;

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
                    firmwareApi().setToolThemeOriginal =
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
                    firmwareApi().renderVolumeOriginal =
                        reinterpret_cast<RenderVolume>(originalRender);
                    renderHookValid = true;
                }
            }
            hookState().notebookLifecycleHooksReady =
                lifecycleSymbols && themeHookValid && renderHookValid;
            if (hookState().notebookLifecycleHooksReady) {
                if (layerState().hooksReady)
                    trace("layers: active-layer lifecycle hooks installed");
                if (eraserState().sizeApisReady) {
                    trace("eraser-size: notebook lifecycle reapply hooks installed");
                }
                if (pluginState().notebookSleep.coreHooksReady)
                    trace("notebook-sleep: notebook-open observer hook installed");
            } else {
                if (layerState().hooksReady)
                    trace("layers: lifecycle hook validation failed; feature disabled");
                if (eraserState().sizeApisReady) {
                    trace("eraser-size: lifecycle hook validation failed; feature disabled");
                }
                layerState().hooksReady = false;
                eraserState().sizeApisReady = false;
            }
        }

        if (hookState().notebookLifecycleHooksReady && eraserState().sizeApisReady
                && !cnt::eraser_menu::installHooks(
                    firmwareApi(), eraserState(), handle)) {
            eraserState().sizeApisReady = false;
            trace("eraser-size: popup augmentation unavailable; feature disabled");
        }

        if (layerState().hooksReady
                && !cnt::cover_cache::loadZipApis(coverState())) {
            trace("layers: read-only archive persistence probe unavailable");
        }

        // The parser hook is also the stock notebook-cover source for the
        // sleep screen, so keep it independent from custom cover PNGs and the
        // optional archive scanner. Exact-caller checks in routeParserImage()
        // leave every non-thumbnail ParserInterface signal untouched.
        void* originalImageParsed = nullptr;
        ParserImageParsed resolvedImageParsed = nullptr;
        if (firmwareApi().contentGetId
                && resolvePinned(handle, kParserImageParsedSymbol,
                    kParserImageParsedVma, &resolvedImageParsed)) {
            firmwareApi().parserImageParsedOriginal = resolvedImageParsed;
            ParserImageParsedAddress imageReplacement;
            imageReplacement.function = _cnt_parser_image_parsed_hook;
            originalImageParsed = nh_dlhook(
                handle,
                kParserImageParsedSymbol,
                imageReplacement.pointer);
            if (pointerMatchesVma(
                    originalImageParsed, kParserImageParsedVma)) {
                firmwareApi().parserImageParsedOriginal =
                    reinterpret_cast<ParserImageParsed>(originalImageParsed);
                coverState().parserHookReady = true;
                trace("notebook-sleep: stock page-zero thumbnail hook installed");
            } else {
                trace("notebook-sleep: parser-thumbnail hook validation failed");
            }
        }

        if (!coverState().customCovers.isEmpty()
                && cnt::cover_cache::loadZipApis(coverState())) {
            SetDialogTitle resolvedTitle = nullptr;
            bool const coverHookSymbols =
                resolvePinned(handle, kSetDialogTitleSymbol,
                    kSetDialogTitleVma, &resolvedTitle);
            void* originalTitle = nullptr;
            if (coverHookSymbols) {
                firmwareApi().setDialogTitleOriginal = resolvedTitle;

                SetDialogTitleAddress titleReplacement;
                titleReplacement.function = _cnt_set_dialog_title_hook;
                originalTitle = nh_dlhook(
                    handle,
                    kSetDialogTitleSymbol,
                    titleReplacement.pointer);
                if (pointerMatchesVma(originalTitle, kSetDialogTitleVma)) {
                    firmwareApi().setDialogTitleOriginal =
                        reinterpret_cast<SetDialogTitle>(originalTitle);
                }
            }

            void* const nickelHandle = dlopen(
                "libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
            void* originalLoadCover = nullptr;
            if (nickelHandle) {
                VolumeLoadCover resolvedLoadCover = nullptr;
                bool const gridSymbols =
                    resolvePinned(nickelHandle, kContentGetIdSymbol,
                        kContentGetIdVma, &firmwareApi().contentGetId)
                    && resolvePinned(nickelHandle, kContentGetImageIdSymbol,
                        kContentGetImageIdVma, &firmwareApi().contentGetImageId)
                    && resolvePinned(nickelHandle, kPixmapSetImageSymbol,
                        kPixmapSetImageVma, &firmwareApi().pixmapSetImage)
                    && resolvePinned(nickelHandle, kVolumeLoadCoverSymbol,
                        kVolumeLoadCoverVma, &resolvedLoadCover);
                if (gridSymbols) {
                    firmwareApi().volumeLoadCoverOriginal = resolvedLoadCover;
                    VolumeLoadCoverAddress loadCoverReplacement;
                    loadCoverReplacement.function = _cnt_volume_load_cover_hook;
                    originalLoadCover = nh_dlhook(
                        nickelHandle,
                        kVolumeLoadCoverSymbol,
                        loadCoverReplacement.pointer);
                    if (pointerMatchesVma(
                            originalLoadCover, kVolumeLoadCoverVma)) {
                        firmwareApi().volumeLoadCoverOriginal =
                            reinterpret_cast<VolumeLoadCover>(originalLoadCover);
                    }
                    if (pointerMatchesVma(
                            originalLoadCover, kVolumeLoadCoverVma)) {
                        coverState().gridHookReady = true;
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
                    && coverState().parserHookReady) {
                coverState().hooksReady = true;
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
    hookState().stopTimer();
}

static int initialize() {
    publishPluginState(new PluginState());
    trace("NickelHook init entered");
    cnt::notebook_sleep::installHooks(pluginState().notebookSleep);
    cnt::visibility_hooks::install();
    cnt::visibility::hideLegacyNotebookBackups(coverBackupRoot());
    hookState().timer = new QTimer(QCoreApplication::instance());
    hookState().timer->setInterval(100);
    QObject::connect(hookState().timer, &QTimer::timeout, installHookAfterNaturalLoad);
    hookState().timer->start();
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
