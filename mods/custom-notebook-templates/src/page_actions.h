#pragma once

#include <type_traits>

class QObject;
struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}

namespace page_actions {

// This value is copied into notebook-menu signal functors. Its object pointers
// and backupRoot must therefore refer to process-lifetime plugin storage.
struct Dependencies {
    FirmwareApi* firmware;
    cover_cache::State* coverCache;
    int maximumNotebookPages;
    char const* backupRoot;
};
static_assert(std::is_pod<Dependencies>::value,
    "page action dependencies must remain copyable POD data");

enum PageOperation {
    DuplicatePageOperation,
    MovePageEarlierOperation,
    MovePageLaterOperation
};

void runForController(
    Dependencies dependencies,
    QObject* controller,
    PageOperation operation);

void runForWidget(
    FirmwareApi& firmware,
    cover_cache::State& coverCache,
    void* widget,
    PageOperation operation,
    int maximumNotebookPages,
    char const* backupRoot);

} // namespace page_actions
} // namespace cnt
