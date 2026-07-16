#pragma once

#include "layers_preview.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class QObject;

namespace cnt {
namespace layers_menu {

// Firmware-private object sizes and base offsets used only while constructing
// the native layer popup. The umbrella supplies the values verified for the
// pinned firmware; this aggregate itself has no initialization side effects.
struct Pins {
    size_t nickelTouchMenuBytes;
    size_t toolRowBytes;
    uintptr_t rowGestureReceiverOffset;
    uintptr_t controllerViewOffset;
};

// Copied into Qt signal functors. Every pointer in preview refers to
// process-lifetime PluginState and Pins contains only scalar firmware facts.
struct Dependencies {
    layers_preview::Dependencies preview;
    Pins pins;
};
static_assert(std::is_pod<Pins>::value,
    "layer menu pins must remain inert POD data");
static_assert(std::is_pod<Dependencies>::value,
    "layer menu dependencies must remain copyable POD data");

void showPopup(Dependencies dependencies, QObject* controller);

} // namespace layers_menu
} // namespace cnt
