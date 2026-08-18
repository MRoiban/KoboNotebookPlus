#include "page_actions.h"

#include "cover_cache.h"
#include "export_range.h"
#include "firmware_api.h"
#include "notebook_widget.h"
#include "pages.h"
#include "settings.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <exception>

namespace cnt {
namespace page_actions {
namespace {

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

void showOperationError(
        FirmwareApi& firmware,
        void* widget,
        QString const& error) {
    trace("pages: operation failed safely");
    if (firmware.showErrorPopup)
        firmware.showErrorPopup(widget, error);
}

} // namespace

void runForController(
        Dependencies dependencies,
        QObject* controller,
        PageOperation operation) {
    if (!controller || !dependencies.firmware || !dependencies.coverCache
            || !dependencies.backupRoot) {
        return;
    }
    QObject* const widgetObject = notebook_widget::findNotebookWidget(controller);
    if (!widgetObject)
        return;
    if (widgetObject->property("_cnt_page_operation_active").toBool())
        return;

    PageOperationActiveGuard const activeGuard(widgetObject);
    runForWidget(
        *dependencies.firmware,
        *dependencies.coverCache,
        widgetObject,
        operation,
        dependencies.maximumNotebookPages,
        dependencies.backupRoot);
}

void runMoveToForController(
        Dependencies dependencies,
        QObject* controller) {
    if (!controller || !dependencies.firmware || !dependencies.coverCache
            || !dependencies.backupRoot) {
        return;
    }
    QObject* const widgetObject = notebook_widget::findNotebookWidget(controller);
    if (!widgetObject)
        return;
    if (widgetObject->property("_cnt_page_operation_active").toBool())
        return;

    PageOperationActiveGuard const activeGuard(widgetObject);
    FirmwareApi& firmware = *dependencies.firmware;
    QString error;
    try {
        firmware.widgetSave(widgetObject);
        pages::PageContext context;
        if (!pages::loadPageContext(
                firmware,
                widgetObject,
                dependencies.maximumNotebookPages,
                &context,
                &error)) {
            showOperationError(firmware, widgetObject, error);
            return;
        }
        if (context.count == 1) {
            if (firmware.showErrorPopup) {
                firmware.showErrorPopup(
                    widgetObject,
                    QLatin1String("This notebook has only one page."));
            }
            return;
        }

        QWidget* const anchor =
            firmware.abstractNickelMenuControllerPopupFromWidget
                ? firmware.abstractNickelMenuControllerPopupFromWidget(controller)
                : nullptr;
        int selectedPage = 0;
        bool const selected = export_range::choosePage(
            firmware,
            controller,
            anchor,
            QLatin1String("Move to"),
            1,
            context.count,
            &selectedPage,
            &error);
        if (!selected) {
            if (!error.isEmpty())
                showOperationError(firmware, widgetObject, error);
            else
                trace("pages: move-to selection cancelled");
            return;
        }

        int const target = selectedPage - 1;
        if (target == context.index) {
            trace("pages: move-to selected the current position");
            return;
        }
        if (!pages::reorderCurrentPage(
                firmware,
                *dependencies.coverCache,
                widgetObject,
                target,
                dependencies.maximumNotebookPages,
                dependencies.backupRoot,
                &error)) {
            showOperationError(firmware, widgetObject, error);
        }
    } catch (std::exception const&) {
        showOperationError(
            firmware,
            widgetObject,
            QLatin1String("Page move failed: the notebook engine rejected it."));
    } catch (...) {
        showOperationError(
            firmware,
            widgetObject,
            QLatin1String("Page move failed: unexpected notebook error."));
    }
}

void runForWidget(
        FirmwareApi& firmware,
        cover_cache::State& coverCache,
        void* widget,
        PageOperation operation,
        int maximumNotebookPages,
        char const* backupRoot) {
    QString error;
    try {
        bool succeeded = false;
        if (operation == DuplicatePageOperation) {
            succeeded = pages::duplicateCurrentPage(
                firmware,
                coverCache,
                widget,
                maximumNotebookPages,
                backupRoot,
                &error);
        } else {
            firmware.widgetSave(widget);
            pages::PageContext context;
            if (!pages::loadPageContext(
                    firmware,
                    widget,
                    maximumNotebookPages,
                    &context,
                    &error)) {
                showOperationError(firmware, widget, error);
                return;
            }

            int const target = context.index
                + (operation == MovePageEarlierOperation ? -1 : 1);
            if (target < 0 || target >= context.count) {
                if (firmware.showErrorPopup) {
                    firmware.showErrorPopup(
                        widget,
                        operation == MovePageEarlierOperation
                            ? QLatin1String("This is already the first page.")
                            : QLatin1String("This is already the last page."));
                }
                return;
            }
            succeeded = pages::reorderCurrentPage(
                firmware,
                coverCache,
                widget,
                target,
                maximumNotebookPages,
                backupRoot,
                &error);
        }

        if (!succeeded)
            showOperationError(firmware, widget, error);
    } catch (std::exception const&) {
        showOperationError(
            firmware,
            widget,
            QLatin1String("Page operation failed: the notebook engine rejected it."));
    } catch (...) {
        showOperationError(
            firmware,
            widget,
            QLatin1String("Page operation failed: unexpected notebook error."));
    }
}

} // namespace page_actions
} // namespace cnt
