#include "passive_input.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cnt {
namespace passive_input {

TouchPoint::TouchPoint()
    : id(0),
      state(TouchPointState::Stationary),
      x(0.0),
      y(0.0),
      previousX(0.0),
      previousY(0.0),
      startX(0.0),
      startY(0.0) {}

bool TouchPoint::isActive() const {
    return state != TouchPointState::Released;
}

double TouchPoint::distanceFromStart() const {
    double const dx = x - startX;
    double const dy = y - startY;
    return std::sqrt(dx * dx + dy * dy);
}

TouchFrame::TouchFrame()
    : phase(TouchFramePhase::Update),
      timestampMs(0),
      sequenceElapsedMs(0),
      deltaMs(0),
      target(nullptr),
      targetWidth(0.0),
      targetHeight(0.0),
      points() {}

int TouchFrame::activePointCount() const {
    int count = 0;
    for (TouchPoint const& point : points) {
        if (point.isActive())
            ++count;
    }
    return count;
}

TouchPoint const* TouchFrame::pointById(int id) const {
    for (TouchPoint const& point : points) {
        if (point.id == id)
            return &point;
    }
    return nullptr;
}

double TouchFrame::horizontalVelocity(TouchPoint const& point) const {
    if (deltaMs == 0)
        return 0.0;
    return (point.x - point.previousX)
        * 1000.0 / static_cast<double>(deltaMs);
}

double TouchFrame::verticalVelocity(TouchPoint const& point) const {
    if (deltaMs == 0)
        return 0.0;
    return (point.y - point.previousY)
        * 1000.0 / static_cast<double>(deltaMs);
}

bool GestureRegistry::registerRecognizer(GestureRecognizer& recognizer) {
    if (std::find(
            recognizers_.begin(), recognizers_.end(), &recognizer)
            != recognizers_.end()) {
        return false;
    }
    recognizer.reset();
    recognizers_.push_back(&recognizer);
    return true;
}

bool GestureRegistry::unregisterRecognizer(GestureRecognizer& recognizer) {
    std::vector<GestureRecognizer*>::iterator const found = std::find(
        recognizers_.begin(), recognizers_.end(), &recognizer);
    if (found == recognizers_.end())
        return false;
    recognizer.reset();
    recognizers_.erase(found);
    return true;
}

void GestureRegistry::reset() noexcept {
    for (GestureRecognizer* recognizer : recognizers_) {
        if (!recognizer)
            continue;
        try {
            recognizer->reset();
        } catch (...) {
            // One custom recognizer must not unwind through Qt or prevent the
            // remaining recognizers from resetting their sequence state.
        }
    }
}

GestureAction GestureRegistry::observe(TouchFrame const& frame) {
    GestureAction selected = { 0 };
    int selectedPriority = std::numeric_limits<int>::min();
    for (GestureRecognizer* recognizer : recognizers_) {
        if (!recognizer)
            continue;
        GestureAction const candidate = recognizer->observe(frame);
        if (!candidate.isValid())
            continue;
        int const candidatePriority = recognizer->priority();
        if (!selected.isValid() || candidatePriority > selectedPriority) {
            selected = candidate;
            selectedPriority = candidatePriority;
        }
    }
    return selected;
}

std::size_t GestureRegistry::size() const {
    return recognizers_.size();
}

} // namespace passive_input
} // namespace cnt
