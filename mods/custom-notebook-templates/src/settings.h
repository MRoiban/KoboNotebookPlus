#pragma once

#include <QMutex>

class QString;

namespace cnt {

void trace(char const* message, bool truncate = false);
void trace(QString const& message);
bool validEraserSizeIndex(int index);

class SettingsStore {
public:
    int configuredEraserSizeIndex();
    bool persistEraserSizeIndex(int index);

private:
    void rememberEraserSizeIndex(int index);

    QMutex mutex_;
    bool loaded_ = false;
    int sizeIndex_ = 2;
};

} // namespace cnt
