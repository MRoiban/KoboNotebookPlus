# Layer regression fix verification

Date: 2026-07-15  
Target: Kobo firmware 4.38.23697, ARMv7 hard-float

This record covers the sixth instrumented eraser build. The fifth hardware
test proved that updating the exact cached DiagramPen while DiagramEraser was
current still did not erase custom-layer B. The sixth build fixes the ordering
of Diagram's geometric layer selection rather than treating pen state as an
eraser-layer setter.

## Root cause and firmware evidence

- Analyzed `libiink.so` SHA-256:
  `0dc02118b9b763450c228da1e632dadbcce5cfd2acb41a3ca76a2472aed71ac6`.
- The completed GUI-hosted Binary Ninja view is ELF/ARMv7 at start `0x10000`,
  analysis generation 1150, fingerprint
  `26b2db8bc952bfc4687ce9aa7b6bdbb7bd859e3f64b06d7649fb345b9e6fbbbe`.
  No Binary Ninja mutation capability was enabled and no binary or BNDB was
  modified.
- `scripts/verify-layer-abi.py`: 66 symbols, five exact vtable sizes, 31
  vtable slots, and 13 Thumb instruction pins verified.
- `scripts/verify-layer-preview-abi.py`: listener proxy ABI verified.
- `scripts/test-verify-layer-artifacts.py`: 3 tests passed.

The fifth device trace is
`backups/device-diagram-native-ab-20260715-134301/plugin-status.txt`. Lines
157--164 show current exact DiagramEraser, exact cached DiagramPen,
`exact=yes restricted=yes`, `pen-restricted=1`, and current=yes while B was
selected. Eraser input refreshed the screen but did not erase B. Therefore
DiagramPen data `+0x60` is not sufficient to scope Diagram erasing.

Binary Ninja exposes the actual ordering bug:

1. Core `Eraser::selectionFromPoints` BN `0xa87470` loads eraser width from
   `+0xcc` and view scale from `+0x110` at BN `0xa8748c`/`0xa87492`, multiplies
   them at BN `0xa874a8`, and calls stock `Eraser::strokerPolygon` BN
   `0xa85fb0`.
2. It applies that polygon with Mode 0/Replace at BN
   `0xa8764c`--`0xa87658`. Intersecting a custom named layer only after this
   stock Diagram-context hit selection cannot recover its strokes.
3. Firmware `Selector::computeSelection` BN `0xa8ce70` proves the supported
   layer-isolated order: `selectLayer(..., Replace)` with Mode 0 at BN
   `0xa8ced0`--`0xa8ced8`, then `selectPolygon(..., Intersection)` with Mode 2
   at BN `0xa8d07e`--`0xa8d082`.

The exact DiagramEraser `+0x78` wrapper now calls the stock original only to
construct a Selection bound to the live Layout, discards the stock contents
with selected-layer/Replace, obtains the exact stock eraser polygon, and
applies polygon/Intersection. Stock Diagram `updateSelection` `+0x7c` and
`eraseSelection` `+0x80` are unchanged. The wrapper fails closed by clearing
the Selection on any missing API, invalid width, empty polygon, or exception.

## Build and package

The source was built only with NickelTC, sequentially:

```text
make -C mods/custom-notebook-templates clean       exit 0
make -C mods/custom-notebook-templates -j2         exit 0
make -C mods/custom-notebook-templates koboroot    exit 0
```

The compile used GCC 4.9.4 with `-Wall -Wextra -Werror`. SHA-256 values:

```text
4e26f2d5760ee8e3ae6f03485675571d92b231f1ab6a99e263207653f28caa9c  KoboRoot.tgz
ff88956731fc42a767eb076d8d9651bed9a183b04975a8c088100b6d47b16833  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  libiinknote.so
```

The archive contains only the exact stock `libiinknote.so` and plugin at their
expected Kobo paths. Both extracted payloads compare byte-for-byte with their
package/build sources. The plugin is ELF32 ARM, DYN, EABI5 hard-float,
Cortex-A8, Thumb-2, VFPv3, and NEON.

Final plugin disassembly proves the opaque hard-float ABI and ordering in
`layerAwareDiagramEraserSelectionFromPoints` at plugin `0xd2b4`:

- call the exact stock selection function with result `r0`, eraser `r1`, and
  PointerInfo-vector address `r2` at `0xd2c8`--`0xd2cc`;
- call selected-layer/Replace with `r2=0` at `0xd32e`--`0xd33e`;
- load width `+0xcc`, view scale `+0x110`, and multiply into hard-float `s0` at
  `0xd34c`--`0xd356`;
- call stock `strokerPolygon` with vector result storage in `r0`, the original
  opaque PointerInfo-vector reference in `r1`, and width still in `s0` at
  `0xd380`--`0xd38c`; and
- call polygon/Intersection with `r3=2` at `0xd398`--`0xd3a8`.

The generic vtable initializer copies exactly 148 bytes at plugin
`0xc7fa`--`0xc7fe`, reads only clone entry `+0x80` (Itanium header 8 plus
virtual slot `+0x78`) at `0xc802`--`0xc804`, verifies its exact firmware VMA,
and replaces only that entry. Diagram slots `+0x7c/+0x80` remain copied stock.

## Required device validation

This record does not claim hardware success yet. After safe installation and
reboot, use fresh crossing strokes:

1. Select A and draw a horizontal line.
2. Select B and draw a vertical line crossing it.
3. Select A, switch to eraser, and erase the horizontal line.
4. Keep eraser active, select B, then erase the vertical line.

Trace must show the exact `diagram-layer-first` clone. Its bounded input lines
must show `layer-mode=0 polygon-mode=2`; on a hit, `layer=nonempty` and
`geometry=nonempty`. Both observed erases must pass before success is claimed.

## Seventh guarded DiagramPen build

The sixth hardware build did not pass the fresh Layer 2/Layer 3 test. The
preserved state is
`backups/device-fresh-layer23-test-20260715-142447/`. Layer 2's new horizontal
stroke erased with non-empty layer-first geometry. Layer 3's new vertical
stroke did not erase; its gesture produced only one empty stock hit. Before
the Layer 3 draw, the trace proved apply-time restriction on the exact current
and cached DiagramPen, but the sixth build had no observation at physical
pen-down.

The seventh build adds a per-DiagramPen desired-layer guard at exact vslot
`+0x3c`. Binary Ninja SDK replay proves DiagramPen -> SmartPen -> core Pen at
BN `0x7bf41e` and `0xa93cf8`; core Pen then loads virtual
`restrictedLayer()` from `+0x58` at BN `0xa894b0` and calls
`InkSampler::setStrokeLayer` at BN `0xa894be`. The wrapper compares and, only
on mismatch, re-applies the desired ID immediately before calling the stock
DiagramPen function. Its bounded trace records `desired`, `before`, `after`,
and `corrected` for each new stroke.

Verification for this seventh build:

```text
verify-layer-abi.py: 68 symbols, 5 vtable sizes, 31 vslots, 17 instruction pins
verify-layer-preview-abi.py: passed
test-verify-layer-artifacts.py: 3 passed
NickelTC clean / -j2 / koboroot: exit 0, -Werror
19a70610202b333c5520f107953dc47f760b0c48b3951c10f12ec03438064960  KoboRoot.tgz
e4fa02cf7f22d69c253a2ab232ea004eedeecf7462a61f0e1ba171475d84a3c3  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  libiinknote.so
```

The archive contains exactly the two expected members; the packaged plugin is
byte-identical to the build output and the stock library matches the required
hash. The plugin is ELF32 ARM DYN, EABI5 hard-float, Cortex-A8, Thumb-2, VFPv3,
NEON. Emitted wrapper disassembly begins at plugin `0xd4b0`: it locks and reads
the per-tool map at `0xd4e2`--`0xd51c`, reads the real restriction through the
helper at `0xd534`, compares desired/current at `0xd800`--`0xd808`, invokes
virtual `restrictToLayer` from slot `+0x54` at `0xd810`--`0xd824` only on
mismatch, and tail-calls the preserved stock DiagramPen pointer at
`0xd7ca`--`0xd7d6`.

Hardware success remains unclaimed until a new trace shows the pen-down layer
guard for both fresh strokes and both layer-isolated erases succeed.

## Eighth build: diagram eraser layer-commit (+0x7c)

The seventh build's device trace
(`backups/device-diagram-pen-guard-failure-20260715-144359/plugin-status.txt`)
was decisive: the Layer 3 pen-down guard confirmed the stroke was tagged to
Layer 3 (`desired=before=after=Layer3 corrected=no`), and the +0x78 layer-first
selection returned `stock=nonempty layer=nonempty geometry=nonempty` (lines
89-90) over the Layer 3 ink -- yet nothing erased. The selection was correct
but discarded downstream.

Binary Ninja traced why (BN addr = ELF VMA + 0x10000). `DiagramEraser::penDown`
(BN `0x76b670`) direct-`bl`s core `Eraser::penDown` (BN `0xa88668`), which
virtually calls `+0x78` selectionFromPoints (our wrapper), then `+0x7c`
`DiagramEraser::updateSelection` (BN `0x76b73c`), then `+0x84`
`Eraser::updateRenderer` (BN `0xa85db0`, cursor redraw only). On pen-up, core
`Eraser::penUp` loads virtual `+0x80` at BN `0xa86f0a` and calls it at
`0xa86f0e`; the absence of a direct xref to `DiagramEraser::eraseSelection`
was therefore misleading. The `+0x7c`
override throws away its incoming layer-scoped selection and rebuilds the erase
set from `Diagram::layoutGroup()` -> `Selection::selectByType(0xcc4)` ->
`Diagram::itemsIntersecting` (BN `0x76b784/0x76b7ae/0x76b7da`), which does not
enumerate custom named-layer ink, then commits it via core
`Eraser::updateSelection` (BN `0xa85ce4`): `if isEmpty(sel) return 0; else
combine(this+0xd0, sel, 1)`. For custom layers the rebuilt set is empty, so
`Eraser+0xd0` stays empty. `DiagramEraser::eraseSelection` reads that exact
member at BN `0x76c228` and calls `Diagram::removeEraseSelection` at BN
`0x76c284` only when it is non-empty.

The eighth build clones `+0x7c` on the exact DiagramEraser. The wrapper runs the
stock override first (base-layer and diagram-item erase byte-identical), then,
only when the eraser's `restrictedLayer()` (`vptr+0x58`) is non-empty and the
incoming `+0x78` selection is non-empty, calls base `Eraser::updateSelection`
directly to union that layer-scoped selection into `Eraser+0xd0`. Every other
slot and all firmware code remain untouched.

Verification for this eighth build:

```text
verify-layer-abi.py: 69 symbols, 5 vtable sizes, 31 vslots, 20 instruction pins
verify-layer-preview-abi.py: passed
test-verify-layer-artifacts.py: 3 passed
NickelTC clean / -j2 / koboroot: exit 0, -Werror
dd11a2480f728f5d7ba94a03d8c4af64722f9ade6f2b894bdcd5ef347b18798c  KoboRoot.tgz
2ae554ace2f28af32faf9736c7ab1650798def7632bfdd0d2bce138ee2be445d  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  libiinknote.so
```

The archive holds exactly the two expected members; the packaged plugin is
byte-identical to the build output and the stock library matches the required
hash. Emitted wrapper at plugin `0xc8a4`: `blx` the stock original at `0xc8c0`,
load `restrictedLayer` via `ldr [r2,#0x58]` at `0xc8e6`, guard on
`selectionIsEmpty(selection)` at `0xc920`, then call core `updateSelection`
with `r0=eraser r1=selection r2=points` at `0xc934` only when non-empty. ABI
pins: core `Eraser::updateSelection` symbol `0xA75CE4` plus instruction pins
`0xA75CFA` (isEmpty guard), `0xA75D08` (`adds r4,#0xd0`), `0xA75D16`
(`movs r2,#1`).

Installed atomically 2026-07-15 (device pre-state snapshot
`backups/device-diagram-eraser-layer-commit-20260715-152532/`). Hardware
success remains unclaimed until a trace shows per-stroke
`diagram eraser layer-commit ... injected=yes` and a two-layer test erases each
layer only when it is selected.

## Core-only custom-layer update and real removal observer

The post-eighth trace at
`backups/device-eraser-fsync-diag-20260715-154935/plugin-status.txt` separates
the two custom layers. Layer 2 reaches non-empty `+0x78` geometry and the
stock-first `+0x7c` wrapper returns repeatedly (`injected=yes`, lines
123--135). After switching the already-active eraser to Layer 3, `+0x78`
again becomes non-empty (lines 170--171), but the wrapper never emits its
post-stock line. The current fsync build reproduces this at lines 64--71 of
the mounted trace, ruling out buffered trace loss. Thus valid Layer 3 geometry
enters the diagram-specific update but its stock transform does not return to
the wrapper normally.

Binary Ninja also proves that the `+0x7c` return value is functional, not just
diagnostic: core `Eraser::penMove` loads/calls `+0x7c` at BN
`0xa87c40`/`0xa87c4c`, then forwards that boolean to renderer vslot `+0x84` at
BN `0xa87c84`/`0xa87c88`. Returning the stock result after a successful base
injection can therefore suppress the eraser preview when the diagram-narrowed
set is empty.

The fix-forward wrapper now detects only plugin-generated `cnt.layer.*` IDs
and directly returns core `Eraser::updateSelection`. The base/document layer
still calls the exact stock Diagram override, preserving semantic Diagram
item handling. A no-semantic-change `+0x80` observer records the final
`Eraser+0xd0` state, calls stock `DiagramEraser::eraseSelection` exactly once,
and returns its result unchanged. Both downstream slots are validated and
installed in the clone before the tool vptr is published. This claim is
intentionally limited to raw ink on plugin-created layers; recognized Diagram
objects on such layers remain hardware-unproven.

Verification for the core-only custom-layer build:

```text
verify-layer-abi.py: 71 symbols, 5 vtable sizes, 31 vslots, 27 instruction pins
verify-layer-preview-abi.py: passed
test-verify-layer-artifacts.py: 3 passed
NickelTC clean / -j2 / koboroot: exit 0, -Wall -Wextra -Werror
dfded42c1f81be78ac064e93f744dbf116bfcd7cbe074d4c2ad90eaf73ae1c59  KoboRoot.tgz
b0222c7c5ab515281cb80f32433411325edcbc7ffd2585d39c2ef2108cec2c96  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  packaged libiinknote.so
```

The Binary Ninja SDK replay used the GUI-attached ARMv7 view with bridge
fingerprint
`26b2db8bc952bfc4687ce9aa7b6bdbb7bd859e3f64b06d7649fb345b9e6fbbbe`
and stock `libiink.so` SHA-256
`0dc02118b9b763450c228da1e632dadbcce5cfd2acb41a3ca76a2472aed71ac6`.
It re-confirmed the virtual `+0x80` call at BN `0xa86f0a`/`0xa86f0e`, the
`Eraser+0xd0` removal source and `Diagram::removeEraseSelection` call at BN
`0x76c228`/`0x76c284`, and the `+0x7c` boolean-to-renderer path at BN
`0xa87c40`/`0xa87c4c` and `0xa87c84`/`0xa87c88`.

The archive contains exactly stock `libiinknote.so` and the plugin; its plugin
member is byte-identical to the build output. The plugin is ELF32 little-endian
ARM DYN, EABI5 hard-float. Emitted wrapper disassembly places the `+0x80`
observer at plugin `0xdb1c`: it reads `eraser+0xd0` at `0xdb64`, calls the
preserved stock function exactly once at `0xdca2`--`0xdca6`, reads `+0xd0`
again at `0xdcc2`--`0xdcc6`, and returns the preserved result. The `+0x7c`
wrapper is at plugin `0xe058`: it reads virtual `restrictedLayer()` through
slot `+0x58` at `0xe0a0`--`0xe0a6`, compares the ten-byte `cnt.layer.` prefix
at `0xe0b8`--`0xe0c4`, sends all non-custom layers to the preserved stock
function at `0xe0d2`--`0xe0d6`, and calls core `Eraser::updateSelection` with
`r0=eraser`, `r1=selection`, `r2=points` at `0xe2d0`--`0xe2de` for custom
layers. Hardware success remains unclaimed until both selected custom layers
erase independently and the trace proves update and removal for each.

## Policy-aware whole-stroke build

The core-only custom-layer build passed the layer-isolation test on hardware,
but the user observed that Kobo's Object Eraser removed ink little by little
like Brush Eraser. The post-test device state and trace are preserved at
`backups/device-object-eraser-regression-post-20260715-174039/`. In that trace,
custom layer `cnt.layer.bf8ada4e-9712-4d3f-97ad-fd387db218a5` reaches
non-empty layer-first geometry, commits through the custom-only core update,
and exits the exact stock removal function with `removed=yes` (trace lines
488--527). This proves the working layer route and removal sink; the remaining
regression is the shape of the committed Selection.

Binary Ninja pins the lost native transform (BN address = ELF VMA +
`0x10000`):

- `Eraser::setEraserPolicy` ELF `0xa75a94` stores the enum at `Eraser+0xc4`
  at BN `0xa85a98`; `eraserPolicy() const` ELF `0xa75aa4` reads it at BN
  `0xa85aa8`.
- `ToolDispatcher::setPropertiesForTool` maps `eraser.policy=stroke` to `0`
  at BN `0x5d3998`--`0x5d399c` and `precise` to `1` at BN
  `0x5d3b1e`--`0x5d3b22`.
- Stock `snt::DrawingEraser::selectionFromPoints` reads `+0xc4` at BN
  `0x572848`; policy `0` calls
  `InkSelection::adjustToStrokeBoundaries_(0.0f)` at BN `0x5728e6`, while
  policy `1` leaves the polygon fragment precise.
- Stock `DiagramEraser::updateSelection` makes the same policy split at BN
  `0x76b7b4`--`0x76b7ba` and uses the exact `0.0f` boundary adjustment at BN
  `0x76ba00`--`0x76ba04`. The custom-only core `+0x7c` path necessarily
  bypasses that Diagram transform.
- The public plugin-safe entry is
  `Selection::adjustToStrokeBoundaries(float)` ELF `0x9e2d70`, BN
  `0x9f2d70`. It mutates the existing Selection through PageSelection at BN
  `0xb06e54` and performs no new page or layer hit-test.

The fix reads the concrete eraser's native policy on every exact DiagramEraser
`+0x78` callback. After the existing custom-layer/Replace and
polygon/Intersection sequence, only a non-empty `cnt.layer.*` selection with
policy `0` is expanded with `adjustToStrokeBoundaries(0.0f)`. Policy `1`
remains precise, and the document/base layer continues through the unmodified
stock Diagram `+0x7c`. There is deliberately no second layer lookup after
expansion: one stroke has one layer, the adjustment mutates only selected
strokes, and a redundant `selectLayer` lookup can clear a valid Selection on
lookup failure.

Verification for this build:

```text
Binary Ninja SDK replay: 24 functions, 41 instructions, 3 data literals; read-only capabilities={}
verify-layer-abi.py: 74 symbols, 5 vtable sizes, 31 vslots, 44 instruction/data pins
verify-layer-preview-abi.py: passed
test-verify-layer-artifacts.py: 3 passed
NickelTC clean / -j2 / koboroot: exit 0, -Wall -Wextra -Werror
6e860dba3d9b9bf85389ad46f09f0199c14ecce966418766c92148317b07a3ab  KoboRoot.tgz
84a975654de23087ce016fc349ed29da71be6c90f289350f2a1411c343b81403  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  packaged libiinknote.so
```

The archive contains exactly the two expected members, contains no macOS
metadata, and both members are byte-identical to their build/package sources.
The plugin is ELF32 little-endian ARM DYN, EABI5 hard-float, Cortex-A8,
Thumb-2, VFPv3, and NEON.

Emitted wrapper disassembly begins at plugin `0xed0c`. It preserves the stock
constructor call at `0xed24`, applies layer/Replace with `r2=0` at
`0xed98`--`0xeda4`, derives the polygon with width `+0xcc` and scale `+0x110`
at `0xedb2`--`0xedbc`, and applies polygon/Intersection with `r3=2` at
`0xee1c`--`0xee28`. It compares the ten-byte `cnt.layer.` prefix at
`0xee3e`--`0xee4a`, calls the resolved native policy accessor at
`0xee4e`--`0xee62`, and reaches the expansion only for prefix match, policy
zero, and non-empty geometry. At `0xf13a`--`0xf148` it loads exact `0.0f` into
hard-float register `s0` and calls the resolved
`Selection::adjustToStrokeBoundaries` pointer; it then rechecks emptiness and
sets the bounded `boundary-adjusted=yes` trace state. No layer-selection call
occurs after the adjustment.

Hardware verification passed. The final state is preserved at
`backups/device-object-eraser-success-post-20260715-175128/`. The user reported
that Object Eraser, Brush Eraser, and independent layer switching all work
correctly. The trace provides matching internal proof:

- custom ID `cnt.layer.3da82475-016b-44ec-a100-b4baaad619eb` records non-empty
  `policy=0 boundary-adjusted=yes` selections at lines 107--116, commits a
  non-empty final Selection, and stock removal exits `removed=yes` at line 126;
- the same ID records non-empty `policy=1 boundary-adjusted=no` at lines
  161--164, proving the native Brush/precise mode bypasses expansion;
- custom ID `cnt.layer.bf8ada4e-9712-4d3f-97ad-fd387db218a5` independently
  records non-empty `policy=0 boundary-adjusted=yes` at lines 339--354 and
  successful core-only commits beginning at lines 340--341;
- the saved sidecar contains both IDs, with the second active, and the saved
  notebook is present; and
- no fail-closed, hook, crash, fatal, symbol, or routing error appears in the
  trace.

The `+0x80` observer budget was exhausted before the second Object Eraser
pen-up, so that gesture's final removal is proven by the user's visible result
plus its non-empty adjusted selection and committed sink, rather than a second
`removed=yes` line. Together with the first layer's complete removal trace,
this closes the raw-stroke regression without broadening the claim to
recognized Diagram objects.

## Native toolbar-anchored layer popup build

The pen-style layer rows initially appeared in an unanchored rectangle because
the plugin parented `NickelTouchMenu` to the notebook widget and supplied a
hand-calculated global point. Read-only Binary Ninja analysis of stock
`libnickel.so.1.0.0` (SHA-256 `577235f308b4dea1c150caf87a376ecf34c8ecab04aacee0806feddd9e268744`)
confirmed that `AbstractNickelMenuController::show()` (ELF `0xeb58b8`, BN
`0xec58b8`) retrieves the toolbar anchor through `popupFromWidget()` (ELF
`0xeb4f94`, BN `0xec4f94`), calls the menu's
`popupPosition(QWidget*)` vslot (ELF `0xed7248`, BN `0xee7248`), then opens the
menu at that returned `QPoint`. `drawDecoration()` uses the popup parent as its
pointer target when the optional target rect is invalid, which is stock's
ordinary pen/eraser behavior.

The fix parents the layer popup to the overflow toolbar button returned by the
controller and passes that same button to `popupPosition()`. It keeps modal
`QMenu::exec(position)` only because the injected gesture rows still depend on
the overflow controller, which stock schedules for deletion after its own menu
closes.

Verification for this build:

```text
verify-layer-abi.py: 99 symbols, 5 vtable sizes, 31 vslots, 87 instruction/data pins
verify-layer-preview-abi.py: passed
test-verify-layer-artifacts.py: 3 passed
NickelTC clean / -j2 / koboroot: exit 0, -Wall -Wextra -Werror
5999088bc3143dcbceabb010d25f769e295258da04ff8497f69dfdc857be701b  KoboRoot.tgz
a696183aa0d02ebeb2a4fc2489ad3943c94da36b77105b61efdd15e9755a8ee7  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  packaged libiinknote.so
```

The archive has exactly the two expected members. Both are byte-identical to
their build/package sources, and the plugin is ELF32 little-endian ARM DYN,
EABI5. Read-only Binary Ninja verification of the emitted plugin (SHA-256
`a696183a...`) finds the guarded anchor call and null check at BN
`0x32068`-`0x32084`. The emitted `QPoint` aggregate call at BN
`0x324e2`-`0x324f2` passes hidden result storage in `r0`, popup in `r1`, and
anchor in `r2`; BN `0x32564`-`0x32568` then passes that result to
`QMenu::exec(QPoint const&, QAction*)`. Hardware appearance remains unclaimed
until the popup is seen under the overflow button with its triangular pointer.

## Per-layer notebook-preview build

The native layer rows now use a 54x72 card. That is the same 3:4 aspect ratio
as the Elipsa 2E notebook page (1404x1872) while preserving the already proven
72-pixel stock menu-row height. Opening the popup saves the notebook and
generates only missing or stale cache entries before constructing the rows;
failure remains per-row and non-fatal, so a placeholder never prevents layer
selection. The export proxy restricts only the fresh export renderer and never
the live editor renderer.

The firmware exporter itself is fixed at 1024x1024 (ELF
`0x513308`-`0x513338`, BN `0x523308`-`0x523338`). The first hardware build
therefore keeps the complete square image centered and undistorted inside the
3:4 card. That deliberately follows stock `PixmapView` behavior—centered,
scaled, no crop—and avoids cutting edge strokes before an actual exported PNG
shows whether the square contains removable side padding.

The original no-preview failure was not an asynchronous export race. In stock
`libiinkuiref.so.1.0.0`, `ImagePainter::saveImage` (ELF `0x28354`, BN
`0x38354`) calls `QFileInfo::suffix()` at ELF `0x2844c` and supplies that text
as the encoder name to `QImage::save()` at ELF `0x2846a`. The old atomic name
`digest.png.tmp-<pid>` therefore requested an encoder named `tmp-<pid>` and
produced no file. The emitted plugin now constructs
`digest.png.tmp-<pid>.png`, validates the file size and decoded image, then
atomically renames it over the cache entry.

`PageController::exportToPNG` is synchronous: at BN
`0x523392`-`0x5233d0` it loads the listener's `+0x08` vslot and invokes it;
only after that call returns does it release the temporary renderer/selection
owners and return success at BN `0x5233ec`. `setRendererListener` at ELF
`0x505498` / BN `0x515498`, the listener call sequence, and the UI-reference
suffix-to-encoder sequence are pinned in `scripts/verify-layer-abi.py`.

Verification for this build:

```text
verify-layer-abi.py: 101 symbols, 5 vtable sizes, 31 vslots, 93 instruction/data pins
verify-layer-preview-abi.py: passed
test-verify-layer-artifacts.py: 3 passed
NickelTC clean / -j2 / koboroot: exit 0, -Wall -Wextra -Werror
b767c4c14d1c5a3db198d602367f00a586c6cd85655172ced0213c82628f2059  KoboRoot.tgz
8f98b879242eff70ab9b141a65be2b29937f53d5df26f1b1665ab12c70211a3c  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  packaged libiinknote.so
```

The archive contains exactly the stock `libiinknote.so` and the plugin. Both
members are byte-identical to their package/build sources and are ELF32
little-endian ARM EABI5 shared objects. Read-only Binary Ninja analysis of the
exact emitted plugin hash above is complete (692 functions, ARMv7 ELF,
capabilities `{}`). `generateLayerPreview` is Thumb-2 at BN `0x25174`.
BN `0x2533e` constructs the five-byte `.tmp-` fragment, BN `0x2535a` obtains
the PID, and BN `0x25396`-`0x253b0` appends a final four-byte `.png` fragment.
The resolved `PageController::exportToPNG` pointer is invoked at BN `0x25510`;
the subsequent file, size, and image-decode guards precede the two atomic
renames. The instrumented strings for export result and popup stale-refresh
counts are present at BN `0x3a6c4` and `0x3d8f8`.

That build was tested on hardware and failed before export. The preserved trace
`backups/device-layer-preview-failure-20260715-230215/plugin-status.txt` records
the exact runtime error `Layer preview unavailable: image writer is missing.`
for the base and custom layer at lines 80--81, `attempted=2 generated=0` at
line 82, and the same result on subsequent popup/refresh attempts. No isolated
PNG was produced.

### Preview writer root cause and fix-forward design

The failed build copied a `shared_ptr<RendererListener>` from
`PageController::Data+0x34` and assumed it was the controller's persistent PNG
writer. The field and `PageController::setRendererListener` are real, but the
field is optional and was null on the tested live notebook. The failure is not
an export race: preview generation rejected the request synchronously before
calling `PageController::exportToPNG`.

Read-only Binary Ninja analysis also shows why the null is valid. Stock
`NeboBackend::export_` allocates an anonymous listener for one export at BN
`0x49946c`, initializes its listener vptr and owned writer at
`0x499494`--`0x49949e`, passes it to the export call, and releases it after the
call. Firmware therefore supports call-owned listeners; it does not promise a
writer in the stored PageController field.

The replacement path removes that field dependency while preserving the
working isolated-renderer design:

1. `PageController::exportToPNG` still creates the temporary 1024x1024
   renderer. The temporary proxy applies `Renderer::restrictToLayers({id})`
   only to that renderer.
2. The plugin constructs the stock UI-reference `ImagePainter` with its
   no-argument constructor (ELF `0x28b48`) and firmware destructor (ELF
   `0x297e0`). Its primary `IImagePainter` base is at offset `+0`; offset
   `+0xc` is a secondary base and must never be passed to the drawer.
3. `EditorImpl::getEngine` (ELF `0x425bb0`) and `getConfiguration` (ELF
   `0x425cf0`) return the exact shared owners at EditorImpl `+0x2c/+0x30` and
   `+0x44/+0x48`.
4. The exported stock `BackendImageDrawer` make-shared helper (ELF
   `0x443eac`) creates the 0x34-byte drawer in its native control block and
   retains the Engine and ImagePainter.
5. In the isolated listener callback, Renderer::layout (ELF `0xa57450`) and
   Layout::page (ELF `0x9d9530`) obtain the matching Page. The callback copies
   the incoming Renderer and Selection and calls
   `BackendImageDrawer::drawImage` (ELF `0x3e7b8c`). The verified AAPCS shape
   is drawer/Page/Renderer/backend in `r0`--`r3`, four-float Extent in
   `s0`--`s3`, and Selection/path pointers in the first two stack words.
6. The temporary filename continues to end in `.png`; decode, size, and atomic
   replacement guards remain unchanged.

The UI-reference library used for this design has SHA-256
`693aa804c31e3d783314ea2a61282730b0ada6f0b48029a3bc955ea78acd409a`.
The live editor Renderer is never passed to `restrictToLayers`; only the
short-lived renderer supplied to the export listener is filtered. The fix
therefore cannot alter the live page's visibility, writing route, or eraser
scope.

The first implementation of that replacement was stopped before packaging or
device installation after a source/binary safety audit found that the default
`ImagePainter` is incomplete. Its constructor zeros the loader member at
`+0x1c/+0x20`; `ImagePainter::createCanvas` loads `+0x1c` at BN `0x38bc0`
and immediately calls `ImageLoader::getEditor`. A no-loader painter would
therefore have a deterministic null-dereference path.

The corrected replacement copies the notebook's real configured loader:

1. `IInkNotePadWidget` constructor BN `0x70f84`--`0x70f94` creates the live
   UI-reference `EditorWidget`; BN `0x70fd6/0x70fda` stores its Qt guard at
   notebook `+0x44` and object at `+0x48`. BN `0x71004`--`0x71014` proves the
   stock guard-then-object validation sequence.
2. `EditorWidget::setEngine` stores a strong `shared_ptr<ImageLoader>` at
   `EditorWidget+0x20/+0x24` at BN `0x2e86c`--`0x2e870`; its destructor's
   release at BN `0x2d1a8` proves ownership.
3. The plugin repeats the Qt guard check, requires the exact `EditorWidget`
   address-point vptr, and verifies `EditorWidget+0x18/+0x1c` matches the live
   notebook editor identity before copying that loader owner.
4. Stock `ImagePainter::setImageLoader` (ELF `0x28a60`, BN `0x38a60`) copies
   the loader into the new painter before the `BackendImageDrawer` factory is
   called. Any missing or changed object retains the row placeholder and exits
   before export.

Clean verification for the corrected deployment candidate:

```text
verify-layer-abi.py: 108 symbols, 5 vtable sizes, 31 vslots, 117 instruction pins
verify-layer-preview-abi.py: passed
test-verify-layer-artifacts.py: 3 passed
NickelTC clean / -j2 / koboroot: exit 0, -Wall -Wextra -Werror
3280ae101a2a7e788df4f6488454e3b228350698473a06749b39a54760f2296e  KoboRoot.tgz
cf0ce5fbd5db71ebc30d8fc7cc587ebc033a0feaac2df24e2dca20591452f05a  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  packaged libiinknote.so
```

The archive has exactly the stock `libiinknote.so` and plugin members; both
are byte-identical to their source artifacts and both are ELF32 little-endian
ARM EABI5 shared objects. Read-only Binary Ninja analysis of a byte-identical
copy of the emitted plugin is complete (661 functions, ARMv7 ELF, capabilities
`{}`, SHA-256 `cf0ce5...f05a`). `generateLayerPreview` is Thumb-2 at BN
`0x268e0`. BN `0x27034`--`0x2704e` checks notebook `+0x44/+0x48` and the exact
vptr; BN `0x273dc`--`0x27420` validates the live editor identity; BN
`0x27422`--`0x2743a` reads and strongly copies loader `+0x20/+0x24`; and BN
`0x275c4`--`0x275c8` invokes the resolved setter with the painter and copied
loader before the drawer factory at BN `0x2760a`.

At that checkpoint hardware verification was still pending; the clean build,
package audit, and ARM disassembly prerequisites above were complete. The later
device run supplied independently filtered row ink and trace evidence of
`exported=yes`, `restricted=yes`, `writer=yes`, `file=yes`, and positive byte
counts. A successful build, popup opening, or placeholder rendering alone was
not treated as proof.

## Cache-first layer-popup performance revision

The corrected image-writer build was subsequently verified on the Elipsa 2E:
the user confirmed that isolated thumbnail contents render correctly. The next
hardware finding was performance: opening **Layers** took several seconds.

Source-level timing explains the regression. The popup formerly called
`widgetSave()` and then compared every preview PNG mtime against the one
notebook archive mtime. The save advanced that shared timestamp, so all layer
previews became stale together. `generateLayerPreview()` then performed every
1024×1024 export, validation decode, and atomic replacement synchronously on
the GUI thread before the native menu was even constructed.

The fix removes save/export work from the popup's first-paint path. Any
bounded, non-empty cached PNG is displayed immediately even when stale. A
popup-owned single-shot timer starts only after the menu enters its event loop,
reloads and verifies the live notebook/part identity, and refreshes at most one
stale row per open, active first. Closing the popup destroys the timer. The
explicit **Refresh layer previews** command retains its save-and-refresh-all
behavior. New trace fields record `native popup ready ms=`, deferred queue
size, per-row time, generated count, and remaining work. Device proof requires
that ready time plus confirmation that layer selection, isolated cards, and
eraser behavior remain unchanged.

The release candidate also persists the validated preview as the 48×48-or-less
inner image used by the 54×72 card, maintains a bounded 64-entry scaled-image
memory cache, treats decode failure as pending work, serializes exports per
widget, and adds a UUID to every temporary filename. These changes remove
repeated full-PNG decode/scaling and the PID-only collision case without adding
firmware-private APIs.

Clean local verification for this performance candidate:

```text
verify-layer-abi.py: 108 symbols, 5 vtable sizes, 31 vslots, 117 instruction pins
verify-layer-preview-abi.py: passed
test-verify-layer-artifacts.py: 3 passed
NickelTC clean / -j2 / koboroot: exit 0, Qt 5.2.1, -Wall -Wextra -Werror
e92ec6bb2c8a1162d851169685ecd078476f6c362f5098ce540e9f28cd9ffd60  KoboRoot.tgz
ba56052ad40841dae2a5db1854c54db291366a03a3e74b3690bd3bd299bd6419  libcustomnotebooktemplates.so
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea  packaged libiinknote.so
```

The archive has exactly the stock library and the plugin; both extracted
members are byte-identical to their package/build sources and ELF32 ARM EABI5.
Cross-objdump shows `showLayerPopup` starts its timer at `0x24f0a` and reaches
`QMenu::exec` at `0x24f74` without a call to `generateLayerPreview`. The only
automatic export call is in `continueDeferredLayerPreviewRefresh` at
`0x25f2c`, after live-context reload at `0x259de`. `generateLayerPreview`
emits the UUID at `0x167a6`, card downscale at `0x17002`, PNG save at
`0x1702c`, and guarded atomic rename path at `0x1710c` onward.
