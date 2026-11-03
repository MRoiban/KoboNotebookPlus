#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

// Notebook-open pre-warm (notes/NOTEBOOK_OPEN_PROFILE.md). The first open
// after boot pays for decoding the page background and for faulting in the
// iink/MyScript code from flash. Shortly after boot, while idle:
//  * ask the kernel to read the iink/MyScript libraries ahead
//    (posix_fadvise WILLNEED; asynchronous, never blocks the GUI thread);
//  * replay the backgrounds of the most recently opened notebooks through
//    Kobo's own BackgroundWidget::setBackgroundType on a hidden widget, which
//    fills Qt's pixmap cache under exactly the key the real open uses.
// Skipped entirely once a notebook has been opened this boot.
// Kill switch: perf-experiment.txt `prewarm=0`.

namespace cnt {
namespace prewarm {

// Records the time of the last touch/key/mouse input while installed, so the
// engine preload (a one-time ~0.7 s GUI block) only runs when idle.
class InputWatcher : public QObject {
public:
    InputWatcher();
    qint64 idleMs() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QElapsedTimer lastInput_;
};

struct RuntimeState {
    QPointer<InputWatcher> inputWatcher;
    int preloadAttempts = 0;
    bool loaded = false;
    bool notebookOpened = false;
    bool scheduled = false;
    // "type\tdevice" entries, most recent first.
    QStringList recent;
};

// Plugin init: schedule the library read-ahead (12 s) and the idle load of
// Nickel's notebook plugin (20 s), which also brings the plugin's delayed
// hook setup and the background replay off the first open.
void schedule();

// Nickel has naturally loaded libiinknote (first My Notebooks visit):
// replay recent backgrounds 1.5 s later, unless a notebook opened first.
void notebookEngineLoaded();

// After a notebook open: remember its visible page background.
void recordOpenedNotebook(void* notebookWidget);

} // namespace prewarm
} // namespace cnt
