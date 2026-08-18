#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cnt {
namespace passive_input {

// Zero is reserved for "no action". Action IDs are assigned by the host
// feature, so recognizers can remain independent of firmware entry points.
struct GestureAction {
    std::uint32_t id;

    bool isValid() const {
        return id != 0;
    }
};

enum class TouchFramePhase : std::uint8_t {
    Begin,
    Update,
    End,
};

enum class TouchPointState : std::uint8_t {
    Pressed,
    Moved,
    Stationary,
    Released,
};

struct TouchPoint {
    int id;
    TouchPointState state;
    double x;
    double y;
    double previousX;
    double previousY;
    double startX;
    double startY;

    TouchPoint();

    bool isActive() const;
    double distanceFromStart() const;
};

// A normalized, transport-independent snapshot. target is an opaque identity;
// recognizers must not mutate it. Geometry and timing let recognizers evaluate
// distance and velocity without depending on Qt event objects. The frame and
// all point references are borrowed only for the duration of observe().
struct TouchFrame {
    TouchFramePhase phase;
    std::uint32_t timestampMs;
    std::uint32_t sequenceElapsedMs;
    std::uint32_t deltaMs;
    void const* target;
    double targetWidth;
    double targetHeight;
    std::vector<TouchPoint> points;

    TouchFrame();

    int activePointCount() const;
    TouchPoint const* pointById(int id) const;
    double horizontalVelocity(TouchPoint const& point) const;
    double verticalVelocity(TouchPoint const& point) const;
};

// Recognizers observe copies of the input state. They never own or consume the
// platform event. A valid action must be emitted on a frame with no active
// contacts; the passive router rejects actions emitted before full release.
class GestureRecognizer {
public:
    virtual ~GestureRecognizer() {}

    virtual GestureAction observe(TouchFrame const& frame) = 0;
    virtual void reset() = 0;
    virtual int priority() const = 0;
};

// The registry is non-owning. Every registered recognizer sees every frame.
// If recognizers finish together, the highest priority wins; registration
// order is the deterministic tie-breaker. "Winning" is internal only and
// never affects Kobo's event delivery.
class GestureRegistry {
public:
    bool registerRecognizer(GestureRecognizer& recognizer);
    bool unregisterRecognizer(GestureRecognizer& recognizer);
    void reset() noexcept;
    GestureAction observe(TouchFrame const& frame);
    std::size_t size() const;

private:
    std::vector<GestureRecognizer*> recognizers_;
};

} // namespace passive_input
} // namespace cnt
