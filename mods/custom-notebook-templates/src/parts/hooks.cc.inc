#line 7501 "src/customnotebooktemplates.cc"
NickelHook(initialize, &info, nullptr, nullptr, nullptr)

extern "C" __attribute__((visibility("default")))
void _cnt_remove_common_book_data_hook(
    void* manager,
    void const* device,
    void* volume,
    bool removeBackingFile) {
    if (!firmwareApi().removeCommonBookDataOriginal)
        return;

    if (removeBackingFile && firmwareApi().contentGetId && volume) {
        QString const id = firmwareApi().contentGetId(volume);
        cnt::visibility::applyBackingFilePolicy(
            visibilityState(), removeBackingFile, id);
    }

    firmwareApi().removeCommonBookDataOriginal(
        manager, device, volume, removeBackingFile);
}

extern "C" __attribute__((visibility("default")))
QString _cnt_exclude_sync_folders_hook(void* featureSettings) {
    if (!firmwareApi().excludeSyncFoldersOriginal)
        return QString();

    QString exclusions = firmwareApi().excludeSyncFoldersOriginal(featureSettings);
    cnt::visibility::applySyncExclusion(visibilityState(), exclusions);
    return exclusions;
}

extern "C" __attribute__((visibility("default")))
QVector<BackgroundOption> _cnt_background_options_hook() {
    trace("backgroundOptions hook invoked");
    QVector<BackgroundOption> options = firmwareApi().backgroundOptionsOriginal();
    cnt::notebook_hook_services::routeBackgroundOptions(
        templateState(), coverState(), options);
    return options;
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_dialog_title_hook(void* dialog, QString const& title) {
    if (!firmwareApi().setDialogTitleOriginal)
        return;
    cnt::notebook_hook_services::routeCoverDialogTitle(
        firmwareApi(), coverState(), dialog, title);
}

extern "C" __attribute__((visibility("default")))
void _cnt_parser_image_parsed_hook(
    void* parser,
    void const* volume,
    QImage const& image) {
    if (!firmwareApi().parserImageParsedOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    cnt::notebook_hook_services::routeParserImage(
        firmwareApi(),
        coverState(),
        caller,
        kThumbnailCallbackReturnVma,
        parser,
        volume,
        image);
}

extern "C" __attribute__((visibility("default")))
void _cnt_volume_load_cover_hook(void* view) {
    if (!firmwareApi().volumeLoadCoverOriginal)
        return;

    // Preserve Kobo's normal cache/download/default-cover behavior first.
    firmwareApi().volumeLoadCoverOriginal(view);
    cnt::notebook_hook_services::augmentNotebookGridCover(
        firmwareApi(), coverState(), view, kVolumeInPixmapViewOffset);
}

extern "C" __attribute__((visibility("default")))
void _cnt_tool_menu_constructor_hook(
    void* controller,
    QWidget* parent,
    QVector<int> const* tools,
    QVector<int> const* brushSections,
    void* themeStorage) {
    cnt::eraser_menu::constructController(
        firmwareApi(),
        eraserState(),
        settingsStore(),
        controller,
        parent,
        tools,
        brushSections,
        themeStorage);
}

extern "C" __attribute__((visibility("default")))
void _cnt_create_brush_size_row_hook(
    void* controller,
    NickelTouchMenu* menu,
    QString const& title) {
    cnt::eraser_menu::createBrushSizeRow(
        firmwareApi(), eraserState(), controller, menu, title);
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_brush_size_index_hook(void* controller, int index) {
    if (!firmwareApi().setBrushSizeIndexOriginal)
        return;
    // Preserve every stock theme/button update first, including unknown future
    // callers. Only the two pinned firmware callsites below have plugin work.
    firmwareApi().setBrushSizeIndexOriginal(controller, index);
    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    cnt::eraser_menu::afterBrushSizeIndex(
        firmwareApi(),
        eraserState(),
        settingsStore(),
        caller,
        controller,
        index,
        applyConfiguredEraserSizeForWidget);
}

extern "C" __attribute__((visibility("default")))
void _cnt_set_active_tool_hook(void* widget, int tool) {
    if (!firmwareApi().setActiveToolOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    firmwareApi().setActiveToolOriginal(widget, tool);
    cnt::eraser_menu::afterActiveTool(
        firmwareApi(),
        eraserState(),
        settingsStore(),
        caller,
        widget,
        tool,
        applyConfiguredEraserSizeForWidget);
}

extern "C" __attribute__((visibility("default")))
void _cnt_render_volume_hook(void* widget, void const* volume) {
    if (!firmwareApi().renderVolumeOriginal)
        return;

    // Stock renderVolume synchronously constructs the editor/backend/tool and,
    // on its saved-page path, installs the final part after setupVolume. The
    // caller reaches this symbol through the PLT, so running after the original
    // is the first hookable point where all three identities are coherent.
    firmwareApi().renderVolumeOriginal(widget, volume);
    if (!hookState().notebookLifecycleHooksReady || !widget)
        return;

    QObject* const object = reinterpret_cast<QObject*>(widget);
    if (!cnt::notebook_widget::isNotebookWidget(object))
        return;
    if (eraserState().sizeApisReady) {
        try {
            applyConfiguredEraserSizeForWidget(object, "notebook-open");
        } catch (...) {
            trace("eraser-size: notebook-open replay threw; stock radius preserved");
        }
    }
    if (!layerState().hooksReady)
        return;

    try {
        trace("layers: notebook render complete; restoring saved active layer");
        LayerContext context;
        QString error;
        if (!cnt::layers_state::loadLayerContext(
                firmwareApi(),
                layerState(),
                object,
                kMaximumNotebookLayers,
                &context,
                &error)) {
            trace(QLatin1String(
                "layers: notebook-open context unavailable; metadata preserved: ")
                + error);
            return;
        }
        if (!cnt::layers_service::synchronizeSavedActiveLayer(
                firmwareApi(),
                kNeboBackendPageControllerOffset,
                kLayerToolRoutingOperations,
                context,
                "notebook-open",
                &error)) {
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
    if (!firmwareApi().setToolThemeOriginal)
        return;
    firmwareApi().setToolThemeOriginal(widget, theme);
    if (!hookState().notebookLifecycleHooksReady || !widget)
        return;

    QObject* const object = reinterpret_cast<QObject*>(widget);
    if (!cnt::notebook_widget::isNotebookWidget(object))
        return;
    if (eraserState().sizeApisReady) {
        try {
            applyConfiguredEraserSizeForWidget(object, "tool-theme");
        } catch (...) {
            trace("eraser-size: tool-theme replay threw; stock radius preserved");
        }
    }
    if (!layerState().hooksReady)
        return;
    trace("layers: setToolTheme hook invoked for notebook");
    LayerContext context;
    QString error;
    if (!cnt::layers_state::loadLayerContext(
            firmwareApi(),
            layerState(),
            object,
            kMaximumNotebookLayers,
            &context,
            &error)) {
        trace(QLatin1String("layers: setToolTheme context unavailable: ") + error);
        return;
    }
    if (!cnt::layers_service::applyActiveLayer(
            firmwareApi(),
            kNeboBackendPageControllerOffset,
            kLayerToolRoutingOperations,
            context,
            context.state.activeId,
            &error)) {
        trace(QLatin1String(
            "layers: active layer could not be reapplied after tool change: ")
            + error);
        return;
    }
    if (context.state.activeId
            != cnt::layers_state::nativeDocumentLayerId(firmwareApi()))
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
    if (!firmwareApi().addWidgetActionOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    firmwareApi().addWidgetActionOriginal(
        controller, menu, widget, receiver, member,
        closesMenu, enabled, separatorAfter);

    if ((!coverState().hooksReady && !pageState().hooksReady && !layerState().hooksReady)
            || !controller
            || !menu
            || !firmwareApi().iinknoteBase
            || caller < firmwareApi().iinknoteBase + kMenuLoadViewVma
            || caller >= firmwareApi().iinknoteBase + kMenuLoadViewVma + kMenuLoadViewSize
            || menu->property("_cnt_notebook_actions_added").toBool()) {
        return;
    }

    menu->setProperty("_cnt_notebook_actions_added", true);
    QObject* const controllerObject = reinterpret_cast<QObject*>(controller);
    QPixmap noIcon;

    cnt::notebook_menu::addCoverContribution(
        firmwareApi(),
        coverState(),
        controller,
        controllerObject,
        menu,
        noIcon,
        kCoverBackupRoot);

    if (layerState().hooksReady) {
        CoverMenuReceiver* const layerReceiver = new CoverMenuReceiver(
            controllerObject, menu);
        QObject::connect(
            layerReceiver,
            &CoverMenuReceiver::layersRequested,
            [](QObject* target) {
                if (target)
                    showLayerPopup(target);
            });
        QWidget* const layerItem = firmwareApi().createIInkMenuItem(
            controller,
            menu,
            QLatin1String("Layers"),
            noIcon,
            false);
        if (layerItem) {
            firmwareApi().addWidgetActionOriginal(
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

    if (pageState().hooksReady) {
        cnt::notebook_menu::addPageContribution(
            firmwareApi(),
            controller,
            controllerObject,
            menu,
            noIcon,
            &runPageOperation);
    }
}
