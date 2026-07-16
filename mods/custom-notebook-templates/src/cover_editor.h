#pragma once

class QObject;
struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}

namespace cover_editor {

// backupRoot is retained by the asynchronous picker and must outlive it.
void beginPicker(
    FirmwareApi& firmware,
    cover_cache::State& coverCache,
    QObject* controller,
    QObject* widgetObject,
    char const* backupRoot);

} // namespace cover_editor
} // namespace cnt
