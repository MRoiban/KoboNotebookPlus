#pragma once

#include "passive_input.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cnt {
namespace notebook_gestures {

struct ThreeFingerSwipeConfig {
    std::uint32_t enrollmentWindowMs;
    double enrollmentSlopPx;
    double minimumDistancePx;
    double perFingerDistanceRatio;
    double maximumCrossAxisDriftPx;
    double maximumCentroidCrossAxisRatio;
    double maximumFingerCrossAxisRatio;
    double maximumSpreadChangeRatio;
};

ThreeFingerSwipeConfig threeFingerSwipeConfigForSpan(double spanPx);

// The first recognizer built on the passive input framework. Left and right
// actions are injected by its host, keeping gesture math independent of Kobo
// APIs and making it usable in deterministic host tests.
class ThreeFingerSwipeRecognizer final
    : public passive_input::GestureRecognizer {
public:
    ThreeFingerSwipeRecognizer(
        ThreeFingerSwipeConfig const& config,
        passive_input::GestureAction const& negativeAction,
        passive_input::GestureAction const& positiveAction,
        int priority = 100);

    passive_input::GestureAction observe(
        passive_input::TouchFrame const& frame) override;
    void reset() override;
    int priority() const override;

    void cancel();
    void setConfig(ThreeFingerSwipeConfig const& config);
    bool isIdle() const;

private:
    enum class Phase {
        Idle,
        Enrolling,
        Tracking,
        Committed,
        Rejected,
    };

    enum class MotionResult {
        Pending,
        CommitNegative,
        CommitPositive,
        Reject,
    };

    struct Finger {
        int id;
        double firstX;
        double firstY;
        double startX;
        double startY;
        double currentX;
        double currentY;
        bool active;
    };

    void reject();
    int findFinger(int id) const;
    bool addEnrollmentFinger(
        passive_input::TouchPoint const& contact);
    bool beginTracking();
    MotionResult evaluateMotion() const;
    bool updateTrackedContacts(
        std::vector<passive_input::TouchPoint> const& contacts);

    ThreeFingerSwipeConfig config_;
    passive_input::GestureAction negativeAction_;
    passive_input::GestureAction positiveAction_;
    int priority_;
    Phase phase_;
    Finger fingers_[3];
    int fingerCount_;
    std::uint32_t enrollmentStartedMs_;
    double initialPairDistances_[3];
    passive_input::GestureAction committedAction_;
};

} // namespace notebook_gestures
} // namespace cnt
