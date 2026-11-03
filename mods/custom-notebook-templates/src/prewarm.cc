#include "prewarm.h"

#include "notebook_widget.h"
#include "perf_trace.h"
#include "plugin_runtime.h"
#include "plugin_state.h"
#include "settings.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QObject>
#include <QPixmapCache>
#include <QSaveFile>
#include <QTimer>

#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

namespace cnt {
namespace prewarm {

namespace {

struct Limits {
    enum {
        kMaxRecent = 2,
        kBackgroundWidgetSize = 0x28,
        kBackgroundWidgetDeviceOffset = 0x24,
        kReadaheadDelayMs = 12000,
        kPreloadDelayMs = 20000,
        kPreloadRetryMs = 2000,
        kPreloadIdleMs = 3000,
        kPreloadMaxAttempts = 60,
        kBackgroundDelayMs = 1500,
    };
};

enum Step { ReadaheadStep, PreloadStep, BackgroundStep };

QString recentPath() {
    return QString::fromLatin1(
        "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/prewarm.txt");
}

bool onboardMounted() {
    QFile mounts(QString::fromLatin1("/proc/mounts"));
    if (!mounts.open(QIODevice::ReadOnly))
        return false;
    // procfs reports size zero; read line by line until EOF.
    for (QByteArray line = mounts.readLine(); !line.isEmpty(); line = mounts.readLine()) {
        QList<QByteArray> const fields = line.simplified().split(' ');
        if (fields.size() >= 3 && fields.at(1) == "/mnt/onboard" && fields.at(2) == "vfat")
            return true;
    }
    return false;
}

RuntimeState& state() {
    return pluginState().prewarm;
}

void load() {
    RuntimeState& s = state();
    if (s.loaded)
        return;
    s.loaded = true;
    if (!onboardMounted())
        return;
    QFile file(recentPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QByteArray const bytes = file.read(4097);
    if (bytes.size() > 4096)
        return;
    foreach (QByteArray const& line, bytes.split('\n')) {
        QString const entry = QString::fromUtf8(line).trimmed();
        if (entry.count(QLatin1Char('\t')) == 1 && !entry.startsWith(QLatin1Char('\t'))
                && !s.recent.contains(entry) && s.recent.size() < Limits::kMaxRecent)
            s.recent.append(entry);
    }
}

void save() {
    if (!onboardMounted())
        return;
    QSaveFile file(recentPath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(state().recent.join(QLatin1String("\n")).toUtf8() + "\n");
    file.commit();
}

bool enabled() {
    return perf::currentExperiment().prewarm;
}

// Asynchronous kernel read-ahead of the notebook engine; returns bytes hinted.
qint64 readaheadLibraries(int* files) {
    static char const* const kLibraries[] = {
        "/usr/local/Kobo/libiink.so",
        "/usr/local/Kobo/libiinknote.so",
        "/usr/local/Kobo/libiinkuiref.so.1.0.0",
        "/usr/local/Kobo/libMyScriptEngine.so",
        "/usr/local/Kobo/libMyScriptDocument.so",
        "/usr/local/Kobo/libMyScriptInk.so",
        "/usr/local/Kobo/libMyScriptGesture.so",
        "/usr/local/Kobo/libMyScriptText.so",
        "/usr/local/Kobo/libMyScriptAnalyzer.so",
        "/usr/local/Kobo/libMyScriptShape.so",
        "/usr/local/Kobo/libMyScriptMath.so",
        "/usr/local/Kobo/libMyScriptPrediction.so",
        "/usr/local/Kobo/libMyScript2D.so",
    };
    qint64 bytes = 0;
    *files = 0;
    for (std::size_t i = 0; i < sizeof(kLibraries) / sizeof(kLibraries[0]); ++i) {
        int const fd = open(kLibraries[i], O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        struct stat info;
        std::memset(&info, 0, sizeof(info));
        if (fstat(fd, &info) == 0
                && posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED) == 0) {
            bytes += info.st_size;
            ++*files;
        }
        close(fd);
    }
    return bytes;
}

// Kobo's own loader on a hidden widget, allocated and destroyed exactly as
// IInkNotePadWidget does (operator new(0x28), deleting destructor).
bool replayBackground(QString const& type, QString const& device) {
    FirmwareApi& firmware = firmwareApi();
    if (!firmware.backgroundWidgetCtor || !firmware.backgroundWidgetDelete
            || !firmware.backgroundWidgetSetType)
        return false;
    void* const widget = ::operator new(Limits::kBackgroundWidgetSize, std::nothrow);
    if (!widget)
        return false;
    try {
        firmware.backgroundWidgetCtor(widget, nullptr);
    } catch (...) {
        ::operator delete(widget);
        return false;
    }
    bool ok = true;
    try {
        if (!device.isEmpty()) {
            *reinterpret_cast<QString*>(
                static_cast<char*>(widget) + Limits::kBackgroundWidgetDeviceOffset) = device;
        }
        firmware.backgroundWidgetSetType(widget, type);
    } catch (...) {
        ok = false;
    }
    firmware.backgroundWidgetDelete(widget);
    return ok;
}

void runBackgrounds() {
    RuntimeState& s = state();
    if (s.notebookOpened) {
        trace("prewarm: backgrounds skipped; a notebook is already open");
        return;
    }
    if (!enabled())
        return;
    load();
    QElapsedTimer timer;
    timer.start();
    int replayed = 0;
    foreach (QString const& entry, s.recent) {
        if (s.notebookOpened)
            break;
        int const tab = entry.indexOf(QLatin1Char('\t'));
        if (replayBackground(entry.left(tab), entry.mid(tab + 1)))
            ++replayed;
    }
    trace(QLatin1String("perf: prewarm backgrounds=") + QString::number(replayed)
        + QLatin1String(" of ") + QString::number(s.recent.size())
        + QLatin1String(" ms=") + QString::number(timer.elapsed())
        + QLatin1String(" pixmap-cache-limit-kb=")
        + QString::number(QPixmapCache::cacheLimit()));
}

void runReadahead() {
    if (!enabled()) {
        trace("prewarm: disabled by perf-experiment.txt");
        return;
    }
    QElapsedTimer timer;
    timer.start();
    int files = 0;
    qint64 const bytes = readaheadLibraries(&files);
    trace(QLatin1String("perf: prewarm readahead files=") + QString::number(files)
        + QLatin1String(" mb=") + QString::number(bytes / (1024 * 1024))
        + QLatin1String(" ms=") + QString::number(timer.elapsed()));
}

// Load Nickel's notebook plugin while idle instead of at the first notebook
// open. Nickel's own QPluginLoader later gets the same, already relocated
// library; the plugin's delayed hook setup (which polls for this library)
// and the background replay then also run before the first open.
void stopInputWatcher() {
    if (InputWatcher* const watcher = state().inputWatcher.data()) {
        QCoreApplication::instance()->removeEventFilter(watcher);
        watcher->deleteLater();
    }
}

void runPreload() {
    if (!enabled()) {
        stopInputWatcher();
        return;
    }
    RuntimeState& s = state();
    InputWatcher* const watcher = s.inputWatcher.data();
    if (watcher && watcher->idleMs() < Limits::kPreloadIdleMs
            && ++s.preloadAttempts < Limits::kPreloadMaxAttempts) {
        QTimer* const retry = new QTimer(QCoreApplication::instance());
        retry->setSingleShot(true);
        retry->setInterval(Limits::kPreloadRetryMs);
        QObject::connect(retry, &QTimer::timeout, [retry]() {
            try {
                runPreload();
            } catch (...) {
                trace("prewarm: preload threw and was contained");
            }
            retry->deleteLater();
        });
        retry->start();
        return;
    }
    stopInputWatcher();
    if (void* const loaded = dlopen("libiinknote.so", RTLD_LAZY | RTLD_NOLOAD)) {
        dlclose(loaded);
        trace("prewarm: notebook engine already loaded");
        return;
    }
    QElapsedTimer timer;
    timer.start();
    // Never closed: Nickel keeps the plugin loaded for the session anyway.
    void* const handle = dlopen("/usr/local/Kobo/libiinknote.so", RTLD_LAZY);
    perf::stamp(handle ? "engine-preloaded" : "engine-preload-failed", timer.elapsed());
}

} // namespace

InputWatcher::InputWatcher() : QObject(QCoreApplication::instance()) {
    lastInput_.start();
}

qint64 InputWatcher::idleMs() const {
    return lastInput_.elapsed();
}

bool InputWatcher::eventFilter(QObject*, QEvent* event) {
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::KeyPress:
        lastInput_.restart();
        break;
    default:
        break;
    }
    return false;
}

namespace {

void singleShot(int delayMs, int step) {
    QTimer* const timer = new QTimer(QCoreApplication::instance());
    timer->setSingleShot(true);
    timer->setInterval(delayMs);
    QObject::connect(timer, &QTimer::timeout, [timer, step]() {
        try {
            if (step == BackgroundStep)
                runBackgrounds();
            else if (step == PreloadStep)
                runPreload();
            else
                runReadahead();
        } catch (...) {
            trace("prewarm: step threw and was contained");
        }
        timer->deleteLater();
    });
    timer->start();
}

} // namespace

void schedule() {
    RuntimeState& s = state();
    if (s.scheduled)
        return;
    s.scheduled = true;
    singleShot(Limits::kReadaheadDelayMs, ReadaheadStep);
    try {
        InputWatcher* const watcher = new InputWatcher;
        QCoreApplication::instance()->installEventFilter(watcher);
        s.inputWatcher = watcher;
    } catch (...) {
    }
    singleShot(Limits::kPreloadDelayMs, PreloadStep);
}

void notebookEngineLoaded() {
    singleShot(Limits::kBackgroundDelayMs, BackgroundStep);
}

void recordOpenedNotebook(void* notebookWidget) {
    RuntimeState& s = state();
    s.notebookOpened = true;
    FirmwareApi& firmware = firmwareApi();
    // The device field offset is only trusted with the verified widget pins.
    if (!firmware.backgroundType || !firmware.backgroundWidgetCtor)
        return;
    void* const background = notebook_widget::notePadBackgroundWidget(notebookWidget);
    if (!background)
        return;
    QObject const* const object = static_cast<QObject const*>(background);
    if (std::strcmp(object->metaObject()->className(), "BackgroundWidget") != 0)
        return;
    QString const type = firmware.backgroundType(background);
    QString const device = *reinterpret_cast<QString const*>(
        static_cast<char const*>(background) + Limits::kBackgroundWidgetDeviceOffset);
    if (type.isEmpty() || type.contains(QLatin1Char('\t')) || type.contains(QLatin1Char('\n'))
            || device.contains(QLatin1Char('\t')) || device.contains(QLatin1Char('\n')))
        return;
    load();
    QString const entry = type + QLatin1Char('\t') + device;
    if (!s.recent.isEmpty() && s.recent.first() == entry)
        return;
    s.recent.removeAll(entry);
    s.recent.prepend(entry);
    while (s.recent.size() > Limits::kMaxRecent)
        s.recent.removeLast();
    save();
}

} // namespace prewarm
} // namespace cnt
