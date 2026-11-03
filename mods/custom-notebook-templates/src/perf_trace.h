#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class QCoreApplication;
struct FirmwareApi;

namespace cnt {
namespace perf {

enum Variant { Stock, Position, SetPart, None };
struct Experiment {
    Variant move = Position;
    Variant duplicate = SetPart;
    Variant layerActivate = None;
    Variant layerAdd = None;
    bool settle = false;
    bool profileOpen = false;
    bool prewarm = true;
};

// Reads perf-experiment.txt (see readExperiment rules); all defaults when
// absent or invalid. Used by notebook open, which has no EditScope.
Experiment currentExperiment();

// Process-lifetime observer owned by PluginState. Filters never consume events.
// Qt's EventNotifyCallback brackets actual Paint delivery (filters alone only
// see its start). Both callback and filters are removed when not observing.
class PaintObserver : public QObject {
public:
    PaintObserver();
    ~PaintObserver();
    void begin(QObject* widget, QElapsedTimer const& start);
    void complete(QString const& operation, char const* variant);
    void cancel();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

private:
    typedef bool (*NotifyInternal)(QCoreApplication*, QObject*, QEvent*);
    static bool notifyCallback(void** data);
    bool contains(QObject* object) const;
    void watch(QObject* object);
    void schedule();
    void report();

    QPointer<QObject> widget_;
    QVector<QPointer<QObject> > watched_;
    QElapsedTimer start_;
    QString operation_;
    QString variant_;
    NotifyInternal notifyInternal_;
    QObject* forwardingObject_;
    QEvent* forwardingEvent_;
    qint64 lastPaintNs_;
    qint64 quietSinceNs_;
    qint64 paintNs_;
    quint64 generation_;
    int paints_;
    int paintDepth_;
    int timerId_;
    bool registered_;
    bool completed_;
};

// One synchronous notebook edit. Destruction reports failure unless finish(true)
// was called. Nested edit scopes are ignored; helpers contribute to the outer
// record through PluginState, without extra file I/O or namespace-scope storage.
class EditScope {
public:
    explicit EditScope(char const* operation, QObject* widget = nullptr);
    ~EditScope();
    void finish(bool ok);

private:
    EditScope(EditScope const&);
    EditScope& operator=(EditScope const&);
    friend class PhaseScope;
    friend void mark(char const* phase);
    friend void note(char const* key, qint64 value);
    friend Variant experimentVariant(char const* key, FirmwareApi const& firmware);
    void recordPhase(char const* phase);

    QElapsedTimer total_;
    QString record_;
    Experiment experiment_;
    char const* operation_;
    char const* variant_;
    char const* currentPhase_;
    qint64 lastMarkNs_;
    qint64 otherNs_;
    bool published_;
    bool finished_;
};

// File is snapshotted once at the edit's start; missing/invalid => defaults.
// Move/duplicate need positionUpdated; missing pin => traced stock-fallback.
// Layer activation/add need no optional pin. Explicit stock is the kill switch.
Variant experimentVariant(char const* key, FirmwareApi const& firmware);

// Measures exclusive block time; repeated phase fields expose repeated calls.
// Names must match [a-z0-9-]+. Gaps accumulate as other-ms.
class PhaseScope {
public:
    explicit PhaseScope(char const* phase);
    ~PhaseScope();
private:
    PhaseScope(PhaseScope const&);
    PhaseScope& operator=(PhaseScope const&);
    EditScope* scope_;
    char const* previousPhase_;
};

void mark(char const* phase);
void note(char const* key, qint64 value);

// "perf: stamp event=<event> mono-ms=<CLOCK_MONOTONIC ms>[ duration-ms=<d>]"
// for lining device events up with host-side input timestamps (/proc/uptime).
void stamp(char const* event, qint64 durationMs = -1);

} // namespace perf
} // namespace cnt
