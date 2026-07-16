#include "cover_editor.h"

#include "abi_types.h"
#include "cover_cache.h"
#include "firmware_api.h"
#include "notebook_widget.h"
#include "page_io.h"
#include "pages.h"
#include "settings.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <memory>

namespace cnt {
namespace cover_editor {
namespace {

struct ScopedIInkString {
    IInkStringStorage value;

    ScopedIInkString(FirmwareApi& firmware, char const* utf8) {
        value.impl = nullptr;
        firmware.iinkStringCtor(&value, utf8);
    }

    ~ScopedIInkString() {
        delete value.impl;
    }
};

bool backupNotebook(
        FirmwareApi& firmware,
        void* widget,
        char const* backupRoot,
        QString* backupPath,
        QString* error) {
    return page_io::backupNotebookPath(
        firmware.widgetFilePath(widget),
        QLatin1String(backupRoot),
        QLatin1String("Cover not changed"),
        backupPath,
        error);
}

bool applyNotebookCover(
        FirmwareApi& firmware,
        cover_cache::State& coverCache,
        void* widget,
        QString const& type,
        char const* backupRoot,
        QString* error) {
    void* const editor = notebook_widget::notePadEditor(widget);
    void* const editorControl = notebook_widget::notePadEditorControl(widget);
    if (!editor || !editorControl) {
        *error = QLatin1String("Cover not changed: notebook editor is not ready.");
        return false;
    }

    firmware.widgetSave(widget);
    SharedPart const originalPart = firmware.editorGetPart(editor);
    if (!originalPart) {
        *error = QLatin1String("Cover not changed: current page was not found.");
        return false;
    }

    SharedPackage const package = firmware.partGetPackage(originalPart.get());
    if (!package) {
        *error = QLatin1String("Cover not changed: notebook package was not found.");
        return false;
    }

    int const oldCount = firmware.packagePartCount(package.get());
    if (oldCount < 1 || oldCount > 4096) {
        *error = QLatin1String("Cover not changed: notebook page count is invalid.");
        return false;
    }

    SharedPart const firstPart = firmware.packageGetPart(package.get(), 0);
    if (!firstPart) {
        *error = QLatin1String("Cover not changed: first page was not found.");
        return false;
    }

    firmware.editorSetPart(editor, firstPart);
    void* const backgroundWidget = notebook_widget::notePadBackgroundWidget(widget);
    if (!backgroundWidget) {
        firmware.editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: page background is not ready.");
        return false;
    }

    QString const firstBackground = firmware.backgroundType(backgroundWidget);
    bool const alreadyHasCover = firstBackground.startsWith(
        QLatin1String("Custom_Cover_"));

    QString backupPath;
    if (!backupNotebook(
            firmware, widget, backupRoot, &backupPath, error)) {
        firmware.editorSetPart(editor, originalPart);
        return false;
    }

    if (alreadyHasCover) {
        firmware.setBackgroundTypeOriginal(widget, type);
        firmware.widgetSave(widget);
        firmware.packageSave(package.get());
        firmware.widgetRefresh(widget);
        cover_cache::invalidateNotebookScanEntry(
            coverCache, firmware.widgetFilePath(widget));
        trace("covers: existing cover background changed");
        return true;
    }

    ScopedIInkString rawContent(firmware, "Raw Content");
    if (!rawContent.value.impl) {
        firmware.editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: page type could not be created.");
        return false;
    }

    SharedPart const coverPart = firmware.packageCreatePart(
        package.get(), rawContent.value);
    if (!coverPart || firmware.packagePartCount(package.get()) != oldCount + 1) {
        firmware.editorSetPart(editor, originalPart);
        *error = QLatin1String("Cover not changed: a new page could not be created.");
        return false;
    }

    firmware.editorSetPart(editor, coverPart);
    firmware.setBackgroundTypeOriginal(widget, type);
    firmware.widgetSave(widget);
    firmware.packageSave(package.get());
    // The archive is mutated from here on; drop any cached verdict even if a
    // later validation step fails.
    cover_cache::invalidateNotebookScanEntry(
        coverCache, firmware.widgetFilePath(widget));

    SharedDocument const document = pages::documentForPart(firmware, coverPart);
    int const appendedIndex = firmware.packageIndexOfPart(package.get(), coverPart);
    if (!document
            || firmware.documentPageCount(document.get()) != oldCount + 1
            || appendedIndex != oldCount) {
        *error = QLatin1String(
            "Cover page was added at the end, but could not be moved safely. "
            "The original notebook backup is in the covers/backups folder.");
        trace("covers: pre-move validation failed");
        return false;
    }

    firmware.documentMovePage(document.get(), appendedIndex, 0);
    firmware.packageSave(package.get());
    if (firmware.packageIndexOfPart(package.get(), coverPart) != 0
            || firmware.packagePartCount(package.get()) != oldCount + 1) {
        *error = QLatin1String(
            "Cover move could not be verified. The original notebook backup "
            "is in the covers/backups folder.");
        trace("covers: post-move validation failed");
        return false;
    }

    firmware.editorSetPart(editor, coverPart);
    firmware.widgetRefresh(widget);
    cover_cache::invalidateNotebookScanEntry(
        coverCache, firmware.widgetFilePath(widget));
    trace("covers: new writable cover inserted as page zero");
    return true;
}

struct CoverPickerState {
    FirmwareApi* firmware;
    cover_cache::State* coverCache;
    char const* backupRoot;
    QPointer<QObject> widgetObject;
    void* widget;
    void* editor;
    SharedPart originalPart;
    QString originalFirstBackground;
    QPointer<QTimer> timer;
    int polls;

    CoverPickerState(
            FirmwareApi& firmware_,
            cover_cache::State& coverCache_,
            char const* backupRoot_)
        : firmware(&firmware_),
          coverCache(&coverCache_),
          backupRoot(backupRoot_),
          widget(nullptr),
          editor(nullptr),
          polls(0) {}
};

} // namespace

void beginPicker(
        FirmwareApi& firmware,
        cover_cache::State& coverCache,
        QObject* controller,
        QObject* widgetObject,
        char const* backupRoot) {
    if (!widgetObject) {
        trace("covers: notebook widget not found from menu controller");
        return;
    }
    if (widgetObject->property("_cnt_cover_picker_active").toBool())
        return;

    void* const widget = widgetObject;
    void* const editor = notebook_widget::notePadEditor(widget);
    void* const editorControl = notebook_widget::notePadEditorControl(widget);
    void* const backgroundWidget = notebook_widget::notePadBackgroundWidget(widget);
    if (!editor || !editorControl || !backgroundWidget) {
        trace("covers: notebook editor not ready for picker");
        return;
    }

    SharedPart const originalPart = firmware.editorGetPart(editor);
    if (!originalPart) {
        trace("covers: current page missing before picker");
        return;
    }
    SharedPackage const package = firmware.partGetPackage(originalPart.get());
    if (!package || firmware.packagePartCount(package.get()) < 1) {
        trace("covers: notebook package missing before picker");
        return;
    }
    SharedPart const firstPart = firmware.packageGetPart(package.get(), 0);
    if (!firstPart) {
        trace("covers: first page missing before picker");
        return;
    }

    firmware.editorSetPart(editor, firstPart);
    std::shared_ptr<CoverPickerState> const state(
        new CoverPickerState(firmware, coverCache, backupRoot));
    state->widgetObject = widgetObject;
    state->widget = widget;
    state->editor = editor;
    state->originalPart = originalPart;
    state->originalFirstBackground = firmware.backgroundType(backgroundWidget);

    QTimer* const timer = new QTimer(widgetObject);
    state->timer = timer;
    timer->setInterval(100);
    widgetObject->setProperty("_cnt_cover_picker_active", true);
    QObject::connect(timer, &QTimer::timeout, [state]() {
        if (!state->widgetObject || !state->timer)
            return;

        FirmwareApi& firmware = *state->firmware;
        cover_cache::State& coverCache = *state->coverCache;
        QObject* const object = state->widgetObject.data();
        void* const backgroundWidget = notebook_widget::notePadBackgroundWidget(
            state->widget);
        QString const selected = backgroundWidget
            ? firmware.backgroundType(backgroundWidget) : QString();

        if (selected.startsWith(QLatin1String("Custom_Cover_"))
                && selected != state->originalFirstBackground) {
            state->timer->stop();
            object->setProperty("_cnt_cover_picker_active", false);
            coverCache.titlePending = false;

            // Nickel has applied the picker result to page zero. Restore that
            // page first, then let the backed-up insertion routine decide
            // whether to update an existing cover or create a new one.
            firmware.setBackgroundTypeOriginal(
                state->widget, state->originalFirstBackground);
            firmware.widgetSave(state->widget);

            QString error;
            if (!applyNotebookCover(
                    firmware,
                    coverCache,
                    state->widget,
                    selected,
                    state->backupRoot,
                    &error)) {
                firmware.editorSetPart(state->editor, state->originalPart);
                firmware.widgetRefresh(state->widget);
                trace("covers: cover operation failed safely");
                if (firmware.showErrorPopup)
                    firmware.showErrorPopup(state->widget, error);
            }
            state->timer->deleteLater();
            return;
        }

        // Treat a closed picker as a harmless cancel. The long timeout avoids
        // relying on private picker-controller lifetime or dialog signals.
        if (++state->polls >= 300) {
            state->timer->stop();
            object->setProperty("_cnt_cover_picker_active", false);
            coverCache.titlePending = false;
            firmware.editorSetPart(state->editor, state->originalPart);
            firmware.widgetRefresh(state->widget);
            state->timer->deleteLater();
            trace("covers: picker cancelled or unchanged");
        }
    });

    coverCache.pickerPending = true;
    coverCache.titlePending = true;
    firmware.menuSelectBackground(controller);
    // backgroundOptions() is invoked synchronously by this signal on Nickel's
    // UI thread. A leftover flag means controller creation failed.
    coverCache.pickerPending = false;
    timer->start();
    trace("covers: cover picker opened from notebook menu");
}

} // namespace cover_editor
} // namespace cnt
