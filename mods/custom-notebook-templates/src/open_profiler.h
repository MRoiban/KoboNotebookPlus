#pragma once

// Opt-in wall-clock sampling profiler for notebook open (perf-experiment.txt
// `profile-open=1`). While armed, a 1 ms CLOCK_MONOTONIC timer signals only
// the GUI thread; the handler records PC and LR. Nothing is installed unless
// the experiment key is present, and the signal is ignored (discarding any
// pending tick) before the previous disposition is restored.

namespace cnt {
namespace open_profiler {

struct Sampler;

struct RuntimeState {
    Sampler* sampler = nullptr;
};

// Arms sampling of the calling thread. Returns false when unavailable.
bool begin();

// Disarms and writes the samples with `label` (nullptr: discard). When
// `cutoffMonotonicNs` is set, samples taken after that instant are dropped.
// Safe if not armed.
void end(char const* label, long long cutoffMonotonicNs = 0);

// CLOCK_MONOTONIC now, in nanoseconds.
long long monotonicNs();

} // namespace open_profiler
} // namespace cnt
