#include "perf_trace.h"

#include "open_profiler.h"
#include "plugin_runtime.h"
#include "plugin_state.h"
#include "settings.h"

#include <QChildEvent>
#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QTimerEvent>

#include <algorithm>
#include <cstring>
#include <dlfcn.h>

namespace cnt {
namespace perf {
namespace {

QString milliseconds(qint64 nanoseconds) {
    return QString::number(static_cast<double>(nanoseconds) / 1000000.0, 'f', 1);
}

// Reject the whole file on unknown/duplicate keys or values. Defaults keep
// diagnostics off; only a valid, explicit settle=1 can arm the observer.
Experiment readExperiment() {
    Experiment defaults;
    QFile mounts(QLatin1String("/proc/mounts"));
    if (!mounts.open(QIODevice::ReadOnly))
        return defaults;
    bool mounted = false;
    // procfs reports size zero; QFile::atEnd() cannot be used as the gate.
    for (QByteArray line = mounts.readLine(); !line.isEmpty(); line = mounts.readLine()) {
        QList<QByteArray> const fields = line.simplified().split(' ');
        if (fields.size() >= 3 && fields.at(1) == "/mnt/onboard"
                && fields.at(2) == "vfat") {
            mounted = true;
            break;
        }
    }
    if (!mounted)
        return defaults;
    QFile file(QLatin1String("/mnt/onboard/.kobo/iink/custom-notebook-templates/"
                            "templates/perf-experiment.txt"));
    if (!file.open(QIODevice::ReadOnly))
        return defaults;
    QByteArray const bytes = file.read(257);
    if (bytes.size() > 256 || file.error() != QFile::NoError)
        return defaults;
    Experiment result;
    unsigned seen = 0;
    foreach (QByteArray const& token, bytes.simplified().split(' ')) {
        if (token.isEmpty())
            continue;
        QList<QByteArray> const pair = token.split('=');
        if (pair.size() != 2)
            return defaults;
        Variant* destination = nullptr;
        unsigned bit = 0;
        if (pair.at(0) == "move") {
            destination = &result.move;
            bit = 1;
        } else if (pair.at(0) == "dup") {
            destination = &result.duplicate;
            bit = 2;
        } else if (pair.at(0) == "layer-activate") {
            destination = &result.layerActivate;
            bit = 4;
        } else if (pair.at(0) == "layer-add") {
            destination = &result.layerAdd;
            bit = 16;
        } else if (pair.at(0) == "settle") {
            bit = 8;
        } else if (pair.at(0) == "profile-open") {
            bit = 64;
        } else if (pair.at(0) == "prewarm") {
            bit = 128;
        } else {
            return defaults;
        }
        if (seen & bit)
            return defaults;
        seen |= bit;
        QByteArray const value = pair.at(1);
        if (bit == 8 || bit == 64 || bit == 128) {
            if (value != "0" && value != "1")
                return defaults;
            if (bit == 8)
                result.settle = value == "1";
            else if (bit == 64)
                result.profileOpen = value == "1";
            else
                result.prewarm = value == "1";
            continue;
        }
        if (value == "stock")
            *destination = Stock;
        else if (bit == 1 && value == "position")
            *destination = Position;
        else if (bit == 2 && value == "setpart")
            *destination = SetPart;
        else if ((bit == 4 || bit == 16) && value == "none")
            *destination = None;
        else
            return defaults;
    }
    return result;
}

char const* variantName(Variant variant) {
    switch (variant) {
    case Position: return "position";
    case SetPart: return "setpart";
    case None: return "none";
    default: return "stock";
    }
}

} // namespace

PaintObserver::PaintObserver()
    : notifyInternal_(nullptr), forwardingObject_(nullptr), forwardingEvent_(nullptr),
      lastPaintNs_(0), quietSinceNs_(0), paintNs_(0), generation_(0), paints_(0),
      paintDepth_(0), timerId_(0), registered_(false), completed_(false) {}

PaintObserver::~PaintObserver() { cancel(); }

bool PaintObserver::contains(QObject* object) const {
    if (!widget_)
        return false;
    for (; object; object = object->parent()) {
        if (object == widget_)
            return true;
    }
    return false;
}

void PaintObserver::watch(QObject* object) {
    if (!object || watched_.contains(QPointer<QObject>(object)))
        return;
    watched_.append(QPointer<QObject>(object));
    object->installEventFilter(this);
    foreach (QObject* child, object->children())
        watch(child);
}

void PaintObserver::begin(QObject* widget, QElapsedTimer const& start) {
    cancel();
    if (!widget || !QCoreApplication::instance())
        return;
    // Qt 5.2 exports this symbol. Recursing through the original entry (rather
    // than sendEvent or notify) preserves spontaneity, loop-level bookkeeping,
    // application/object filters, and the original event acceptance/return.
    // No firmware vtable or event handler is patched. Missing Qt seam => no
    // observer; synchronous records and all editing behavior remain available.
    if (!notifyInternal_) {
        notifyInternal_ = reinterpret_cast<NotifyInternal>(dlsym(RTLD_DEFAULT,
            "_ZN16QCoreApplication14notifyInternalEP7QObjectP6QEvent"));
    }
    if (!notifyInternal_)
        return;
    widget_ = widget;
    start_ = start;
    lastPaintNs_ = quietSinceNs_ = paintNs_ = 0;
    paints_ = paintDepth_ = 0;
    completed_ = false;
    watch(widget);
    registered_ = QInternal::registerCallback(QInternal::EventNotifyCallback,
                                              &PaintObserver::notifyCallback);
    if (!registered_)
        cancel();
}

void PaintObserver::cancel() {
    open_profiler::end(nullptr);
    ++generation_;
    if (timerId_) {
        killTimer(timerId_);
        timerId_ = 0;
    }
    if (registered_) {
        QInternal::unregisterCallback(QInternal::EventNotifyCallback,
                                     &PaintObserver::notifyCallback);
        registered_ = false;
    }
    foreach (QPointer<QObject> const& object, watched_) {
        if (object)
            object->removeEventFilter(this);
    }
    watched_.clear();
    widget_.clear();
    completed_ = false;
}

bool PaintObserver::eventFilter(QObject* watched, QEvent* event) {
    try {
        if (event->type() == QEvent::Destroy && watched == widget_)
            cancel();
        else if (event->type() == QEvent::ChildAdded && contains(watched))
            watch(static_cast<QChildEvent*>(event)->child());
    } catch (...) {
        cancel();
    }
    return false;
}

bool PaintObserver::notifyCallback(void** data) {
    PaintObserver* const observer = pluginState().paintObserver.get();
    QEvent* const event = static_cast<QEvent*>(data[1]);
    // The callback can run on other threads; never examine GUI objects there.
    if (!observer || QThread::currentThread() != observer->thread()
            || event->type() != QEvent::Paint || !observer->registered_)
        return false;
    QObject* const object = static_cast<QObject*>(data[0]);
    if ((observer->forwardingObject_ == object && observer->forwardingEvent_ == event)
            || !observer->contains(object))
        return false;

    quint64 const generation = observer->generation_;
    QObject* const previousObject = observer->forwardingObject_;
    QEvent* const previousEvent = observer->forwardingEvent_;
    observer->forwardingObject_ = object;
    observer->forwardingEvent_ = event;
    ++observer->paintDepth_;
    qint64 const began = observer->start_.nsecsElapsed();
    try {
        // The recursive callback declines this exact delivery; the event then
        // follows Qt's untouched path exactly once. Nested *other* Paint events
        // are still timed, inclusively, by their own callback frames.
        *static_cast<bool*>(data[2]) = observer->notifyInternal_(
            QCoreApplication::instance(), object, event);
    } catch (...) {
        observer->forwardingObject_ = previousObject;
        observer->forwardingEvent_ = previousEvent;
        observer->cancel();
        throw; // Do not change firmware/Qt exception semantics.
    }
    observer->forwardingObject_ = previousObject;
    observer->forwardingEvent_ = previousEvent;
    if (generation == observer->generation_) {
        qint64 const ended = observer->start_.nsecsElapsed();
        observer->lastPaintNs_ = observer->quietSinceNs_ = ended;
        observer->paintNs_ += ended - began;
        ++observer->paints_;
        --observer->paintDepth_;
        if (observer->completed_ && !observer->paintDepth_)
            observer->schedule();
    }
    // Delivery already happened above; this is a notify wrapper, never an
    // event-filter veto. Returning false here would deliver the Paint twice.
    return true;
}

void PaintObserver::complete(QString const& operation, char const* variant) {
    if (!registered_ || !widget_)
        return;
    operation_ = operation;
    variant_ = QLatin1String(variant);
    completed_ = true;
    quietSinceNs_ = start_.nsecsElapsed();
    schedule();
}

void PaintObserver::schedule() {
    if (timerId_) {
        killTimer(timerId_);
        timerId_ = 0;
    }
    if (!completed_ || !widget_ || paintDepth_)
        return;
    qint64 const now = start_.nsecsElapsed();
    qint64 const remaining = std::min(qint64(3000000000LL) - now,
                                     quietSinceNs_ + 250000000LL - now);
    // Even an already-expired hard cap reports on the next event-loop turn,
    // after the synchronous record. No forced painting or nested event loop.
    timerId_ = startTimer(int(std::max(qint64(1), (remaining + 999999) / 1000000)),
                          Qt::PreciseTimer);
    if (!timerId_)
        cancel();
}

void PaintObserver::timerEvent(QTimerEvent* event) {
    if (event->timerId() != timerId_) {
        QObject::timerEvent(event);
        return;
    }
    killTimer(timerId_);
    timerId_ = 0;
    if (!widget_) {
        cancel();
        return;
    }
    qint64 const now = start_.nsecsElapsed();
    if (paintDepth_)
        return; // The outer delivery return schedules the timer again.
    if (now >= 3000000000LL || now - quietSinceNs_ >= 250000000LL)
        report();
    else
        schedule();
}

void PaintObserver::report() {
    try {
        // The quiet window is detection latency, not part of time-to-painted.
        // With no Paint, report synchronous completion (paints=0 is explicit).
        trace(QLatin1String("perf: notebook-edit op=") + operation_
            + QLatin1String("-settle result=ok variant=") + variant_
            + QLatin1String(" total-ms=")
            + milliseconds(paints_ ? lastPaintNs_ : quietSinceNs_)
            + QLatin1String(" paint-ms=") + milliseconds(paintNs_)
            + QLatin1String(" paints=") + QString::number(paints_)
            + QLatin1String(" capped=")
            + QString::number(start_.nsecsElapsed() >= 3000000000LL ? 1 : 0));
        // An armed open profile (profile-open=1) ends at the last paint.
        open_profiler::end("post-render", open_profiler::monotonicNs()
            - (start_.nsecsElapsed() - (paints_ ? lastPaintNs_ : quietSinceNs_)));
    } catch (...) {
        // Diagnostics never affect notebook behavior.
    }
    cancel();
}

EditScope::EditScope(char const* operation, QObject* widget)
    : operation_(operation), variant_("stock"), currentPhase_(nullptr), lastMarkNs_(0),
      otherNs_(0), published_(false), finished_(false) {
    if (!pluginState().activeEdit) {
        total_.start();
        pluginState().activeEdit = this;
        published_ = true;
        try {
            if (pluginState().paintObserver)
                pluginState().paintObserver->cancel();
            experiment_ = readExperiment();
            if (experiment_.settle) {
                if (!pluginState().paintObserver)
                    pluginState().paintObserver.reset(new PaintObserver);
                pluginState().paintObserver->begin(widget, total_);
            }
        } catch (...) {
            if (pluginState().paintObserver)
                pluginState().paintObserver->cancel();
        }
    }
}

EditScope::~EditScope() {
    if (!finished_)
        finish(false);
}

void EditScope::recordPhase(char const* phase) {
    qint64 const now = total_.nsecsElapsed();
    qint64 const elapsed = now - lastMarkNs_;
    lastMarkNs_ = now;
    if (!phase) {
        otherNs_ += elapsed;
        return;
    }
    try {
        record_ += QLatin1Char(' ') + QLatin1String(phase)
            + QLatin1String("-ms=") + milliseconds(elapsed);
    } catch (...) {
        // Diagnostics never affect the edit itself, even on allocation failure.
    }
}

void EditScope::finish(bool ok) {
    if (finished_)
        return;
    finished_ = true;
    if (!published_)
        return;
    recordPhase(currentPhase_);
    pluginState().activeEdit = nullptr;
    try {
        trace(QLatin1String("perf: notebook-edit op=")
            + QLatin1String(operation_)
            + QLatin1String(ok ? " result=ok" : " result=failed")
            + QLatin1String(" variant=") + QLatin1String(variant_)
            + QLatin1String(" total-ms=") + milliseconds(total_.nsecsElapsed())
            + record_ + QLatin1String(" other-ms=") + milliseconds(otherNs_));
        if (pluginState().paintObserver) {
            if (ok)
                pluginState().paintObserver->complete(QLatin1String(operation_), variant_);
            else
                pluginState().paintObserver->cancel();
        }
    } catch (...) {
        if (pluginState().paintObserver)
            pluginState().paintObserver->cancel();
        // Diagnostics never affect the edit itself.
    }
}

void stamp(char const* event, qint64 durationMs) {
    try {
        QString line = QLatin1String("perf: stamp event=") + QLatin1String(event)
            + QLatin1String(" mono-ms=")
            + QString::number(open_profiler::monotonicNs() / 1000000LL);
        if (durationMs >= 0)
            line += QLatin1String(" duration-ms=") + QString::number(durationMs);
        trace(line);
    } catch (...) {
    }
}

Experiment currentExperiment() {
    try {
        return readExperiment();
    } catch (...) {
        return Experiment();
    }
}

Variant experimentVariant(char const* key, FirmwareApi const& firmware) {
    EditScope* const scope = pluginState().activeEdit;
    Experiment const defaults;
    Experiment const& experiment = scope ? scope->experiment_ : defaults;
    Variant result = Stock;
    if (!std::strcmp(key, "move"))
        result = experiment.move;
    else if (!std::strcmp(key, "dup"))
        result = experiment.duplicate;
    else if (!std::strcmp(key, "layer-activate"))
        result = experiment.layerActivate;
    else if (!std::strcmp(key, "layer-add"))
        result = experiment.layerAdd;

    bool const fallback = (result == Position || result == SetPart)
        && !firmware.widgetPositionUpdated;
    if (scope)
        scope->variant_ = fallback ? "stock-fallback" : variantName(result);
    if (fallback)
        return Stock;
    return result;
}

PhaseScope::PhaseScope(char const* phase)
    : scope_(pluginState().activeEdit), previousPhase_(nullptr) {
    if (scope_) {
        scope_->recordPhase(scope_->currentPhase_);
        previousPhase_ = scope_->currentPhase_;
        scope_->currentPhase_ = phase;
    }
}

PhaseScope::~PhaseScope() {
    if (scope_ && !scope_->finished_) {
        scope_->recordPhase(scope_->currentPhase_);
        scope_->currentPhase_ = previousPhase_;
    }
}

void mark(char const* phase) {
    EditScope* const scope = pluginState().activeEdit;
    if (scope && phase)
        scope->recordPhase(phase);
}

void note(char const* key, qint64 value) {
    EditScope* const scope = pluginState().activeEdit;
    if (!scope || !key)
        return;
    try {
        QString const field = QLatin1Char(' ') + QLatin1String(key)
            + QLatin1Char('=');
        // A caller may load context again. Keep the first (pre-edit) fact,
        // and keep the record's counter keys unique for the log analyzer.
        if (!scope->record_.contains(field))
            scope->record_ += field + QString::number(value);
    } catch (...) {
        // Diagnostics never affect the edit itself.
    }
}

} // namespace perf
} // namespace cnt
