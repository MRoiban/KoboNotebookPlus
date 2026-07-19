#pragma once

#include <QObject>

class QEvent;

// Application-level input watcher for notebook power images. It never accepts
// or consumes input: ordinary events only invalidate the current-page cache.
// Rendering is confined to the power boundary.
class NotebookSleepRefreshReceiver : public QObject {
public:
    explicit NotebookSleepRefreshReceiver(QObject* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;
};
