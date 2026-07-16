#pragma once

#include "abi_types.h"

#include <cstdint>

struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}
namespace templates {
struct TemplateRuntimeState;
}

namespace notebook_hook_services {

void routeBackgroundOptions(
    templates::TemplateRuntimeState const& templateState,
    cover_cache::State& coverState,
    QVector<BackgroundOption>& options);
void routeCoverDialogTitle(
    FirmwareApi& firmware,
    cover_cache::State& coverState,
    void* dialog,
    QString const& title);
void routeParserImage(
    FirmwareApi& firmware,
    cover_cache::State& coverState,
    uintptr_t callerAddress,
    uintptr_t thumbnailCallbackReturnVma,
    void* parser,
    void const* volume,
    QImage const& image);
void augmentNotebookGridCover(
    FirmwareApi& firmware,
    cover_cache::State& coverState,
    void* view,
    uintptr_t volumeInPixmapViewOffset);

} // namespace notebook_hook_services
} // namespace cnt
