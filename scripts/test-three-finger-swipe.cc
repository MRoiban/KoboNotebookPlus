#include "three_finger_swipe.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>

using cnt::notebook_gestures::ThreeFingerSwipeConfig;
using cnt::notebook_gestures::ThreeFingerSwipeRecognizer;
using cnt::passive_input::GestureAction;
using cnt::passive_input::TouchFrame;
using cnt::passive_input::TouchFramePhase;
using cnt::passive_input::TouchPoint;
using cnt::passive_input::TouchPointState;

namespace {

enum class SwipeAction : std::uint32_t {
    None = 0,
    Undo = 41,
    Redo = 42,
};

bool operator==(GestureAction const& actual, SwipeAction expected) {
    return actual.id == static_cast<std::uint32_t>(expected);
}

TouchPoint point(
        int id,
        double x,
        double y,
        bool active = true) {
    TouchPoint sample;
    sample.id = id;
    sample.state = active
        ? TouchPointState::Moved : TouchPointState::Released;
    sample.x = x;
    sample.y = y;
    sample.previousX = x;
    sample.previousY = y;
    sample.startX = x;
    sample.startY = y;
    return sample;
}

std::vector<TouchPoint> frame(
        std::initializer_list<TouchPoint> contacts) {
    return std::vector<TouchPoint>(contacts);
}

GestureAction update(
        ThreeFingerSwipeRecognizer& swipe,
        std::uint32_t timestampMs,
        std::vector<TouchPoint> const& contacts) {
    TouchFrame touchFrame;
    touchFrame.phase = swipe.isIdle()
        ? TouchFramePhase::Begin : TouchFramePhase::Update;
    touchFrame.timestampMs = timestampMs;
    touchFrame.points = contacts;
    if (touchFrame.activePointCount() == 0)
        touchFrame.phase = TouchFramePhase::End;
    return swipe.observe(touchFrame);
}

void require(bool condition, char const* message) {
    if (condition)
        return;
    std::cerr << "three-finger swipe test failed: " << message << '\n';
    std::exit(1);
}

ThreeFingerSwipeRecognizer recognizer() {
    ThreeFingerSwipeConfig const config = {
        200,
        10.0,
        100.0,
        0.65,
        30.0,
        0.35,
        0.45,
        0.25,
    };
    return ThreeFingerSwipeRecognizer(
        config,
        GestureAction{
            static_cast<std::uint32_t>(SwipeAction::Undo) },
        GestureAction{
            static_cast<std::uint32_t>(SwipeAction::Redo) });
}

void enroll(ThreeFingerSwipeRecognizer& swipe) {
    require(
        update(swipe, 100, frame({ point(1, 300, 300) }))
            == SwipeAction::None,
        "first contact must not trigger");
    require(
        update(
            swipe,
            150,
            frame({
                point(1, 302, 300),
                point(2, 300, 400),
            })) == SwipeAction::None,
        "second contact must not trigger");
    require(
        update(
            swipe,
            200,
            frame({
                point(1, 302, 300),
                point(2, 300, 400),
                point(3, 300, 500),
            })) == SwipeAction::None,
        "third contact must only arm the recognizer");
}

void releaseAll(
        ThreeFingerSwipeRecognizer& swipe,
        double x,
        SwipeAction expected) {
    require(
        update(
            swipe,
            350,
            frame({
                point(1, x, 300, false),
                point(2, x, 400, false),
                point(3, x, 500, false),
            })) == expected,
        "unexpected action on final release");
}

void testLeftUndoAfterRelease() {
    ThreeFingerSwipeRecognizer swipe = recognizer();
    enroll(swipe);
    require(
        update(
            swipe,
            300,
            frame({
                point(1, 180, 300),
                point(2, 178, 400),
                point(3, 180, 500),
            })) == SwipeAction::None,
        "threshold crossing must wait for release");
    releaseAll(swipe, 178, SwipeAction::Undo);
    require(
        update(swipe, 360, frame({})) == SwipeAction::None,
        "committed gesture must emit once");
}

void testRightRedoAfterStaggeredRelease() {
    ThreeFingerSwipeRecognizer swipe = recognizer();
    enroll(swipe);
    require(
        update(
            swipe,
            300,
            frame({
                point(1, 425, 300),
                point(2, 425, 400),
                point(3, 425, 500),
            })) == SwipeAction::None,
        "right threshold crossing must wait for release");
    require(
        update(
            swipe,
            320,
            frame({
                point(1, 425, 300, false),
                point(2, 425, 400),
                point(3, 425, 500),
            })) == SwipeAction::None,
        "first release must not execute");
    require(
        update(
            swipe,
            330,
            frame({
                point(2, 425, 400, false),
                point(3, 425, 500),
            })) == SwipeAction::None,
        "second release must not execute");
    require(
        update(
            swipe,
            340,
            frame({ point(3, 425, 500, false) }))
            == SwipeAction::Redo,
        "last release must execute redo");
}

void testShortSwipeAndTapAreIgnored() {
    ThreeFingerSwipeRecognizer swipe = recognizer();
    enroll(swipe);
    require(
        update(
            swipe,
            280,
            frame({
                point(1, 250, 300),
                point(2, 250, 400),
                point(3, 250, 500),
            })) == SwipeAction::None,
        "short movement must not trigger");
    releaseAll(swipe, 250, SwipeAction::None);

    enroll(swipe);
    releaseAll(swipe, 300, SwipeAction::None);
}

void testOneAndTwoFingersAreIgnored() {
    ThreeFingerSwipeRecognizer swipe = recognizer();
    require(
        update(swipe, 100, frame({ point(1, 300, 300) }))
            == SwipeAction::None,
        "one finger must be ignored");
    require(
        update(swipe, 120, frame({ point(1, 100, 300) }))
            == SwipeAction::None,
        "one-finger swipe must be ignored");
    require(
        update(swipe, 130, frame({ point(1, 100, 300, false) }))
            == SwipeAction::None,
        "one-finger release must be ignored");

    require(
        update(
            swipe,
            200,
            frame({ point(1, 300, 300), point(2, 300, 400) }))
            == SwipeAction::None,
        "two fingers must be ignored");
    require(
        update(
            swipe,
            220,
            frame({ point(1, 300, 300, false),
                    point(2, 300, 400, false) }))
            == SwipeAction::None,
        "two-finger release must be ignored");
}

void testLateOrMovingEnrollmentIsRejected() {
    ThreeFingerSwipeRecognizer swipe = recognizer();
    update(swipe, 100, frame({ point(1, 300, 300) }));
    update(
        swipe,
        150,
        frame({ point(1, 300, 300), point(2, 300, 400) }));
    require(
        update(
            swipe,
            310,
            frame({
                point(1, 300, 300),
                point(2, 300, 400),
                point(3, 300, 500),
            })) == SwipeAction::None,
        "late third contact must be rejected");
    releaseAll(swipe, 100, SwipeAction::None);

    update(swipe, 400, frame({ point(1, 300, 300) }));
    require(
        update(
            swipe,
            450,
            frame({ point(1, 280, 300), point(2, 300, 400) }))
            == SwipeAction::None,
        "movement during enrollment must reject");
    require(
        update(
            swipe,
            470,
            frame({
                point(1, 150, 300),
                point(2, 150, 400),
                point(3, 150, 500),
            })) == SwipeAction::None,
        "rejected enrollment must stay rejected");
}

void testFourthFingerRejects() {
    ThreeFingerSwipeRecognizer swipe = recognizer();
    enroll(swipe);
    require(
        update(
            swipe,
            250,
            frame({
                point(1, 250, 300),
                point(2, 250, 400),
                point(3, 250, 500),
                point(4, 250, 600),
            })) == SwipeAction::None,
        "fourth finger must reject");
    require(
        update(
            swipe,
            300,
            frame({
                point(1, 150, 300, false),
                point(2, 150, 400, false),
                point(3, 150, 500, false),
                point(4, 150, 600, false),
            })) == SwipeAction::None,
        "four-finger sequence must not execute");
}

void testVerticalStationaryAndSpreadReject() {
    {
        ThreeFingerSwipeRecognizer swipe = recognizer();
        enroll(swipe);
        require(
            update(
                swipe,
                280,
                frame({
                    point(1, 180, 335),
                    point(2, 180, 435),
                    point(3, 180, 535),
                })) == SwipeAction::None,
            "vertical drift must reject");
        releaseAll(swipe, 180, SwipeAction::None);
    }
    {
        ThreeFingerSwipeRecognizer swipe = recognizer();
        enroll(swipe);
        require(
            update(
                swipe,
                280,
                frame({
                    point(1, 100, 300),
                    point(2, 100, 400),
                    point(3, 300, 500),
                })) == SwipeAction::None,
            "stationary finger must reject");
        releaseAll(swipe, 100, SwipeAction::None);
    }
    {
        ThreeFingerSwipeRecognizer swipe = recognizer();
        enroll(swipe);
        require(
            update(
                swipe,
                280,
                frame({
                    point(1, 180, 270),
                    point(2, 180, 400),
                    point(3, 180, 530),
                })) == SwipeAction::None,
            "splay must reject");
        releaseAll(swipe, 180, SwipeAction::None);
    }
}

void testCancelDropsCommittedAction() {
    ThreeFingerSwipeRecognizer swipe = recognizer();
    enroll(swipe);
    update(
        swipe,
        280,
        frame({
            point(1, 180, 300),
            point(2, 180, 400),
            point(3, 180, 500),
        }));
    swipe.cancel();
    require(
        update(
            swipe,
            300,
            frame({
                point(1, 180, 300, false),
                point(2, 180, 400, false),
                point(3, 180, 500, false),
            })) == SwipeAction::None,
        "cancelled gesture must not execute");
}

std::uint32_t nextRandom(std::uint32_t& state) {
    state = state * UINT32_C(1664525) + UINT32_C(1013904223);
    return state;
}

void testDeterministicMalformedSequenceStress() {
    std::uint32_t randomState = UINT32_C(0x4b6f626f);
    int sequenceCount = 100000;
    if (char const* const configured =
            std::getenv("KOBONOTES_STRESS_ITERATIONS")) {
        char* end = nullptr;
        long const parsed = std::strtol(configured, &end, 10);
        if (end && *end == '\0' && parsed >= 1 && parsed <= 1000000)
            sequenceCount = static_cast<int>(parsed);
    }
    for (int sequence = 0; sequence < sequenceCount; ++sequence) {
        ThreeFingerSwipeRecognizer swipe = recognizer();
        int const fingerCount =
            1 + static_cast<int>(nextRandom(randomState) % 4);
        std::uint32_t timestamp = nextRandom(randomState);
        std::vector<TouchPoint> contacts;
        for (int finger = 0; finger < fingerCount; ++finger) {
            contacts.push_back(point(
                finger + 1,
                static_cast<double>(nextRandom(randomState) % 1404),
                static_cast<double>(nextRandom(randomState) % 1872)));
        }

        int emitted = 0;
        GestureAction action = update(swipe, timestamp, contacts);
        emitted += action.isValid() ? 1 : 0;
        require(!action.isValid(), "stress sequence acted before release");

        int const updates =
            1 + static_cast<int>(nextRandom(randomState) % 8);
        for (int step = 0; step < updates; ++step) {
            timestamp += 1 + nextRandom(randomState) % 80;
            for (std::size_t i = 0; i < contacts.size(); ++i) {
                contacts[i].previousX = contacts[i].x;
                contacts[i].previousY = contacts[i].y;
                contacts[i].x += static_cast<int>(
                    nextRandom(randomState) % 161) - 80;
                contacts[i].y += static_cast<int>(
                    nextRandom(randomState) % 81) - 40;
            }
            action = update(swipe, timestamp, contacts);
            emitted += action.isValid() ? 1 : 0;
            require(!action.isValid(), "stress sequence acted while held");
        }

        timestamp += 1 + nextRandom(randomState) % 80;
        for (TouchPoint& contact : contacts)
            contact.state = TouchPointState::Released;
        action = update(swipe, timestamp, contacts);
        emitted += action.isValid() ? 1 : 0;
        require(
            !action.isValid()
                || action == SwipeAction::Undo
                || action == SwipeAction::Redo,
            "stress sequence emitted an unknown action");
        action = update(swipe, timestamp + 1, frame({}));
        emitted += action.isValid() ? 1 : 0;
        require(emitted <= 1, "stress sequence emitted more than once");
    }
}

} // namespace

int main() {
    testLeftUndoAfterRelease();
    testRightRedoAfterStaggeredRelease();
    testShortSwipeAndTapAreIgnored();
    testOneAndTwoFingersAreIgnored();
    testLateOrMovingEnrollmentIsRejected();
    testFourthFingerRejects();
    testVerticalStationaryAndSpreadReject();
    testCancelDropsCommittedAction();
    testDeterministicMalformedSequenceStress();
    std::cout << "three-finger swipe tests passed\n";
    return 0;
}
