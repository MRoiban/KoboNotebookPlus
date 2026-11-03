#pragma once

#include "cover_cache.h"
#include "eraser_menu.h"
#include "export_range.h"
#include "firmware_api.h"
#include "layers.h"
#include "notebook_gestures.h"
#include "notebook_sleep.h"
#include "open_profiler.h"
#include "page_io.h"
#include "prewarm.h"
#include "settings.h"
#include "perf_trace.h"

#include <memory>
#include "templates.h"
#include "visibility.h"

#include <QTimer>

namespace cnt {
namespace perf {
class EditScope;
}
}

struct PageRuntimeState {
    bool hooksReady = false;
};

struct HookRuntimeState {
    QTimer* timer = nullptr;
    bool notebookLifecycleHooksReady = false;
    int renderVolumeDepth = 0;

    void stopTimer() {
        if (!timer)
            return;
        timer->stop();
        timer->deleteLater();
        timer = nullptr;
    }
};

// Process-lifetime state is published before the first hook mutation. The
// object is intentionally never moved or destroyed: firmware callbacks and
// writable vtable clones may remain reachable through Nickel shutdown.
struct PluginState {
    FirmwareApi firmware = {};
    // Index of the audited libnickel profile (see nickel_pins.h); -1 until
    // detectNickelProfile() proves an exact match, which keeps every
    // libnickel pin failing closed.
    int nickelProfile = -1;
    // The notebook edit currently being timed (perf_trace.h), if any.
    cnt::perf::EditScope* activeEdit = nullptr;
    std::unique_ptr<cnt::perf::PaintObserver> paintObserver;
    cnt::templates::TemplateRuntimeState templates;
    cnt::cover_cache::State covers;
    cnt::page_io::RuntimeState backups;
    cnt::open_profiler::RuntimeState openProfiler;
    cnt::prewarm::RuntimeState prewarm;
    PageRuntimeState pages;
    cnt::export_range::RuntimeState exportRange;
    cnt::layers::RuntimeState layers;
    cnt::eraser_menu::RuntimeState eraser;
    cnt::SettingsStore settings;
    cnt::notebook_gestures::RuntimeState notebookGestures;
    cnt::notebook_sleep::RuntimeState notebookSleep;
    cnt::visibility::RuntimeState visibility;
    HookRuntimeState hooks;
};
