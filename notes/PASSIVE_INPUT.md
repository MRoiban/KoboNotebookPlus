# Passive input framework

The notebook gesture extension has one hard contract: custom recognition may
observe Kobo input, but it must never accept, mutate, synthesize, or consume
the original Qt event.

`PassiveInputRouter` is the only application-level event filter installed by
the feature. Its `eventFilter` override is `final` and has one return value:
`false`. It normalizes touchscreen snapshots, passes them to every registered
gesture recognizer, and lets Nickel continue its original dispatch unchanged.

## Components

- `TouchFrame` is a Qt-independent snapshot containing the sequence phase,
  current and elapsed timestamps, frame delta, opaque target identity, target
  size, and all touch-point IDs, states, current/previous/start positions.
  It exposes active-point count, lookup, distance, and velocity helpers.
- `GestureRecognizer` is the interface for custom gesture state machines.
  Its `observe` method receives each frame, `reset` handles cancellation and
  target changes, and `priority` resolves simultaneous custom matches.
- `GestureRegistry` owns no recognizers. It registers and unregisters
  process-lifetime recognizer objects, broadcasts every frame to every
  recognizer, and returns at most one custom action. A higher priority wins;
  registration order breaks ties.
- `PassiveInputRouter` owns the registry, target lifecycle, touchscreen
  normalization, and queued action delivery.
- `NotebookGestureRouter` maps opaque custom action IDs to the pinned Kobo
  history functions.
- `ThreeFingerSwipeRecognizer` drives horizontal undo/redo.

## Recognition and dispatch workflow

```text
Kobo Qt touch event
  -> PassiveInputRouter copies it into TouchFrame
  -> GestureRegistry broadcasts the frame to every recognizer
  -> recognizers update independent state machines
  -> registry resolves simultaneous custom matches
  -> router accepts a result only after all contacts are released
  -> zero-delay timer runs the custom action after Qt dispatch returns

Original Kobo event ---------------------------------------------> unchanged
```

Touch cancellation, target changes, and a missing notebook reset the relevant
recognizers. Movement recognition never joins an already-running touch stream;
it starts only on `TouchBegin`.

## Adding a recognizer

Implement the three-method interface without including Qt:

```cpp
class ExampleRecognizer final
    : public cnt::passive_input::GestureRecognizer {
public:
    cnt::passive_input::GestureAction observe(
        cnt::passive_input::TouchFrame const& frame) override;
    void reset() override;
    int priority() const override;
};
```

The recognizer should:

1. Track only normalized `TouchFrame` values.
2. Reject invalid finger counts, IDs, timing, direction, or distance in its
   own state machine.
3. Return action ID `0` while recognition is incomplete or rejected.
4. Return a nonzero action only on the terminal frame after every finger is
   released. The router deliberately drops early results.
5. Return to idle in `reset`, without retaining the target pointer.

Instantiate the recognizer as a member of the notebook router, inject its
action IDs, then call:

```cpp
gestures().registerRecognizer(exampleRecognizer_);
```

Add the action mapping only in `NotebookGestureRouter::dispatchAction`.
Firmware calls therefore remain outside both the Qt event filter and the
transport-independent recognizer.

This design deliberately cannot suppress a stock page turn or other Kobo
response. A one- or two-finger custom action can coexist with stock behavior,
but the custom recognizer has no mechanism to cancel that stock behavior.

## Verification

`scripts/test-three-finger-swipe.py` enforces the passive event-filter shape,
checks that the core has no Qt dependency, verifies registration of the
horizontal recognizer, verifies that the retired vertical and held-input
paths are absent, and builds two host test executables:

- the existing three-finger thresholds, rejection rules, staggered release,
  cancel, undo, redo, and wrong-axis cases; and
- registry lifecycle, broadcast, conflict-priority, tie-break, normalized
  point lookup, distance, and velocity cases.

The release build additionally compiles the router against Kobo's Qt 5.2.1
headers with `-Werror`.
