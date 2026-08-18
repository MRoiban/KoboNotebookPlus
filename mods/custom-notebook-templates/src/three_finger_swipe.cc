#include "three_finger_swipe.h"

#include <algorithm>
#include <cmath>

namespace cnt {
namespace notebook_gestures {
namespace {

using passive_input::GestureAction;
using passive_input::TouchPoint;

double distance(double x1, double y1, double x2, double y2) {
    double const dx = x2 - x1;
    double const dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

bool validContact(TouchPoint const& contact) {
    return std::isfinite(contact.x) && std::isfinite(contact.y);
}

int activeContactCount(std::vector<TouchPoint> const& contacts) {
    int count = 0;
    for (TouchPoint const& contact : contacts) {
        if (contact.isActive())
            ++count;
    }
    return count;
}

bool hasDuplicateActiveIds(std::vector<TouchPoint> const& contacts) {
    for (std::size_t i = 0; i < contacts.size(); ++i) {
        if (!contacts[i].isActive())
            continue;
        for (std::size_t j = i + 1; j < contacts.size(); ++j) {
            if (contacts[j].isActive()
                    && contacts[i].id == contacts[j].id) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

ThreeFingerSwipeConfig threeFingerSwipeConfigForSpan(double spanPx) {
    double const span = std::isfinite(spanPx) && spanPx >= 400.0
        ? spanPx : 1404.0;
    ThreeFingerSwipeConfig const config = {
        220,
        std::max(20.0, span * 0.02),
        std::max(120.0, span * 0.12),
        0.65,
        std::max(40.0, span * 0.04),
        0.35,
        0.45,
        0.25,
    };
    return config;
}

ThreeFingerSwipeRecognizer::ThreeFingerSwipeRecognizer(
        ThreeFingerSwipeConfig const& config,
        GestureAction const& negativeAction,
        GestureAction const& positiveAction,
        int priority)
    : config_(config),
      negativeAction_(negativeAction),
      positiveAction_(positiveAction),
      priority_(priority),
      phase_(Phase::Idle),
      fingers_(),
      fingerCount_(0),
      enrollmentStartedMs_(0),
      initialPairDistances_(),
      committedAction_({ 0 }) {}

void ThreeFingerSwipeRecognizer::reset() {
    phase_ = Phase::Idle;
    fingerCount_ = 0;
    enrollmentStartedMs_ = 0;
    committedAction_ = GestureAction{ 0 };
    for (Finger& finger : fingers_)
        finger = Finger();
    for (double& pairDistance : initialPairDistances_)
        pairDistance = 0.0;
}

void ThreeFingerSwipeRecognizer::cancel() {
    reset();
}

int ThreeFingerSwipeRecognizer::priority() const {
    return priority_;
}

void ThreeFingerSwipeRecognizer::setConfig(
        ThreeFingerSwipeConfig const& config) {
    config_ = config;
    reset();
}

bool ThreeFingerSwipeRecognizer::isIdle() const {
    return phase_ == Phase::Idle;
}

void ThreeFingerSwipeRecognizer::reject() {
    phase_ = Phase::Rejected;
    committedAction_ = GestureAction{ 0 };
}

int ThreeFingerSwipeRecognizer::findFinger(int id) const {
    for (int i = 0; i < fingerCount_; ++i) {
        if (fingers_[i].id == id)
            return i;
    }
    return -1;
}

bool ThreeFingerSwipeRecognizer::addEnrollmentFinger(
        TouchPoint const& contact) {
    if (!validContact(contact) || fingerCount_ >= 3)
        return false;
    Finger& finger = fingers_[fingerCount_++];
    finger.id = contact.id;
    finger.firstX = contact.x;
    finger.firstY = contact.y;
    finger.startX = contact.x;
    finger.startY = contact.y;
    finger.currentX = contact.x;
    finger.currentY = contact.y;
    finger.active = contact.isActive();
    return true;
}

bool ThreeFingerSwipeRecognizer::beginTracking() {
    if (fingerCount_ != 3)
        return false;
    for (Finger& finger : fingers_) {
        finger.startX = finger.currentX;
        finger.startY = finger.currentY;
        finger.active = true;
    }
    initialPairDistances_[0] = distance(
        fingers_[0].startX, fingers_[0].startY,
        fingers_[1].startX, fingers_[1].startY);
    initialPairDistances_[1] = distance(
        fingers_[0].startX, fingers_[0].startY,
        fingers_[2].startX, fingers_[2].startY);
    initialPairDistances_[2] = distance(
        fingers_[1].startX, fingers_[1].startY,
        fingers_[2].startX, fingers_[2].startY);
    for (double const pairDistance : initialPairDistances_) {
        if (pairDistance < 1.0 || !std::isfinite(pairDistance))
            return false;
    }
    phase_ = Phase::Tracking;
    return true;
}

ThreeFingerSwipeRecognizer::MotionResult
ThreeFingerSwipeRecognizer::evaluateMotion() const {
    double centroidPrimary = 0.0;
    double centroidCross = 0.0;
    for (Finger const& finger : fingers_) {
        double const dx = finger.currentX - finger.startX;
        double const dy = finger.currentY - finger.startY;
        centroidPrimary += dx;
        centroidCross += dy;
    }
    centroidPrimary /= 3.0;
    centroidCross /= 3.0;

    if (std::abs(centroidCross) > config_.maximumCrossAxisDriftPx)
        return MotionResult::Reject;

    double const currentPairDistances[3] = {
        distance(
            fingers_[0].currentX, fingers_[0].currentY,
            fingers_[1].currentX, fingers_[1].currentY),
        distance(
            fingers_[0].currentX, fingers_[0].currentY,
            fingers_[2].currentX, fingers_[2].currentY),
        distance(
            fingers_[1].currentX, fingers_[1].currentY,
            fingers_[2].currentX, fingers_[2].currentY),
    };
    for (int i = 0; i < 3; ++i) {
        double const change = std::abs(
            currentPairDistances[i] - initialPairDistances_[i]);
        if (change / initialPairDistances_[i]
                > config_.maximumSpreadChangeRatio) {
            return MotionResult::Reject;
        }
    }

    double const primaryDistance = std::abs(centroidPrimary);
    if (primaryDistance < config_.minimumDistancePx)
        return MotionResult::Pending;

    if (std::abs(centroidCross)
            > primaryDistance
                * config_.maximumCentroidCrossAxisRatio) {
        return MotionResult::Reject;
    }

    double const direction = centroidPrimary < 0.0 ? -1.0 : 1.0;
    double const requiredFingerDistance =
        config_.minimumDistancePx * config_.perFingerDistanceRatio;
    for (Finger const& finger : fingers_) {
        double const dx = finger.currentX - finger.startX;
        double const dy = finger.currentY - finger.startY;
        double const primary = dx;
        double const cross = dy;
        double const projectedDistance = primary * direction;
        if (projectedDistance < requiredFingerDistance
                || std::abs(cross) > config_.maximumCrossAxisDriftPx
                || std::abs(cross)
                    > projectedDistance
                        * config_.maximumFingerCrossAxisRatio) {
            return MotionResult::Reject;
        }
    }

    return direction < 0.0
        ? MotionResult::CommitNegative : MotionResult::CommitPositive;
}

bool ThreeFingerSwipeRecognizer::updateTrackedContacts(
        std::vector<TouchPoint> const& contacts) {
    bool seen[3] = { false, false, false };
    for (TouchPoint const& contact : contacts) {
        if (!validContact(contact))
            return false;
        int const index = findFinger(contact.id);
        if (index < 0)
            return false;
        if (seen[index])
            return false;
        seen[index] = true;
        Finger& finger = fingers_[index];
        finger.currentX = contact.x;
        finger.currentY = contact.y;
        finger.active = contact.isActive();
    }

    // Qt includes every currently active touch point in each snapshot.
    for (int i = 0; i < 3; ++i) {
        if (fingers_[i].active && !seen[i])
            return false;
    }
    return true;
}

GestureAction ThreeFingerSwipeRecognizer::observe(
        passive_input::TouchFrame const& frame) {
    std::uint32_t const timestampMs = frame.timestampMs;
    std::vector<TouchPoint> const& contacts = frame.points;
    int const activeCount = activeContactCount(contacts);
    if (hasDuplicateActiveIds(contacts)) {
        reject();
        return GestureAction{ 0 };
    }

    if (phase_ == Phase::Rejected) {
        if (activeCount == 0)
            reset();
        return GestureAction{ 0 };
    }

    if (phase_ == Phase::Idle) {
        if (activeCount == 0)
            return GestureAction{ 0 };
        phase_ = Phase::Enrolling;
        enrollmentStartedMs_ = timestampMs;
    }

    if (phase_ == Phase::Enrolling) {
        if (activeCount == 0) {
            reset();
            return GestureAction{ 0 };
        }
        if (activeCount > 3) {
            reject();
            return GestureAction{ 0 };
        }

        bool present[3] = { false, false, false };
        for (TouchPoint const& contact : contacts) {
            if (!contact.isActive())
                continue;
            if (!validContact(contact)) {
                reject();
                return GestureAction{ 0 };
            }
            int index = findFinger(contact.id);
            if (index < 0) {
                if (!addEnrollmentFinger(contact)) {
                    reject();
                    return GestureAction{ 0 };
                }
                index = fingerCount_ - 1;
            }
            present[index] = true;
            Finger& finger = fingers_[index];
            finger.currentX = contact.x;
            finger.currentY = contact.y;
            finger.active = true;
            if (distance(
                    finger.firstX, finger.firstY,
                    finger.currentX, finger.currentY)
                    > config_.enrollmentSlopPx) {
                reject();
                return GestureAction{ 0 };
            }
        }

        for (int i = 0; i < fingerCount_; ++i) {
            if (!present[i]) {
                reject();
                return GestureAction{ 0 };
            }
        }

        std::uint32_t const elapsed =
            timestampMs - enrollmentStartedMs_;
        if (elapsed > config_.enrollmentWindowMs) {
            reject();
            return GestureAction{ 0 };
        }
        if (activeCount < 3)
            return GestureAction{ 0 };
        if (!beginTracking()) {
            reject();
            return GestureAction{ 0 };
        }
        return GestureAction{ 0 };
    }

    if (!updateTrackedContacts(contacts)) {
        reject();
        if (activeCount == 0)
            reset();
        return GestureAction{ 0 };
    }

    if (phase_ == Phase::Tracking) {
        MotionResult const motion = evaluateMotion();
        if (motion == MotionResult::Reject) {
            reject();
            if (activeCount == 0)
                reset();
            return GestureAction{ 0 };
        }
        if (motion == MotionResult::CommitNegative) {
            phase_ = Phase::Committed;
            committedAction_ = negativeAction_;
        } else if (motion == MotionResult::CommitPositive) {
            phase_ = Phase::Committed;
            committedAction_ = positiveAction_;
        }

        if (phase_ == Phase::Tracking && activeCount < 3) {
            reject();
            if (activeCount == 0)
                reset();
            return GestureAction{ 0 };
        }
    }

    if (phase_ == Phase::Committed && activeCount == 0) {
        GestureAction const action = committedAction_;
        reset();
        return action;
    }
    return GestureAction{ 0 };
}

} // namespace notebook_gestures
} // namespace cnt
