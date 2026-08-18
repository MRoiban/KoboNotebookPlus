#pragma once

#include "passive_input.h"

#include <QObject>
#include <QPointer>

class QEvent;
class QTimerEvent;
class QWidget;

namespace cnt {
namespace passive_input {

// One application-level event filter for passive custom gestures. The filter
// method is final and always returns false: subclasses can register
// recognizers and dispatch completed actions, but cannot consume Kobo events.
class PassiveInputRouter : public QObject {
public:
    explicit PassiveInputRouter(QObject* parent);
    ~PassiveInputRouter() override;

    void setTarget(QWidget* target);
    // A completed render creates a new editor/page lifetime even when Kobo
    // reuses the same QWidget address. Rebinding always invalidates sequences
    // and queued actions.
    void rebindTarget(QWidget* target);
    QWidget* target() const;
    GestureRegistry& gestures();

protected:
    virtual void prepareTouchSequence(QWidget* target);
    virtual void dispatchAction(
        QWidget* target,
        GestureAction const& action) = 0;

private:
    struct PendingAction {
        PendingAction()
            : target(),
              action{0},
              targetEpoch(0),
              inputEpoch(0) {}

        QPointer<QWidget> target;
        GestureAction action;
        std::uint64_t targetEpoch;
        std::uint64_t inputEpoch;
    };

    bool eventFilter(QObject* watched, QEvent* event) final;
    void timerEvent(QTimerEvent* event) override;

    void observeEvent(QObject* watched, QEvent* event);
    void observeTouchEvent(QEvent* event, QWidget* target);
    void resetSequence();
    void cancelInputSequence();
    void clearQueuedAction();
    void clearPendingActions();
    void queueAction(QWidget* target, GestureAction const& action);
    bool eventTargetsTarget(QObject* watched, QWidget* target) const;

    GestureRegistry gestures_;
    std::vector<TouchPoint> pointBuffer_;
    QPointer<QWidget> target_;
    PendingAction pendingAction_;
    bool actionPending_;
    int actionTimerId_;
    int actionCooldownTimerId_;
    bool touchSequenceOpen_;
    bool tabletContactOpen_;
    bool actionDispatching_;
    bool actionSettling_;
    std::uint32_t sequenceStartedMs_;
    std::uint32_t previousTimestampMs_;
    std::uint64_t targetEpoch_;
    std::uint64_t inputEpoch_;
};

} // namespace passive_input
} // namespace cnt
