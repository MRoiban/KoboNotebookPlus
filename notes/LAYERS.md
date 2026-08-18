# Notebook layer support

This note describes a firmware-pinned design for adding real, named layers to
Kobo notebooks on firmware `4.38.23697`. The important result is that layers do
not need to be simulated with tags or separate documents: the bundled MyScript
engine already has document-layer, capture-routing, selection, and
layer-filtered rendering APIs. Kobo's notebook UI simply does not expose them.

The feature should be implemented as a small group of cooperating components:

```text
Layers toolbar button
        |
        v
LayerMenuController ----> LayerDocumentAdapter ----> MyScript Layout
        |                         |                      |
        |                         +--> active layer -----+--> cached writing tools
        |                         |                     |     restrictToLayer
        |                         |                     +--> eraser hit selection
        |                         |                          intersect selected layer
        +--> LayerPreviewCache --> isolated Renderer <---+
                                  restrictToLayers({id})
```

The popup is a Nickel/Qt concern, layer contents and stroke routing are
MyScript concerns, and thumbnail generation is a rendering concern. Keeping
those boundaries explicit avoids putting document logic into row widgets.

## Prototype status

`mods/custom-notebook-templates` now contains a buildable first implementation
of the create/select/delete path:

- a guarded **Layers** row in the stock notebook overflow menu;
- a firmware-native `NickelTouchMenu` whose selectable layers use stock
  `IInkToolMenuWidget` rows, newest-first order, 54×72 page-ratio preview
  pixmaps, and
  active-state labels; generic Add/Delete/Refresh commands remain native
  `IInkMenuItem` rows;
- native `Layout::appendLayerWithName` and `removeLayer` calls;
- active stroke routing through the verified Nebo backend chain;
- per-part sidecar state keyed by MyScript's stable part ID;
- active-layer reapplication after notebook `renderVolume`, popup open, and
  `setToolTheme`;
- complete notebook backups before add/delete operations;
- non-destructive sidecar/native existence validation on every popup or tool
  reload; and
- isolated per-layer PNG generation through `PageController::exportToPNG`
  with a plugin-owned stock UI-reference image writer.

The implementation compiles with NickelTC/Qt 5.2.1. Its pinned layer/UI
symbols, exact vtable sizes, and routing vtable slots are checked by
`scripts/verify-layer-abi.py`. Layer routing, erasing, persistence, and isolated
preview output have now all been verified on the Elipsa 2E. Opening **Layers**
constructs rows immediately from any structurally valid cached cards, including
stale ones; it never saves the notebook or performs an export before first
paint. At most one stale card is refreshed through a popup-owned GUI-thread
timer after the menu is visible, with the active row first. **Refresh layer
previews** remains the explicit full-cache command. Preview generation invokes
a separate 1024×1024 export renderer, applies
`Renderer::restrictToLayers({id})` through a temporary listener proxy, renders
through a plugin-owned instance of Kobo's stock `ImagePainter` and
`BackendImageDrawer`, and atomically updates bounded PNG entries under
`/mnt/onboard/.kobo/iink/custom-notebook-templates/layers/previews/`.
Placeholder cards are used only when a preview is missing or unreadable; a
stale valid card is kept on screen until its replacement succeeds. Successful
exports are validated at full resolution and then persisted as the
already-scaled inner card image; a 64-entry mtime/size-keyed in-memory `QImage`
cache avoids decoding those files again on repeated opens. Preview jobs use
UUID-qualified temporary names and a per-widget serialization guard.
Add/delete also probe the native layout after mutation before changing the
sidecar, and a per-widget guard rejects double taps during an in-flight layer
operation. `scripts/verify-layer-artifacts.py` provides a read-only post-test
audit of sidecar identity, active IDs, PNG chunk CRCs, dimensions, bounds, and
freshness; its synthetic regression suite is
`scripts/test-verify-layer-artifacts.py`.

The first device build used `QWidgetAction` plus a plain `QToolButton`. It
painted correctly but was not touch-interactable because Kobo's menu
controller routes taps through `GestureReceiver`, not ordinary Qt button
events inside the popup. The first working native build followed the recovered
dropdown path: construct `NickelTouchMenu`, create each row with
`IInkDropdownMenuController::createIInkMenuItem()` (which calls
`grabTapGesture()`), and attach the receiver slot with
`AbstractMenuController::addWidgetAction()`.

The pen-style layer-row revision keeps that proven action path but changes only
the selectable layer widgets. Stock `IInkToolMenuController::loadView()`
allocates `0x58` bytes and constructs `IInkToolMenuWidget(QWidget*)` at ELF
`0x761ac` / BN `0x861ac`, calls `setSelected(bool)` at ELF `0x76580` / BN
`0x86580`, and registers its `GestureReceiver` secondary base at row `+0x44`
with `AbstractMenuController::grabTapGesture()` (libnickel ELF `0xb3cfac`).
The plugin repeats that exact construction, writes the named `toolButton` and
`toolText` QLabel children, and leaves `IInkToolMenuWidget::setTool()` unused:
its switch accepts only Kobo's real pen/eraser enums. Layer thumbnails retain
their dark active border, while the native row supplies Kobo typography and
reversible press/release feedback. All new symbols, allocation size, adjusted
base, and call sites are fail-closed and pinned in `verify-layer-abi.py`.

The first native-menu device test then showed that row creation alone is not
enough for a second popup: `AbstractMenuController::tapGesture()` uses the
controller's stored view and `QMenu::actionAt()` to choose the action. With the
original view still installed, **Add layer** resolved to the underlying
**Change notebook template** row. The popup now guards and temporarily swaps
the pinned Qt `QPointer` view at controller offset `0x10`, then restores it when
the modal menu returns. The base dispatcher is
`AbstractMenuController::tapGesture()` at libnickel ELF `0xb3d00c`; its
`+0x10/+0x14` QPointer reads, `QMenu::actionAt()` path, and QAction activation
are included in the ABI gate alongside the symbol.

The first pen-style popup was still visually unanchored because it used the
notebook widget as its parent and guessed a screen point. The stock controller
instead retains the overflow toolbar button at `+0x18/+0x1c`, retrieves it
through `AbstractNickelMenuController::popupFromWidget()` (libnickel ELF
`0xeb4f94`, BN `0xec4f94`), and passes it to
`NickelTouchMenu::popupPosition(QWidget*)` (ELF `0xed7248`, BN `0xee7248`).
The layer menu now uses that button as both its QObject parent and native
position anchor, producing the stock under-toolbar placement and triangular
pointer. It intentionally keeps `QMenu::exec(position)` rather than the stock
asynchronous `popup(position)`: the nested event loop preserves the overflow
controller needed by the proven gesture/action bridge until the layer popup
closes.

The next device test confirmed layer creation and selection metadata but not
stroke isolation. Binary Ninja showed why: `PageController::toolDispatcher()`
actually forwards the toolbar's transient `currentTool()` as a
`shared_ptr<atk::core::Tool>`. On this page the PID contains a concrete cached
tool, not a `ToolDispatcher`; casting it always fails. The working route is
`PageController::inputDispatcher()` followed by the PID's
`CompositeBoxFactory::mainBackend()`. A vtable-verified `DrawingBackend`
caches Pen at `+0x44`, Eraser at `+0x4c`, Selector at `+0x54`, and Brush at
`+0x5c`. The plugin calls virtual `Tool::restrictToLayer()` on Pen, Selector,
and Brush. It stores the same selected ID on cached concrete Erasers and uses
verified `selectionFromPoints` adapters for the exact firmware eraser
implementations to intersect the stock polygon hit selection with that named
layer.

Opening the layer picker now reapplies the saved active ID before it marks any
row active. An earlier build only loaded and reconciled metadata there. After
notebook reopen, that could display Layer B as active while the fresh concrete
pen/eraser still carried Kobo's default or previous restriction. Selecting a
row or changing tools repaired the mismatch, which made stable old strokes
appear to have swapped layers. Popup synchronization fails closed rather than
showing a dishonest active marker.

An intermediate build also called `Renderer::restrictToLayers()` on the live
renderer from the tool-theme/menu path and crashed the reader. Reverse
engineering shows firmware uses this unsynchronized `RenderPad` vector setter
for isolated thumbnail/export renderers, not the live canvas. The live call is
therefore removed: selection routes new strokes, the canvas stays composite,
and only isolated preview renderers are filtered.

The device crash reports in `.kobo/stack_01.log` and `stack_02.log` identify
the immediate fault more precisely: both terminate in
`ToolDispatcher::restrictToLayer() -> CompositeBoxFactory::backends()` with a
`SIGSEGV` at address `0x20`. The receiver was a concrete `Tool`, not a
`ToolDispatcher`; invoking the derived method on that wrong object interpreted
unrelated memory as dispatcher members. The current route uses the concrete
tool's virtual slot and does not perform that cast. The live-renderer call
remains removed independently because firmware provides no synchronized live
use to copy.

## What Binary Ninja confirmed

### Kobo's UI has no existing layer feature

The analyzed `libiinknote.so` has no layer-named functions or strings. It does,
however, provide the reusable pieces described in `notes/PEN_MENU.md`:

- `IInkToolBar::onPenButtonTapped()` at Binary Ninja `0x81028`;
- `IInkToolBar` construction at `0x811bc`;
- `IInkToolMenuController::loadView()` at `0x853c8`;
- generic `NickelTouchMenu` popup framing; and
- `AbstractNickelMenuController`, `IInkDropdownMenuController`, and stock menu
  row patterns.

A layer menu should therefore reuse `NickelTouchMenu` and the stock visual
language. This prototype reuses the live tool controller only for native tap
routing and temporarily installs the popup as its active view; the layer
state and operations remain owned by the plugin.

### MyScript has native named layers

The following functions are present in the analyzed ARMv7 `libiink.so`. The
addresses are Binary Ninja virtual addresses for the view loaded at
`0x10000`.

| Capability | Function | Binary Ninja |
| --- | --- | --- |
| Create by name | `atk::core::Layout::appendLayerWithName(string)` | `0x9ea780` |
| Get or create | `atk::core::Layout::getOrCreateLayer(string)` | `0x9eec90` |
| Add a layer | `atk::core::Layout::addLayer()` | `0x9ef364` |
| Remove by name | `atk::core::Layout::removeLayer(string)` | `0x9e9be8` |
| Set layer color | `atk::core::Layout::setLayerColor(...)` | `0x9e9cf0` |
| Move a selection | `atk::core::Layout::moveSelectionToLayer(...)` | `0x9ef3ac` |
| Select a layer's items | `atk::core::Selection::selectLayer(...)` | `0x9f1ec4` |
| Filter a renderer | `atk::core::Renderer::restrictToLayers(...)` | `0xa67714` |
| Store only the base field | `atk::core::Tool::restrictToLayer(string)` | `0xa94358` |
| Route captured stroke | `atk::core::InkSampler::setStrokeLayer(...)` | `0xaab710` |
| Route active tools | `snt::ToolDispatcher::restrictToLayer(string)` | `0x5d4c90` |

The document side also exposes `Layout::getLayerCount_`, `getLayers_`,
`getLayer_`, `addLayer_`, and a `LayerIterator` with ID, item count, extent,
items, next, and remove operations. Static names include
`DOCUMENT_LAYER_NAME`, `BACKGROUND_LAYER_NAME`, and
`BACKGROUND_OBJECT_LAYER_NAME`.

The prototype uses the public wrapper chain below as a read-only existence
probe before trusting sidecar metadata:

```text
atk::core::Layout::_layout()                 ELF 0x9da8c8
  -> myscript::document::Layout::getLayer()  ELF 0x4b3d00
  -> LayerIterator::isAtEnd()                ELF 0x4b3e88
```

All returned wrappers are one-pointer `ManagedObject`s and are released with
the pinned SDK destructor. A negative native lookup during passive popup or
tool-theme lifecycle validation is treated as transient: the picker/reapply
fails closed, but every sidecar row and active ID is preserved. Only explicit
add/delete operations rewrite layer membership after probing their mutation.
The prototype does
not yet adopt arbitrary native custom layers that were created by another
application without matching sidecar metadata; that requires full iterator
enumeration and a naming policy.

The layer operations open a MyScript transaction and commit it with
`commitAsGhost`. This is useful evidence that they are intended to mutate a
document coherently, but it also means they must not be assumed to participate
in Kobo's visible undo stack. Delete and move operations need device tests and
a backup/undo policy before release.

### Page identity and serialization are persistent

`ContentPartImpl::getId` (BN `0x405bfc`) reaches the document `Page` stored in
the part and calls `myscript::document::Page::getId_` (BN `0xb198a4`). The
returned value is the persistent page ID also used by the archive paths
`pages/<id>/`; it is not a per-session object handle. Sidecar keys therefore
remain `(canonical notebook path, persistent page ID)` across reopen.

The July 15 reopen trace and preserved archives rule out a positional swap.
For page `jlkzyrsp`, Layer 2 remains
`cnt.layer.bf8ada4e-9712-4d3f-97ad-fd387db218a5` and Layer 3 remains
`cnt.layer.3da82475-016b-44ec-a100-b4baaad619eb` in every captured sidecar and
`page.bdom`. Binary Ninja also confirms that add, get, select, pen routing, and
erase selection all consume the full layer string, never a layer ordinal.
The same trace records the notebook later reopening on persistent page
`ugtjlxed`; that page correctly has its own sidecar and native layer table.
Layer stacks are page-local because MyScript's native `Layout` is page-local.

### Reopen-time active routing is restored after the final page is installed

The actual reopen regression was a split between persistent state and live
tool state. The sidecar restored the correct active string, but the only
automatic routing callback was `setToolTheme`, which need not fire while a
notebook is reopened. The popup could therefore show the saved active row
before the new editor's concrete pen and eraser had received that restriction.

The crash-safe lifecycle point is
`IInkNotePadWidget::renderVolume(Volume const&)` at ELF `0x66720`, BN
`0x76720`. `IInkNotePad::renderVolume` calls it through the PLT at ELF
`0x4da82`, BN `0x5da82`, so the existing `nh_dlhook` mechanism can intercept
it. Inside the widget function, both paths call `setupVolume` (BN `0x76a92`
and `0x76ed8`); `setupVolume` installs the active tool at BN `0x7289c`. The
saved-page path then installs its final part through the virtual editor call at
BN `0x76f18`. The plugin consequently calls the stock function first and
reapplies the exact saved ID only after it returns. This passive restore never
saves or reconciles away sidecar metadata. Popup open repeats the same
idempotent synchronization as a fallback for a widget which predated hook
installation.

Two tempting callbacks are explicitly unsuitable. Widget `partChanged` at BN
`0x67fa0` only emits a Qt signal and precedes another internal part-change
handler, while `showEvent` at BN `0x67d84` only tail-calls
`QWidget::grabKeyboard`. Neither proves editor/part/tool coherence.

**Hardware verification passed.** The post-test snapshot is
`backups/device-reopen-layer-success-post-20260715-190125/`. Its trace records
the new hook installation at line 28, stock notebook rendering completing at
line 33, sidecar/native reconciliation for page `ugtjlxed` at lines 34--37,
and successful `reason=notebook-open` synchronization of custom ID
`cnt.layer.8448dac8-826f-4f82-bef4-99b5cce275c2` at lines 38--51. Eraser input
then uses that same ID starting at line 52, without an intervening
`setToolTheme` or explicit layer activation. The popup fallback independently
completes at lines 115--125. The user confirmed correct behavior; the artifact
audit passes with 12 sidecars/14 custom layers, the `.nebo` archive has no ZIP
errors, both page-layer markers occur exactly once, and the trace has no
lifecycle, routing, reconciliation, or fail-closed error.

Layer mutation is also wired to real package serialization. The public
`Layout::appendLayerWithName` transaction reaches document `addLayer_` and
`commitAsGhost`; the plugin then calls `ContentPackageImpl::save` (BN
`0x401960`), which reaches `atk::core::Document::save` (BN `0xa01584`) and
`IDocumentPriv::save2_` (BN `0xb69e84`). The instrumented build additionally
opens the saved `.nebo` read-only and reports whether
`pages/<part>/page.bdom` contains the exact new layer ID.

The data-loss regression was therefore in reconciliation, not a volatile part
identifier or a missing save entry point: a passive lifecycle callback could
observe a transiently incomplete layout, treat one negative `getLayer` probe
as authoritative, and rewrite the sidecar without that row. Reconciliation is
now non-destructive. The device archive probe and sidecar/context traces must
still confirm the observed reopen sequence before the fix is declared proven
on hardware.

### The active layer can be applied while ink is captured

`atk::core::Pen::penDown` at `0xa893ec` obtains the selected tool's restricted
layer name and passes it to `InkSampler::setStrokeLayer`. New ink can therefore
be assigned to the active layer at pen-down time; it does not need to be moved
after every stroke.

For the Nebo backend, the route recovered from the binary is:

```text
EditorImpl::getRenderer()                 BN 0x435c00
  -> RendererImpl::getBackend()           BN 0x4ae318
  -> NeboBackend / PageController
  -> PageController::inputDispatcher()    BN 0x511960
  -> PlatformInputDispatcher::getCurrentTool()
                                            BN 0x5cc80c
  -> PID+0x24 CompositeBoxFactory::mainBackend()
                                            BN 0x5c81b8
  -> DrawingBackend cached Tool shared_ptrs
  -> virtual Tool::restrictToLayer()       BN 0xa94358
```

`DrawingBackend::getTool` returns cached objects and rewrites only their box
ID; it never resets `Tool+0xbc`. Restricting all writing-capable cached tools
therefore survives PID category changes. `DrawingBackend::init` proves the
stock writing-tool policy: restrict Pen, Selector, and Brush. `DrawingBrush`
shares `Pen::penDown` and consumes `restrictedLayer()`. The stock core Eraser
path never reads that field directly. For `snt::DrawingEraser` and global
`DrawingEraser`, the plugin keeps the stock hit test and intersects its
`selectionFromPoints` result with the selected layer, using the same
`selectLayer`/Intersection operation recovered from firmware Selector code.
Raw-content `DiagramEraser` is different: both a layer intersection after its
stock hit test and an intersection of final `Eraser+0xd0` were proven
on-device to preserve the base-only behavior. Core Eraser builds a fresh
Selection and applies its polygon with Replace (BN `0xa8764c`--`0xa87658`).
That is already too late to recover a custom Diagram layer. The exact Diagram
adapter instead copies firmware Selector's order: selected layer/Replace (BN
`0xa8ced0`--`0xa8ced8`), then Kobo's own eraser polygon/Intersection (BN
`0xa8d07e`--`0xa8d082`). It reuses stock `Eraser::strokerPolygon`, including
the exact width and view-scale fields. For plugin-created `cnt.layer.*` raw
ink, the exact Diagram adapter also replaces `updateSelection` at `+0x7c` with
a direct call to core `Eraser::updateSelection`, because the stock Diagram
override narrows through `layoutGroup`/semantic items and was observed not to
return for one valid custom-layer selection. The base/document layer still
uses stock update behavior. Slot `+0x80` is wrapped only to observe the final
`Eraser+0xd0` Selection and delegates once to stock `eraseSelection`.
Dispatcher-backed Nebo pages may return global `DrawingEraser` (vtable
BN `0xd956e0`) or, as the hardware trace proved for
`com.myscript.raw-content`, `atk::diagram::DiagramEraser` (BN `0xd9eb30`)
rather than `snt::DrawingEraser` (BN `0xd9a478`). All three 148-byte vtables
are cloned independently at exact slot `+0x78`; only Diagram uses the
layer-first replacement. The exact DiagramPen (BN vtable `0xd9ffb0`) is still
updated so switching back to pen writes on the selected layer. The guarded
build additionally clones only DiagramPen vslot `+0x3c`: immediately before
stock DiagramPen pen-down it compares the popup's per-tool desired ID with
virtual `restrictedLayer()`, reasserts it on mismatch, and traces the exact
value core Pen will consume. Binary Ninja pins DiagramPen -> SmartPen -> core
Pen at `0x7bf41e` and `0xa93cf8`, then the virtual layer read and
`InkSampler::setStrokeLayer` at `0xa894b0` and `0xa894be`. No generic, text,
or math tool class is modified.

#### Pending policy-aware Diagram eraser correction

The custom-only core `+0x7c` route currently preserves layer scope but drops
one stock eraser-mode transform. Binary Ninja proves that the concrete
Eraser's live policy is stored at `Eraser+0xc4`: public
`Eraser::eraserPolicy() const` is ELF `0xa75aa4` / BN `0xa85aa4`, and its
load is at BN `0xa85aa8`. `ToolDispatcher::setPropertiesForTool` maps the
`eraser.policy` string `stroke` to enum `0` at BN
`0x5d3998`--`0x5d399c` (ELF `0x5c3998`--`0x5c399c`) and `precise` to enum
`1` at BN `0x5d3b1e`--`0x5d3b22` (ELF
`0x5c3b1e`--`0x5c3b22`).

The corresponding public setter is `Eraser::setEraserPolicy` at ELF
`0xa75a94` / BN `0xa85a94`; its only engine mutation is the exact
`Eraser+0xc4` store at BN `0xa85a98`. The activation-state repair uses that
setter only after exact-vtable classification and fans it out with the saved
eraser radius. It verifies the tool's restricted layer before and after, so
mode synchronization remains orthogonal to layer routing.

Stock `snt::DrawingEraser::selectionFromPoints` reads that field at BN
`0x572848`; policy `0` expands its polygon hit to complete stroke boundaries
with a `0.0f` threshold at BN `0x5728e2`--`0x5728e6`, while nonzero policy
returns the precise polygon selection. Global `DrawingEraser` has the same
behavior at BN `0x431324` and `0x431378`--`0x43137e`. The public operation the
plugin can reuse is `Selection::adjustToStrokeBoundaries(float)` at ELF
`0x9e2d70` / BN `0x9f2d70`. Diagram's stock update also branches on
`Eraser+0xc4` at BN `0x76b7b4`--`0x76b7ba`, confirming that the current
custom-layer direct-core path is what turns whole-stroke mode into a
partial/brush erase.

The implemented, build-verified narrow fix keeps the live renderer, layer-first hit test,
custom-only core update, stock base behavior, and stock removal sink intact.
For a `cnt.layer.*` Selection only, the `+0x78` adapter will read the live
policy and, when it is `0`, call
`Selection::adjustToStrokeBoundaries(0.0f)` after polygon Intersection. It
will return that adjusted Selection directly to the custom-only core `+0x7c`.
The public wrapper at BN `0x9f2d70` mutates only the existing Selection through
`PageSelection::adjustToStrokeBoundaries_` at BN `0xb06e54`; it does not run a
new Page or layer hit test. A stroke has one layer ID, so the original
layer/Replace already bounds the expansion. The fix intentionally performs no
second `selectLayer` lookup: that redundant lookup can clear a valid Selection
if layer resolution transiently fails. Policy `1` remains precise, and the
document/base layer remains on exact stock Diagram `+0x7c`. Required symbols
and field/code identities must be ABI-pinned; missing APIs or exceptions fail
closed.

**Hardware verification passed.** The preserved snapshot at
`backups/device-object-eraser-success-post-20260715-175128/` confirms live
policy `0` selections are boundary-adjusted on both custom layers, policy `1`
selections remain precise, the adjusted Selection commits to the removal sink,
and stock removal succeeds. The user confirmed complete-stroke Object Eraser,
segment-level Brush Eraser, and independent erasing after layer switches. The
saved sidecar retains both layer IDs and the trace contains no fail-closed,
hook, crash, or routing errors. This result is limited to raw strokes; it does
not claim full stock whole-object semantics for arbitrary recognized Diagram
shapes, text, borders, or images.

## Popup design

The sketched popup maps cleanly to a custom `NickelTouchMenu`:

```text
+----------------------------------+
| [preview]  Layer 3           [x] |
| [preview]  Layer 2  <active> [x] |
| [preview]  Layer 1           [ ] |
+----------------------------------+
|                       [ + layer] |
+----------------------------------+
```

Recommended responsibilities:

- `LayerMenuController` creates the popup, binds rows, and emits user actions.
- `LayerRowWidget` owns only the preview, display name, active marker, and
  delete affordance.
- `LayerDocumentAdapter` enumerates and mutates MyScript layers, tracks the
  active ID, and exposes a UI-safe model.
- `LayerPreviewCache` renders and caches previews without changing the live
  editor renderer.

Rows should be shown newest/topmost first if device testing confirms that
MyScript's iterator order is bottom-to-top. Do not bake that assumption into
the widget. The active row can use the same dark selection treatment as stock
menus. Selecting a row must update stroke routing before the popup closes.

For an MVP:

- treat `DOCUMENT_LAYER_NAME` as the existing base, displayed as `Layer 1`;
- never delete the base or the last remaining drawable layer;
- add a generated, stable layer ID and a derived `Layer N` display name;
- persist the active ID per notebook/part, but fall back to the base if it no
  longer exists;
- cap the number of layers initially, for example at 8 or 16; and
- defer rename, visibility, locking, and reordering.

There is no clear public reorder API. A private `ILayoutPriv::insertLayer`
exists, but relying on it in the first version would add unnecessary ABI risk.

## Creating per-layer preview thumbnails

The engine already has the necessary primitive:

```cpp
renderer.restrictToLayers({layerId});
```

Use a separate renderer for previews. Applying the restriction to the live
renderer would hide content in the editor and could also create unexpected
hit-testing or eraser behavior.

The prototype now implements this pipeline:

1. Open from the last valid cache without saving or exporting on the popup's
   first-paint path. A popup-owned timer may refresh the active stale row after
   the menu is visible; the explicit refresh action saves first and rebuilds
   every row on demand.
2. Ask the current Nebo `PageController` to export the complete root box.
3. Let `PageController::exportToPNG` create its own 1024×1024 `Renderer`.
4. Interpose a temporary `RendererListener` proxy which restricts that new
   renderer to `{layerId}` and invokes a plugin-owned stock image writer.
5. Build that writer from the editor's stock Engine and Configuration, the
   public no-argument UI-reference `ImagePainter`, and libiink's own
   `BackendImageDrawer` `make_shared` helper. Before constructing the drawer,
   copy the live notebook `EditorWidget`'s configured `ImageLoader` owner into
   the new painter with stock `ImagePainter::setImageLoader`. Its callback then
   derives the Page from the isolated renderer and calls the stock
   `BackendImageDrawer::drawImage`.
6. Validate the resulting PNG dimensions and byte size before an atomic cache
   replacement.
7. Scale the cached image into the row's 54×72 page-shaped card on the Qt GUI
   thread. This exact 3:4 frame matches the notebook's 1404×1872 page while
   retaining the previously proven 72-pixel native row height.

The first hardware run of the listener-delegation design did not reach step 4.
The preserved trace
`backups/device-layer-preview-failure-20260715-230215/plugin-status.txt`
records `Layer preview unavailable: image writer is missing.` for both rows at
lines 80--82, followed by the same failure on later refreshes. The lookup had
treated the `shared_ptr<RendererListener>` at `PageController::Data+0x34` as a
persistent PNG writer. It is optional and is null for the live notebook path on
this firmware. `setRendererListener` proves the field exists, but not that every
controller installs a listener there. Stock `NeboBackend::export_` instead
demonstrates call-owned lifetime: at BN `0x49946c`--`0x49949e` it allocates and
initializes an anonymous listener, passes it to one export, and releases it
after the call. A null stored listener is therefore valid firmware state, not
an asynchronous readiness condition.

The replacement never reads or populates that optional controller field.
`ImagePainter` is constructed from `libiinkuiref.so.1.0.0` (SHA-256
`693aa804c31e3d783314ea2a61282730b0ada6f0b48029a3bc955ea78acd409a`):
constructor ELF `0x28b48`, deleting destructor ELF `0x29948`, with its primary
`IImagePainter` base at object offset zero. `EditorImpl::getEngine` (ELF
`0x425bb0`) and `getConfiguration` (ELF `0x425cf0`) supply the exact shared
owners consumed by the stock `BackendImageDrawer` helper at ELF `0x443eac`.
The isolated callback then invokes `BackendImageDrawer::drawImage` at ELF
`0x3e7b8c` using the firmware's indirect Page/Renderer/Selection/path ABI and
hard-float `Extent` in `s0`--`s3`.

A second pre-deployment audit caught an independent crash path in that
replacement before it was copied to the Kobo. The no-argument painter
constructor deliberately zeros its `shared_ptr<ImageLoader>` at painter
`+0x1c/+0x20`. `ImagePainter::createCanvas` loads `+0x1c` at BN `0x38bc0`
and immediately reaches `ImageLoader::getEditor`; a loader-less painter is not
a valid export writer.

The fix borrows a real configured loader instead of reconstructing the private
`ImageLoader` object. `IInkNotePadWidget`'s constructor (ELF `0x609d4`, BN
`0x709d4`) creates the live UI-reference `EditorWidget` at BN
`0x70f84`--`0x70f94`, stores its Qt guard at notebook widget `+0x44` and raw
object at `+0x48` at BN `0x70fd6/0x70fda`, and itself checks guard `+4` before
using the raw object at BN `0x71004`--`0x71014`. That `EditorWidget` owns a
strong `shared_ptr<ImageLoader>` at `+0x20/+0x24`, stored by
`EditorWidget::setEngine` at BN `0x2e86c`--`0x2e870`. Preview generation
repeats the stock Qt guard check, verifies the exact `EditorWidget` vptr and
that its editor at `+0x18/+0x1c` matches the live notebook editor, copies the
loader owner, and calls `ImagePainter::setImageLoader` (ELF `0x28a60`, BN
`0x38a60`) before the drawer factory. Failure of any validation keeps the
placeholder and never enters export.

Only the renderer created inside `PageController::exportToPNG` receives
`restrictToLayers`. The live editor renderer is never restricted, replaced, or
passed to the image drawer, so opening or refreshing the popup cannot hide live
ink or change eraser hit-testing.

`exportToPNG` hard-codes a 1024×1024 renderer, so the first hardware build
centers the complete square export with `Qt::KeepAspectRatio`. It is lossless
and undistorted, but its ink occupies about 48×48 pixels inside the 54×72 page
frame. This matches stock `PixmapView` semantics (centered, scaling enabled,
cropping disabled). Do not crop to 768×1024 until a real device PNG proves
that the discarded 128-pixel side regions are only renderer padding; otherwise
a crop could silently remove strokes near the page edges.

The first preview implementation never produced a cache file because its
atomic temporary path ended in `.png.tmp-<pid>`. Kobo's bundled
`ImagePainter::saveImage` (ELF `0x28354`, Binary Ninja `0x38354` in
`libiinkuiref.so.1.0.0`) calls `QFileInfo::suffix()` at ELF `0x2844c` and passes
that suffix to `QImage::save()` at ELF `0x2846a`. It therefore requested an
encoder named `tmp-<pid>` instead of `png`. Temporary preview names now end in
`.tmp-<pid>.png` and are still atomically renamed only after decoding and size
validation. The exact suffix-to-encoder sequence is pinned by
`scripts/verify-layer-abi.py`.

The proxy ABI is pinned by the call at `0x513392..0x5133d0`: renderer and
selection references occupy `r1/r2`, export flags use `r3`, the four-float
extent uses `s0-s3`, and the output path is the first stack argument. The
cross-compiled callback must preserve that shape before calling the stock image
drawer. Its direct draw call must place drawer/Page/Renderer/backend in
`r0`--`r3`, retain `Extent` in `s0`--`s3`, and place Selection/path pointers in
the first two stack words. No live renderer restriction or code patch is
involved.

Rendering remains serialized on the UI thread because the live MyScript and Qt
objects are thread-affine. Hardware timing showed that the earlier synchronous
path made the popup take several seconds to appear: it called `widgetSave()`
first, the notebook-wide mtime then made every layer cache stale, and all stale
1024×1024 exports ran sequentially before `QMenu::exec()`. The optimized path
shows stale-but-valid caches immediately and limits deferred automatic work to
one export per open. The timer reloads the live context and verifies the
canonical notebook path and stable part ID before export; closing the popup
destroys the timer and cancels queued work.

Without the old forced save, freshness is relative to the last persisted
`.nebo` archive rather than unsaved live editor state. Nickel autosave or the
explicit refresh command advances that state. This deliberate tradeoff avoids
turning every popup open into a blocking save and invalidating all rows through
one notebook-wide timestamp.

The replacement writer is hardware-verified: real ink appeared independently
filtered in each layer row, and the device trace recorded `exported=yes`,
`restricted=yes`, `writer=yes`, `file=yes`, and positive byte counts. Popup
latency still needs a post-optimization timing trace; compilation or a fast
placeholder alone is not proof of the performance fix.

Invalidate the active layer's preview after ordinary ink changes. Moving a
selection invalidates both source and destination layers; deleting or adding a
layer invalidates the row model and the composite thumbnail. Empty layers can
use a blank placeholder immediately.

### Why the existing D-Bus thumbnail method is not enough

`MyScriptThumbnailerProxyFacade` in `libiinknote.so` calls the separate
`com.kobo.myscript.Thumbnailer` service. Its method accepts the document/part
and geometry parameters, but no layer filter. It can produce a whole-page
thumbnail, not one isolated layer preview.

The service executable's
`MyScriptThumbnailerService::thumbnail(QString const&, QString const&, int,
int, int, int, bool)` is at Binary Ninja `0x234dc`. Its flow is:

```text
create engine/editor and ImagePainter
  -> open the requested package and part
  -> apply IInkEditorCss
  -> choose Raw Content or Drawing handling
  -> ImagePainter::invalidate(renderer)
  -> ImagePainter::getImage()
  -> QImage::save(...)
  -> emit ready(...)
```

The service imports no layer or `restrictToLayers` function and applies no
layer restriction. The best current inference is that Kobo's normal D-Bus page
thumbnail will render all custom layers by default. That must be verified with
a fixture containing visible ink only on a custom layer.

There is a second internal path in `libiink.so`:
`snt::Thumbnailer::drawThumbnail` at `0x6f9f38` and
`addThumbnailToQueue` at `0x6fa9e8`. It explicitly restricts rendering to
`DOCUMENT_LAYER_NAME` and `BACKGROUND_OBJECT_LAYER_NAME`. Any surface that
uses that internal path will omit custom layers. The device test matrix must
therefore cover the notebook list thumbnail, cover/overview image, export, and
reopen, not just the live page.

## Interaction and lifecycle rules

Selecting a row should execute this sequence:

1. Validate that the selected layer still exists.
2. Store the active layer ID for the current notebook part/page.
3. Call the backend adapter's `restrictToLayer(activeId)`.
4. Refresh the toolbar indicator and active row.
5. Close the popup.

Reapply the restriction after editor creation, part/page changes, tool
selection changes, undo/redo that replaces the document model, and notebook
reopen. A transient missing native layer must fail closed without changing the
sidecar; only an explicit user deletion may remove its metadata.

Adding a layer should create it transactionally, make it active, apply stroke
routing, and enqueue its blank preview. Deleting should require confirmation,
switch away from the target first, then remove it and invalidate previews.
Because `removeLayer` and ghost-transaction undo behavior have not been tested
on device, a release build should keep a recoverable notebook backup before
the first destructive layer operation.

The live canvas intentionally remains an unrestricted composite. Erasing,
selection, recognition, hit testing, and renderer synchronization all need
separate investigation before independent visibility eye icons are honest.

## Implementation plan

### Phase 1: read-only layer probe

1. Hook a known notebook editor after its part is loaded.
2. Enumerate layer IDs, item counts, and extents; confirm stock documents have
   the expected base/background layers.
3. Render one base-layer preview with an isolated renderer.
4. Display a diagnostic `NickelTouchMenu` without mutations.

### Phase 2: create and select

1. Add one named custom layer.
2. Apply virtual `Tool::restrictToLayer` to the vtable-verified
   `DrawingBackend` Pen, Selector, and Brush caches; arm exact concrete Erasers
   with the active ID plus the layer-intersection adapter, and keep the live
   renderer composite.
3. Draw one stroke, reopen the notebook, and prove its layer ID/item count.
4. Switch to the base layer and prove subsequent strokes are separated.
5. Render both per-layer previews and the unrestricted composite.

### Phase 3: integrate the popup

1. Add a layers button to `IInkToolBar` or its overflow area.
2. Implement the custom controller, row widget, active selection, and add
   action.
3. Add preview caching and content-change invalidation.
4. Persist only the active layer ID and conservative display metadata.

### Phase 4: destructive operations and compatibility

1. Test selection moves between layers and their undo behavior.
2. Test delete with backups, empty/non-empty layers, and last-layer guards.
3. Exercise the D-Bus thumbnail, internal thumbnail surfaces, export,
   recognition, copy/paste, and mod-absent reopen.
4. Only then consider rename, hide/show, lock, reorder, or more notebook
   backends.

## Required device test fixture

Create one notebook page with visually distinct marks:

- base layer: a black square in the upper left;
- custom layer A: a gray circle in the center;
- custom layer B: a black triangle in the lower right.

The three isolated previews, unrestricted live page, reopened page, notebook
list thumbnail, and exported image/PDF should make omissions immediately
visible. Also test drawing immediately after every layer switch; that catches
late application of `restrictToLayer`.

## Provenance and limits

The UI findings came from `libiinknote-4.38.23697.bndb`; native layer and
renderer findings came from the fully analyzed ARMv7 `libiink.so`; thumbnail
service findings came from `iink-4.38.23697.bndb` (`linux-thumb2`, 516
functions, analysis complete). The service database fingerprint was
`b2704cd18af852c4a409b08568402847433bae201d2f62acdf613d47ba3d2ea7`.

The Binary Ninja bridge was used read-only. No capabilities were enabled, no
annotations or patches were made, and no analysis database was saved. The
bridge was stopped after inspection.

Open questions requiring device work are: backend differences between Basic
and Advanced notebooks, iterator/display order, transaction/undo behavior,
thread ownership for preview rendering, deletion semantics, and which
non-D-Bus surfaces use the internal restricted thumbnailer.
