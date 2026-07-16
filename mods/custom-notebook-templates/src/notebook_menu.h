#pragma once

#include "page_actions.h"

class QMenu;
class QObject;
class QPixmap;
struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}

namespace notebook_menu {

typedef void (*PageActivation)(
    QObject* controller,
    page_actions::PageOperation operation);

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
    PageActivation activatePage);

} // namespace notebook_menu
} // namespace cnt
