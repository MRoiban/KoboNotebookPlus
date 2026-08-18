#include "notebook_menu.h"

#include "cover_editor.h"
#include "cover_cache.h"
#include "covermenureceiver.h"
#include "firmware_api.h"
#include "notebook_sleep.h"
#include "notebook_widget.h"
#include "settings.h"

#include <QMenu>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QWidget>

namespace cnt {
namespace notebook_menu {

void addCoverContribution(
        FirmwareApi& firmware,
        cover_cache::State& coverState,
        void* controller,
        QObject* controllerObject,
        QMenu* menu,
        QPixmap const& noIcon,
        char const* backupRoot) {
    if (!coverState.hooksReady || coverState.customCovers.isEmpty())
        return;

    CoverMenuReceiver* const coverReceiver = new CoverMenuReceiver(
        controllerObject, menu);
    FirmwareApi* const firmwarePointer = &firmware;
    cover_cache::State* const coverStatePointer = &coverState;
    QObject::connect(
        coverReceiver,
        &CoverMenuReceiver::coverRequested,
        [firmwarePointer, coverStatePointer, backupRoot](QObject* target) {
            if (target) {
                QObject* const widgetObject =
                    notebook_widget::findNotebookWidget(target);
                cover_editor::beginPicker(
                    *firmwarePointer,
                    *coverStatePointer,
                    target,
                    widgetObject,
                    backupRoot);
            }
        });

    QWidget* const coverItem = firmware.createIInkMenuItem(
        controller,
        menu,
        QLatin1String("Change notebook cover"),
        noIcon,
        false);
    if (coverItem) {
        firmware.addWidgetActionOriginal(
            controller,
            menu,
            coverItem,
            coverReceiver,
            SLOT(activate()),
            true,
            true,
            true);
        trace("covers: native notebook menu item added");
    } else {
        coverReceiver->deleteLater();
        trace("covers: native menu item creation failed");
    }
}

void addPageContribution(
        FirmwareApi& firmware,
        void* controller,
        QObject* controllerObject,
        QMenu* menu,
        QPixmap const& noIcon,
        page_actions::Dependencies dependencies) {
    CoverMenuReceiver* const pageReceiver = new CoverMenuReceiver(
        controllerObject, menu);
    QObject::connect(
        pageReceiver,
        &CoverMenuReceiver::duplicatePageRequested,
        [dependencies](QObject* target) {
            if (target)
                page_actions::runForController(
                    dependencies,
                    target,
                    page_actions::DuplicatePageOperation);
        });
    QObject::connect(
        pageReceiver,
        &CoverMenuReceiver::movePageEarlierRequested,
        [dependencies](QObject* target) {
            if (target)
                page_actions::runForController(
                    dependencies,
                    target,
                    page_actions::MovePageEarlierOperation);
        });
    QObject::connect(
        pageReceiver,
        &CoverMenuReceiver::movePageLaterRequested,
        [dependencies](QObject* target) {
            if (target)
                page_actions::runForController(
                    dependencies,
                    target,
                    page_actions::MovePageLaterOperation);
        });
    QObject::connect(
        pageReceiver,
        &CoverMenuReceiver::movePageToRequested,
        [dependencies](QObject* target) {
            if (target)
                page_actions::runMoveToForController(dependencies, target);
        });

    QWidget* const duplicateItem = firmware.createIInkMenuItem(
        controller,
        menu,
        QLatin1String("Duplicate page"),
        noIcon,
        false);
    if (duplicateItem) {
        firmware.addWidgetActionOriginal(
            controller,
            menu,
            duplicateItem,
            pageReceiver,
            SLOT(activateDuplicatePage()),
            true,
            true,
            false);
        trace("pages: native duplicate menu item added");
    } else {
        trace("pages: native duplicate menu item creation failed");
    }

    QWidget* const earlierItem = firmware.createIInkMenuItem(
        controller,
        menu,
        QLatin1String("Move page earlier"),
        noIcon,
        false);
    if (earlierItem) {
        firmware.addWidgetActionOriginal(
            controller,
            menu,
            earlierItem,
            pageReceiver,
            SLOT(activateMovePageEarlier()),
            true,
            true,
            false);
        trace("pages: native move-earlier menu item added");
    } else {
        trace("pages: native move-earlier menu item creation failed");
    }

    QWidget* const laterItem = firmware.createIInkMenuItem(
        controller,
        menu,
        QLatin1String("Move page later"),
        noIcon,
        false);
    if (laterItem) {
        firmware.addWidgetActionOriginal(
            controller,
            menu,
            laterItem,
            pageReceiver,
            SLOT(activateMovePageLater()),
            true,
            true,
            false);
        trace("pages: native move-later menu item added");
    } else {
        trace("pages: native move-later menu item creation failed");
    }

    QWidget* const moveToItem = firmware.createIInkMenuItem(
        controller,
        menu,
        QLatin1String("Move page to") + QChar(0x2026),
        noIcon,
        false);
    if (moveToItem) {
        firmware.addWidgetActionOriginal(
            controller,
            menu,
            moveToItem,
            pageReceiver,
            SLOT(activateMovePageTo()),
            true,
            true,
            true);
        trace("pages: native move-to menu item added");
    } else {
        trace("pages: native move-to menu item creation failed");
    }
}

bool addSleepPinContribution(
        FirmwareApi& firmware,
        notebook_sleep::RuntimeState& sleepState,
        void* controller,
        QObject* controllerObject,
        QMenu* menu,
        QPixmap const& noIcon,
        QString const& text,
        int maximumPageCount) {
    if (text.isEmpty())
        return false;

    CoverMenuReceiver* const receiver = new CoverMenuReceiver(
        controllerObject, menu);
    notebook_sleep::RuntimeState* const sleepStatePointer = &sleepState;
    QObject::connect(
        receiver,
        &CoverMenuReceiver::sleepPagePinRequested,
        [sleepStatePointer, maximumPageCount](QObject* target) {
            if (target) {
                notebook_sleep::togglePinnedPageForController(
                    *sleepStatePointer, target, maximumPageCount);
            }
        });

    QWidget* const item = firmware.createIInkMenuItem(
        controller,
        menu,
        text,
        noIcon,
        false);
    if (!item) {
        receiver->deleteLater();
        trace("notebook-sleep: native pin menu item creation failed");
        return false;
    }
    firmware.addWidgetActionOriginal(
        controller,
        menu,
        item,
        receiver,
        SLOT(activateSleepPagePin()),
        true,
        true,
        true);
    trace("notebook-sleep: native pin menu item added");
    return true;
}

} // namespace notebook_menu
} // namespace cnt
