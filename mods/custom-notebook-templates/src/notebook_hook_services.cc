#include "notebook_hook_services.h"

#include "cover_cache.h"
#include "firmware_api.h"
#include "fs_util.h"
#include "settings.h"
#include "templates.h"

#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <QVector>

namespace cnt {
namespace notebook_hook_services {

void routeBackgroundOptions(
        templates::TemplateRuntimeState const& templateState,
        cover_cache::State& coverState,
        QVector<BackgroundOption>& options) {
    if (coverState.pickerPending) {
        coverState.pickerPending = false;
        options.clear();
        for (int i = 0; i < coverState.customCovers.size(); ++i) {
            templates::CustomTemplate const& value =
                coverState.customCovers.at(i);
            options.append(BackgroundOption(value.id, value.icon, value.label));
        }
        trace("backgroundOptions hook returned cover-only rows");
        return;
    }

    for (int i = 0; i < templateState.customTemplates.size(); ++i) {
        templates::CustomTemplate const& value =
            templateState.customTemplates.at(i);
        options.append(BackgroundOption(value.id, value.icon, value.label));
    }
    trace("backgroundOptions hook appended custom rows");
}

void routeCoverDialogTitle(
        FirmwareApi& firmware,
        cover_cache::State& coverState,
        void* dialog,
        QString const& title) {
    if (coverState.hooksReady
            && coverState.titlePending
            && title == QLatin1String("Notebook Templates")) {
        coverState.titlePending = false;
        firmware.setDialogTitleOriginal(
            dialog, QLatin1String("Notebook Covers"));
        trace("covers: picker title changed to Notebook Covers");
        return;
    }
    firmware.setDialogTitleOriginal(dialog, title);
}

void routeParserImage(
        FirmwareApi& firmware,
        cover_cache::State& coverState,
        uintptr_t callerAddress,
        uintptr_t thumbnailCallbackReturnVma,
        void* parser,
        void const* volume,
        QImage const& image) {
    if (!coverState.parserHookReady
            || !firmware.contentGetId
            || !firmware.iinknoteBase
            || callerAddress
                != firmware.iinknoteBase + thumbnailCallbackReturnVma) {
        firmware.parserImageParsedOriginal(parser, volume, image);
        return;
    }

    QImage replacement;
    bool useReplacement = false;
    try {
        QElapsedTimer timer;
        timer.start();
        QString const notebookPath = cover_cache::notebookPathFromContentId(
            firmware.contentGetId(volume));
        double const pathResolveMs = fs_util::elapsedMs(timer);
        if (!notebookPath.isEmpty()) {
            QString type;
            if (coverState.hooksReady
                    && cover_cache::zipApisReady(coverState)
                    && cover_cache::cachedNotebookCoverType(
                        coverState, notebookPath, &type, pathResolveMs)) {
                QImage const composed =
                    cover_cache::composeCoverWithRenderedInk(
                        coverState, type, image);
                if (!composed.isNull()) {
                    cover_cache::cacheRenderedCoverImage(
                        coverState, notebookPath, type, composed);
                    replacement = composed;
                    useReplacement = true;
                } else {
                    trace(
                        "covers: cover-and-ink composition failed; "
                        "stock preview preserved");
                }
            }

            if (!useReplacement) {
                // Keep Kobo's own page-zero render for notebooks without a
                // plugin cover. The empty type annotation distinguishes stock
                // page zero from a composed custom cover.
                cover_cache::cacheRenderedCoverImage(
                    coverState, notebookPath, QString(), image);
            }
        }
    } catch (...) {
        replacement = QImage();
        useReplacement = false;
        trace("covers: parser augmentation threw; stock preview preserved");
    }

    if (useReplacement)
        firmware.parserImageParsedOriginal(parser, volume, replacement);
    else
        firmware.parserImageParsedOriginal(parser, volume, image);
}

void augmentNotebookGridCover(
        FirmwareApi& firmware,
        cover_cache::State& coverState,
        void* view,
        uintptr_t volumeInPixmapViewOffset) {
    if (!coverState.gridHookReady
            || !view
            || !firmware.contentGetId
            || !firmware.contentGetImageId
            || !firmware.pixmapSetImage
            || !cover_cache::zipApisReady(coverState)) {
        return;
    }

    try {
        void const* const volume = static_cast<char const*>(view)
            + volumeInPixmapViewOffset;
        QElapsedTimer timer;
        timer.start();
        QString const notebookPath = cover_cache::notebookPathFromContentId(
            firmware.contentGetId(volume));
        double const pathResolveMs = fs_util::elapsedMs(timer);
        QString type;
        if (notebookPath.isEmpty()
                || !cover_cache::cachedNotebookCoverType(
                    coverState, notebookPath, &type, pathResolveMs)) {
            return;
        }

        QImage preview = cover_cache::cachedRenderedCoverImage(
            coverState, notebookPath, type);
        if (preview.isNull())
            preview = cover_cache::cleanCustomCoverImage(coverState, type);
        if (preview.isNull()) {
            trace("covers: no rendered or clean preview; stock card preserved");
            return;
        }

        QString const imageId = firmware.contentGetImageId(volume);
        firmware.pixmapSetImage(view, preview, imageId);
    } catch (...) {
        trace("covers: grid augmentation threw; stock card preserved");
    }
}

} // namespace notebook_hook_services
} // namespace cnt
