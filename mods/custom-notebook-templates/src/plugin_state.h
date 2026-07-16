#pragma once

struct PageRuntimeState {
    bool hooksReady = false;
};

struct HookRuntimeState {
    QTimer* timer = nullptr;
    bool notebookLifecycleHooksReady = false;

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
    cnt::templates::TemplateRuntimeState templates;
    cnt::cover_cache::State covers;
    PageRuntimeState pages;
    cnt::layers::RuntimeState layers;
    cnt::eraser_menu::RuntimeState eraser;
    cnt::SettingsStore settings;
    cnt::visibility::RuntimeState visibility;
    HookRuntimeState hooks;
};
