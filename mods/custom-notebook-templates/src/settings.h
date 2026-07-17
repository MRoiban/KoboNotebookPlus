#pragma once

#include <QMutex>

class QString;

namespace cnt {

void trace(char const* message, bool truncate = false);
void trace(QString const& message);
bool validEraserSizeIndex(int index);

enum NotebookSleepImageMode {
    NotebookSleepCover = 0,
    NotebookSleepCurrentPage = 1,
};

struct NotebookSleepSettings {
    bool enabled;
    NotebookSleepImageMode mode;

    NotebookSleepSettings()
        : enabled(false), mode(NotebookSleepCover) {}
};

class SettingsStore {
public:
    int configuredEraserSizeIndex();
    bool persistEraserSizeIndex(int index);
    NotebookSleepSettings configuredNotebookSleep();
    bool persistNotebookSleep(
        bool enabled,
        NotebookSleepImageMode mode);

private:
    void rememberEraserSizeIndex(int index);
    void rememberNotebookSleep(NotebookSleepSettings const& settings);

    QMutex mutex_;
    bool eraserLoaded_ = false;
    int sizeIndex_ = 2;
    bool notebookSleepLoaded_ = false;
    NotebookSleepSettings notebookSleep_;
};

} // namespace cnt
