# Writing latency and feel

Target firmware: Kobo 4.38.23697, Elipsa 2E. This note maps every latency- and
feel-related mechanism found in the shipped binaries, ordered by risk. The
headline finding: **Kobo already ships hidden, settings-file-controlled latency
tuning** — the first experiments require no code at all.

All addresses/symbols verified statically on the extracted rootfs with
`strings`/`readelf`/cross-`objdump` (NickelTC container). Nothing here has been
installed or tested on-device yet.

## 1. Zero-code: hidden developer latency settings (verified end to end)

Nickel's hidden developer-options page
(`N3SettingsDeveloperOptionsView.cpp` in `libnickel`, unlocked by typing
`devmodeon` in the home search box) contains a dropdown labelled with the
choices **"Deactivate smoother" / "Reduce margin" / "Reduce margin and
coefficient"** (strings at `libnickel` `0x140ebe8..0x140ec30`).

It persists as `[DeveloperSettings] ReduceIInkUpdateAreaBy=<int>` in
`.kobo/Kobo/Kobo eReader.conf` (accessors
`DevSettings::reduceIInkUpdateAreaBy()` / `setReduceIInkUpdateAreaBy(int)`).

The consumer is the `IInkNotePad` constructor in `libiinknote.so` (call site
`0x4dec0`), which applies MyScript engine configuration at notebook open:

| Value | Engine configuration applied | Source-string evidence |
|---|---|---|
| 0 | nothing (stock) | branch at `0x4dec4` skips |
| 1 | `renderer.stroker.smoother = "disabled"` | "Reduce iink update area by deactivating smoother" |
| 2 | `renderer.stroker.smoother-invalidation-extent-margin = 1.0` | "Reduce iink update area by setting margin to 1" |
| 3 | margin = 1.0 **and** `renderer.stroker.invalidation-extent-coef = 0.9` (both constants visible at `0x4df0a` `vmov.f64 d0,#1.0` and `0x4df56` `vldr` 0.9) | "Reduce iink update area by setting margin to 1 and coef to 0.9" |

These shrink the region MyScript invalidates per pen sample (the smoother
re-renders a trailing window of the stroke every sample). Option 1 disables the
live-ink smoother entirely — Kobo's own engineers built this specifically to
cut update area, which is the dominant e-ink cost.

Related engine keys that exist in this `libiink` build (settable through the
same `Configuration` interface): `renderer.stroker.smoothing`
(default `"legacy"`, alternative `"none"` per CSS analogue),
`renderer.stroker.enable-partial-invalidates`,
`renderer.stroker.max-point-displacement`,
`renderer.stroker.real-pressure-factor` (the last two are referenced from
`libiinkuiref.so.1.0.0`, i.e. Kobo already sets them).

Other latency-adjacent dev/feature settings found:

- `[DeveloperSettings] IinkSimpleStroker=true` — adds an extra pen tool wired
  to Kobo's own registered `"Simple"` stroker (see §4). Debug-quality
  rendering; **strokes drawn with it will not render in exports/thumbnails**
  (the `iink` exporter binary does not link `libiinknote.so`, which is the
  only registrar). Useful only as a latency-floor measurement, on a
  disposable notebook.
- `[DeveloperSettings] IInkThrottlerLimitAndIsInversed=<uint>` — read in the
  `IInkNotePadWidget` constructor (`0x60c50`) only when
  `DevSettings::allowDeveloperConsole()` is true; packs a limit plus an
  "inversed" flag (`iinkThrottlerLimit()` / `iinkThrottlerLimitIsInversed()`).
  The dev page's `12.5% … 87.5%` dropdown belongs to it. Exact consumer not
  yet traced; likely event/update throttling.
- `[FeatureSettings] IInkPressureSettings=true`
  (`FeatureSettings::iinkPressureSettings()`) — un-gates the stock pressure
  menu (`IInkToolBar::onPressureButtonTapped` → `IInkPressureMenuController`):
  eleven pressure curves plus minimum pressure, forwarded to
  `EditorWidget::setPressureCurve/setMinPressure`. This is the "pressure feel"
  lever, already fully implemented in firmware (see
  `notes/CUSTOM_BRUSH_PHYSICS.md`).
- The `300ms/500ms/700ms` dev dropdown is `FeatureSettings::handwritingTimeout`
  (recognition timeout), not ink latency.

## 2. Smoothing is also per-tool CSS

Tool styles resolve `-myscript-pen-smoothing: none | legacy | Default`
(parsed by `libiink`; `myscript::iink::ui::InkStyle::setSmoothing`,
`StrokerWrapper::setSmooth(bool)` at `0x4d6eb4..`). Stock Kobo pen classes
(`.ballpoint-*` FeltPen, `.fountain-*` FountainPen, `.calligraphic-*`
CalligraphicQuill, `.brush-*` CalligraphicBrush, `.highlighter-*` FountainPen)
set only brush/width/color — smoothing is left at engine default. A plugin
that appends e.g. `.ballpoint-1-0 { -myscript-pen-smoothing: none; }` via the
existing `IInkEditorCss::setCss` path would disable smoothing per tool rather
than engine-wide. Untested; the engine-wide dev setting should be tried first
since it costs nothing.

## 3. No stroke prediction exists in this build

`libMyScriptPrediction.so` is **text/word candidate prediction**
(`CandidateInfo::predictions()`), not ink-endpoint prediction. There is no
`renderer.prediction.*` configuration key in this `libiink`. Short-horizon
endpoint prediction (drawing 5–15 ms ahead of the nib) would have to be built
by us in the pointer path (`EditorWidget::stylusTouchUpdate` →
`IInkRenderTarget::pointerMove`, see `notes/CUSTOM_BRUSH_PHYSICS.md`) and
reconciled on real samples — genuinely custom work, and the riskiest item in
this note after raster effects. Defer until the free knobs are measured.

## 4. Kobo's own `SimpleStroker` proves the custom-stroker ABI

`libiinknote.so` implements `SimpleStroker`/`SimpleStrokerFactory` and
registers it **unconditionally** in the `IInkNotePadWidget` constructor
(`0x61870`) under the name `"Simple"` (string at `0x8719c`) via the renderer's
`registerStroker` virtual. This upgrades the `notes/MYSCRIPT.md` "custom vector
nib" plan from inference to firmware-proven, and hands us the ABI:

- `IStroker::stroke(std::vector<InkPoint> const&, float width, float pixelSize,
  std::shared_ptr<IPath>)`; `isFill() const` (returns true here).
- `InkPoint` stride is **24 bytes**, `x` at `+0x0`, `y` at `+0x4` (floats).
- `IPath` vtable: `+0x0c` moveTo(float,float), `+0x10` lineTo(float,float),
  `+0x20` close. (`SimpleStroker` emits an axis-aligned square of side `width`
  per sample — a debug marker, not a nib.)
- Factory: single virtual `createStroker()` returning
  `shared_ptr<IStroker>`.

Persistence caveat confirmed: the `iink` export/thumbnail executable links
`libiink`/`libiinkuiref`/`libnickel` but **not** `libiinknote`, so nothing
registers `"Simple"` there. Any custom live stroker must either be registered
in that process too, or only ever be used as a *live preview* while the saved
stroke keeps a stock brush name.

## 5. Event pipeline notes

- `libiink` has both single (`EditorImpl::pointerMove_` `0x436560`) and batched
  (`pointerMoves_` `0x43a8e4`, `pointerEvents` `0x43ac58`) entry points; Kobo's
  UI path emits one Qt signal per touch update into the single-sample entry.
  Whether Qt coalesces touch updates under load is a runtime question —
  measure before assuming.
- `EditorWidget::flushInvalidArea()` exists in `libiinkuiref` — candidate probe
  point for timing (event timestamp → flush) if we instrument latency later.

## 6. Further levers (second investigation pass)

### CPU cores and power management (zero-code, same dev page)

The same hidden developer page has **CPU 1 / CPU 2 / CPU 3 toggles**
(`toggleCPU1..3` strings next to the smoother options). Nickel runs a
`CPUAllocationManager` (`CPUAllocationEvent(context, cpus)`,
`MTK::cpuManager()`) that hot-plugs cores per UI context — so during writing
the device may be running fewer cores than it has, and core bring-up adds
latency jitter. Forcing all cores on is a directly testable feel improvement
(battery cost). `DevSettings::disablePM()` additionally disables power
management (aggressive downclocking is a classic source of first-stroke lag).

### The e-ink waveform path on MTK "Condor" (largest potential win)

The QPA plugin (`platforms/libkobo.so`) picks waveform, dither, and update
mode **per screen update**: `KoboScreenMTK::updateModeFromQt(QRect, QRect,
uint)` / `ditherModeFromQt(uint)`, debug format strings
`"wf=0x%0 can_collide=%1 monochrome=%2 rot=%3 hw=%4"` and
`"wf=%1 upd=%2 flags=%3 dither=%4 quant=%5 can_collide=%6"`. A `monochrome`
flag selects the fast 1-bit waveform class. `KoboScreenMTK` also owns a
dedicated worker thread named `"handwriting"` (string table next to
`rectworker`), i.e. pen updates already have their own update path.

Found but **not applicable to this device**: `Disp::enableHandwritingMode(bool)`
(exported at `0x1a3b0`) is `ioctl(disp_fd, 0x4015, &enable)` — a hardware
handwriting mode in the display driver — but its only caller is
`KoboScreenAllWinner::doRedraw()`. The Elipsa 2E is MTK Condor
(`KoboScreenMTKDeviceConfigCondor`), where the equivalent lives in the hwtcon
kernel driver and remains to be mapped (repo `firmware/` has the kernel).

Concrete next steps, in order of effort:

1. **Zero-code instrumentation:** the QPA logs the actual waveform per update
   through the format strings above. `DevSettings::loggingFilterRules()` /
   `logThreadNames()` exist — enabling the right logging categories should
   reveal, on-device, which waveform live ink actually uses while writing.
   Measure before changing anything.
2. If live ink is not already taking the monochrome/fast-waveform path,
   forcing the monochrome hint for notebook stroke updates is the
   highest-value plugin change in this whole note.
3. Kernel hwtcon pen-mode mapping (from `firmware/`) only if 1–2 show the
   driver supports a faster mode Nickel isn't using. Waveform changes carry
   ghosting/artifact risk — screen-only, still reversible by reboot, but test
   carefully.

### The ink throttler (dev page, zero-code)

`IInkThrottlerLimitAndIsInversed` (the `12.5% … 87.5%` dropdown) is a
Kobo-built throttle read in the `IInkNotePadWidget` constructor when
`allowDeveloperConsole()` is true. Its consumer is not yet traced, but as a
dev-page setting it can simply be swept experimentally alongside the smoother
options.

Also present: `FeatureSettings::antiAliasing()` / `setAntiAliasing(bool)` —
worth an A/B while already experimenting.

## Recommended experiment order

1. **No install, reversible:** on the mounted device, add to
   `.kobo/Kobo/Kobo eReader.conf`:
   `[DeveloperSettings]` → `ReduceIInkUpdateAreaBy=1`. Restart, write, judge.
   Then compare `=3`, then `=0` (revert). One line of config each time.
2. **Pressure feel, no install:** add `[FeatureSettings]` →
   `IInkPressureSettings=true`, use the new toolbar pressure menu; tune curve
   and minimum pressure per taste.
3. **Latency floor probe (disposable notebook only):**
   `IinkSimpleStroker=true`, feel how fast the pipeline is with a trivial
   stroker; do not keep those strokes (exports won't render them).
4. Only if 1–3 leave the user wanting more: plugin work — per-tool
   `-myscript-pen-smoothing: none` CSS override, then a real custom live
   stroker on the now-proven `registerStroker` ABI, then endpoint prediction.
   E-ink waveform changes remain out of scope (risk policy).

Editing `Kobo eReader.conf` is a user-space, reversible config change; back up
the file first. It is not firmware, database, or boot state.
