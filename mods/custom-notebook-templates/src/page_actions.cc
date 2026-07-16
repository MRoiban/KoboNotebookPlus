#include "page_actions.h"

#include "cover_cache.h"
#include "firmware_api.h"
#include "notebook_widget.h"
#include "pages.h"
#include "settings.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

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
