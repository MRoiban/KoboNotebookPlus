#line 7283 "src/customnotebooktemplates.cc"

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
