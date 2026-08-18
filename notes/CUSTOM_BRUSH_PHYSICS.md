# Custom brush physics

This note documents what "custom brush physics" means on Kobo firmware
`4.38.23697`, where the practical extension points are, and why a genuinely
new brush engine remains a high-risk project.

The short version is:

- A custom pressure response is feasible. Kobo already transforms stylus
  pressure in `libiinkuiref.so.1.0.0` before forwarding pointer samples to
  MyScript.
- Velocity-sensitive pressure, pressure smoothing, or coordinate filtering
  should also be possible by intercepting that sample path.
- A new nib model is not exposed by Kobo's UI layer, but deeper analysis of
  `libiink.so` found an explicit registry for user-defined
  `IStrokerFactory`/`IStroker` implementations. This is a real extension point
  for custom vector-path geometry.
- The custom stroker writes to `graphics::IPath`. Textures, wet-ink
  simulation, particles, and arbitrary per-pixel raster effects may still
  require a parallel renderer rather than merely another registered stroker.
- Custom sample preprocessing can continue to use a stock MyScript brush and
  therefore has a plausible persistence/export story. A parallel renderer
  does not inherit one automatically.

No binary patch or runtime hook described below has been installed or tested
on the device. Addresses and call relationships are confirmed statically for
this firmware; implementation proposals are marked as such.

## Analysed targets

Binary Ninja loads all four shared libraries at image base `0x10000`. The primary
addresses below are Binary Ninja addresses. The parenthesized address is the
corresponding ELF-relative VMA.

| Binary Ninja database | Architecture/view | Fingerprint | Role |
| --- | --- | --- | --- |
| `binary-ninja/libiinknote-4.38.23697.bndb` | ARMv7, ELF; Thumb-2 functions | `afdbf399e765da5670be61a38a2c82cd20131fe23a7cdceff778b983c1964683` | Kobo notebook UI, tool selection, and pressure-settings menu |
| `binary-ninja/libiinkuiref-4.38.23697.bndb` | ARMv7, ELF; Thumb-2 functions | `05f15c7d4a38a483a6aa1d77729da239ced2b160e2f241a6da54c90c81d29123` | CSS construction, stylus normalization, Qt pointer signals, and MyScript render-target adapter |
| Raw `libiink.so` view | ARMv7, ELF | `7ab9ecbe55afc8a0db7a5d8b2893ed34ddbfdd660e9bceeb18656519d2a5380e` | Editor input, CSS parsing, built-in strokers, user-defined stroker registry, and path rendering |
| Raw `libMyScriptInk.so` view | ARMv7, ELF | `6b31f190c1a7e2e831ba3ed9501acca27a657eeedff37287acf2443e0cfc563f` | Digital-ink stroke model and BINK/JINK/JINKSON codecs |

Analysis was complete and idle for all views. The investigation was
read-only; no Binary Ninja capabilities were enabled and no analysis database
was modified.

## Ownership boundary

The drawing path has three distinct layers:

```text
libiinknote.so
  chooses IInkTool + selector, e.g. "brush-2-0"
  selects a PressureCurve and minimum pressure
        |
        v
libiinkuiref.so.1.0.0
  converts QTouchEvent position/pressure
  applies calculateNormalizedPressure(rawPressure)
  emits pointerDown/pointerMove/pointerUp with:
    x, y, timestamp, pressure, PointerType, pointerId
        |
        v
myscript::iink::Editor virtual interface
  receives the pointer sample
        |
        v
libiink.so / libMyScriptInk.so and related MyScript code
  constructs, stores, and renders the stroke using a named brush engine
```

Kobo controls the selector, numeric width/color, minimum pressure, and the
pressure value sent with each point. The UI-reference call surface does not
pass nib geometry, bristle state, texture, tilt, azimuth, or a user callback
to the editor.

That makes sample preprocessing a realistic mod target. It also identifies
the point where a new renderer would stop being a Kobo UI modification and
become reverse engineering of the MyScript engine itself.

## Tool style selection is declarative

`IInkEditorCss::getToolStyles()` at `0x33970` (`0x23970`) returns a static CSS
table. Its selectors choose a named MyScript brush and only a few visible
properties:

```css
.ballpoint-0-0 {
    -myscript-pen-brush: FeltPen;
    -myscript-pen-width: 0.4;
    color: #000000;
}

.brush-0-0 {
    -myscript-pen-brush: CalligraphicBrush;
    -myscript-pen-width: 0.5;
    color: #000000;
}
```

The complete string begins at `0x45cd8` (`0x35cd8`).
`IInkEditorCss::setCss(Editor, float)` at `0x3398c` (`0x2398c`) combines the
table with the rest of the editor CSS and invokes the MyScript editor's CSS
setter through its virtual interface.

This code selects an engine; it does not implement `FeltPen`, `FountainPen`,
`CalligraphicQuill`, or `CalligraphicBrush`. No registration interface is
visible in these two Kobo-facing libraries. The deeper `libiink.so` renderer,
however, does provide one: `RendererImpl::registerStroker()` at `0x4b15fc`.
Its lookup path checks registered names before built-ins, so a registered name
should be selectable through `-myscript-pen-brush`. This last step is a strong
static inference, not yet an on-device result.

## Kobo already contains a pressure-physics subsystem

The notebook library contains a feature-gated pressure menu:

- `FeatureSettings::iinkPressureSettings()` import at `0x4530c`.
- `IInkToolBar::onPressureButtonTapped()` at `0x80890` (`0x70890`).
- `IInkPressureMenuController::loadView()` at `0x7948c` (`0x6948c`).
- `IInkNotePadWidget::updateActivePressureCurve()` at `0x68608`
  (`0x58608`).
- `IInkNotePadWidget::setMinPressure(float)` at `0x685b0` (`0x585b0`).

`updateActivePressureCurve()` forwards the chosen enum to
`myscript::iink::uireferenceimplementation::EditorWidget::setPressureCurve()`.
`setMinPressure()` forwards the threshold in the same way.

In `libiinkuiref.so.1.0.0`, these setters simply store values in the
`EditorWidget` object:

| Binary Ninja | ELF-relative | Function | Stored field |
| --- | --- | --- | --- |
| `0x2d6c4` | `0x1d6c4` | `EditorWidget::setPressureCurve(PressureCurve)` | enum at `this + 0x48` |
| `0x2d6d4` | `0x1d6d4` | `EditorWidget::setMinPressure(float)` | float at `this + 0x4c` |

The actual transform is
`EditorWidget::calculateNormalizedPressure(float)` at `0x2d3b8`
(`0x1d3b8`). For curves 1 through 10, its final operation is:

```text
normalized = minPressure + (1 - minPressure) * curve(rawPressure)
```

The pressure-menu slots load these enum values:

| Enum | Slot name | Implemented transform before minimum-pressure scaling |
| ---: | --- | --- |
| 0 | `flatPressure` | returns `0` directly |
| 1 | `linearPressure` | `p` |
| 2 | `quadraticPressure` | `p^2` |
| 3 | `cubicPressure` | `p^3` |
| 4 | `quinticPressure` | `p^4` |
| 5 | `quarticPressure` | `p^5` |
| 6 | `sixDegreePressure` | `p^6` |
| 7 | `sqrtPressure` | `sqrt(p)` |
| 8 | `sqrtSqrtPressure` | `sqrt(sqrt(p))` |
| 9 | `smoothstepPressure` | `p^2 * (3 - 2p)` |
| 10 | `doubleSmoothstepPressure` | `smoothstep(smoothstep(p))` |

The apparent quartic/quintic label swap in enum values 4 and 5 is present in
the machine code: the menu slots pass `4` and `5` respectively, while the
normalizer computes fourth and fifth powers in that order.

An enum outside `1..10` takes the normalizer's default branch, whose curve
value is `1.0`. Therefore, inventing enum value `11` does not create an
extension slot; it produces full normalized pressure after the affine blend.
A custom curve requires replacing/intercepting the transform or preprocessing
the sample elsewhere.

## Where transformed samples enter MyScript

`EditorWidget::stylusTouchUpdate(QTouchEvent*)` reads
`QTouchEvent::TouchPoint::pressure()` and calls the pressure normalizer at
`0x2d97c` (`0x1d97c`). `stylusTouchEnd(QTouchEvent*)` repeats the operation at
`0x2db4c` (`0x1db4c`). Both calls go through the module's PLT/import stub at
`0x29748` (`0x19748`), rather than directly to the implementation body.

After normalization, `EditorWidget` emits a Qt pointer signal. The signal
contains:

```text
shared_ptr<Editor>, x, y, timestamp, pressure, PointerType, pointerId
```

`IInkRenderTarget::pointerMove()` at `0x3715c` (`0x2715c`) receives that signal
and invokes the MyScript `Editor` through virtual slot `+0x50` at call site
`0x37196` (`0x27196`). That unresolved virtual call is the confirmed boundary
between the visible Kobo/UI-reference sample pipeline and the deeper MyScript
stroke implementation.

The begin path is separate (`EditorWidget::stylusTouchBegin()` at `0x2d710`),
and no direct normalizer call was found in it. A runtime prototype must test
the first point of every stroke rather than assuming that hooking only update
and end produces a seamless curve.

## Feasible modification levels

### 1. Expose the stock pressure controls — medium-low risk

The safest physics-related modification is to expose Kobo's existing pressure
menu or reproduce its controls in the pen popup. It already offers eleven
curve choices and a minimum-pressure setting. This does not add a new curve,
but it substantially changes how the stock brushes respond.

Implementation candidates:

- enable or bypass the `iinkPressureSettings` feature gate;
- call `EditorWidget::setPressureCurve()` and `setMinPressure()` through the
  notebook widget's existing forwarding methods; or
- add curve/minimum controls to a custom menu row and reuse the existing
  `IInkNotePadWidget` settings path.

### 2. Add an arbitrary pressure curve — medium risk

A NickelHook plugin could interpose the exported mangled symbol for
`EditorWidget::calculateNormalizedPressure(float)`. The two update/end call
sites already use the PLT stub, which is favourable for `nh_dlhook`-style
interposition.

A candidate hook would:

1. keep the original function pointer;
2. activate only for an explicitly selected custom preset;
3. clamp raw input to a tested domain;
4. compute a deterministic `f(p)`;
5. apply the minimum-pressure policy intentionally; and
6. call the original function for every stock curve and failure path.

This requires careful ARM hard-float ABI validation: the method has an
implicit `this` pointer and a `float` argument/return value. The hook should be
firmware-pinned and must fail closed when the expected symbol, VMA, or mapping
does not match.

### 3. Add velocity/smoothing dynamics — medium-high risk

Pressure can be synthesized from recent pointer samples before the signal
reaches `IInkRenderTarget`, for example:

```text
effectivePressure = mix(
    pressureCurve(rawPressure),
    velocityToPressure(distance / deltaTime),
    velocityWeight
)
```

This could implement pressure smoothing, velocity-sensitive width, tapering,
or jitter filtering while still sending ordinary samples to a stock MyScript
brush. It needs per-stroke state keyed by pointer id and strict handling for
begin/cancel/end. Latency, timestamp units, event coalescing, and sampling rate
must be measured on-device.

Intercepting `calculateNormalizedPressure()` alone is insufficient for
velocity-based behaviour because it receives only one pressure value and no
position or timestamp. A velocity-aware hook must operate around
`stylusTouchBegin/Update/End`, the pointer Qt signals, or the render-target
slots.

### 4. Add a custom vector nib — high risk, but architecturally supported

`libiink.so` contains a user-defined stroker registry:

| Binary Ninja | Function |
| --- | --- |
| `0x4b15fc` | `RendererImpl::registerStroker(name, factory)` |
| `0x4b1014` | `RendererImpl::unregisterStroker(name)` |
| `0x4b10d0` | `RendererImpl::getStroker(name)` |
| `0x4d7040` | `UserDefinedStrokerWrapper::stroke` |

The wrapper converts the engine's internal path points to
`vector<graphics::InkPoint>`, calls the user `IStroker`, and supplies a
`graphics::IPath` output. This can support fundamentally different vector nib
geometry without replacing the whole MyScript document engine.

The difficulty is integration rather than absence of an extension point. The
firmware does not ship the needed C++ headers, and a Kobo plugin must match the
32-bit ARM C++ ABI, vtables, `shared_ptr` ABI, `InkPoint` layout, factory
lifetime, and renderer-registration timing. It must also register the same
name in every process that renders the document.

### 5. Add arbitrary raster or simulation effects — very high risk

A textured pencil, wet-ink simulator, particle spray, or other per-pixel
effect may not fit the observed `IPath` output contract. If it cannot be
reduced to vector geometry, it requires bypassing MyScript drawing or adding a
parallel renderer plus a storage/export representation.

That option is effectively a new drawing engine. It must solve live e-ink
rendering, undo/redo, selection, erasing, notebook persistence, thumbnailing,
PDF/image export, and reload in processes that do not host the same plugin.

## Persistence and export consequences

There are three materially different cases:

1. **Stock engine, custom input transform.** The mod sends modified pressure or
   coordinates into an existing MyScript brush. There is a reasonable chance
   the resulting stroke remains serializable and reproducible by Kobo's stock
   thumbnail/export processes, but this must be verified by save/reopen,
   thumbnail, and export tests.
2. **Registered custom stroker.** The document may retain samples and a brush
   name rather than generated pixels. Kobo's separate `iink` executable must
   therefore register the same stroker before export and thumbnail rendering,
   or the custom path must be materialized into stock-compatible document
   geometry. This remains to be tested.
3. **Parallel custom renderer.** A visual produced only inside Nickel will not
   automatically exist in save/reload, thumbnail, or export paths. This needs
   a cross-process representation and equivalent rendering support.

For that reason, pressure/coordinate preprocessing on top of a known brush is
the highest-leverage interpretation of "custom brush physics" for a first
implementation.

## Recommended prototype order

1. Expose the existing pressure menu without altering any transform.
2. Verify all eleven curves and minimum pressure on a disposable notebook.
3. Add one custom pressure curve through a symbol/VMA-guarded hook.
4. Test stroke begin, update, end, cancel, undo, erase, save/reopen, thumbnail,
   and export.
5. Add velocity-based state only after the stateless curve is stable.
6. Recover the exact custom-stroker ABI and test a trivial registered vector
   stroker under temporary CSS before adding a menu row.
7. Keep arbitrary raster effects as a separate project; do not mix them into
   the menu/UI mod.

## Confidence and open questions

Confirmed statically:

- CSS chooses named MyScript engines rather than implementing them.
- Kobo/UI-reference code owns pressure normalization.
- Eleven built-in pressure curves and a minimum-pressure value exist.
- Normalized pressure reaches MyScript through pointer events and an unresolved
  `Editor` virtual call.
- Invalid custom curve enum values fall back to a curve value of `1.0`.
- `libiink.so` contains a name-to-`IStrokerFactory` registry checked before
  built-in brush-name dispatch.
- A user-defined stroker consumes ink points and writes a `graphics::IPath`.

Still requires runtime validation:

- whether the feature-gated pressure menu is stable on the Elipsa 2E build;
- how stroke-begin pressure is initialized;
- whether transformed pressure is serialized in a way that stock thumbnail and
  export services reproduce exactly;
- whether the normalizer symbol can be reliably interposed with NickelHook on
  this firmware's loader;
- the exact `IStrokerFactory`, `IStroker`, `InkPoint`, and `IPath` ABI;
- whether a custom brush name and its generated geometry survive save/reopen;
  and
- whether registering the same stroker in the export/thumbnail process is
  sufficient for full fidelity.

See `notes/MYSCRIPT.md` for the complete module, renderer, stroker, and
persistence map.
