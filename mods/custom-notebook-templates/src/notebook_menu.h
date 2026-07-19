#pragma once

#include "page_actions.h"

class QMenu;
class QObject;
class QPixmap;
class QString;
struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}
namespace notebook_sleep {
struct RuntimeState;
}

namespace notebook_menu {

// backupRoot is retained by the asynchronous cover receiver and must outlive it.
void addCoverContribution(
    FirmwareApi& firmware,
    cover_cache::State& coverState,
    void* controller,
    QObject* controllerObject,
    QMenu* menu,
    QPixmap const& noIcon,
    char const* backupRoot);
void addPageContribution(
    FirmwareApi& firmware,
    void* controller,
    QObject* controllerObject,
    QMenu* menu,
    QPixmap const& noIcon,
    page_actions::Dependencies dependencies);
bool addSleepPinContribution(
    FirmwareApi& firmware,
    notebook_sleep::RuntimeState& sleepState,
    void* controller,
    QObject* controllerObject,
    QMenu* menu,
    QPixmap const& noIcon,
    QString const& text,
    int maximumPageCount);

} // namespace notebook_menu
} // namespace cnt
