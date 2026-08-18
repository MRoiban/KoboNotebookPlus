#include "passive_input.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using cnt::passive_input::GestureAction;
using cnt::passive_input::GestureRecognizer;
using cnt::passive_input::GestureRegistry;
using cnt::passive_input::TouchFrame;
using cnt::passive_input::TouchPoint;
using cnt::passive_input::TouchPointState;

namespace {

void require(bool condition, char const* message) {
    if (condition)
        return;
    std::cerr << "passive input test failed: " << message << '\n';
    std::exit(1);
}

class RecordingRecognizer : public GestureRecognizer {
public:
    RecordingRecognizer(
            std::uint32_t actionId,
            std::uint32_t triggerTimestamp,
            int recognizerPriority)
        : actionId_(actionId),
          triggerTimestamp_(triggerTimestamp),
          priority_(recognizerPriority),
          observations_(0),
          resets_(0) {}

    GestureAction observe(TouchFrame const& frame) override {
        ++observations_;
        GestureAction const action = {
            frame.timestampMs == triggerTimestamp_ ? actionId_ : 0
        };
        return action;
    }

    void reset() override {
        ++resets_;
    }

    int priority() const override {
        return priority_;
    }

    int observations() const {
        return observations_;
    }

    int resets() const {
        return resets_;
    }

private:
    std::uint32_t actionId_;
    std::uint32_t triggerTimestamp_;
    int priority_;
    int observations_;
    int resets_;
};

class ThrowingResetRecognizer : public GestureRecognizer {
public:
    ThrowingResetRecognizer() : throwOnReset_(false), resets_(0) {}

    GestureAction observe(TouchFrame const&) override {
        return GestureAction{ 0 };
    }

    void reset() override {
        ++resets_;
        if (throwOnReset_)
            throw 1;
    }

    int priority() const override {
        return 0;
    }

    void enableThrow() {
        throwOnReset_ = true;
    }

    int resets() const {
        return resets_;
    }

private:
    bool throwOnReset_;
    int resets_;
};

TouchFrame frameAt(std::uint32_t timestamp) {
    TouchFrame frame;
    frame.timestampMs = timestamp;
    return frame;
}

void testRegistrationAndResetLifecycle() {
    GestureRegistry registry;
    RecordingRecognizer first(11, 100, 5);
    RecordingRecognizer second(12, 100, 10);

    require(registry.registerRecognizer(first), "first registration failed");
    require(
        !registry.registerRecognizer(first),
        "duplicate registration must be rejected");
    require(
        registry.registerRecognizer(second),
        "second registration failed");
    require(registry.size() == 2, "registry size is wrong");
    require(first.resets() == 1, "registration must reset first recognizer");
    require(second.resets() == 1, "registration must reset second recognizer");

    registry.reset();
    require(first.resets() == 2, "registry reset skipped first recognizer");
    require(second.resets() == 2, "registry reset skipped second recognizer");

    require(
        registry.unregisterRecognizer(first),
        "registered recognizer did not unregister");
    require(first.resets() == 3, "unregister must reset recognizer");
    require(registry.size() == 1, "unregister did not shrink registry");
    require(
        !registry.unregisterRecognizer(first),
        "second unregister must report false");
}

void testEveryRecognizerObservesAndPriorityWins() {
    GestureRegistry registry;
    RecordingRecognizer low(21, 200, 1);
    RecordingRecognizer high(22, 200, 100);
    RecordingRecognizer silent(23, 999, 1000);
    registry.registerRecognizer(low);
    registry.registerRecognizer(high);
    registry.registerRecognizer(silent);

    GestureAction const selected = registry.observe(frameAt(200));
    require(selected.id == 22, "highest-priority action did not win");
    require(low.observations() == 1, "low recognizer missed frame");
    require(high.observations() == 1, "high recognizer missed frame");
    require(silent.observations() == 1, "silent recognizer missed frame");
}

void testRegistrationOrderBreaksPriorityTie() {
    GestureRegistry registry;
    RecordingRecognizer first(31, 300, 7);
    RecordingRecognizer second(32, 300, 7);
    registry.registerRecognizer(first);
    registry.registerRecognizer(second);
    require(
        registry.observe(frameAt(300)).id == 31,
        "first registration must win a priority tie");
}

void testNormalizedFrameMetrics() {
    TouchPoint point;
    point.id = 8;
    point.state = TouchPointState::Moved;
    point.x = 13.0;
    point.y = 24.0;
    point.previousX = 3.0;
    point.previousY = 4.0;
    point.startX = 10.0;
    point.startY = 20.0;

    TouchFrame frame;
    frame.deltaMs = 20;
    frame.points.push_back(point);

    require(frame.activePointCount() == 1, "active count is wrong");
    require(frame.pointById(8) != nullptr, "point lookup failed");
    require(frame.pointById(9) == nullptr, "missing point lookup succeeded");
    require(
        std::abs(point.distanceFromStart() - 5.0) < 0.0001,
        "distance metric is wrong");
    require(
        std::abs(frame.horizontalVelocity(point) - 500.0) < 0.0001,
        "horizontal velocity is wrong");
    require(
        std::abs(frame.verticalVelocity(point) - 1000.0) < 0.0001,
        "vertical velocity is wrong");

    frame.points[0].state = TouchPointState::Released;
    require(frame.activePointCount() == 0, "released point stayed active");
}

void testThrowingResetCannotEscapeRegistry() {
    GestureRegistry registry;
    ThrowingResetRecognizer throwing;
    RecordingRecognizer healthy(41, 400, 1);
    require(
        registry.registerRecognizer(throwing),
        "throwing recognizer registration failed");
    require(
        registry.registerRecognizer(healthy),
        "healthy recognizer registration failed");
    int const healthyResetsBefore = healthy.resets();
    throwing.enableThrow();
    registry.reset();
    require(
        throwing.resets() == 2,
        "throwing recognizer was not reset");
    require(
        healthy.resets() == healthyResetsBefore + 1,
        "throwing recognizer prevented later reset");
}

} // namespace

int main() {
    testRegistrationAndResetLifecycle();
    testEveryRecognizerObservesAndPriorityWins();
    testRegistrationOrderBreaksPriorityTie();
    testNormalizedFrameMetrics();
    testThrowingResetCannotEscapeRegistry();
    std::cout << "passive input tests passed\n";
    return 0;
}
