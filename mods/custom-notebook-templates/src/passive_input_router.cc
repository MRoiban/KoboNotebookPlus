#include "passive_input_router.h"

#include <QEvent>
#include <QPointF>
#include <QTimerEvent>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QWidget>

#include <algorithm>
#include <cstdint>

namespace cnt {
namespace passive_input {
namespace {

int const kActionSettleDelayMs = 50;
int const kActionCooldownMs = 250;

class PointBufferLease {
public:
    PointBufferLease(
            std::vector<TouchPoint>& reusable,
            std::vector<TouchPoint>& framePoints)
        : reusable_(reusable),
          framePoints_(framePoints) {
        framePoints_.swap(reusable_);
        framePoints_.clear();
    }

    ~PointBufferLease() {
        framePoints_.clear();
        framePoints_.swap(reusable_);
    }

private:
    PointBufferLease(PointBufferLease const&);
    PointBufferLease& operator=(PointBufferLease const&);

    std::vector<TouchPoint>& reusable_;
    std::vector<TouchPoint>& framePoints_;
};

TouchPointState normalizedState(Qt::TouchPointState state) {
    if (state == Qt::TouchPointPressed)
        return TouchPointState::Pressed;
    if (state == Qt::TouchPointReleased)
        return TouchPointState::Released;
    if (state == Qt::TouchPointMoved)
        return TouchPointState::Moved;
    return TouchPointState::Stationary;
}

TouchFramePhase normalizedPhase(QEvent::Type type) {
    if (type == QEvent::TouchBegin)
        return TouchFramePhase::Begin;
    if (type == QEvent::TouchEnd)
        return TouchFramePhase::End;
    return TouchFramePhase::Update;
}

} // namespace

PassiveInputRouter::PassiveInputRouter(QObject* parent)
    : QObject(parent),
      gestures_(),
      pointBuffer_(),
      target_(),
      pendingAction_(),
      actionPending_(false),
      actionTimerId_(0),
      actionCooldownTimerId_(0),
      touchSequenceOpen_(false),
      tabletContactOpen_(false),
      actionDispatching_(false),
      actionSettling_(false),
      sequenceStartedMs_(0),
      previousTimestampMs_(0),
      targetEpoch_(0),
      inputEpoch_(0) {}

PassiveInputRouter::~PassiveInputRouter() {
    clearPendingActions();
}

void PassiveInputRouter::setTarget(QWidget* target) {
    if (target_.data() == target)
        return;
    rebindTarget(target);
}

void PassiveInputRouter::rebindTarget(QWidget* target) {
    resetSequence();
    clearPendingActions();
    tabletContactOpen_ = false;
    target_ = target;
    ++targetEpoch_;
    ++inputEpoch_;
}

QWidget* PassiveInputRouter::target() const {
    return target_.data();
}

GestureRegistry& PassiveInputRouter::gestures() {
    return gestures_;
}

void PassiveInputRouter::prepareTouchSequence(QWidget*) {}

bool PassiveInputRouter::eventFilter(QObject* watched, QEvent* event) {
    try {
        observeEvent(watched, event);
    } catch (...) {
        cancelInputSequence();
    }
    return false;
}

void PassiveInputRouter::timerEvent(QTimerEvent* event) {
    if (!event) {
        QObject::timerEvent(event);
        return;
    }

    int const timerId = event->timerId();
    if (timerId == actionCooldownTimerId_) {
        killTimer(actionCooldownTimerId_);
        actionCooldownTimerId_ = 0;
        actionSettling_ = false;
        return;
    }
    if (timerId != actionTimerId_) {
        QObject::timerEvent(event);
        return;
    }

    killTimer(actionTimerId_);
    actionTimerId_ = 0;
    if (!actionPending_)
        return;

    PendingAction const pending = pendingAction_;
    actionPending_ = false;
    pendingAction_.target.clear();
    QWidget* const actionTarget = pending.target.data();
    bool const current = actionTarget
        && actionTarget == target_.data()
        && pending.targetEpoch == targetEpoch_
        && pending.inputEpoch == inputEpoch_;
    if (!current || touchSequenceOpen_ || tabletContactOpen_
            || actionDispatching_ || actionSettling_) {
        return;
    }

    actionDispatching_ = true;
    try {
        dispatchAction(actionTarget, pending.action);
    } catch (...) {
        // Firmware callbacks must never unwind through Qt's timer delivery.
    }
    actionDispatching_ = false;
    actionSettling_ = true;
    actionCooldownTimerId_ = startTimer(kActionCooldownMs);
    if (actionCooldownTimerId_ == 0)
        actionSettling_ = false;
}

void PassiveInputRouter::observeEvent(QObject* watched, QEvent* event) {
    QWidget* const currentTarget = target_.data();
    if (!event || !currentTarget)
        return;

    QEvent::Type const type = event->type();
    bool const stylusContactEvent = type == QEvent::TabletPress
        || type == QEvent::TabletRelease;
    if (stylusContactEvent) {
        if (!eventTargetsTarget(watched, currentTarget))
            return;
        if (type == QEvent::TabletPress) {
            tabletContactOpen_ = true;
            cancelInputSequence();
        } else if (type == QEvent::TabletRelease) {
            tabletContactOpen_ = false;
            cancelInputSequence();
        }
        return;
    }

    bool const touchEvent = type == QEvent::TouchBegin
        || type == QEvent::TouchUpdate
        || type == QEvent::TouchEnd
        || type == QEvent::TouchCancel;
    if (!touchEvent)
        return;
    if (!eventTargetsTarget(watched, currentTarget))
        return;

    QTouchEvent const* const touch =
        static_cast<QTouchEvent const*>(event);
    if (type == QEvent::TouchCancel) {
        cancelInputSequence();
        return;
    }

    if (!touch->device()
            || touch->device()->type() != QTouchDevice::TouchScreen) {
        cancelInputSequence();
        return;
    }
    if (tabletContactOpen_) {
        cancelInputSequence();
        return;
    }

    // A passive observer never joins a stream midway: an ignored or cancelled
    // sequence stays ignored until Kobo sends the next TouchBegin.
    if (!touchSequenceOpen_ && type != QEvent::TouchBegin)
        return;
    observeTouchEvent(event, currentTarget);
}

void PassiveInputRouter::observeTouchEvent(
        QEvent* event,
        QWidget* currentTarget) {
    QTouchEvent const* const touch =
        static_cast<QTouchEvent const*>(event);
    std::uint32_t const timestamp =
        static_cast<std::uint32_t>(touch->timestamp());
    if (event->type() == QEvent::TouchBegin) {
        // Any new input invalidates a history action from the preceding
        // sequence, even if its settle timer has not fired yet.
        clearQueuedAction();
        resetSequence();
        ++inputEpoch_;
        touchSequenceOpen_ = true;
        sequenceStartedMs_ = timestamp;
        previousTimestampMs_ = timestamp;
        prepareTouchSequence(currentTarget);
    }

    TouchFrame frame;
    PointBufferLease const pointLease(pointBuffer_, frame.points);
    frame.phase = normalizedPhase(event->type());
    frame.timestampMs = timestamp;
    frame.sequenceElapsedMs = timestamp - sequenceStartedMs_;
    frame.deltaMs = timestamp - previousTimestampMs_;
    frame.target = currentTarget;
    frame.targetWidth = static_cast<double>(currentTarget->width());
    frame.targetHeight = static_cast<double>(currentTarget->height());

    QList<QTouchEvent::TouchPoint> const& points = touch->touchPoints();
    std::size_t const requiredCapacity = std::max<std::size_t>(
        4, static_cast<std::size_t>(points.size()));
    if (frame.points.capacity() < requiredCapacity)
        frame.points.reserve(requiredCapacity);
    int activeContacts = 0;
    for (QTouchEvent::TouchPoint const& source : points) {
        QPointF const position = source.screenPos();
        QPointF const previousPosition = source.lastScreenPos();
        QPointF const startPosition = source.startScreenPos();
        frame.points.emplace_back();
        TouchPoint& point = frame.points.back();
        point.id = source.id();
        point.state = normalizedState(source.state());
        point.x = position.x();
        point.y = position.y();
        point.previousX = previousPosition.x();
        point.previousY = previousPosition.y();
        point.startX = startPosition.x();
        point.startY = startPosition.y();
        if (point.isActive())
            ++activeContacts;
    }
    previousTimestampMs_ = timestamp;

    GestureAction const action = gestures_.observe(frame);
    if (action.isValid() && activeContacts == 0)
        queueAction(currentTarget, action);

    if (activeContacts == 0 || event->type() == QEvent::TouchEnd)
        resetSequence();
}

void PassiveInputRouter::resetSequence() {
    gestures_.reset();
    touchSequenceOpen_ = false;
    sequenceStartedMs_ = 0;
    previousTimestampMs_ = 0;
}

void PassiveInputRouter::cancelInputSequence() {
    resetSequence();
    clearQueuedAction();
    ++inputEpoch_;
}

void PassiveInputRouter::clearQueuedAction() {
    if (actionTimerId_ != 0) {
        killTimer(actionTimerId_);
        actionTimerId_ = 0;
    }
    actionPending_ = false;
    pendingAction_.target.clear();
    pendingAction_.action.id = 0;
    pendingAction_.targetEpoch = 0;
    pendingAction_.inputEpoch = 0;
}

void PassiveInputRouter::clearPendingActions() {
    clearQueuedAction();
    if (actionCooldownTimerId_ != 0) {
        killTimer(actionCooldownTimerId_);
        actionCooldownTimerId_ = 0;
    }
    actionSettling_ = false;
}

void PassiveInputRouter::queueAction(
        QWidget* actionTarget,
        GestureAction const& action) {
    if (!actionTarget || !action.isValid())
        return;
    if (actionDispatching_ || actionSettling_ || actionPending_)
        return;
    pendingAction_.target = actionTarget;
    pendingAction_.action = action;
    pendingAction_.targetEpoch = targetEpoch_;
    pendingAction_.inputEpoch = inputEpoch_;
    actionPending_ = true;
    if (actionTimerId_ == 0) {
        actionTimerId_ = startTimer(kActionSettleDelayMs);
        if (actionTimerId_ == 0)
            clearQueuedAction();
    }
}

bool PassiveInputRouter::eventTargetsTarget(
        QObject* watched,
        QWidget* currentTarget) const {
    if (!watched || !currentTarget)
        return false;
    for (QObject* current = watched; current; current = current->parent()) {
        if (current == currentTarget)
            return true;
    }
    return false;
}

} // namespace passive_input
} // namespace cnt
