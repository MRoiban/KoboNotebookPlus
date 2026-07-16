#line 7283 "src/customnotebooktemplates.cc"

static void installHookAfterNaturalLoad() {
    void* const handle = dlopen("libiinknote.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!handle)
        return;

    trace("libiinknote observed after natural load");
    BackgroundOptions resolvedBackgroundOptions = nullptr;
    if (!resolvePinned(handle, kBackgroundOptionsSymbol,
            kBackgroundOptionsVma, &resolvedBackgroundOptions)) {
        trace("delayed backgroundOptions symbol validation failed");
        stopHookTimer();
        return;
    }
    firmwareApi().backgroundOptionsOriginal = resolvedBackgroundOptions;

    if (!cnt::templates::locateRendererMap(
            nh_symptr(firmwareApi().backgroundOptionsOriginal),
            firmwareApi().rendererMap,
            firmwareApi().iinknoteBase)) {
        stopHookTimer();
        return;
    }

    // A malformed user manifest is non-fatal: Nickel continues with every
    // built-in template and no custom rows.
    if (!cnt::templates::loadManifest(
            *firmwareApi().rendererMap, templateState()))
        nh_log("custom templates disabled because the manifest is invalid");
    cnt::templates::loadAutomaticTemplates(
        *firmwareApi().rendererMap, templateState());
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
        stopHookTimer();
        return;
    }
    firmwareApi().backgroundOptionsOriginal = reinterpret_cast<BackgroundOptions>(original);
    trace("delayed backgroundOptions hook installed");

    if (resolveCoverApis(handle)) {
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
            pageState().hooksReady = true;
            trace("pages: guarded notebook-menu hook installed");
        } else {
            layerState().hooksReady = false;
            trace("pages: notebook-menu hook validation failed");
            trace("layers: native action hook unavailable; feature disabled");
        }

        if (layerState().hooksReady || eraserState().sizeApisReady) {
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
                trace("layers: active-layer lifecycle hooks installed");
                trace("eraser-size: notebook lifecycle reapply hooks installed");
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
                && !installEraserSizeMenuHooks(handle)) {
            eraserState().sizeApisReady = false;
            trace("eraser-size: popup augmentation unavailable; feature disabled");
        }

        if (layerState().hooksReady
                && !cnt::cover_cache::loadZipApis(coverState())) {
            trace("layers: read-only archive persistence probe unavailable");
        }

        if (!coverState().customCovers.isEmpty()
                && cnt::cover_cache::loadZipApis(coverState())) {
            SetDialogTitle resolvedTitle = nullptr;
            ParserImageParsed resolvedImageParsed = nullptr;
            bool const coverHookSymbols =
                resolvePinned(handle, kSetDialogTitleSymbol,
                    kSetDialogTitleVma, &resolvedTitle)
                && resolvePinned(handle, kParserImageParsedSymbol,
                    kParserImageParsedVma, &resolvedImageParsed);
            void* originalTitle = nullptr;
            void* originalImageParsed = nullptr;
            if (coverHookSymbols) {
                firmwareApi().setDialogTitleOriginal = resolvedTitle;
                firmwareApi().parserImageParsedOriginal = resolvedImageParsed;

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
                    && pointerMatchesVma(
                        originalImageParsed, kParserImageParsedVma)) {
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
    stopHookTimer();
}

static int initialize() {
    if (!gPluginState)
        gPluginState = new PluginState();
    trace("NickelHook init entered");
    installCustomAssetVisibilityHooks();
    hideLegacyNotebookBackups();
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
