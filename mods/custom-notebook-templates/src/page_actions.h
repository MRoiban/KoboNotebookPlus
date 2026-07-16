#pragma once

struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}

namespace page_actions {

enum PageOperation {
    DuplicatePageOperation,
    MovePageEarlierOperation,
    MovePageLaterOperation
};

void runForWidget(
    FirmwareApi& firmware,
    cover_cache::State& coverCache,
    void* widget,
    PageOperation operation,
    int maximumNotebookPages,
    char const* backupRoot);

} // namespace page_actions
} // namespace cnt
