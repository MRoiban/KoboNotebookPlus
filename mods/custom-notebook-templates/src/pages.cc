#include "pages.h"

#include "cover_cache.h"
#include "firmware_api.h"
#include "notebook_widget.h"
#include "page_io.h"
#include "settings.h"

#include <QString>

#include <cstring>
#include <exception>
#include <stdexcept>

namespace cnt {
namespace pages {
namespace {

struct PageStorage {
    alignas(8) unsigned char bytes[64];
    FirmwareApi& firmware;
    bool constructed;

    explicit PageStorage(FirmwareApi& firmware_)
        : firmware(firmware_), constructed(false) {
        memset(bytes, 0, sizeof(bytes));
    }

    ~PageStorage() {
        if (constructed)
            firmware.pageDestructor(bytes);
    }
};

} // namespace

SharedDocument documentForPart(
        FirmwareApi& firmware,
        SharedPart const& part) {
    PageStorage page(firmware);
    firmware.partGetPage(page.bytes, part.get());
    page.constructed = true;
    return firmware.pageDocument(page.bytes);
}

bool loadPageContext(
        FirmwareApi& firmware,
        void* widget,
        int maximumPageCount,
        PageContext* context,
        QString* error) {
    if (!widget || !context) {
        *error = QLatin1String("Page operation failed: notebook is not ready.");
        return false;
    }

    void* const editor = notebook_widget::notePadEditor(widget);
    void* const editorControl = notebook_widget::notePadEditorControl(widget);
    if (!editor || !editorControl) {
        *error = QLatin1String("Page operation failed: notebook editor is not ready.");
        return false;
    }

    SharedPart const part = firmware.editorGetPart(editor);
    SharedPackage const package = part
        ? firmware.partGetPackage(part.get())
        : SharedPackage();
    int const count = package
        ? firmware.packagePartCount(package.get())
        : 0;
    int const index = package
        ? firmware.packageIndexOfPart(package.get(), part)
        : -1;
    SharedDocument const document = part
        ? documentForPart(firmware, part)
        : SharedDocument();
    if (!part
            || !package
            || !document
            || count < 1
            || count > maximumPageCount
            || index < 0
            || index >= count
            || firmware.documentPageCount(document.get()) != count) {
        *error = QLatin1String(
            "Page operation failed: current page could not be verified.");
        return false;
    }

    context->widget = widget;
    context->editor = editor;
    context->part = part;
    context->package = package;
    context->document = document;
    context->index = index;
    context->count = count;
    return true;
}

bool reorderCurrentPage(
        FirmwareApi& firmware,
        cover_cache::State& coverCache,
        void* widget,
        int target,
        int maximumPageCount,
        char const* backupRoot,
        QString* error) {
    try {
        firmware.widgetSave(widget);
        PageContext context;
        if (!loadPageContext(
                firmware, widget, maximumPageCount, &context, error)) {
            return false;
        }
        if (target < 0 || target >= context.count) {
            *error = QLatin1String(
                "Page not reordered: destination position is invalid.");
            return false;
        }
        bool hasCover = false;
        if (!cover_cache::openNotebookHasPluginCover(
                coverCache, firmware.widgetFilePath(widget), &hasCover)) {
            *error = QLatin1String(
                "Page not reordered: the notebook cover could not be checked.");
            return false;
        }
        if (hasCover && (context.index == 0 || target == 0)) {
            *error = QLatin1String(
                "Page not reordered: the notebook cover must stay the first page.");
            return false;
        }
        if (target == context.index)
            return true;

        QString backupPath;
        if (!page_io::backupNotebookPath(
                firmware.widgetFilePath(widget),
                QLatin1String(backupRoot),
                QLatin1String("Page not reordered"),
                &backupPath,
                error)) {
            return false;
        }

        firmware.documentMovePage(
            context.document.get(), context.index, target);
        firmware.packageSave(context.package.get());
        if (firmware.packagePartCount(context.package.get()) != context.count
                || firmware.packageIndexOfPart(
                    context.package.get(), context.part) != target) {
            // Best-effort in-memory rollback. The hidden archive backup is the
            // authoritative recovery copy if this validation itself failed.
            int const actual = firmware.packageIndexOfPart(
                context.package.get(), context.part);
            if (actual >= 0 && actual < context.count) {
                firmware.documentMovePage(
                    context.document.get(), actual, context.index);
                firmware.packageSave(context.package.get());
            }
            *error = QLatin1String(
                "Page reorder could not be verified. The original notebook "
                "is preserved in the page-manager backup folder.");
            return false;
        }

        firmware.widgetSave(widget);
        cover_cache::invalidateNotebookScanEntry(
            coverCache, firmware.widgetFilePath(widget));
        firmware.editorSetPart(context.editor, context.part);
        firmware.widgetRefresh(widget);
        trace("pages: current page reordered");
        return true;
    } catch (std::exception const&) {
        *error = QLatin1String(
            "Page not reordered: the notebook engine rejected the operation.");
    } catch (...) {
        *error = QLatin1String(
            "Page not reordered: unexpected notebook error.");
    }
    trace("pages: reorder threw and was contained");
    return false;
}

bool duplicateCurrentPage(
        FirmwareApi& firmware,
        cover_cache::State& coverCache,
        void* widget,
        int maximumPageCount,
        char const* backupRoot,
        QString* error) {
    SharedPackage package;
    SharedPart clone;
    int oldCount = 0;
    bool cloneAttached = false;
    try {
        firmware.widgetSave(widget);
        PageContext context;
        if (!loadPageContext(
                firmware, widget, maximumPageCount, &context, error)) {
            return false;
        }
        bool hasCover = false;
        if (!cover_cache::openNotebookHasPluginCover(
                coverCache, firmware.widgetFilePath(widget), &hasCover)) {
            *error = QLatin1String(
                "Page not duplicated: the notebook cover could not be checked.");
            return false;
        }
        if (hasCover && context.index == 0) {
            *error = QLatin1String(
                "Page not duplicated: the notebook cover cannot be duplicated.");
            return false;
        }
        if (context.count >= maximumPageCount) {
            *error = QLatin1String(
                "Page not duplicated: notebook has too many pages.");
            return false;
        }

        QString backupPath;
        if (!page_io::backupNotebookPath(
                firmware.widgetFilePath(widget),
                QLatin1String(backupRoot),
                QLatin1String("Page not duplicated"),
                &backupPath,
                error)) {
            return false;
        }

        package = context.package;
        oldCount = context.count;
        clone = firmware.packageClonePart(package.get(), context.part);
        cloneAttached = bool(clone);
        if (!clone
                || firmware.packagePartCount(package.get()) != oldCount + 1
                || firmware.packageIndexOfPart(package.get(), clone) != oldCount) {
            *error = QLatin1String(
                "Page not duplicated: cloned page could not be verified.");
            throw std::runtime_error("clone validation failed");
        }

        SharedDocument const document = documentForPart(firmware, clone);
        int const target = context.index + 1;
        if (!document
                || firmware.documentPageCount(document.get()) != oldCount + 1) {
            *error = QLatin1String(
                "Page not duplicated: cloned document could not be verified.");
            throw std::runtime_error("clone document validation failed");
        }
        if (target != oldCount)
            firmware.documentMovePage(document.get(), oldCount, target);
        firmware.packageSave(package.get());

        if (firmware.packagePartCount(package.get()) != oldCount + 1
                || firmware.packageIndexOfPart(package.get(), clone) != target) {
            *error = QLatin1String(
                "Duplicate could not be verified. The original notebook is "
                "preserved in the page-manager backup folder.");
            throw std::runtime_error("clone commit validation failed");
        }

        firmware.widgetSave(widget);
        cover_cache::invalidateNotebookScanEntry(
            coverCache, firmware.widgetFilePath(widget));
        firmware.editorSetPart(context.editor, clone);
        firmware.widgetRefresh(widget);
        trace("pages: current page duplicated");
        return true;
    } catch (...) {
        if (package && clone && cloneAttached) {
            try {
                firmware.packageRemovePart(package.get(), clone);
                firmware.packageSave(package.get());
                if (firmware.packagePartCount(package.get()) == oldCount) {
                    trace("pages: failed duplicate rolled back");
                } else {
                    trace(
                        "pages: failed duplicate rollback also failed; backup retained");
                }
            } catch (...) {
                trace(
                    "pages: failed duplicate rollback also failed; backup retained");
            }
        }
        if (error->isEmpty()) {
            *error = QLatin1String(
                "Page not duplicated: the notebook engine rejected the operation.");
        }
        return false;
    }
}

} // namespace pages
} // namespace cnt
