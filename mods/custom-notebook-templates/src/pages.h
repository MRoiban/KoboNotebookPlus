#pragma once

#include "abi_types.h"

class QString;
struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}

namespace pages {

struct PageContext {
    void* widget;
    void* editor;
    SharedPart part;
    SharedPackage package;
    SharedDocument document;
    int index;
    int count;

    PageContext()
        : widget(nullptr), editor(nullptr), index(-1), count(0) {}
};

SharedDocument documentForPart(
    FirmwareApi& firmware,
    SharedPart const& part);

bool loadPageContext(
    FirmwareApi& firmware,
    void* widget,
    int maximumPageCount,
    PageContext* context,
    QString* error);

bool reorderCurrentPage(
    FirmwareApi& firmware,
    cover_cache::State& coverCache,
    void* widget,
    int target,
    int maximumPageCount,
    char const* backupRoot,
    QString* error);

bool duplicateCurrentPage(
    FirmwareApi& firmware,
    cover_cache::State& coverCache,
    void* widget,
    int maximumPageCount,
    char const* backupRoot,
    QString* error);

} // namespace pages
} // namespace cnt
