# Notebook pen popup

This note documents the popup shown by the notebook pen button on Kobo
firmware `4.38.23697`: which library owns it, how its rows are assembled, what
is reusable, and where adding a visual tool stops being the same problem as
adding a new MyScript stroker.

## Ownership and construction

The popup is Kobo Qt UI in `libiinknote.so`, not a MyScript-provided widget.
The toolbar enters it through:

```text
IInkToolBar::onPenButtonTapped()
  ELF 0x71028 / Binary Ninja 0x81028
        |
        v
IInkToolMenuController(QWidget*,
                       QVector<IInkTool> const& tools,
                       QVector<IInkToolBrush> const& brushes,
                       IInkToolTheme currentTheme)
  ELF 0x74760 / Binary Ninja 0x84760
        |
        v
IInkToolMenuController::loadView()
  ELF 0x753c8 / Binary Ninja 0x853c8
        |
        +--> one IInkToolMenuWidget per tool
        +--> createBrushSizeRow(NickelTouchMenu*, "Pen Size")
        +--> createBrushColorRow(NickelTouchMenu*, "Pen Shade")
```

The frame is a `NickelTouchMenu`, Kobo's reusable popup/menu surface. The
notebook controller supplies its content and behaviour. The top pointer,
border, placement, and general menu presentation therefore come from shared
Kobo UI machinery; the five pen rows and the two option sections are specific
to `IInkToolMenuController`.

## Components corresponding to the screenshot

| Visible part | Owning class/function | Binary Ninja |
| --- | --- | --- |
| Popup frame and placement | `NickelTouchMenu` used by `IInkToolMenuController` | constructed from `loadView` at `0x853c8` |
| Ballpoint/Fountain/Calligraphy/Brush/Highlighter rows | `IInkToolMenuWidget` | constructor `0x861ac` |
| Row icon and label | `IInkToolMenuWidget::setTool(IInkTool)` | `0x86594` |
| Selected dark circle/state | `IInkToolMenuWidget::setSelected(bool)` | `0x86580` |
| Size swatches | `IInkToolMenuController::createBrushSizeRow` | `0x84fcc` |
| Size selection | `IInkToolMenuController::setBrushSizeIndex(int)` | `0x84ba4` |
| Shade swatches | `IInkToolMenuController::createBrushColorRow` | `0x8491c` |
| Shade selection | `IInkToolMenuController::setBrushColor(QColor const&)` | `0x84d8c` |
| Final tool switch | `IInkToolMenuController::setTool(IInkTool)` | `0x851bc` |
| Emitted selection | `IInkToolMenuController::toolSelected(IInkToolTheme&)` | `0x8457c` |

The controller contains the literal section labels `Pen Size` and `Pen
Shade`. `IInkToolMenuWidget` loads its appearance from
`:/qss/IInkToolMenuWidget.qss` and uses child object names including
`toolButton` and `toolText`.

The stock display-name and selector tables are hard-coded next to each other:

| Display name | Selector prefix | MyScript brush selected by CSS |
| --- | --- | --- |
| `Ballpoint Pen` | `ballpoint` | `FeltPen` |
| `Fountain Pen` | `fountain` | `FountainPen` |
| `Calligraphy Pen` | `calligraphic` | `CalligraphicQuill` |
| `Brush` | `brush` | `CalligraphicBrush` |
| `Highlighter` | `highlighter` | `FountainPen` with highlighter widths/alpha shades |

The theme formats a selector with the shape `%1-%2-%3`, producing values such
as `fountain-1-0`: tool prefix, size index, and shade index.
`IInkEditorCss::getToolStyles()` in `libiinkuiref.so.1.0.0` resolves that
selector to `-myscript-pen-brush`, width, and color CSS.

## State and persistence

`IInkToolTheme` is the popup's value object. Its exported surface includes:

```text
setTool(IInkTool)
setBrushSize(double)
setBrushSizeIndex(int)
setBrushColor(QColor)
setBrushColorIndex(int)
getBrushSizesForTool(IInkTool)
```

It also owns static selector mappings for tools, colors, default sizes,
ballpoint sizes, and highlighter sizes.

The controller emits the updated `IInkToolTheme`; `IInkNotePadWidget` then
applies it through `setToolTheme`, `setPenCss`, or `setHighlighterCss`.
`IInkSettings` stores pen type, pen size, pen color, highlighter size, and
highlighter color through separate getters/setters.

This is why adding a sixth visible row is more than adding a child widget. A
durable tool must have:

1. a row label and icon;
2. an `IInkTool` or equivalent UI identity;
3. size and shade rules in `IInkToolTheme`;
4. a selector string;
5. matching editor CSS;
6. a settings representation; and
7. an available MyScript stroker under the CSS brush name.

## How reusable is the exact widget/frame?

### Reuse the frame: reasonably feasible

The `NickelTouchMenu` frame is generic Kobo UI. A NickelHook plugin that can
construct Kobo Qt classes can create another menu with the same visual frame
and place custom rows inside it. This is the cleanest way to reproduce the
look while owning the model.

### Reuse the complete stock controller: possible but constrained

`IInkToolMenuController` has exported dynamic symbols and its constructor
accepts caller-provided tool/brush vectors, which makes direct construction
plausible. It is still firmware-private C++ ABI rather than a stable plugin
API. A plugin must match Qt, ARM C++ ABI, object ownership, signals, and Kobo
class layouts exactly.

More importantly, the controller and theme contain hard-coded switch/table
logic for the stock enum. Passing an invented `IInkTool` value is unlikely to
produce a complete sixth tool: label, icon, sizes, selector, settings, and CSS
fallback behaviour all need auditing.

### Extend the existing popup in place: highest UI fragility

Hooking `loadView()` and injecting another row can preserve the exact live
popup, but it couples the mod to internal layout order, widget ownership, tap
handling, and firmware-specific object fields. It is viable for a
firmware-pinned experiment, not a portable extension API.

The preferred UI design is therefore to reuse the `NickelTouchMenu` frame and
stock row styling while owning a small custom controller/model, then call the
same `IInkNotePadWidget`/editor style paths.

## Adding custom pens: four distinct levels

| Addition | UI work | MyScript work | Overall risk |
| --- | --- | --- | --- |
| Another width/shade preset for a stock tool | Add choice and CSS selector; extend settings if persistent | None; reuse built-in stroker | Medium |
| Another named stock MyScript brush | Add row/theme/settings/CSS | Verify the installed name is recognized | Medium-high |
| A fundamentally new vector pen | Add row/theme/settings/CSS | Register a custom `IStrokerFactory` in every renderer process | High |
| Textured/particle/wet-ink renderer | Custom UI and settings | Likely parallel renderer and persistence/export path | Very high |

`libiink.so` recognizes more built-ins than Kobo exposes in this popup,
including `Qalam`, `Highlighter`, `Square`, `DynamicEnvelope`, `QuadBezier`,
and `Polyline`. Exposing one of those is a better first experiment than
implementing a new stroker, but its visual behaviour and document support must
still be tested.

For a genuinely new vector pen, `RendererImpl::registerStroker` exists and is
checked before built-in-name dispatch. Inference from the static flow suggests
that CSS such as this can select the registered name:

```css
.custom-0-0 {
    -myscript-pen-brush: MyBrush;
    -myscript-pen-width: 1.0;
    color: #000000;
}
```

That does not make the row persistent by itself, and a custom stroker
registered only in the live notebook process will not automatically exist in
Kobo's separate thumbnail/export process.

## Recommended implementation order

1. Instantiate a separate `NickelTouchMenu` with one diagnostic row and reuse
   the stock QSS/icon dimensions.
2. Add a preset that selects an already supported built-in brush name.
3. Verify selection, close/reopen, notebook save/reopen, thumbnail, and export.
4. Decide on a settings representation that fails back to a stock tool when
   the mod is absent.
5. Only after that, recover and test the custom-stroker ABI described in
   `notes/MYSCRIPT.md`.
6. Add the sixth durable row after its renderer and persistence behaviour are
   known.

The layer prototype now validates this construction strategy on real firmware:
its first plain `QToolButton` popup rendered but did not receive Kobo tap
gestures. It now creates `NickelTouchMenu` with the same decoration/alignment
arguments as `IInkToolMenuController::loadView`, creates rows through
`createIInkMenuItem`, and attaches QObject slots through `addWidgetAction`.

The subsequent pen-style layer selector reuses `IInkToolMenuWidget` only as a
native visual/touch surface. It allocates the stock `0x58`-byte object, calls
the real constructor at ELF `0x761ac`, fills the existing `toolButton` and
`toolText` QLabel children with the layer preview/name, and registers the
stock `GestureReceiver` secondary base at `+0x44`. It deliberately does not
call `setTool(IInkTool)`: that switch is hard-coded to real Kobo tool enums.
Add/Delete/Refresh remain generic `IInkMenuItem` rows. This keeps layer state
out of `IInkToolTheme` while matching the pen/eraser row typography and press
feedback.
The second device test exposed one more controller invariant: native rows were
clickable, but `AbstractMenuController::tapGesture()` mapped their global tap
through the controller's stored original menu and `QMenu::actionAt()`. Thus an
**Add layer** tap selected the underlying **Change notebook template** row.
The layer popup now temporarily occupies the controller's pinned
`AbstractController` view `QPointer` for its modal lifetime and restores the
previous weak pointer afterward, matching the stock action lookup path.

The first tool-row build still did not reproduce the popup frame completely:
it parented the `NickelTouchMenu` to the notebook and called `QMenu::exec()` at
a hand-calculated screen point. That bypassed the stock toolbar anchor, so the
menu appeared as an unanchored rectangle without the triangular pointer.

The stock show path is now pinned end to end. After constructing the tool-menu
controller, `IInkToolBar::onToolButtonTapped(bool)` calls virtual slot `+0x34`
at BN `0x80ebc`-`0x80ec4`. This is
`AbstractNickelMenuController::show()` (ELF `0xeb58b8`, BN `0xec58b8`). The
show routine:

1. obtains the toolbar button with
   `AbstractNickelMenuController::popupFromWidget()` (ELF `0xeb4f94`, BN
   `0xec4f94`; controller `QPointer<QWidget>` at `+0x18/+0x1c`);
2. polishes the `TouchMenu`;
3. calls the menu's virtual `popupPosition(QWidget*)` slot `+0xd0`, resolving
   to `NickelTouchMenu::popupPosition` (ELF `0xed7248`, BN `0xee7248`); and
4. passes the returned `QPoint` to `QMenu::popup()` at BN
   `0xec5a40`-`0xec5a4c`.

The layer popup now uses that same overflow-toolbar button as both its QObject
parent and `popupPosition()` anchor. It retains modal `exec(position)` only to
keep the existing overflow controller alive while its gesture delegate is
servicing custom rows; the stock controller schedules itself for deletion when
the outer overflow menu closes. Placement, boundary adjustment, and the
triangular decoration therefore remain native even though the lifetime is
modal.

## Native eraser-size extension

The eraser button does not own a different popup. Both toolbar buttons enter
`IInkToolBar::onToolButtonTapped(bool)` (ELF `0x70d28`, BN `0x80d28`) and
construct the same `IInkToolMenuController`. The eraser branch supplies tools
`{1, 2}` (Object Eraser and Brush Eraser) but an empty
`QVector<IInkToolBrush>`; the pen branch supplies brush sections `{0, 1}`
(size and shade). Brush section `0` dispatches to the existing
`createBrushSizeRow` at ELF `0x74fcc` / BN `0x84fcc`, which creates exactly
five native `BrushButton` swatches.

The plugin therefore hooks four exported PLT/GOT seams, rather than building
or injecting a parallel Qt layout:

| Seam | ELF | Purpose |
| --- | ---: | --- |
| `IInkToolMenuController` C1 constructor | `0x74760` | Append brush section `0` only for exact eraser tools `{1,2}` |
| `createBrushSizeRow` | `0x74fcc` | Show `Eraser Size` while restoring the row widget's stock style identity |
| `setBrushSizeIndex` | `0x74ba4` | Preserve stock button state, then store/apply the eraser index |
| `IInkNotePadWidget::setActiveTool` | `0x62194` | Mirror a physical-stylus eraser reset into the runtime popup index |

The constructor receives its non-trivial by-value `IInkToolTheme` indirectly
as the fifth machine argument on ARM. The wrapper leaves that pointer intact,
calls the stock constructor first, then marks the fully constructed QObject
and seeds the saved index before `loadView` runs. Setter side effects are
strictly caller-gated: the stock load call returns to ELF `0x75702`; an actual
button click returns to ELF `0x74d7c`. Only the latter writes
`.kobo/custom/templates/eraser-size.json`. Hook installation is setter, row,
hardware-tool synchronization, then constructor-last; a final ready flag makes
every partial installation a stock pass-through.

Two follow-up UI details are firmware-private but important. First,
`createBrushSizeRow` uses its title both for visible/action text and for the
`GenericContainerWidget` object name (ELF `0x75028`). Replacing that argument
with `Eraser Size` changed the native style identity and made the header retain
an active-row highlight. The wrapper now keeps `Eraser Size` as the visible
text but restores only the newly created row widget's object name to the stock
translated `Pen Size`. No label, palette, action, or selected-button state is
modified.

Second, physical stylus inversion does not pass through the menu controller.
`IInkNotePadWidget::stylusTouchBegin` loads the saved eraser enum from
`IInkNotePadWidget+0xb8` and calls `setActiveTool` at ELF `0x6244a`, returning
at `0x6244e`. That path resets the live eraser to its native ratio-0 size but
does not update the controller theme at `controller+0x28`, its selected index
at `+0x48`, or the five `BrushButton*` values at `+0x58`. A caller-gated
`setActiveTool` wrapper therefore replays `configuredEraserSizeIndex()` through
the existing exact-erasure radius fan-out after stock activation, then calls
the stock `setBrushSizeIndex(controller, configuredIndex)` on a live marked
controller. The radius setter verifies that eraser policy and restricted layer
are unchanged; the stock UI setter emits no `toolSelected` signal, so neither
operation can recurse into tool selection. Hardware activation never changes
the in-memory preference or writes JSON; explicit size-button taps remain the
only persistence event.

### Why the popup theme cannot size an eraser

`IInkToolTheme::getBrushSizesForTool` (ELF `0x76954`) returns the default five
values for eraser enums, so it can draw the row. However,
`IInkNotePadWidget::setToolTheme` (ELF `0x62200`) sends tools 1 and 2 directly
to `setActiveTool`; it skips the pen/highlighter brush-size path. Kobo exposes
no eraser-size `IInkSettings` entry. Reusing the visuals is therefore safe,
but eraser geometry and persistence must remain plugin-owned so the pen size
is never overwritten.

Firmware's real geometry path is:

```text
PageController+0xe4 -> LayoutGrid::lineGap()          ELF 0x6051fc
PenStyleUtils::eraserWidthFromThicknessRatio(ratio, lineGap)
                                                     ELF 0x69c278
radius = width * 0.5
atk::core::Eraser::setRadius(radius)                  ELF 0xa75b2c
```

`setRadius`, not a raw write to `Eraser+0xcc`, is required because it also
updates the eraser's MyScript style through its virtual property interface.
`Eraser::selectionFromPoints` later consumes the resulting radius. The five
indices use ratios `{0.00, 0.25, 0.35, 0.70, 1.00}`, mapping to native radius
buckets `{1/16, 1/8, 1/4, 9/16, 7/8} * lineGap`. Index 2 is the default and
preserves the common 3px radius at a 12px line gap.

The radius is applied synchronously to the current exact eraser and every
deduplicated cached kind-4 eraser returned by
`CompositeBoxFactory::backends()`, with a main-DrawingBackend fallback. Exact
accepted classes are the two DrawingErasers, DiagramEraser, TextEraserSNT,
MathEraser, TextEraser, and core Eraser; the three layer-aware cloned vtables
remain recognized. Policy and `restrictedLayer()` are read before and after
every call and must stay unchanged. This makes size orthogonal to the working
layer routing: Brush Eraser changes corridor width, while Object Eraser changes
the hit radius but still expands a hit to the complete stroke. The saved size
is replayed after `setToolTheme` and `renderVolume` because firmware may create
or restyle cached tools at those lifecycle points.

### Activation-state replay

Device trace from the modularized build exposed a second lifecycle seam. The
popup and JSON retained size index `4`, while a later cached DiagramEraser was
again observed at the firmware radius `2.500`; the live policy could likewise
differ from the mode highlighted by the toolbar. The immediate radius replay
was succeeding, but `setActiveTool` calls outside the previously caller-gated
hardware path could still restyle or replace the concrete eraser.

The repair follows Nickel's own authoritative state instead of keeping a
second eraser-mode preference. Binary Ninja proves that `setActiveTool` stores
the live `IInkTool` at `IInkNotePadWidget+0xac` (BN `0x7219c`, ELF
`0x6219c`). `setEraserTool` maps Object Eraser `1` to core policy `0` and
Brush Eraser `2` to policy `1` through `clz(tool-2)>>5` (BN
`0x7084e`--`0x70860`), then retains that eraser enum at widget `+0xb8` (BN
`0x708e0`). Core `Eraser::setEraserPolicy` is BN `0xa85a94` / ELF
`0xa75a94`; it only stores the enum at exact Eraser `+0xc4`.

Every hooked eraser activation now reapplies both the selected policy and the
configured radius to the same exact, deduplicated Eraser subclasses used by
the size fan-out. A coalesced, parented zero-delay `QTimer` repeats that work
after the current Qt event stack unwinds. Its callback holds only guarded
`QPointer`s, reacquires the editor/backend/tools, reads the final live tool at
`+0xac`, skips non-eraser tools, and verifies that the restricted layer remains
unchanged. It neither calls `setActiveTool` nor writes settings, so it cannot
re-enter tool selection or turn a transient firmware default into a saved
preference.

## Provenance and limits

The menu functions above were identified in the analyzed
`libiinknote-4.38.23697.bndb` and cross-checked against exported dynamic
symbols and adjacent literal/resource strings in the original ELF. Addresses
are Binary Ninja addresses; subtract `0x10000` for the ELF VMA.

The native layer popup and its stock gesture rows have now been instantiated
on firmware 4.38.23697. Layer mutation, preview export, and behavior of
non-stock pen enum values remain runtime questions.

See also:

- `notes/LAYERS.md` for a layer popup using the same menu frame, native
  MyScript layer routing, and per-layer preview rendering;
- `notes/MYSCRIPT.md` for the renderer/stroker and document stack;
- `notes/CUSTOM_BRUSH_PHYSICS.md` for pressure/velocity modification paths;
- `notes/FIRMWARE_MAP.md` for stock selector-to-CSS mappings.
