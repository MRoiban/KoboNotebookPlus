#line 6460 "src/customnotebooktemplates.cc"

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
    QObject* const widgetObject =
        cnt::notebook_widget::findNotebookWidget(controller);
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
    if (!cnt::layers_state::loadLayerContext(
            firmwareApi(),
            layerState(),
            controller,
            kMaximumNotebookLayers,
            &context,
            &error)) {
        QObject* const widgetObject =
            cnt::notebook_widget::findNotebookWidget(controller);
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
    if (!anchor || !firmwareApi().nickelTouchMenuConstructor || !firmwareApi().nickelTouchMenuSetAlignment
            || !firmwareApi().touchMenuSetCustomPopupPositionOffset) {
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
        firmwareApi().nickelTouchMenuConstructor(storage, anchor, 0);
        constructed = true;
        firmwareApi().nickelTouchMenuSetAlignment(
            storage, Qt::AlignHCenter | Qt::AlignBottom);
        QPoint const offset(0, 14);
        firmwareApi().touchMenuSetCustomPopupPositionOffset(storage, offset);
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
    if (!controller || !popup || !firmwareApi().iInkToolMenuWidgetConstructor
            || !firmwareApi().iInkToolMenuWidgetSetSelected
            || !firmwareApi().abstractMenuControllerGrabTapGesture) {
        if (error)
            *error = QLatin1String("Layers unavailable: native row API is missing.");
        return nullptr;
    }

    void* storage = nullptr;
    bool constructed = false;
    try {
        storage = ::operator new(kIInkToolMenuWidgetSize);
        firmwareApi().iInkToolMenuWidgetConstructor(storage, popup);
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
        firmwareApi().iInkToolMenuWidgetSetSelected(storage, active);
        button->setAlignment(Qt::AlignCenter);
        button->setPixmap(preview);
        label->setText(text);

        // IInkToolMenuWidget inherits GestureReceiver through its secondary
        // base at +0x44 on this exact firmware. Stock loadView registers that
        // adjusted pointer, not the QWidget address, before creating the
        // QAction. The ABI verifier pins both the adjustment and call site.
        firmwareApi().abstractMenuControllerGrabTapGesture(
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
    if (!cnt::layers_state::loadLayerContext(
            firmwareApi(),
            layerState(),
            controller,
            kMaximumNotebookLayers,
            &context,
            &error)) {
        QObject* const widgetObject =
            cnt::notebook_widget::findNotebookWidget(controller);
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
    if (!parent || !firmwareApi().createIInkMenuItem || !firmwareApi().addWidgetActionOriginal
            || !firmwareApi().abstractNickelMenuControllerPopupFromWidget
            || !firmwareApi().nickelTouchMenuPopupPosition) {
        showLayerError(
            context.widget,
            QLatin1String("Layers unavailable: native menu actions are missing."));
        return;
    }

    // IInkMenuController is opened from the notebook toolbar's overflow
    // button. Recover that exact stock anchor instead of guessing a screen
    // position from the notebook rectangle.
    QWidget* const anchor = firmwareApi().abstractNickelMenuControllerPopupFromWidget(
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
        firmwareApi().addWidgetActionOriginal(
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

    QString const baseId =
        cnt::layers_state::nativeDocumentLayerId(firmwareApi());
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
    QWidget* const add = firmwareApi().createIInkMenuItem(
        controller, popup, QLatin1String("Add layer"), noIcon, false);
    QWidget* const remove = firmwareApi().createIInkMenuItem(
        controller, popup, QLatin1String("Delete active layer"), noIcon, false);
    QWidget* const refresh = firmwareApi().createIInkMenuItem(
        controller,
        popup,
        QLatin1String("Refresh layer previews"),
        noIcon,
        false);
    if (!add || !remove || !refresh) {
        menuComplete = false;
    } else {
        firmwareApi().addWidgetActionOriginal(
            controller,
            popup,
            add,
            commands,
            SLOT(addLayer()),
            true,
            context.state.customLayers.size() + 1 < kMaximumNotebookLayers,
            false);
        firmwareApi().addWidgetActionOriginal(
            controller,
            popup,
            remove,
            commands,
            SLOT(deleteLayer()),
            true,
            !baseActive,
            false);
        firmwareApi().addWidgetActionOriginal(
            controller,
            popup,
            refresh,
            commands,
            SLOT(refreshPreviews()),
            true,
            layerState().previewApisReady,
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
    QPoint const position = firmwareApi().nickelTouchMenuPopupPosition(popup, anchor);
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

static void runPageOperation(
        QObject* controller,
        cnt::page_actions::PageOperation operation) {
    if (!controller)
        return;
    QObject* const widgetObject =
        cnt::notebook_widget::findNotebookWidget(controller);
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

    cnt::page_actions::runForWidget(
        firmwareApi(),
        coverState(),
        widget,
        operation,
        kMaximumNotebookPages,
        kPageBackupRoot);
}
