#include "page_actions.h"

#include "cover_cache.h"
#include "firmware_api.h"
#include "pages.h"
#include "settings.h"

#include <QString>

#include <exception>

namespace cnt {
namespace page_actions {
namespace {

void showOperationError(
        FirmwareApi& firmware,
        void* widget,
        QString const& error) {
    trace("pages: operation failed safely");
    if (firmware.showErrorPopup)
        firmware.showErrorPopup(widget, error);
}

} // namespace

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
