#include "layers_menu.h"

#include "covermenureceiver.h"
#include "firmware_api.h"
#include "layers_state.h"
#include "notebook_widget.h"
#include "settings.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QPixmap>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include <exception>
#include <new>

namespace cnt {
namespace layers_menu {
namespace {

using layers::ActivateLayerOperation;
using layers::AddLayerOperation;
using layers::DeleteActiveLayerOperation;
using layers::LayerContext;
using layers::LayerOperation;
using layers::LayerRecord;
using layers::RefreshLayerPreviewsOperation;

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

void runLayerOperation(
        Dependencies dependencies,
        QObject* controller,
        LayerOperation operation,
        QString const& id = QString()) {
    if (!controller || !dependencies.preview.firmware
            || !dependencies.preview.runtime
            || !dependencies.preview.coverCache) {
        return;
    }
    FirmwareApi& firmware = *dependencies.preview.firmware;
    QObject* const widgetObject =
        notebook_widget::findNotebookWidget(controller);
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
    if (!layers_state::loadLayerContext(
            firmware,
            *dependencies.preview.runtime,
            controller,
            dependencies.preview.pins.maximumNotebookLayers,
            &context,
            &error)) {
        QObject* const contextWidget =
            notebook_widget::findNotebookWidget(controller);
        layers_preview::showLayerError(
            dependencies.preview, contextWidget, error);
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
        succeeded = layers_preview::performLayerOperation(
            dependencies.preview,
            &context,
            operation,
            id,
            &error);
    } catch (std::exception const&) {
        error = QLatin1String(
            "Layer operation failed: notebook engine rejected it.");
    } catch (...) {
        error = QLatin1String(
            "Layer operation failed: unexpected notebook error.");
    }
    if (!succeeded) {
        layers_preview::showLayerError(
            dependencies.preview, context.widget, error);
    }
}

QMenu* createNativeLayerPopup(
        Dependencies dependencies,
        QWidget* anchor,
        QString* error) {
    FirmwareApi& firmware = *dependencies.preview.firmware;
    if (!anchor || !firmware.nickelTouchMenuConstructor
            || !firmware.nickelTouchMenuSetAlignment
            || !firmware.touchMenuSetCustomPopupPositionOffset) {
        if (error)
            *error = QLatin1String("Layers unavailable: native menu API is missing.");
        return nullptr;
    }

    void* storage = nullptr;
    bool constructed = false;
    try {
        storage = ::operator new(dependencies.pins.nickelTouchMenuBytes);
        // Exact constructor arguments recovered from
        // IInkToolMenuController::loadView on firmware 4.38.23697.
        // Stock loadView parents the menu to popupFromWidget(), which is the
        // toolbar button used to open the controller. That relationship is
        // also consumed by NickelTouchMenu's decoration/placement code.
        firmware.nickelTouchMenuConstructor(storage, anchor, 0);
        constructed = true;
        firmware.nickelTouchMenuSetAlignment(
            storage, Qt::AlignHCenter | Qt::AlignBottom);
        QPoint const offset(0, 14);
        firmware.touchMenuSetCustomPopupPositionOffset(storage, offset);
        QMenu* const popup = reinterpret_cast<QMenu*>(storage);
        popup->setAttribute(static_cast<Qt::WidgetAttribute>(55), true);
        return popup;
    } catch (...) {
        if (constructed)
            delete reinterpret_cast<QMenu*>(storage);
        else if (storage)
            ::operator delete(storage);
        if (error) {
            *error = QLatin1String(
                "Layers unavailable: native menu creation failed.");
        }
        return nullptr;
    }
}

QWidget* createNativeLayerToolRow(
        Dependencies dependencies,
        void* controller,
        QMenu* popup,
        QString const& text,
        QPixmap const& preview,
        bool active,
        QString* error) {
    FirmwareApi& firmware = *dependencies.preview.firmware;
    if (!controller || !popup || !firmware.iInkToolMenuWidgetConstructor
            || !firmware.iInkToolMenuWidgetSetSelected
            || !firmware.abstractMenuControllerGrabTapGesture) {
        if (error)
            *error = QLatin1String("Layers unavailable: native row API is missing.");
        return nullptr;
    }

    void* storage = nullptr;
    bool constructed = false;
    try {
        storage = ::operator new(dependencies.pins.toolRowBytes);
        firmware.iInkToolMenuWidgetConstructor(storage, popup);
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
        firmware.iInkToolMenuWidgetSetSelected(storage, active);
        button->setAlignment(Qt::AlignCenter);
        button->setPixmap(preview);
        label->setText(text);

        // IInkToolMenuWidget inherits GestureReceiver through its secondary
        // base at +0x44 on this exact firmware. Stock loadView registers that
        // adjusted pointer, not the QWidget address, before creating the
        // QAction. The ABI verifier pins both the adjustment and call site.
        firmware.abstractMenuControllerGrabTapGesture(
            controller,
            reinterpret_cast<char*>(storage)
                + dependencies.pins.rowGestureReceiverOffset);
        return row;
    } catch (...) {
        if (constructed)
            delete reinterpret_cast<QWidget*>(storage);
        else if (storage)
            ::operator delete(storage);
        if (error) {
            *error = QLatin1String(
                "Layers unavailable: native row creation failed.");
        }
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
    ControllerMenuViewGuard(
            QObject* controller,
            QMenu* menu,
            uintptr_t controllerViewOffset)
        : controller_(controller), original_(), view_(nullptr), active_(false) {
        static_assert(sizeof(QPointer<QObject>) == sizeof(void*) * 2,
            "unexpected Qt 5 QPointer ABI");
        if (!controller_ || !menu)
            return;
        view_ = reinterpret_cast<QPointer<QObject>*>(
            reinterpret_cast<char*>(controller) + controllerViewOffset);
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

} // namespace

void showPopup(Dependencies dependencies, QObject* controller) {
    if (!controller || !dependencies.preview.firmware
            || !dependencies.preview.runtime
            || !dependencies.preview.coverCache) {
        return;
    }
    FirmwareApi& firmware = *dependencies.preview.firmware;
    QElapsedTimer popupOpenTimer;
    popupOpenTimer.start();
    LayerContext context;
    QString error;
    if (!layers_state::loadLayerContext(
            firmware,
            *dependencies.preview.runtime,
            controller,
            dependencies.preview.pins.maximumNotebookLayers,
            &context,
            &error)) {
        QObject* const widgetObject =
            notebook_widget::findNotebookWidget(controller);
        layers_preview::showLayerError(
            dependencies.preview, widgetObject, error);
        return;
    }
    // A reopened notebook can restore the sidecar before any tool-theme
    // callback occurs. Synchronize first so the row marked "active" always
    // matches the concrete pen/eraser restriction used by the next stroke.
    if (!layers_service::synchronizeSavedActiveLayer(
            firmware,
            dependencies.preview.pins.neboBackendPageControllerOffset,
            dependencies.preview.toolRoutingOperations,
            context,
            "popup-open",
            &error)) {
        layers_preview::showLayerError(
            dependencies.preview, context.widget, error);
        return;
    }
    QWidget* const parent = qobject_cast<QWidget*>(context.widgetObject.data());
    if (!parent || !firmware.createIInkMenuItem
            || !firmware.addWidgetActionOriginal
            || !firmware.abstractNickelMenuControllerPopupFromWidget
            || !firmware.nickelTouchMenuPopupPosition) {
        layers_preview::showLayerError(
            dependencies.preview,
            context.widget,
            QLatin1String("Layers unavailable: native menu actions are missing."));
        return;
    }

    // IInkMenuController is opened from the notebook toolbar's overflow
    // button. Recover that exact stock anchor instead of guessing a screen
    // position from the notebook rectangle.
    QWidget* const anchor =
        firmware.abstractNickelMenuControllerPopupFromWidget(controller);
    if (!anchor) {
        layers_preview::showLayerError(
            dependencies.preview,
            context.widget,
            QLatin1String("Layers unavailable: toolbar anchor was not found."));
        return;
    }

    QMenu* const popup = createNativeLayerPopup(dependencies, anchor, &error);
    if (!popup) {
        layers_preview::showLayerError(
            dependencies.preview, context.widget, error);
        return;
    }

    bool menuComplete = true;
    QVector<layers_preview::DeferredLayerPreviewRow> deferredPreviewRows;
    auto addLayerRow = [&](QString const& id,
                           QString const& name,
                           bool active,
                           bool separatorAfter) {
        bool cacheDrawn = false;
        QPixmap const preview = layers_preview::layerPreview(
            dependencies.preview,
            context.state,
            id,
            name,
            active,
            &cacheDrawn);
        bool const previewPending = !cacheDrawn
            || layers_preview::layerPreviewNeedsRefresh(
                dependencies.preview, context.state, id);
        QString const label = layers_preview::layerPopupRowLabel(
            name, active, previewPending);
        LayerMenuReceiver* const receiver = new LayerMenuReceiver(
            controller, id, popup);
        QObject::connect(
            receiver,
            &LayerMenuReceiver::activateRequested,
            [dependencies](QObject* target, QString const& layerId) {
                runLayerOperation(
                    dependencies,
                    target,
                    ActivateLayerOperation,
                    layerId);
            });
        QWidget* const item = createNativeLayerToolRow(
            dependencies,
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
            layers_preview::DeferredLayerPreviewRow row;
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
        firmware.addWidgetActionOriginal(
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

    QString const baseId = layers_state::nativeDocumentLayerId(firmware);
    bool const baseActive = context.state.activeId == baseId;
    addLayerRow(baseId, QLatin1String("Layer 1"), baseActive, true);

    LayerMenuReceiver* const commands = new LayerMenuReceiver(
        controller, QString(), popup);
    QObject::connect(
        commands,
        &LayerMenuReceiver::addRequested,
        [dependencies](QObject* target) {
            runLayerOperation(dependencies, target, AddLayerOperation);
        });
    QObject::connect(
        commands,
        &LayerMenuReceiver::deleteRequested,
        [dependencies](QObject* target) {
            runLayerOperation(
                dependencies, target, DeleteActiveLayerOperation);
        });
    QObject::connect(
        commands,
        &LayerMenuReceiver::refreshRequested,
        [dependencies](QObject* target) {
            runLayerOperation(
                dependencies, target, RefreshLayerPreviewsOperation);
        });

    QPixmap noIcon;
    QWidget* const add = firmware.createIInkMenuItem(
        controller, popup, QLatin1String("Add layer"), noIcon, false);
    QWidget* const remove = firmware.createIInkMenuItem(
        controller,
        popup,
        QLatin1String("Delete active layer"),
        noIcon,
        false);
    QWidget* const refresh = firmware.createIInkMenuItem(
        controller,
        popup,
        QLatin1String("Refresh layer previews"),
        noIcon,
        false);
    if (!add || !remove || !refresh) {
        menuComplete = false;
    } else {
        firmware.addWidgetActionOriginal(
            controller,
            popup,
            add,
            commands,
            SLOT(addLayer()),
            true,
            context.state.customLayers.size() + 1
                < dependencies.preview.pins.maximumNotebookLayers,
            false);
        firmware.addWidgetActionOriginal(
            controller,
            popup,
            remove,
            commands,
            SLOT(deleteLayer()),
            true,
            !baseActive,
            false);
        firmware.addWidgetActionOriginal(
            controller,
            popup,
            refresh,
            commands,
            SLOT(refreshPreviews()),
            true,
            dependencies.preview.runtime->previewApisReady,
            true);
    }

    if (!menuComplete) {
        delete popup;
        layers_preview::showLayerError(
            dependencies.preview,
            context.widget,
            QLatin1String(
                "Layers unavailable: native menu row creation failed."));
        return;
    }

    trace(QLatin1String("layers: native popup ready ms=")
        + QString::number(popupOpenTimer.elapsed())
        + QLatin1String(" deferred=")
        + QString::number(deferredPreviewRows.size()));
    trace("layers: native tool-style NickelTouchMenu popup opened");
    popup->ensurePolished();
    QPoint const position = firmware.nickelTouchMenuPopupPosition(popup, anchor);
    trace("layers: native popup anchored to notebook toolbar");
    ControllerMenuViewGuard const activeMenu(
        controller,
        popup,
        dependencies.pins.controllerViewOffset);
    // Stock uses QMenu::popup(position). Keep exec(position) here because the
    // injected rows still delegate taps to the existing overflow controller:
    // the nested loop keeps that controller alive until this menu closes.
    // Geometry and the triangular decoration are nevertheless computed by
    // the same NickelTouchMenu::popupPosition(anchor) path as pen/eraser.
    // WA_DeleteOnClose (55), matching the stock popup, owns teardown.
    layers_preview::startDeferredLayerPreviewRefresh(
        dependencies.preview,
        controller,
        context,
        popup,
        deferredPreviewRows);
    popup->exec(position);
}

} // namespace layers_menu
} // namespace cnt
