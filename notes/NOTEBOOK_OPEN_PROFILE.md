# Where notebook-open time goes (firmware 4.38.23852)

Measured 2026-11-03 on the Elipsa 2E with the opt-in sampling profiler
(`perf-experiment.txt`: `profile-open=1 settle=1`; `src/open_profiler.cc`).
A 1 ms CLOCK_MONOTONIC timer signals only the GUI thread and copies 1 KB of
its stack. Offline, stack words are kept only when the preceding instruction
in the library is a BL/BLX, then resolved with each library's exported
symbols. The MyScript core libraries (`libMyScript*.so`) are stripped, so
their time appears only per module. Notebook: 100-page, 5 MB copy, page 1.

## Timeline

| Open | `renderVolume` (stock) | after it until last paint | start to settled paint |
| --- | --- | --- | --- |
| first after boot | 326–385 ms | ~200 ms | 597–723 ms |
| repeat opens (n=8) | 134–150 ms | ~118 ms | ~255 ms (excl. profiler) |

The plugin's own share of a normal open is 1–2 ms.

## Repeat open: `renderVolume` (~135 ms)

- 49% self time inside MyScript's stripped engine core.
- ~16 ms: parsing each page's 20 KB `style.css` (`IPagePriv::getStyleSheet2_`).
- ~12 ms: page-format migration on every open
  (`snt::MigrationAssistant::migratePageImpl`, `EditorImpl::finalizeMigration`).
- ~10 ms: grid/layout (`snt::PageController` constructor, `computeGrid`).
- ~10 ms: engine configuration (`NeboBackend::configurationChanged`, `EditorImpl::configure`).
- ~5 ms: `IInkNotePadWidget::scrollValue()` → `ContentPackage::indexOfPart`, a
  linear scan over all pages.
- Heavy allocation throughout (`malloc` inclusive ~34%).

## Repeat open: after `renderVolume` (~118 ms)

- Qt widget construction and QSS matching for the reading menu / page
  scrubber (`IInkNotePad::setupScrubberMenu`, `updateScrubberMenu`), ~14%.
- A second grid computation triggered by `RendererImpl::setViewScale`.
- Painting and a per-pixel format conversion in the display path (~25%,
  nearest exported symbol `qt_convert_rgb888_to_rgb32_neon`; the
  `libiinkuiref` render targets are ARGB32, so this is not the ink buffer).

## First open after boot: the extra ~250–450 ms

- ~65 ms decoding the page background PNG
  (`BackgroundWidget::setBackgroundType` → `QPixmap(QString)` → libpng/zlib →
  `convertToFormat`). Later opens hit a cache.
- ~40 ms in the dynamic loader (lazy symbol binding), plus first-touch code
  and resource loading across the MyScript libraries.

## The first open was slower than `renderVolume` showed

Nickel loads `libiinknote.so` (and with it libiink and the MyScript core)
only when the first notebook is opened. That first open therefore also paid
for loading and relocating the engine and for this plugin's delayed hook
setup (~200 ms), which polls for that library. Measured from the injected tap
(`/proc/uptime` in the same SSH session as the tap) to the settled paint:

| | tap → `renderVolume` | tap → settled |
| --- | --- | --- |
| first open, no pre-warm (3 cold boots) | 1352–1368 ms | 1856–1879 ms |
| first open, pre-warm (4 cold boots) | 723–743 ms | 1118–1184 ms |
| warm reopen (same boot, 3 runs) | 443–483 ms | 695–735 ms |

## Pre-warm (shipped, `src/prewarm.cc`)

Read-ahead of the engine libraries at 12 s; an idle-gated `dlopen` of
`/usr/local/Kobo/libiinknote.so` from 20 s (~0.7 s once, after 3 s without
input); then the backgrounds of the two most recently opened notebooks are
replayed through `BackgroundWidget::setBackgroundType` on a hidden widget
(allocated with `operator new(0x28)` and destroyed with its deleting
destructor, as `IInkNotePadWidget` does), filling Qt's 20 MB pixmap cache
(~130 ms). The remaining ~430 ms gap to a warm reopen is Nickel building the
notebook UI for the first time.

## Conclusions

Repeat opens are almost entirely Kobo/MyScript work (engine core, per-open
migration, stylesheet parsing, layout, Qt widget setup); none of it can be
removed from a plugin without changing how notebook content is loaded.
The realistic plugin-side win was the cold first open, now taken by the
pre-warm above. Caching the `scrollValue` page index would save ~5–10 ms.
