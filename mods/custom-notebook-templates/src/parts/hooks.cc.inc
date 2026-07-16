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
    if (!firmwareApi().toolMenuConstructorOriginal)
        return;

    bool const exactEraserMenu = eraserState().sizeMenuHooksReady
        && controller
        && tools
        && brushSections
        && tools->size() == 2
        && tools->at(0) == 1
        && tools->at(1) == 2
        && brushSections->isEmpty();
    if (!exactEraserMenu) {
        firmwareApi().toolMenuConstructorOriginal(
            controller, parent, tools, brushSections, themeStorage);
        return;
    }

    // IInkToolBrush 0 is Kobo's stock five-button size row. The constructor
    // copies this QVector into controller+0x24, so the local lifetime ends
    // safely when the original returns.
    QVector<int> eraserSections;
    eraserSections.append(0);
    firmwareApi().toolMenuConstructorOriginal(
        controller, parent, tools, &eraserSections, themeStorage);

    try {
        QObject* const object = reinterpret_cast<QObject*>(controller);
        object->setProperty("_cnt_eraser_size_controller", true);
        eraserState().liveSizeController = object;
        int const index = settingsStore().configuredEraserSizeIndex();
        // Seed before onToolButtonTapped invokes virtual loadView immediately
        // after this constructor returns. The empty button vector is handled
        // by stock setBrushSizeIndex.
        if (firmwareApi().setBrushSizeIndexOriginal && validEraserSizeIndex(index))
            firmwareApi().setBrushSizeIndexOriginal(controller, index);
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
    if (!firmwareApi().createBrushSizeRowOriginal)
        return;
    bool marked = false;
    if (eraserState().sizeMenuHooksReady && controller) {
        marked = reinterpret_cast<QObject*>(controller)
            ->property("_cnt_eraser_size_controller").toBool();
    }
    if (!marked || !menu) {
        firmwareApi().createBrushSizeRowOriginal(controller, menu, title);
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
    firmwareApi().createBrushSizeRowOriginal(controller, menu, eraserTitle);
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
    if (!firmwareApi().setBrushSizeIndexOriginal)
        return;
    // Preserve every stock theme/button update first, including unknown future
    // callers. Only the two pinned firmware callsites below have plugin work.
    firmwareApi().setBrushSizeIndexOriginal(controller, index);
    if (!eraserState().sizeMenuHooksReady || !controller
            || !validEraserSizeIndex(index)
            || !reinterpret_cast<QObject*>(controller)
                ->property("_cnt_eraser_size_controller").toBool()
            || !firmwareApi().iinknoteBase) {
        return;
    }

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    bool const clicked =
        caller == firmwareApi().iinknoteBase + kSetBrushSizeIndexClickReturnVma;
    bool const loaded =
        caller == firmwareApi().iinknoteBase + kSetBrushSizeIndexLoadReturnVma;
    if (!clicked && !loaded)
        return;
    if (clicked)
        settingsStore().persistEraserSizeIndex(index);

    QObject* const widget = cnt::notebook_widget::findNotebookWidget(
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
    if (!firmwareApi().setActiveToolOriginal)
        return;

    uintptr_t const caller = reinterpret_cast<uintptr_t>(
        __builtin_return_address(0)) & ~uintptr_t(1);
    firmwareApi().setActiveToolOriginal(widget, tool);

    // stylusTouchBegin switches the engine to its saved eraser tool without
    // visiting IInkToolMenuController, and firmware resets the live radius in
    // that path. The user's configured size remains authoritative: replay it
    // after stock activation, then mirror the same index into any open popup.
    // Nothing here persists; only an explicit five-button tap writes JSON.
    if (!eraserState().sizeMenuHooksReady || !widget || !firmwareApi().iinknoteBase
            || caller != firmwareApi().iinknoteBase
                + kHardwareEraserSetActiveToolReturnVma
            || (tool != 1 && tool != 2)
            || !cnt::notebook_widget::isNotebookWidget(
                reinterpret_cast<QObject*>(widget))) {
        return;
    }

    try {
        int const configuredIndex = settingsStore().configuredEraserSizeIndex();
        bool const engineUpdated = applyConfiguredEraserSizeForWidget(
            reinterpret_cast<QObject*>(widget), "hardware-eraser");
        bool uiUpdated = false;
        QObject* const controller = eraserState().liveSizeController.data();
        if (engineUpdated && controller && firmwareApi().setBrushSizeIndexOriginal
                && cnt::notebook_widget::findNotebookWidget(controller)
                    == reinterpret_cast<QObject*>(widget)
                && controller->property(
                    "_cnt_eraser_size_controller").toBool()) {
            // The stock setter updates controller+0x48 and all five native
            // BrushButtons but emits no toolSelected signal, so this cannot
            // recurse into setToolTheme or mutate the engine a second time.
            firmwareApi().setBrushSizeIndexOriginal(controller, configuredIndex);
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
