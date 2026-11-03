# Cost of notebook refresh and narrower page-operation updates

## Results

On-device A/B tests on Elipsa 2E / firmware **4.38.23852**, in the same boot
with interleaved variants and **n=8–10 per variant**. Times below are medians
in milliseconds (synchronous edit / settled paint):

| Operation | Stock | Chosen default | Default median |
| --- | --- | --- | --- |
| Page move | 788 / 796 | `position` | 140 / 193 |
| Page duplicate | 1033 / 1117 | `setpart` | 465 / 787 |
| Layer activation | 748 / 757 | `none` | 7 / 17 |

All 95% bootstrap confidence intervals for the stock-versus-optimized timing
differences exclude zero. The duplicate `scroll` variant measured 480 / 825 ms,
equivalent to `setpart` but dependent on scroll gating; it was removed.
Correctness checks compared canvas hashes for the current page and its
previous/next neighbours after move and duplicate, and confirmed that the
layer popup closes without artifacts after selection. These measurements
establish synchronous and settled-paint improvements, not physical panel
completion timings.

Ship `move=position`, `dup=setpart`, and `layer-activate=none`: moves sync only
navigation chrome, duplicates keep the genuine part-change callbacks and
position sync, and layer selection only changes tool routing/state. These paths
no longer force a full-screen e-ink refresh. Missing `positionUpdated` falls
back automatically to stock for moves/duplicates; layer activation needs no
optional pin. Explicit `stock` values remain kill switches. Paint observation
is off unless `perf-experiment.txt` contains `settle=1`; synchronous
`perf: notebook-edit` records remain on, including `variant=stock-fallback`
when an optional pin is unavailable.

The original offline audit below predates these tests. Its candidate rankings,
unexecuted plan, and evidence limits describe that earlier investigation.

Offline audit, 2026-11-03. Target: Elipsa 2E / firmware **4.38.23852**.

## Offline finding

`IInkNotePadWidget::refresh()` is **a Qt signal**, not a package reload. Its
stock connection calls `IInkNotePad::refresh()`, which sets the notebook's
full-update counter to zero and synchronously repaints the **outer notebook
widget**. This forces painting of the canvas/background/chrome and selects the
full-update display hints. It does not repair page indices or labels.

The most promising replacements are:

1. **Move the already-current page:** omit both final calls and emit
   `IInkNotePadWidget::positionUpdated()` (`0x57fcc`). The part and canvas are
   unchanged; this updates the scrubber, page progress/label, and scroll arrows
   from the actual package index/count. No full-canvas invalidation is needed.
2. **Select a newly inserted clone:** use stock
   `IInkNotePadWidget::setScrollValue(targetZeroBasedIndex)` (`0x655b0`) in
   place of `editorSetPart(clone); widgetRefresh(widget)`. It selects the part
   through the editor, runs the normal part-change listeners, and increments
   the ordinary page-turn full-update counter. Verify the selected stable ID
   after the call: the function can refuse navigation while scrolling is
   disallowed. Alternatively, keep the existing `setPart(clone)` and remove
   `refresh()`, with an explicit `positionUpdated()` as a cheap final UI sync.

These are **candidates for a controlled device experiment, not hardware-verified
replacements**. In particular, a shorter synchronous duplicate trace can simply
mean that the first paint moved into the event loop. Measure time to completed
paint as well as the current edit timer before claiming a 600 ms saving.

A second important distinction: **widget `onContentChanged()` is not a harmless
UI update**. In a basic notebook it can append a blank page and save the
package. Do not call it, or the MyScript listener's `editorPartChanged`, merely
to repair the label. Widget **`contentChanged()`**, a different function, only
emits the UI-facing signal; its stock outer-pad receiver updates undo/redo and
navigation chrome without that document mutation.

## Scope, provenance, and reproduction

Read `AGENTS.md`, `BINARY_NINJA.md`, `PAGE_MANAGEMENT.md`, `FIRMWARE_MAP.md`, and
`LAYERS.md` before investigating; also consulted `WRITING_LATENCY.md` and the
current page/layer/cover/performance sources. No device connection, SSH,
kobo-touch, ping, or device network request was made. No installation, build,
firmware modification, or test on the reader was performed. The only repository
file written by this research is this new note. Existing working-tree changes
were left alone. Docker used the already-present NickelTC image with a
**read-only repository bind mount**; temporary disassemblies were outside the
repository. Colima was not used.

Exact local inputs, SHA-256:

```text
libiinknote.so
1fba163ca8de76d471ba5c58a4d95c523cc15a73d7eca83ba30c282d000f2ad3
libiink.so
0dc02118b9b763450c228da1e632dadbcce5cfd2acb41a3ca76a2472aed71ac6
libnickel.so.1.0.0
0e8e8ac3414a620d3386e60afcf4ccbf4ad3b8fdec8aeb5b553b01697966ec52
libiinkuiref.so.1.0.0
693aa804c31e3d783314ea2a61282730b0ada6f0b48029a3bc955ea78acd409a
```

All addresses below are **ELF VMAs**, with the Thumb bit cleared. Add `0x10000`
for the repository's Binary Ninja views. Resolve exported symbols normally;
only a raw function pointer synthesized from a verified VMA needs `| 1`.
`libnickel` addresses in this note are **23852**, not 23697 addresses.

Method: `nm -D -S -C`, `objdump -d -C`, dynamic symbol/vtable inspection, and
`.rel.plt` / `.rel.dyn` verification using the ELF tables. ARM PLT entries can
have a four-byte Thumb interworking veneer. A disassembly call shown as
`symbol@plt+0x4` can be the ARM body of the **same** entry, not a different
function. Targets below were checked against their GOT relocation. Unnamed Qt
lambda dispatchers are identified by entry address and constructor connection,
not by objdump's misleading nearest-exported-symbol-plus-offset label.

Example reproduction, read-only:

```sh
docker run --rm -v "$PWD:/w:ro" -w /w ghcr.io/pgaskin/nickeltc:1 \
  arm-nickel-linux-gnueabihf-objdump -d -C \
  --start-address=0x57fcc --stop-address=0x5800c \
  extracted/rootfs/usr/local/Kobo/libiinknote.so
```

Confidence terminology: **high** = directly demonstrated instructions/relocation
or connected control flow; **medium** = strong behavioral inference requiring
runtime validation; **unknown** = not established by these offline binaries.

## 1. The exact refresh path

### 1.1 Signal and stock receiver

`libiinknote.so`, `refresh()` at `0x57fec`, size `0x20`:

```text
r0 = IInkNotePadWidget* (primary QObject/QWidget base, no adjustment)
r1 = &IInkNotePadWidget::staticMetaObject
r2 = 5                     // signal index in this metaobject
r3 = nullptr               // no arguments
0x58000 -> QMetaObject::activate(QObject*, QMetaObject const*, int, void**)
```

The notebook constructor loads the widget `refresh` signal at `0x4ef18` and
the **outer** `IInkNotePad::refresh` slot at `0x4ef1c`; `connectImpl` is at
`0x4ef74`. The connection type on the stack is `0` (AutoConnection), so GUI
thread calls execute the receiver synchronously. Its receiver is the outer pad,
not the inner `EditorWidget`.

`IInkNotePad::refresh()` at `0x4af7c`, size `0x18`:

```cpp
// r0 = IInkNotePad* pad
pad->notepadWidget_at_0x28->forceFullUpdate(); // call 0x4af84
pad->QWidget::repaint();                     // tail call 0x4af90
```

`IInkNotePadWidget::forceFullUpdate()` at `0x58e98`, size `0x12`, only stores
integer **0** at widget `+0x11c`. There is no allocation, package open, editor
creation, save, label update, or thumbnail call in these functions.

The distinction is also visible in the stock overflow menu: its literal at
`0x848e4` is **“Refresh page”**, connected to `2refresh()` at `0x848f4`.
`IInkMenuController::refresh()` (`0x49478`) and `IInkToolBar::refresh()`
(`0x7069c`) are signal emitters, not rendering implementations.

**Conclusion: high confidence.** The measured `refresh` phase contains the
synchronous signal receivers and Qt repaint/display path. It is not evidence
of a 600 ms package reload.

### 1.2 Painting beneath repaint

The important downstream pieces are:

| Library / entry | Evidence and work |
| --- | --- |
| note `BackgroundWidget::paintEvent`, `0x419dc` | Uses the already-loaded brush at `+0x1c`, scaling at `+0x20`, and `QPainter::fillRect` at `0x41a74`; optional transform/clip handling. This is the separate paper/template widget. |
| note `BackgroundWidget::setBackgroundType`, `0x41cb0` | Catalogue/device-name resolution, `QPixmap` loads (e.g. `0x41d0a`, `0x41ec0`) and `QBrush::setTexture`. This setter is not called directly by refresh. A repaint does not itself resolve/reload the template PNG. |
| UI-ref `EditorWidget::paintEvent`, `0x1f508` | Only forwards to `QWidget::paintEvent`; actual ink paint is in its layer widgets. |
| UI-ref `LayerWidget::paintEvent`, `0x29af8` | Reads layer kind `+0x18`, canvas `+0x1c`, renderer `+0x20`, and paint-event rectangle. Kind 1 calls renderer vslot `+0x4c` at `0x29b66` = `drawModel`; kind 2 calls `+0x50` at `0x29b3a` = `drawCaptureStrokes`. These are model/capture rendering surfaces, **not** plugin named document layers. |
| iink `RendererImpl::drawModel`, `0x49f91c` | Computes the model extent (`0x49f9a4`), obtains backend/render target, builds a `RenderingContext`, then calls native renderer `draw` (`0x49fb1e` / `0x49fbaa`) or `drawSync` (`0x49fc04`). |
| iink `atk::core::Renderer::draw`, `0xa57758`; `drawSync`, `0xa57798` | Both reach `RenderPad::drawExtent` (`0xa511b4`). `draw` passes false, `drawSync` true as the stack boolean. Rendering is bounded by the requested viewport/paint rectangle, not a loop over notebook pages. |
| iink `RendererImpl::drawCaptureStrokes`, `0x49f464` | Separate current capture/overlay surface. Not notebook-wide rendering. |

The stock layer-widget vslots were resolved against the `RendererImpl` vtable
at `0xd879f0` (address point `+8`): `+0x4c -> 0x49f91c`, `+0x50 -> 0x49f464`.
A broad outer-widget repaint can therefore revisit the ink, background, and
chrome even though the current part never changed. The live canvas remains an
unrestricted composite of document layers; no `restrictToLayers` call is made
by refresh. This does **not** establish that every stroke is recomputed on
every repaint: RenderPad's tile/offscreen caches and the dirty region matter.

A further descent into `RenderPad::drawExtent` (`0xa511b4`, size `0x98c`)
confirms those alternatives. It obtains the current `ViewContext` at
`0xa511fa`, takes rendering/model locks, prepares animations (`0xa512d2`),
and has a tiled draw call at `0xa5150e -> TilesManager::drawExtent`
(`0xa6be6c`). Its other paint path locks the current Layout (`0xa51544`),
constructs a Selection (`0xa51620`), selects the requested rectangle
(`0xa51634 -> Selection::selectRectangle`, `0x9e2884`), and builds a painter
(`0xa51672 -> createPainter`, `0xa4b254`). It draws temporaries at
`0xa5176a` and can schedule animation continuation at
`0xa518b2 -> Scheduler::add` (`0xa55bdc`). A fallback reaches
`drawExtentWithoutOffscreenRendering` (`0xa50c94`) at `0xa519c0`.
These are current-view raster/cache/animation paths, not package iteration.
Lock contention, tile regeneration, or the rectangle's content can all
contribute to synchronous paint cost; this audit does not assert which live
branch the measured page took.

There is **no call to the separate D-Bus thumbnailer, PageController PNG
export, or a notebook-page enumeration** on this refresh path. The normal
package/database save/thumbnail pipeline is separate from this operation.

### 1.3 Full e-ink update hints

The notebook event filter is exported at `0x58f3c`. For Paint events (type 12,
check at `0x58f98`) it consults the platform screen, paint region, dark-mode /
Regal settings, `ReadingSettings::getNumPartialUpdatePageTurns()`, and the
counter at widget `+0x11c`:

- `0x590ae..0x590c0`: for a nonnegative counter, computes the remainder modulo
  `partialTurns + 1`. A zero remainder enters the full-update branch.
- `forceFullUpdate()` deliberately set that counter to zero, so the next
  eligible broad paint selects this branch.
- `0x59176..0x5919a`: calls `QWidget::setAttribute` with Kobo-specific attribute
  numbers `151=false`, `134=true`, `128=true`, `131=true`.
- `0x591a0`: `fullUpdateReset()` (`0x58e84`) stores **1** at `+0x11c`.
- Other branches select attributes `130`, `138`, `144`, `150`, depending on
  region/platform/settings. The pre-check at `0x58f80` reads a screen property;
  the paint path can be bypassed depending on that property.

Thus refresh **requests full-update behavior through normal Qt/QPA hints**.
It does not directly issue an ioctl, write framebuffer memory, or wait for a
panel completion inside the 32-byte signal function. The exact waveform and
whether the platform flush blocks until completion require runtime evidence;
attribute numbers alone must not be given invented enum names. The existing
`WRITING_LATENCY.md` documents the separate MTK QPA waveform selection and log
formats, but the four analyzed libraries do not prove its completion timing.

### 1.4 What plausibly costs 525–640 ms?

**Strongest localization:** synchronous **outer `QWidget::repaint()` and its
paint/flush work**. `forceFullUpdate` and signal dispatch themselves are tiny.
Within repaint, the current-page model drawing (`LayerWidget -> drawModel ->
RenderPad`), full viewport raster/composition, and the QPA full-update flush /
possible display synchronization are the credible large costs. A template
brush fill is additional raster work; a fresh template file decode is not
inherent to refresh. Notebook-wide package load, editor recreation, and
thumbnail generation are excluded by the observed path.

The supplied n=10 timings establish the aggregate, not this subdivision.
**Cannot honestly assign 600 ms to the waveform or to ink rendering from
static analysis alone.** A nearly constant cost across pages with very
different ink complexity would favor full-frame composition/display overhead;
ink-complexity scaling would favor model rendering. Use the nested timings in
section 6 to distinguish them.

## 2. Stock page changes and navigation chrome

### 2.1 Page navigation: no forced refresh

`IInkNotePadWidget::swipePage(bool,int)` at `0x65bcc` exits handwriting,
queries `scrollValue` / `scrollMaximum`, bounds the direction, and tail-calls
`scrollTo(index)` at `0x65c06`. Horizontal/vertical gesture helpers are at
`0x65c40` / `0x65c68`; the basic-notebook gesture path calls horizontal swipe
at `0x65e78`, whose tail call at `0x65c62` reaches `swipePage`. The vertical
helper similarly calls it at `0x65c8a`. Both converge on the same index setter
used by the scrubber.

`scrollTo(int)` (`0x65b98`) first calls `setScrollValue(int)` at `0x65ba0`.
If bookmark/search highlighting (`+0xaa`) is active, it clears/reapplies
highlight state (`0x65bb0`, `0x65bc6`). Use this wrapper for a navigation that
must preserve stock search-highlight behavior; for a page-menu operation with
no active search, the direct setter is the narrower entry.

`setScrollValue(int)` (`0x655b0`), basic-notebook branch:

```cpp
auto editor = lock(widget->editorWeak_at_0x3c);
if (!editor || !editor->isScrollAllowed()) return;
auto part = editor->getPart();
auto package = part->getPackage();
if (package->indexOfPart(part) != target) {
    editor->setPart(package->getPart(target));
    widget->fullUpdateIncrement();
}
```

Evidence: editor vslot `+0xd0` at `0x655e0`; basic flag `+0xa8` at `0x655e8`;
editor getPart vslot `+0x20` at `0x655fa`; package indexOfPart `+0x20` at
`0x6562a`; equality branch at `0x65636..38`; package getPart `+0x18` at
`0x65646`; editor setPart `+0x24` at `0x65662`; increment at `0x6566e`.
The Advanced notebook branch scrolls the renderer offset instead of selecting
another part. **Do not use it as a basic-page setter on an Advanced notebook.**

There is no `refresh()`, `repaint()`, `openPackage()`, or `renderVolume()` in
this normal basic-page selection path. `fullUpdateIncrement()` merely adds
one to `+0x11c`; stock navigation lets the periodic display policy choose a
full update rather than forcing one on every turn. Passing the current index
is **not** a way to refresh chrome: it hits the equality branch and skips
setPart and its notifications.

### 2.2 Editor setPart: the necessary expensive part switch

`myscript::iink::EditorImpl::setPart(shared_ptr<ContentPart>)`, iink
`0x438154`, size `0xa6c`:

1. `0x43815a..168`: compares incoming raw part pointer against editor `+0x4c`;
   equal pointers return via `0x4386f6` **before** reset, notifications, or
   viewport invalidation. This explains the measured zero-cost same-part call.
2. Changed part: cancels rendering (`0x438208 -> RendererImpl::cancelRendering`,
   `0x49e480`), detaches old part/backend listeners, calls `reset()` at
   `0x438286`, installs the new `ContentPartImpl`, updates metadata/configuration.
3. `reset()` at `0x42ba18` unbinds the existing public renderer (`0x42ba40 ->
   RendererImpl::bind`, `0x49fe30`), releases backend/process registrations.
   `configure()` (`0x437898`, call `0x438536`) looks up the backend factory at
   `0x4378ec`, invokes it at `0x4379d4`, binds the page backend/model renderer
   at `0x437b1a`, and registers recognition/gesture processors. This is real
   **backend/model-renderer reconfiguration**, not creating a fresh public
   Editor or reopening the ZIP package.
4. Calls `EditorImpl::partChanged()` (`0x42efe0`) at `0x4386b2`, dispatching
   listener vslot `+8` at `0x42f26a` (the `IEditorListener::partChanged` slot).
5. Calls `RendererImpl::updateViewport()` (`0x49e78c`) at `0x4386b8`.
   This reaches native `Renderer::setViewport` at `0x49e8f4` and render-target
   rectangle invalidation at `0x49e96c`: rectangle `(0,0,width,height)`, flags
   **1**. UI-ref `IInkRenderTarget::invalidate` (`0x260d0`, full-area overload
   `0x265f0`) requests normal Qt updates via its `update` signal (`0x34558`).

The existing 215–395 ms clone `setPart` timing can include backend setup,
listener work, metadata/background preparation, and cache/viewport work. It
is not eliminated by substituting `setScrollValue`: that function still does
one genuine setPart when selecting a different page.

### 2.3 Part-change listeners: label, undo buttons, background

The widget constructor establishes these separate connections:

| Constructor evidence | Connection |
| --- | --- |
| `0x61346..0x613ae` | `IInkEditorListener::editorPartChanged` -> widget **`contentChanged` signal**, not widget `onContentChanged`. |
| `0x613bc..0x61414` | Same listener signal -> widget `partChanged` signal. |
| `0x61422..0x61518` | Same listener signal -> private Qt lambda dispatcher `0x5bca8`, which restores page background metadata. |
| `0x61526..0x61572` | `QTimer::timeout` on the widget's content-policy timer -> private dispatcher `0x65134`; it runs widget `onContentChanged` when idle, otherwise restarts the `+0x60` timer for 150 ms. The signal identity is saved from the timeout GOT load at `0x60d28..0x60d3a`. |
| `0x6195e..0x619b0` | Renderer listener `positionUpdated` -> private dispatcher `0x5d260`, which schedules widget update and emits widget `positionUpdated`. |

The outer notebook connects widget `contentChanged` ->
`IInkNotePad::onContentChanged(shared_ptr<Editor>)` at `0x4ee48..0x4eea8`.
That slot (`0x4bb7c`, size `0x5c`) does:

```cpp
undoButton->setEnabled(editor->canUndo()); // 0x4bb92 / 0x4bb98
redoButton->setEnabled(editor->canRedo()); // 0x4bbaa / 0x4bbb0
updateScrubberMenu(widget->scrollValue(), widget->scrollMaximum());
updateScrollBar();
```

It is a chrome update and does not append a page. Conversely, outer
`IInkNotePad::onPartChanged()` (`0x4ae70`) is a **no-op** (12 bytes); calling
that slot cannot fix anything.

The private background dispatcher `0x5bca8` is a Qt slot-object implementation,
**not** a callable `void(widget*)` method. It locks the live editor, obtains the
current part's metadata/configuration, resolves `kobo.backgroundType` and
fallbacks, and calls `IInkNotePadWidget::setBackgroundType(QString const&)`
at `0x5bea4`. The setter (`0x5b9fc`) calls the background widget setter at
`0x5ba0c` and reconciles serialized configuration/metadata. This is why simply
painting after changing a raw part pointer is insufficient, and why a real
stock setPart with its listeners is preferable.

`updateViewScale()` (`0x65ef0`) is used on volume open and resize (e.g.
`0x668be`, `0x666fa`); it is a large configuration/geometry function, **not**
the cheap label updater. Ordinary part navigation does not use it as an extra
post-selection refresh.

### 2.4 The exact page-label and scrubber path

Widget `positionUpdated()` at `0x57fcc` emits signal index **4**. In
`IInkNotePad::setupScrubberMenu()` (`0x4b8e0`), the connection created at
`0x4b956` targets private dispatcher `0x4b898`. Its active branch
`0x4b8c0..0x4b8dc` queries `scrollValue()` and `scrollMaximum()` and calls
outer `updateScrubberMenu(int,int)`.

For **basic** notebooks these accessors read live package state:

- `scrollValue()` (`0x59d18`): editor `getPart` -> part `getPackage` -> package
  `indexOfPart(currentPart)`. Basic branch `0x59dfe..0x59e38`. No cached page
  ordinal is read from the widget.
- `scrollMaximum()` (`0x59e9c`): same part/package -> `getPartCount() - 1`, at
  `0x59f6c..0x59f7e`. Both values are **zero-based**.

Outer `IInkNotePad::updateScrubberMenu(i,max)` (`0x4b86c`):

```cpp
if (!pad->scrubberController_at_0x38) return;
pad->scrubberController_at_0x38->updateScrubberMenu(i, max); // 0x4b882
pad->progressController_at_0x40->setProgress(i, max);        // 0x4b892
```

On 23852, `libnickel` implements:

- `IInkNotePadScrubberController::updateScrubberMenu`, **`0xda0500`**:
  obtains/lazily creates the native view, gets its actual scrubber, then
  `QAbstractSlider::setRange(0,max)` at `0xda057a`, `setValue(i)` at
  `0xda0586`, and `updateScrubberArrows()` at `0xda058c` (`0xda00f8`).
- `IInkProgressController::setProgress`, **`0xda19c0`**:
  updates `QuickTurnSlider::setProgress(i,max)` at `0xda1a50`, substitutes
  **i+1** (`0xda1a76`) and **max+1** (`0xda1a8a`) into the translated literal
  **`%1 OF %2`** at `0x13d1720`, and calls `QLabel::setText` at `0xda1aac`.
  Its label at controller `+0x18` is a `MetaDataLabel`: `loadView()`
  (`0xda1f2c`) constructs a QLabel and installs that vtable at `0xda1fe8`.
  This is the numeric “PAGE i OF n” UI requested in the task; the exact
  substitution string in the binary itself is `%1 OF %2`.
- `QuickTurnSlider::setProgress`, **`0xf59a68`**:
  `setMaximum(max)` (`0xf59a74`), then `setSliderPosition` (`0xf59a8a`),
  using `i` or `max-i` according to its direction byte `+0x26`.

`setupScrubberMenu` also connects scrubber-controller `valueChanged(int)`
through private dispatcher `0x4b0e8` to widget `setScrollValue` (tail call
`0x4b12a`). Qt slider value changes can emit this path again; the setter's
live-index equality check makes the ordinary same-current-page update a
no-op. Do not introduce signal blocking unless a separate audit justifies it.
The `positionUpdated` connection also updates the ordinary scrollbar through
`IInkNotePad::updateScrollBar` (`0x4bb24`, connection `0x4bcee..0x4bd46`).

### 2.5 Stock add page, delete, clear, undo, redo

**Add page in a basic notebook:** there is no exported widget `addPage` slot
and no “Add page” menu row in this binary. The important stock structural path
is **automatic blank-tail creation** in widget `onContentChanged` at
`0x64cd4`:

1. Gets current part/package, tests basic flag `+0xa8`.
2. Compares `getPartCount()-1` with `indexOfPart(current)` at `0x64d14` /
   `0x64d56..5a`.
3. If the current page is last, tests its `getViewBox` (part vslot `+0x14`)
   against four zero floats at `0x64f4e..0x64f9e`. A nonempty last page enters
   the append branch.
4. Reads its background metadata (`0x64d76..0x64dba`), gets its part type
   (part vslot `+0x10`, call `0x64de8`), and invokes package **createPart**
   vslot `+0x24` at **`0x64df2`** (`ContentPackageImpl::createPart`,
   `0x3f2ecc`). Copies background metadata to the new blank part at
   `0x64e28`. The new page is not made current in this branch.
5. Outside stylus proximity, saves the package unless demo mode
   (`0x64f44..0x64f4a`), then emits widget **contentChanged** at `0x64ede`.
   The outer receiver updates the range, count/label, and undo buttons.

There is no explicit widget `refresh()` here. Navigation into that new blank
page uses the normal index setter. This also explains why synthesizing engine
content/part callbacks is unsafe for an index-only move: it can change page
count if the moved page is now the nonempty tail. Its extra append is a stock
policy, not evidence that a clone operation malfunctioned internally.

**Delete page:** menu signal `IInkMenuController::deletePage` (`0x4950c`) ->
toolbar signal (`0x7061c`) -> outer `onDeletePage` (`0x4b534`, connection in
constructor `0x4e586..`). The confirmation dialog's accepted signal is
connected directly to widget `deletePage()` (`0x63dd4`, connection built
`0x4b54a..0x4b5b4`). That method gets old index/count, calls package
removePart vslot `+0x28` at `0x63eda`, chooses the surviving index (decrement
if deleting old last, `0x63ee4..0x63ef2`), gets that part at `0x63f0a`, and
sets it via editor vslot `+0x24` at **`0x63f10`**. It saves package at
`0x63f76` unless demo mode. If count was one it calls `clear()` at
`0x63f92`. The multi-page path has **no forced widget refresh**: genuine
setPart/listeners update the view and navigation state.

**Clear page:** menu/controller clear signal (`0x49458`) -> toolbar clear
(`0x7067c`) -> outer `onClearTapped` (`0x4b288`) -> native confirmation.
The accepted callback is private dispatcher `0x4b130`, active branch
`0x4b158`: calls widget `clear()` (`0x63c70`) and then **outer pad refresh**
at `0x4b168`. Widget clear exits handwriting, sets byte `+0x178` at
`0x63cca`, invokes editor clear vslot `+0x28` at `0x63cd2`, and resets the
renderer offset through vslot `+0x30` at `0x63cf2`. Thus stock clear **does**
force a full repaint, via the outer slot rather than emitting widget refresh.

**Undo / redo:** toolbar buttons are connected in the outer constructor at
`0x4e380..` and `0x4e3e8..` to widget `undo()` (`0x64074`) / `redo()`
(`0x64158`). They lock the weak editor and call its vslot `+0x30` at
`0x640be` / `+0x38` at `0x641a2`, respectively (`EditorImpl::undo`,
`0x428a64`; `redo`, `0x428b64`). They do **not** explicitly call refresh.
Engine content/renderer notifications drive subsequent paint and chrome;
the shared content-change path may also run the basic blank-tail policy.

**Explicit Refresh page:** toolbar refresh is wired to outer refresh through
a constructor lambda (signal load `0x4ed14`, dispatcher `0x4b0a8`), ending at
`0x4b0e4 -> IInkNotePad::refresh`. This is the full-display cleanup command,
not the normal page-change notification.

## 3. Proposed sequences and correctness boundaries

### 3.1 Reorder while keeping exactly the same current part

Keep the existing pre-save, cover restrictions, current-session notebook
backup, `documentMovePage`, package save, structural checks, widget save, and
cover-cache invalidation. Replace only the final two calls:

```cpp
// Same exact live Editor and ContentPart; content/layout/background unchanged.
// Existing package move/save and widget-save-post have completed.
firmware.widgetPositionUpdated(widget); // exported 0x57fcc
// No editorSetPart(samePart); no widgetRefresh().
```

Expected correctness:

- Canvas/background stay on the same part, so no repaint or background reload
  is necessary just because its ordinal changed.
- Index/count and scrubber range/value derive from the live package and update
  through the stock UI signal.
- Future next/previous navigation queries that same live index/package, not
  the old ordinal stored in chrome.
- This does not reset backend/capture/recognition or undo state. The existing
  same-part setPart also did not reset them (early equality return).
- The existing widget-save-post is after the move, so it can persist the new
  current index. `IInkNotePadWidget::save()` writes `IInkSettings::setPartIndex`
  at **`0x5fa96`**, separately from package save.

**Confidence:** high for the code/path distinctions, medium for end-to-end
correctness until navigation/undo/reopen are exercised. If undo/redo buttons
prove stale due to structural engine state changes, emit widget
`contentChanged(sharedEditor)` instead of (or before) `positionUpdated`.
Its outer receiver additionally queries canUndo/canRedo and performs the same
chrome sync. Do **not** call widget `onContentChanged` for this purpose.

Do not send an editor-level partChanged notification, temporarily select a
neighbor, or setPart(nullptr) and back: those trigger backend resets and extra
policy work and undermine the point of a same-current-part move.

### 3.2 Insert clone, then make it current

Keep existing backup, clone/import, reorder, and serialization verification.
After the final insertion, before selecting anything, establish target index
and stable clone ID. Use **one** of these alternatives:

```cpp
// Candidate A: stock basic-page navigation, no active bookmark/search.
firmware.widgetSetScrollValue(widget, cloneIndex); // 0x655b0
// Verify editorGetPart() has expected stable ID and package index.
// Optional explicit final chrome sync, if natural notifications are insufficient:
firmware.widgetPositionUpdated(widget);           // 0x57fcc
```

Or, as the smallest implementation experiment:

```cpp
// Candidate B: keep the existing owned SharedPart argument and actual setPart.
firmware.editorSetPart(editor, clone);             // 0x438154
firmware.widgetPositionUpdated(widget);           // cheap final count/index sync
// No widgetRefresh().
```

Candidate A preserves stock `isScrollAllowed` gating and page-turn counter
behavior. Use `scrollTo(index)` (`0x65b98`) if active search/highlight must
follow the transition; it adds those semantics. Candidate B unconditionally
selects the already-validated owned clone as today, but does not automatically
increment the periodic full-update counter; if required, add
`fullUpdateIncrement()` **once, only after a changed part**. Prefer the stock
wrapper over manually replicating its policy.

The genuine setPart runs the background-restoration listeners and invalidates
the new viewport. There is no static evidence that another full outer repaint
is needed for template correctness. Confirm with visibly different templates,
including plugin custom paper and cover/image objects. A private lambda such
as `0x5bca8` is not an exported post-part hook and should not be called directly.

**Persistence ordering:** current `duplicateCurrentPage` calls
`widget-save-post` **before** selecting the clone. This commits document data
but can store the old current-page preference. Refresh never fixed that.
For a candidate implementation, put the existing final widget save **after**
selection and its completed synchronous listeners if immediate reopen-on-clone
is a requirement; keep `package-save` / archive validation before selection
for the existing transaction guarantees. Verify final count after selection
and pending content-policy callbacks, because stock onContentChanged can append
a blank tail; its invocation is not guaranteed merely by selecting a part. Do not silently accept
an unexamined N+2 or turn this into a page-policy change.

**Undo:** selection through stock setPart produces the same backend reset /
configuration as ordinary page navigation. Do not try to preserve old-page
undo by bypassing setPart or forcing a new editor. The expected policy is
stock content undo on the selected page; moving/duplicating is not proven to
be a visible undoable page-structure command. `Document::movePage` (`0x9f09e4`)
forwards to `myscript::document::Document::movePage_` (`0xae5fd0`), then the
provider interface; the wrapper alone does not prove a ghost/visible undo
transaction. Test rather than claiming structural Undo support.

Reapply/validate the plugin's active-layer routing **after** the real part
switch if the operation requires it, as `LAYERS.md` already cautions. A
full refresh does not re-create tool restrictions or perform that restore.
This is an existing lifecycle requirement, independent of removing repaint.

### 3.3 Is setPart + refresh double-rendering? Can one call replace both?

- It is **not** two setPart operations or two package loads. Refresh does no
  setPart at all.
- A changed setPart schedules/requests a full-viewport model invalidation;
  refresh then independently repaints the entire outer pad and forces the
  full display policy. Thus they overlap in canvas work and refresh can cause
  an additional broad paint/display update.
- Static analysis cannot prove two completed renders in every call pair:
  pending Qt invalidations may be consumed by the synchronous repaint. The
  refresh timing can be the **first actual paint** after setPart, not a second
  completed paint. “Guaranteed double-render” would overstate the evidence.
- **Yes**, `setScrollValue(target)` (or `scrollTo(target)` for search handling)
  is an exported single-call candidate replacing both in a basic notebook
  when the target is a different attached part. It is the stock navigation
  operation and leaves painting to the usual render-target/event-loop path.
- **No**, a same-index call to that setter is not the replacement for reorder:
  it returns without notifying chrome. Emit `positionUpdated` for that case.

### 3.4 Layer/cover callers are separate experiments

Do not remove every `widgetRefresh` call globally based on this page audit.
A layer **selection** that only changes tool routing plausibly needs no canvas
repaint; layer add/delete may need engine invalidation or a scheduled widget
update; cover content changes may need image-loader/cache handling. Those are
different from moving an unchanged part. Keep their current behavior until
separately measured. Never compensate with `restrictToLayers` on the live
renderer: `LAYERS.md` already records the unsupported live-canvas path.

## 4. ABI and candidate function ledger

All ordinary member functions below use the complete primary object in `r0`,
with **zero this-adjustment**, unless stated otherwise. A C++ `shared_ptr<T>`
passed by value is nontrivial under this ARM ABI: the explicit parameter is a
pointer to the caller-constructed two-word temporary, **not** two raw argument
registers. Use the repository's cross-compiled `SharedPart`/editor owner types
and real copied ownership. Do not fabricate ownership from `widget+0x40`.

### 4.1 Symbols proposed for direct calls

| Function / ELF VMA / mangled name | ABI, fields, preconditions, risks, verification |
| --- | --- |
| **Widget positionUpdated**, note `0x57fcc`, size `0x20`; `_ZN17IInkNotePadWidget15positionUpdatedEv` | `void(widget*)`, only `r0`. Signal index 4. No direct instance-field reads in emitter. Requires fully constructed, live widget and its outer-pad/scrubber connections, on Qt GUI thread. Receivers read live `+0x3c/+0x40` editor weak owner, basic flag `+0xa8`, actual package index/count. Risk: signal receivers are synchronous and slider changes can reenter stock index setter. Verify label, both slider surfaces, boundary arrows, next/prev, and that setPart/render calls did not happen for reorder. **High** ABI/path, **medium** replacement. |
| **Widget setScrollValue**, note `0x655b0`, size `0x370`; `_ZN17IInkNotePadWidget14setScrollValueEi` | `void(widget*, int)`: `r0=widget`, `r1=zeroBasedTarget`. Reads `+0x3c/+0x40` weak editor, `+0xa8`; successful changed-basic-page path increments `+0x11c`. Requires basic notebook, attached target in `[0,count-1]`, idle/capture-safe editor and isScrollAllowed true. No boolean result: verify actual current ID/index. Catches stock engine errors; a return is not proof of selection. Same index does not sync chrome. Verify clone ID/template, neighbors, undo and one setPart. **High** ABI/path, **medium** replacement. |
| **Widget scrollTo**, note `0x65b98`, size `0x32`; `_ZN17IInkNotePadWidget8scrollToEi` | Same register ABI; wraps setter, reads `+0xaa` highlight state and `+0xa8` basic flag. Handles search unhighlight/highlight. Same gating as setter; do not use to fake an index-only notification. Verify active search navigation if this variant is chosen. **High** path, **medium** replacement. |
| **EditorImpl setPart**, iink `0x438154`, size `0xa6c`; `_ZN8myscript4iink10EditorImpl7setPartESt10shared_ptrINS0_11ContentPartEE` | `r0=EditorImpl*`, `r1=&temporary shared_ptr<ContentPart>`; no sret and no adjusted base. Reads current part `+0x4c/+0x50`, engine `+0x2c`, public renderer `+0x34`, backend owner `+0x54/+0x58`, locks internal mutex `+0x14`. Requires real owned attached clone and valid live editor; same raw pointer short-circuits. Backend/listener exceptions and stock auto-append are relevant. Verify same engine/public renderer identity, changed current part, background, notifications, undo; preserve existing exception boundary. **High** ABI/equality/path, **medium** asynchronous correctness. |
| **Widget contentChanged SIGNAL**, note `0x57f74`, size `0x2c`; `_ZN17IInkNotePadWidget14contentChangedESt10shared_ptrIN8myscript4iink6EditorEE` | `r0=widget`, `r1=&temporary shared_ptr<Editor>`; emits signal index 2 with argument pointer array. Requires valid strong live editor owner and GUI-thread outer-pad connection. This is the optional chrome+undo update; no engine model mutation in emitter or stock outer receiver. Verify canUndo/canRedo button states and page index/count; do not confuse with `onContentChanged`. **High** stock path, **medium** use after external structure mutation. |
| **Outer pad updateScrubberMenu**, note `0x4b86c`, size `0x2c`; `_ZN11IInkNotePad18updateScrubberMenuEii` | `r0=IInkNotePad*`, `r1=i`, `r2=count-1`. Reads controller pointers `+0x38/+0x40`. Obtain/validate the outer pad through QObject ancestry/type, not a guessed offset from widget. Requires initialized controllers and correct 0-based values; bypasses live accessor queries. Updates both surfaces but not undo/redo. Pin controller targets if using directly; prefer positionUpdated to avoid owning this layout. **High** ABI/path, lower implementation desirability. |
| **Widget fullUpdateIncrement**, note `0x58e6c`, size `0x16`; `_ZN17IInkNotePadWidget19fullUpdateIncrementEv` | `void(widget*)`, adds 1 to `+0x11c`; no renderer or label work. Optional only with direct changed-part candidate B to match normal navigation, never in addition to successful stock setter. Verify counter/policy sequence using observed calls, not field writes. **High** operation, **medium** necessity. |
| **QWidget::update()** (linked Qt API; no new firmware VMA proposed) | `_ZN7QWidget6updateEv`, `r0=primary QWidget*=widget`. Schedules painting without forcing `+0x11c=0`. Optional fallback if invalidation evidence shows a needed canvas repaint; it does not repair index/count or load a new part/background. Verify paint completion and ghosting; enqueue time alone is meaningless. **High** distinction, **medium** fallback. |

Additional exported accessors: note `scrollValue()` `0x59d18`
(`_ZNK17IInkNotePadWidget11scrollValueEv`) and `scrollMaximum()` `0x59e9c`
(`_ZNK17IInkNotePadWidget13scrollMaximumEv`), both `int(widget const*)` with
result in `r0`. Basic-path semantics and offsets are documented above. Prefer
existing package count/index APIs for postconditions; these accessors are useful
for proving the widget agrees with that state.

### 4.2 Controller targets, not first-choice direct replacements

| Function (23852 libnickel) | Symbol / ABI / fields |
| --- | --- |
| Scrubber `updateScrubberMenu`, `0xda0500`, size `0x1fc` | `_ZN29IInkNotePadScrubberController18updateScrubberMenuEii`; `r0=complete controller`, `r1=i`, `r2=max`. Controller secondary AbstractController subobject is `+8`; implementation adjusts internally. View QPointer guard/raw at `+0xc/+0x10`; validates guard `+4`, creates/obtains native view. Do **not** pass the `+8` subobject to the exported method. Requires live parent/native view machinery; updates range/value/arrows but not label. |
| Progress `setProgress`, `0xda19c0`, size `0x30c` | `_ZN22IInkProgressController11setProgressEii`; same r0/r1/r2 shape and primary-object requirement. View guard/raw `+0xc/+0x10`, parent `+0x14`, MetaDataLabel `+0x18`. Creates/obtains view, slider maximum/position and translated label. Calling only this leaves scrubber range/arrows stale. |
| QuickTurnSlider `setProgress`, `0xf59a68`, size `0x26` | `_ZN15QuickTurnSlider11setProgressEii`; r0 slider, r1 i, r2 max. Reads direction byte `+0x26`. Updates maximum and slider position only; bypasses label and full scrubber. Not an adequate standalone page-operation update. |

All three have **high** static ABI confidence. Their precondition is correctly
typed initialized firmware objects on the GUI thread. If a future implementation
uses them directly, pin their exact 23852 profile plus the constructor/layout
reads and verify every visible surface. No need to add these private-field
accesses merely to emit the existing widget signal.

### 4.3 Entries to avoid

- Widget **onContentChanged**, `0x64cd4`,
  `_ZN17IInkNotePadWidget16onContentChangedESt10shared_ptrIN8myscript4iink6EditorEE`:
  same shared-editor argument ABI but can append/save pages. Not a label API.
- Outer **onPartChanged**, `0x4ae70`: no-op.
- Widget **refresh**, `0x57fec`, `_ZN17IInkNotePadWidget7refreshEv`: full-update
  signal, not metadata synchronization.
- Outer **refresh**, `0x4af7c`, `_ZN11IInkNotePad7refreshEv`: primary pad receiver
  required, `+0x28` inner widget; forces full update then synchronous repaint.
  Calling it on the inner widget is a wrong-this memory access.
- Widget **forceFullUpdate**, `0x58e98`,
  `_ZN17IInkNotePadWidget15forceFullUpdateEv`: stores zero at `+0x11c` only.
  Removing just this call by patching stock refresh is broader than using a
  narrower exported notification and still leaves the expensive repaint.
- Private dispatchers `0x4b898`, `0x5bca8`, `0x5d260`, `0x65134` have Qt
  slot-object ABI `(operation, slotObject, receiver, void** args, bool* ret)`.
  Their active case is operation 1; captured widget/pad is usually
  `slotObject+8`. They are not safe one-argument member functions.
- UI-ref `EditorWidget::flushInvalidArea` (`0x1d5bc`,
  `_ZN8myscript4iink25uireferenceimplementation12EditorWidget16flushInvalidAreaEv`)
  only flushes a pending render-target invalid rectangle (gated by its
  byte `+0x60`), using target `+0x38` and `QWidget::update(QRect)` at
  `0x1d5e4`; it does not update
  page labels, select a clone, or guarantee complete background redraw.

### 4.4 Pin material for the first experiment

Use exact hash/profile and symbol/VMA/size verification, with call-site checks
for critical semantics. Example first 12 bytes (hex; prologue alone is not a
sufficient identity check):

```text
note positionUpdated 0x57fcc / 0x20:
05 49 00 23 90 b4 04 22 04 4c 00 af
note setScrollValue 0x655b0 / 0x370:
2d e9 f0 4f 93 b0 00 af 05 46 07 f1
note scrollTo 0x65b98 / 0x32:
b0 b5 04 46 00 af 0d 46 d0 f7 cc ec
note contentChanged signal 0x57f74 / 0x2c:
f0 b5 83 b0 07 4c 00 26 00 af 07 4d
note fullUpdateIncrement 0x58e6c / 0x16:
d0 f8 1c 31 80 b4 00 af 01 33 c0 f8
ink setPart 0x438154 / 0xa6c:
2d e9 f0 4f a7 b0 0a 68 00 af c3 6c
```

Pin `positionUpdated`'s activate index 4 and constructor receiver path;
`setScrollValue`'s `+0xa8` branch, equality check, virtual getPart/setPart
slots and counter increment; setPart's same-pointer return and final
partChanged/updateViewport calls. Existing firmware-profile gating remains
required. Do not transplant 23852 libnickel VMAs into the 23697 column.

## 5. Ranked candidate replacements

| Rank | Operation / replacement | Benefit and remaining uncertainty |
| --- | --- | --- |
| 1 | Reorder: `positionUpdated(widget)` only | Highest confidence of avoiding actual unnecessary canvas work: part/content are unchanged. Likely removes most of the supplied 640 ms synchronous cost; repaint of changed chrome remains. Test physical ghosting and navigation. |
| 2 | Duplicate: stock `setScrollValue(cloneIndex)` only; verify ID/index | Closest to normal page navigation; replaces both final calls, includes ordinary counter policy. Does not avoid necessary backend change or first canvas paint. May be refused by scroll gating. |
| 3 | Duplicate: existing `setPart(clone)` + `positionUpdated`, no refresh | Smallest controlled source change; no new private layout access. Add stock increment only if matching page-turn policy is required. Can expose whether refresh was redundant versus merely flushing queued painting. |
| 4 | Chrome-only `contentChanged(sharedEditor)` signal for reorder | Updates undo/redo too. Slightly more work/ABI ownership than positionUpdated; safely bypasses widget onContentChanged policy. Useful if rank 1 leaves toolbar state stale. |
| 5 | Above plus scheduled `QWidget::update()` | Fallback if evidence shows missing repaint; still avoids forcing full flash. Greater painted area and asynchronous timing risk; measure completion. |
| 6 | Outer `updateScrubberMenu(i,count-1)` directly | Precise UI helper but requires finding correct outer pad and supplying values; no advantage sufficient to justify new offsets when a stock widget signal already computes them. |

Do not rank raw renderer invalidation, temporary part deselection, custom
waveform/attribute writes, or direct private lambda calls as cheap safe fixes.
They add ABI/lifecycle risk and are unnecessary to test this hypothesis.

## 6. Concrete minimal experiment plan (not executed)

This research task forbids device access. The following is a handoff plan for
a separately authorized implementation/test session, subject to `AGENTS.md`.
Use the current-session verified backup and required `make ... check` gate
before deployment; no USB/configfs/kernel changes, no manual device field
writes, no new reboot strategy. Stop at the first unexpected result.

### Stage 0: baseline attribution, without changing behavior

1. Pin and optionally instrument the signal receiver chain above. Keep a
   per-operation reentrancy/depth guard so unrelated notebook paint isn't
   charged to the edit.
2. Retain existing exclusive phases: `widget-save-pre`, `context`, `backup`,
   `engine`, `package-save`, `widget-save-post`, `set-part`, `refresh`.
   Inside the existing refresh phase, add named nested phases around the
   **stock** outer refresh, forceFullUpdate, and repaint entry/return where
   feasible. For deeper attribution instrument UI-ref `LayerWidget::paintEvent`
   (`0x29af8`), note background paint (`0x419dc`), and/or iink drawModel
   (`0x49f91c`) with call counts and accumulated monotonic time. Parent phases
   pause under `PhaseScope`, so report inclusive wall time as well as the
   exclusive fields to avoid subtracting the child twice.
3. Record current part stable ID/index/count, editor and public renderer
   identities, count of setPart entries, `positionUpdated`/`contentChanged`
   emissions, model/background paint calls, and forced-refresh calls. Do not
   log unbounded per-stroke/per-item details on the UI path.
4. If painting is fast but repaint is slow, use existing user-space QPA logs
   (if already available) to identify update/flush timing; do not infer panel
   completion from API return. No new waveform, sysfs, configfs, or device-node
   experiment is necessary or proposed.

### Stage 1: reorder only, one change

Replace final same-part setPart + refresh with `positionUpdated`. Keep every
save/backup/validation step. Measure the replacement under existing `refresh`
phase (identify variant in a diagnostic), or add `ui-sync` and keep a clearly
zero `refresh` field. Do not relabel it as rendering time.

Use a disposable notebook with distinct page IDs/marks and at least 3 pages;
then the supplied 100-page fixture. Move the current page forward and backward,
including first/last and near the protected cover. Acceptance:

- same current stable ID, new index, expected count, correct label and both
  slider surfaces/arrows;
- zero genuine setPart/backend-reset calls caused by the final sync;
- canvas/template unchanged, no forced full-canvas paint needed;
- next/previous and scrubber first/last land on verified neighboring IDs;
- edit, undo, redo affect that page only; save/reopen preserves order and the
  intended selected page; serialized ZIP page order/count agrees;
- no repeat of stock blank-tail append just from this chrome sync.

If unexpected state occurs, stop/report, not automatic fallback-and-retry on
the reader. Only after a successful check repeat n=10 under identical fixture
conditions. Compare the existing phases; do not remove backup/save work while
benchmarking refresh.

### Stage 2: clone selection, separately

Try rank 2 (stock setter) or rank 3 (minimal direct-setPart change), **one variant
per controlled deployment/test**. Do not combine with the reorder/layer/cover
experiments before correctness is known. Keep page clone/import transaction
and archive verification. If persisting selected clone matters, relocate the
existing widget-save-post after selection, and measure it separately.

Acceptance fixture: blank, heavily inked, custom-background, image/cover-like,
and custom-layer pages. Test insertion into middle and selection at the tail;
include active search/highlight if `scrollTo` is chosen. Check actual stable ID,
index/count before and after the event loop/150 ms policy timer; distinguish
expected stock tail-page creation from duplicate count guarantees. Exercise
back/forward/scrubber, add ink, undo/redo, save, close/reopen. Confirm active
layer routing after the part switch, not just the selected row in the popup.

Time both:

- synchronous operation completion in the existing trace; and
- mutation/selection -> first correct model/background/chrome paint and
  subsequent event-loop settling, plus visible panel completion if observable.

For the second metric, a temporary GUI-thread diagnostic timer can sample
paint counters/ID without forcing `repaint()`; end-to-end visual observation
must remain separate from queued-call duration. Screenshot before and after
each permitted UI action in the future session. Do not tap delete/reset/storage
or other protected dialogs without the human authorization required by
`AGENTS.md`.

A fast setter with stale/late canvas is not a pass. A 600 ms reduction in edit
return time with 600 ms of deferred paint is a responsiveness change, not a
600 ms rendering saving. Report both outcomes honestly.

### Stage 3: only if needed

If label-only reorder succeeds but the undo toolbar is stale, test the widget
`contentChanged` **signal** in another controlled revision. If clone paint is
missing despite stock invalidation, test one scheduled widget update and trace
which region actually paints. Layer add/delete/activate and cover refresh
removal each require their own mutation/invalidation experiment; retain their
current refresh behavior until then.

## Remaining limits

- No on-device performance attribution, paint count, physical waveform, or
  screenshot evidence was collected in this read-only task.
- No exact fraction of the supplied 600 ms can be assigned to software raster
  work versus QPA flush versus physical panel completion from these four
  libraries alone.
- Native RenderPad cache hit behavior and document-structure undo semantics
  are not proven by the UI wrappers; the candidate sequences deliberately use
  stock page selection and avoid engine callback fabrication.
- The proposed clone replacement preserves normal callbacks and leaves
  already-pending asynchronous/idle policy work in place. Those callbacks and selected-page persistence
  must be included in correctness checks, not omitted from a faster trace.
