# Binary Ninja verification: notebook layer stroke routing

Firmware: Kobo 4.38.23697. All addresses in this report are **Binary Ninja
addresses** (`ELF VMA + 0x10000`).

The GUI-attached view was verified before analysis:

- file: `extracted/rootfs/usr/local/Kobo/libiink.so`
- SHA-256: `0dc02118b9b763450c228da1e632dadbcce5cfd2acb41a3ca76a2472aed71ac6`
- BN fingerprint: `1d894a8832618024595d430528d448b6ebc826296aa6363ba02536deaa5e8dc5`
- `linux-armv7`, ARMv7/Thumb2, image base `0x10000`, analysis complete
- the bridge reported no enabled mutation capabilities; no rename, comment,
  patch, save, or headless open was performed

## 1. `PageController::backendDispatcher()` (`0x5118ec`)

### Exact cast and return type

This accessor does **not** return `snt::ToolDispatcher`.

At `0x5118f0` it reads the raw pointer from the shared-pointer member at
`PageController+0x14c`. The `__dynamic_cast` at `0x511908` uses:

- source RTTI: the GOT-resolved operand at BN `0xda8ea8` contains
  `0xd96700`, the `atk::core::ContentListener` typeinfo object
- destination RTTI: the GOT-resolved operand at BN `0xda9838` contains
  `0xd9ba38`, the `snt::BackendDispatcher` typeinfo object

BN labels the two relocation operands `_ZTIN3atk4core15ContentListenerE` and
`_ZTIN3snt17BackendDispatcherE`, respectively. Thus the exact cast direction is
`atk::core::ContentListener* -> snt::BackendDispatcher*`.

Concise pseudocode:

```cpp
shared_ptr<snt::BackendDispatcher> PageController::backendDispatcher() const {
    auto *p = this->contentListener_at_14c;
    auto *q = dynamic_cast<snt::BackendDispatcher *>(p); // 0x511908
    if (!q) return {};
    return shared_ptr<snt::BackendDispatcher>(q, control_at_150);
}
```

The successful path writes the adjusted pointer at `0x51190e`, copies the
control block from `PageController+0x150` at `0x511910`-`0x511914`, and retains
it. The null/cast-failure path returns an empty shared pointer at
`0x511944`-`0x51194c`.

### Provenance of `PageController+0x14c/+0x150`

The constructor creates a separate `snt::BackendDispatcher`, not the earlier
`snt::ToolDispatcher`:

```cpp
// PageController ctor, condensed
cb = make_shared control block;                         // 0x52b39e...
new (cb.object) snt::BackendDispatcher(gridFactory,
                                       compositeMgr); // 0x52b3f2
p = cb->get_deleter/_M_get_deleter adjusted object;    // vcall 0x52b414
this->member_14c = p;                                  // 0x52b41c
this->member_150 = cb;                                 // 0x52b420
```

The called constructor is
`snt::BackendDispatcher::BackendDispatcher(shared_ptr<GridBoxFactory>,
shared_ptr<CompositeBoxManager>)` at `0x5ebd64`.

This object is unrelated to the `make_shared<snt::ToolDispatcher>` call at
`0x52acaa`. That tool dispatcher is temporarily installed into the toolbar at
`0x52acc6`; `PageController+0x14c` is populated much later from the new backend
dispatcher allocation at `0x52b3f2`.

### Firmware callers

There is exactly one code xref to `backendDispatcher`: `0x6cc964`, in
`snt::DocumentControllerPrivate::temporaryPageController` (`0x6cc848`). The
temporary controller first removes smart-guide listeners at `0x6cc95a`, obtains
the backend dispatcher at `0x6cc964`, and passes that shared pointer to
`atk::core::Content::removeListener` at `0x6cc97c`. This use is precisely a
content-listener use, not a pen-tool use.

**Conclusion:** `backendDispatcher()` is a valid shared-pointer accessor for a
persistent `snt::BackendDispatcher` content listener, but it is not an accessor
for `snt::ToolDispatcher` and is not safe to use as the receiver of
`ToolDispatcher::restrictToLayer`. Calling a `ToolDispatcher` member through
that object would be wrong-object undefined behavior and can crash.

## 2. Constructor overwrite and the `PlatformInputDispatcher` snapshot

At `0x52b358`, the constructor loads `PageController+0x154`, the raw pointer of
the current/main `atk::core::ActiveBackend` shared pointer. It then calls that
backend's vtable slot `+0x2c` at `0x52b36a`, with `Tool::Kind == 2` and the
current box/context string:

```cpp
shared_ptr<atk::core::Tool> tool =
    activeBackend->getTool(/* kind */ 2, context); // 0x52b36a
toolbar.setCurrentTool(tool);                      // 0x52b370
```

Slot `+0x2c` is `atk::core::ActiveBackend::getTool(Tool::Kind,
std::string const&)` (base implementation at `0x9e5870`; backend-specific
overrides implement it). Kind 2 is the pen kind. The exact leaf subclass can
vary with the active page backend, but the result is its concrete pen tool, not
`snt::ToolDispatcher`.

The constructor later calls `ToolbarController::currentTool()` at `0x52b90a`.
That returned shared pointer becomes the first argument to
`PlatformInputDispatcher::PlatformInputDispatcher` at `0x52b990`, whose
constructor stores it at `PlatformInputDispatcher+0x0c/+0x10`.

The earlier sequence is therefore overwritten:

```cpp
toolbar.currentTool = make_shared<snt::ToolDispatcher>(...); // 0x52acaa, 0x52acc6
toolbar.currentTool = activeBackend->getTool(2, context);     // 0x52b36a, 0x52b370
pid.currentTool     = toolbar.currentTool;                    // 0x52b90a, 0x52b990
```

At runtime, `inputDispatcher()->getCurrentTool()` returns the backend-specific
concrete pen stored at `+0x0c`, so `dynamic_pointer_cast<snt::ToolDispatcher>`
correctly rejects it. The rejection is an object-identity mismatch, not an RTTI
or ABI failure.

## 3. Toolbar tool switching

`ToolbarController::setTool(ToolType)` (`0x53a12c`) does not preserve the
initial dispatcher on this path. It queries the PID's current tool at
`0x53a156`/`0x53a348`, aborts an incompatible active stroke at `0x53a374`, and
finishes through `activatePenCategoryFor` at `0x53a2ce`.

`ToolbarController::selectToolKind` (`0x53a500`) is a mapping/front-end for
this same path: it converts `Tool::Kind` to the toolbar `ToolType` and calls
`setTool` (call sites in the `0x53a51e`/`0x53a58a` branches). It does not install
or recover a separate persistent dispatcher.

`ToolbarController::activatePenCategoryFor` (`0x5382e4`) calls
`PlatformInputDispatcher::activatePenCategory` for pen, eraser, and selector
categories (`0x538310`, `0x5383b0`, `0x5383b8`). It then attempts
`Tool -> ToolDispatcher` at `0x538324`. When that cast fails, as it does for
this page, it reads the PID current tool at `0x538386` and replaces the toolbar
shared pointer with it at `0x538396`.

`PlatformInputDispatcher::activatePenCategory` (`0x5cd164`) has two branches:

- If `PID+0x0c` is a `ToolDispatcher`, the cast at `0x5cd190` succeeds and the
  same dispatcher receives `ToolDispatcher::activatePenCategory` at
  `0x5cd1be`.
- For this constructor path the cast fails, so it calls
  `activatePenCategoryForMainBackend` at `0x5cd220`. That function obtains the
  main active backend at `0x5ccd32`, calls its `getTool(kind, rootId)` slot at
  `0x5ccd8c`, and replaces `PID+0x0c/+0x10` with the returned concrete tool at
  `0x5ccd98`/`0x5ccd9a`. It then calls the backend's category activation slot
  `+0x190` at `0x5ccdb8`.

Thus pen/eraser/selector switching on this page makes both PID and toolbar point
to the backend's concrete current tool. Firmware does **not** route these
switches through `ToolDispatcher::activatePenCategory` on one persistent
dispatcher. That same-dispatcher branch exists, but its precondition is false
for the object installed by this `PageController` constructor.

## 4. `ToolDispatcher::restrictToLayer` (`0x5d4c90`)

The full routine reduces to:

```cpp
void ToolDispatcher::restrictToLayer(string const& layer) {
    Tool::restrictToLayer(layer);                         // 0x5d4c9c
    if (!compositeBoxManager_at_ec) return;               // 0x5d4ca0-0x5d4cac

    for (auto& backend : boxFactory_at_dc->backends()) {  // 0x5d4cc2
        currentTool_at_100 = backend->getTool(kind_at_f4,
                                               "");       // 0x5d4d04, 0x5d4d12
        if (currentTool_at_100 &&
            currentTool_at_100 != locked_active_at_c4)
            currentTool_at_100->restrictToLayer(layer);   // vslot +0x54, 0x5d4d8e
    }
}
```

Member meanings:

- `+0xdc/+0xe0`: `shared_ptr<snt::CompositeBoxFactory>`, obtained from
  `ReflowSession::boxFactory()` in the dispatcher constructor at
  `0x5d5738`-`0x5d573c`.
- `+0xec/+0xf0`: `shared_ptr<snt::CompositeBoxManager>`, copied from the final
  constructor argument at `0x5d5780`/`0x5d5786`. A null raw pointer at `+0xec`
  triggers the early return.
- `+0xf4`: cached `atk::core::Tool::Kind`. Category mapping writes it in
  `ToolDispatcher::activatePenCategory` at `0x5d4ad2`, `0x5d4b28`,
  `0x5d4b2e`, `0x5d4b36`, or `0x5d4b3e`.
- `+0x100/+0x104`: cached/scratch `shared_ptr<atk::core::Tool>` returned by the
  selected active backend. `restrictToLayer` replaces it at
  `0x5d4d12`/`0x5d4d16`; `activatePenCategory` clears it at
  `0x5d4adc`/`0x5d4ae0`.

The unconditional base call stores the string at the dispatcher's inherited
`Tool+0xbc` (`Tool::restrictToLayer`, `0xa94358`-`0xa94364`). However, the full
`ToolDispatcher::penDown` (`0x5d5f6c`) never reads the dispatcher's `+0xbc` and
does not call vslot `+0x54` after acquiring a new concrete tool. It gets the
tool at `0x5d6574`, caches it at `0x5d6588`, and eventually invokes the concrete
tool's pen-down vslot `+0x3c` at `0x5d6700`.

Therefore, if the `+0xec == 0` rebuild/propagation path is skipped, the string
is retained only on the dispatcher object; it is **not** applied to a newly
selected concrete pen at the next pen-down.

## 5. Stroke-layer dataflow

`ToolDispatcher::penDown` selects a backend-specific tool through
`ActiveBackend::getTool` at `0x5d6574` using `this+0xf4`, installs it at
`this+0x100/+0x104` at `0x5d6588`/`0x5d658c`, and dispatches pen-down to that
object at `0x5d6700`.

In `atk::core::Pen::penDown` (`0xa893ec`):

1. Ink sampling starts at `0xa89482`; the returned stroke id is saved at
   `0xa89488`.
2. The active `Pen` object's vtable slot `+0x58` is called at
   `0xa894b0`-`0xa894b6`.
3. Its returned string pointer is moved to argument `r2` at `0xa894b8` and is
   passed to `InkSampler::setStrokeLayer(strokeId, layer)` at `0xa894be`.

Vtable slot `+0x58` is `atk::core::Tool::restrictedLayer() const`; its base
implementation at `0xa942f0` returns `this+0xbc`. Hence the sampler receives
the **active concrete Pen sub-tool's own inherited restricted-layer string**.
It does not read the dispatcher's base string directly.

When `ToolDispatcher::restrictToLayer` performs its normal rebuild, the object
created by `backend->getTool` receives virtual `restrictToLayer(layer)` at
`0x5d4d8e`, which populates that concrete tool's string. This is how a
dispatcher restriction reaches `Pen::penDown`.

Tool changes do not preserve that propagation automatically. Dispatcher
category activation clears the cached sub-tool at `0x5d4adc`/`0x5d4ae0`, and
the subsequent pen-down acquires a new one without reapplying the dispatcher's
stored string. On the actual non-dispatcher PID path, category switching
directly replaces `PID+0x0c/+0x10` at `0x5ccd98`/`0x5ccd9a`, likewise producing
a new concrete tool with its own string.

**Conclusion:** do not rely on the PID's current category alone. Reapply the
active layer to every persistent writing-capable `DrawingBackend` cache from
the plugin's `setToolTheme` hook, page/backend reconstruction, and initial
page activation. Section 8 identifies the exact caches and the required
eraser exception.

## 6. `NeboBackend` vtable and `PageController` member

BN data variable `0xd971a0` is exactly
`_ZTVN8myscript4iink11NeboBackendE`. The `NeboBackend` constructor at
`0x48a7a4` writes `_vtable_for_myscript::iink::NeboBackend + 8` to the object at
`0x48a7ca`.

`NeboBackend::createPageController` (`0x490dac`) creates the page controller and
stores its shared pointer in the Nebo object at:

- raw pointer `NeboBackend+0x14` at `0x4914a2`
- control block `NeboBackend+0x18` at `0x4914a4`

`NeboBackend::selectTool` (`0x472644`) confirms the identity: it loads
`NeboBackend+0x14` at `0x47264e`, calls `PageController::inputDispatcher()` at
`0x472654`, and, when needed, calls `PageController::toolDispatcher()` at
`0x472666` before activating the PID category at `0x4726a2`.

So vtable `0xd971a0` belongs to the class whose object owns the
`shared_ptr<PageController>` at `+0x14/+0x18`.

## 7. `PlatformInputDispatcher::penDown` (`0x5cfa2c`)

Pen input **does dispatch through the tool at `PlatformInputDispatcher+0x0c`**.

The routine repeatedly consults that exact pointer. For example, it calls the
tool's kind/type vslot `+0x08` through `PID+0x0c` at
`0x5cfbf6`-`0x5cfbfc` and again at `0x5cfce4`-`0x5cfcea`. On the writing path it
optionally calls the tool's vslot `+0x60` at `0x5cfe00`-`0x5cfe0a`, then calls
the pen-down vslot `+0x3c` with the prepared `PointerInfo` at
`0x5cfe0c`-`0x5cfe14`:

```cpp
Tool *tool = this->currentTool_at_0c;
...
bool accepted = tool->penDown(pointerInfo); // vslot +0x3c, 0x5cfe14
```

There are selection/state-machine and main-backend side paths in this large
routine, including conditional casts to `ToolDispatcher` at `0x5cff2e` and
`0x5d0088`, but they do not replace the final writing dispatch above. The
`+0x0c` snapshot is therefore not merely advisory: its dynamic identity and its
replacement on category changes directly determine which `Tool::penDown` and
which per-tool restricted-layer string are used.

`PageController::penDown` (`0x51a23c`) corroborates the chain by tail-calling
`PlatformInputDispatcher::penDown` through its shared-pointer member at
`PageController+0x13c` (`0x51a262`/`0x51a284`).

## Implications for the plugin

### Correct routing target

The expected `PageController::backendDispatcher()` hypothesis is disproved.
That accessor returns `snt::BackendDispatcher`, which has no role in pen-tool
layer routing.

No safe exported accessor returning the constructor's original persistent
`snt::ToolDispatcher` was found. More importantly, that object is not the one
used by this page's PID writing path, so retaining or recovering it would not
route these strokes.

For this firmware, the correct object and accessor chain is:

```text
NeboBackend+0x14 shared_ptr<PageController>
  -> PageController::inputDispatcher()              (0x511960)
  -> PlatformInputDispatcher::getCurrentTool()      (0x5cc80c)
  -> current concrete atk::core::Tool
  -> virtual Tool::restrictToLayer(layer), slot +0x54
     (base implementation 0xa94358)
```

Do not cast that tool to `snt::ToolDispatcher`. Also do not trust the misleading
name `PageController::toolDispatcher()` (`0x5118d8`): it merely calls
`ToolbarController::currentTool()` on the embedded toolbar at
`PageController+0x1d8` (`0x5118da`-`0x5118e2`). The PID accessor is the
authoritative target because PID pen-down dispatches through `PID+0x0c`.

Use virtual dispatch for `restrictToLayer`, so specialized overrides (for
example diagram tools) remain honored.

### Survival across tool changes

The restriction belongs to each concrete tool instance. On
`DrawingBackend`, however, `getTool` returns persistent cached objects and only
changes their box ID. Restrict all three writing-capable caches when applying
a layer, clear the eraser cache, and repeat that policy after `setToolTheme`,
page activation, and page/backend reconstruction. Capture the current tool
only for diagnostics; do not retain it as the routing target.

### Qt menu callback and crash risks

- Calling `backendDispatcher()` itself on a live `PageController` on the UI
  thread is a null-safe shared-pointer copy plus `dynamic_cast`; it is not
  inherently dangerous. It is simply the wrong object. The crash begins if its
  result is treated as a `ToolDispatcher` or the `PageController*` is stale.
- Run this object-graph traversal on Nickel's/UI thread. Shared-pointer
  reference counts are atomic, but the paired pointer/control-block fields and
  tool replacement are not shown under a common lock; racing
  `activatePenCategoryForMainBackend` can observe inconsistent or stale state.
- Copy/hold the owning `shared_ptr<PageController>`, then the returned
  `shared_ptr<PlatformInputDispatcher>`, then the returned
  `shared_ptr<atk::core::Tool>` for the duration of the call. Null-check every
  stage because the page/controller can be absent during open, close, or
  reconstruction.
- Avoid changing the restriction during an active stroke. `Pen::penDown` uses
  its own mutex at `0xa8940a`, while base `Tool::restrictToLayer` is only a
  string assignment at `0xa94358`; no cross-operation lock is evident. Apply
  after pen-up/abort or in the normal serialized tool-change callback.
- A null result is a recoverable lifecycle state. Calling through a null raw
  pointer, using an unretained page/tool after teardown, or invoking
  `ToolDispatcher::restrictToLayer` on the `snt::BackendDispatcher` returned by
  `backendDispatcher()` are the material crash cases.

## 8. `DrawingBackend` cache policy and the eraser regression

`snt::DrawingBackend::getTool` (`0x5689cc`) selects four cached
`shared_ptr<Tool>` members:

| Tool kind | Cache offset | Concrete class | Layer policy |
| --- | ---: | --- | --- |
| 0, 1, 2 | `+0x44` | `snt::DrawingPen` | restrict to active layer |
| 4 | `+0x4c` | `snt::DrawingEraser` | carry active ID for the plugin selection adapter |
| 3 | `+0x54` | `snt::DrawingSelector` | restrict to active layer |
| 6 | `+0x5c` | `snt::DrawingBrush` | restrict to active layer |

The function rewrites the returned tool's box ID but never clears or copies
`Tool+0xbc`, so a restriction applied to these cache objects survives category
switching. `DrawingBackend::init` (BN `0x56d6f4`) calls virtual slot `+0x54`
on Pen near `0x56d8c8`, Brush near `0x56dec2`, and Selector near `0x56df52`,
but has no such call for the Eraser created near `0x56dcb6`. That stock policy
does not make the eraser layer-aware: the stock eraser never consumes the base
`Tool` restriction.

The vtables explain the distinction:

- `snt::DrawingBrush` (`0xd9a3a0`) uses `atk::core::Pen::penDown` at slot
  `+0x3c`, so it reads `restrictedLayer()` and sends that ID to
  `InkSampler::setStrokeLayer`, exactly like `DrawingPen`.
- `snt::DrawingEraser` (`0xd9a478`) uses
  `atk::core::Eraser::penDown` (`0xa88668`). Neither that routine nor
  `DrawingEraser::selectionFromPoints` (`0x572838`) reads `Tool+0xbc`, calls
  `restrictedLayer()`, or selects a named layer. Its selection is built from
  the composite `Layout`.

The full stock eraser path explains the regression:

1. `Eraser::penDown` (BN `0xa88668`) calls virtual
   `selectionFromPoints` at slot `+0x78`, combines the result, and later hands
   it to `Layout::erase`; it never reads the eraser's `Tool+0xbc` string.
2. Base `Eraser::selectionFromPoints` (BN `0xa87470`) constructs a
   `Selection(Layout)` and calls `Selection::selectPolygon(..., Mode(0))` at
   BN `0xa87658`. The path continues through `Selection(Layout)` at
   `0x9f4ee4`, `Layout::createPageSelection` at `0x9eebb0`, and
   `PageSelection::selectPolygon_` at `0xb07e44`. No selected-layer argument
   or document current-layer field participates.
3. Both concrete drawing erasers only adjust that polygon selection to stroke
   boundaries: `snt::DrawingEraser::selectionFromPoints` is BN `0x572838`,
   while global `DrawingEraser::selectionFromPoints` is BN `0x431314`. Each
   reads the drawing-eraser mode at `this+0xc4`, not
   `Tool::restrictedLayer()`.
4. Writing does not set a shared eraser layer. `InkSampler::setStrokeLayer`
   (BN `0xaab710`) tail-calls
   `SynchronizedPendingStrokes::setStrokeLayer` (BN `0xaaaa34`), which stores
   the ID only in the selected pending stroke at `PendingStroke+0x94` under its
   mutex. It does not mutate Page, Layout, Renderer, or Eraser state.

The apparent "eraser follows the last layer written" is therefore not a
persistent current-layer setter. Layer selection previously updated only the
pen, while the eraser continued to polygon-hit-test the unrestricted composite;
which overlapping stroke that composite selection produced was incidental to
the hit and stroke ordering.

### Firmware-supported eraser layer mechanisms

Kobo already composes named-layer and geometric selection in
`Selector::computeSelection` (BN `0xa8ce70`): it calls
`Selection::selectLayer(name, Replace)` at `0xa8ced8`, then
`Selection::selectPolygon(..., Intersection)` with numeric mode `2` at
`0xa8d082`. This pins both the operation and the enum value used by the fix.

For `snt::DrawingEraser` and global `DrawingEraser`, the plugin keeps the
original `selectionFromPoints` hit test, then calls
`Selection::selectLayer(activeId, Intersection)` on its result. Raw-content
`DiagramEraser` must not use this adapter: hardware tests disproved both its
temporary `selectionFromPoints` result and its final inline `Eraser+0xd0`
Selection as the layer-scope control. Diagram erasing instead follows shared
native Diagram state set by the cached `DiagramPen` override.

The adapter is deliberately narrow:

- clone the two exact 148-byte DrawingEraser vtables independently: the
  `snt::DrawingEraser` vtable at BN `0xd9a478` must have slot `+0x78` equal to
  BN `0x572838`, and the global `DrawingEraser` vtable at BN `0xd956e0` must
  have that slot equal to BN `0x431314`;
- replace `+0x78` only for those two classes, call the corresponding original
  first, and intersect the returned Selection with the ID from
  `Tool::restrictedLayer()`;
- call `Selection::selectNone()` if the ID/filter is missing or throws, so a
  layer-routing failure cannot erase the composite; and
- enumerate `CompositeBoxFactory::backends()` (BN `0x5c83e0`) and obtain kind
  4 through each backend's verified vslot `+0x2c`. An exact DiagramEraser kind
  4 identifies the raw-content Diagram backend; only then obtain kind 0,
  verify its exact DiagramPen vtable, and call its `+0x54` override.

The live Renderer remains unrestricted. The selected layer affects erasure by
filtering the two DrawingEraser selections or by setting native shared Diagram
state. It never hides canvas content.

### First device trace and dispatcher-backed eraser

The first instrumented hardware test was only a partial behavioral pass: the
base-layer mark erased, but the custom-layer mark did not. The trace proved the
first build's `snt::DrawingEraser` adapter never ran:

```text
layers: eraser adapters backends=1 drawing=0 armed=0 current=no
layers: cached writing-tool routing backend is not DrawingBackend
```

There was no vtable-clone or selection-intersection line. Binary Ninja then
identified the other exact class used through the dispatcher backend: global
`DrawingEraser` has a 148-byte vtable at BN `0xd956e0`, uses core
`Eraser::penDown` at `+0x3c`, base `Tool::restrictToLayer` and
`restrictedLayer` at `+0x54/+0x58`, and its own selection override at `+0x78`
(BN `0x431314`). That override calls base `Eraser::selectionFromPoints` at BN
`0xa87470` and performs the same stroke-boundary adjustment as the snt class.

`ToolDispatcher::restrictToLayer` at BN `0x5d4c90` explains the object graph:
it reads the dispatcher's current category at `ToolDispatcher+0xf4`, traverses
`CompositeBoxFactory::backends()`, calls each ActiveBackend's `getTool` vslot
`+0x2c` with that kind, and invokes the returned tool's `restrictToLayer`
vslot. `ToolDispatcher::penDown` at BN `0x5d5f6c` repeats the backend lookup,
stores the selected concrete tool at `ToolDispatcher+0x100`, and dispatches its
`penDown` through slot `+0x3c` at BN `0x5d6700`. The fix therefore arms exact
eraser classes during the existing backend traversal; it does not patch the
dispatcher, generic Eraser, or unrelated text/math erasers.

### Second device trace: exact raw-content eraser

The second hardware test reproduced the same A-pass/B-fail behavior. Its
runtime vtable probe removed the remaining ambiguity:

```text
backend=com.myscript.raw-content
backend-vptr=0xd90290:_ZTVN3atk7diagram18DraftActiveBackendE
getTool=0x728b7c:_ZN3atk7diagram20DiagramActiveBackend7getTool...
kind4 vptr=0xd8eb38:_ZTVN3atk7diagram13DiagramEraserE
penDown=0x75b670:_ZN3atk7diagram13DiagramEraser7penDown...
selection=0xa77470:_ZN3atk4core6Eraser19selectionFromPoints...
```

The trace is from `backups/device-eraser-vptr-probe-20260715-122306/` and
matches the pinned ELF exactly. The `DiagramEraser` vtable is 148 bytes at BN
`0xd9eb30`; its callable vptr is BN `0xd9eb38`. Slots `+0x3c`, `+0x54`,
`+0x58`, and `+0x78` resolve to BN `0x76b670`, `0xa94358`, `0xa942f0`, and
`0xa87470`. Its diagram-specific `updateSelection` and `eraseSelection` slots
are `+0x7c`/`+0x80`, BN `0x76b73c`/`0x76c224`.

Core `Eraser::penDown` calls `selectionFromPoints` through `+0x78` at BN
`0xa88900`--`0xa88904`, then calls the concrete `updateSelection` through
`+0x7c` at BN `0xa88902`--`0xa8890c`. `DiagramEraser::updateSelection` clones
the input at BN `0x76b770`, intersects it with the diagram layout group at BN
`0x76b788`--`0x76b78e`, performs diagram item selection and stroke-boundary
adjustment, then calls core `Eraser::updateSelection` at BN `0x76bb20`.

### Third device trace: the temporary filter is too early

The exact DiagramEraser adapter armed successfully in the third build. The
trace at `backups/device-diagram-eraser-ab-20260715-123806/` proves both the
base and custom IDs reached its `+0x78` wrapper:

```text
layers: layer-aware eraser vtable clone verified implementation=diagram
layers: eraser adapters backends=1 drawing=1 snt=0 global=0 diagram=1 armed=1
layers: eraser selection intersected active layer id=SNT_LAYER mode=2 implementation=diagram
layers: eraser selection intersected active layer id=cnt.layer.602d278d-cd2a-4c8d-ba2f-2c6385745e8e mode=2 implementation=diagram
```

The base-layer horizontal line erased, but the selected custom-layer vertical
line did not. Thus the adapter identity and active ID were correct; the hook
point was not. Binary Ninja pins the actual final erase target:

- core `Eraser::penDown` clears the inline `Selection` at `this+0xd0` at BN
  `0xa888ea`--`0xa888ee` before the `+0x78/+0x7c` calls;
- core `Eraser::updateSelection` (BN `0xa85ce4`) combines its incoming
  Selection into that same member at BN `0xa85d08`--`0xa85d18`;
- `DiagramEraser::eraseSelection` forms `this+0xd0` at BN `0x76c228`, checks
  it with `Selection::isEmpty` at BN `0x76c23a`, and passes it to diagram
  removal at BN `0x76c284`.

The fourth build tested that exact final-Selection hook. The trace at
`backups/device-diagram-final-ab-20260715-125733/` proves the clone was active
and both selected IDs reached it (`implementation=diagram-final`), but the
hardware result was still horizontal A erased and vertical B not erased. This
disproves intersecting the named layer only after Diagram's stock hit path.

### Fifth device trace: DiagramPen state is not the erase scope

The fifth build tested the cached-DiagramPen hypothesis directly. Its trace at
`backups/device-diagram-native-ab-20260715-134301/` proves that selecting B
while DiagramEraser was current reached the exact cached DiagramPen and its
override returned successfully:

```text
current vptr=...DiagramEraserE
diagram cached pen(kind0) ...DiagramPenE exact=yes restricted=yes
diagram=1 armed=1 diagram-pens=1 pen-restricted=1 ... current=yes
```

The screen refreshed on eraser input, but the custom-layer vertical line did
not erase. Thus DiagramPen data `+0x60` is useful for future writing but is not
the DiagramEraser's layer-scope setter. This device result disproves the fifth
implementation's conclusion without ambiguity.

Binary Ninja pins the missing setter and object path:

- `DiagramActiveBackend::getTool` is BN `0x738b7c`; kind 4 loads the cached
  DiagramEraser from backend `+0x64` at BN `0x738c4e`, while pen kinds 0--2
  load the cached DiagramPen from backend `+0x5c` at BN `0x738c76`;
- the DraftActiveBackend vtable is BN `0xda0288`, with `getTool` at `+0x2c`;
- the DiagramPen vtable is BN `0xd9ffb0`; its `+0x54` override is
  `DiagramPen::restrictToLayer` at BN `0x7c0e6c`; and
- that override calls base `Tool::restrictToLayer` at BN `0x7c0e76`, locks
  shared Diagram data at BN `0x7c0e7e`, addresses data `+0x60` at BN
  `0x7c0e88`, and assigns the same layer ID at BN `0x7c0e8c`.

### Root cause and sixth fix: Diagram needs layer-first hit selection

Binary Ninja shows why all post-hit intersections failed:

- core `Eraser::selectionFromPoints` (BN `0xa87470`) constructs a fresh
  Selection, derives its scaled width from Eraser `+0xcc` and `+0x110` at BN
  `0xa8748c`--`0xa874a8`, calls `Eraser::strokerPolygon` at BN `0xa875f6`,
  then calls `Selection::selectPolygon` with Mode 0/Replace at BN
  `0xa8764c`--`0xa87658`;
- therefore the stock geometric Selection has already been replaced in the
  raw-content Diagram context before the plugin's earlier layer intersection;
- firmware `Selector::computeSelection` (BN `0xa8ce70`) demonstrates the
  supported inverse order: `selectLayer(..., Replace)` with Mode 0 at BN
  `0xa8ced0`--`0xa8ced8`, followed by `selectPolygon(..., Intersection)` with
  Mode 2 at BN `0xa8d07e`--`0xa8d082`.

The sixth adapter clones only exact DiagramEraser vslot `+0x78`. It calls the
stock original first solely to construct a Selection bound to the correct
live Layout, replaces that Selection with the selected named layer, invokes
Kobo's exact `strokerPolygon` using the original width and view-scale fields,
then intersects the polygon. Stock Diagram `updateSelection` at `+0x7c` and
`eraseSelection` at `+0x80` remain untouched. Any missing symbol, invalid
width, empty polygon, or exception clears the Selection and fails closed.

The cached DiagramPen update remains only to preserve the selected layer when
switching back to writing. `scripts/investigate-diagram-eraser.py` replays the
read-only SDK proof, and `scripts/verify-layer-abi.py` pins every new symbol,
mode, field offset, vslot, and instruction.

### Fresh Layer 2/Layer 3 trace: observe and guard actual pen-down

The controlled fresh-stroke test in
`backups/device-fresh-layer23-test-20260715-142447/` removed the new Layer 2
horizontal stroke but not the new Layer 3 vertical stroke. The activation
trace proves that the exact live/cached DiagramPen accepted Layer 3 before the
vertical stroke (`current ... DiagramPen`, `exact=yes restricted=yes`, then
`active=cnt.layer.3da...`). It does not, however, observe the restriction at
physical pen-down. The subsequent Layer 3 erase produced only one empty stock
hit, while the Layer 2 gesture produced the normal callback stream and
non-empty layer-first geometry. Apply-time success is therefore not proof of
the layer value that core Pen ultimately consumes.

Binary Ninja pins the complete raw-content write path:

- DiagramPen `penDown` is BN `0x7bf1e4`, vslot `+0x3c` in the 152-byte
  DiagramPen vtable BN `0xd9ffb0`;
- it calls `SmartPen::penDown` at BN `0x7bf41e`;
- SmartPen calls core `Pen::penDown` at BN `0xa93cf8`;
- core Pen loads virtual `restrictedLayer()` from slot `+0x58` at BN
  `0xa894b0` and passes that string to `InkSampler::setStrokeLayer` at BN
  `0xa894be`.

The seventh instrumented fix clones only exact DiagramPen vslot `+0x3c`. A
per-tool desired layer is recorded when popup activation reaches the cached
DiagramPen. Immediately before invoking stock DiagramPen `penDown`, the
wrapper reads the tool's real restriction, re-applies the desired ID only if
they differ, and emits a bounded `desired/before/after/corrected` trace. Every
other DiagramPen slot and all firmware code remain untouched. This both guards
a late native reset and distinguishes it from an eraser hit-test issue on
hardware; success is not claimed until the pen-down trace and a fresh
two-layer erase test agree.

### Downstream custom-layer fix: core update and virtual removal

The later Layer 2/Layer 3 traces prove that the layer-first `+0x78` adapter is
not sufficient. Layer 2's non-empty selection returns through the stock
Diagram `+0x7c` override, but Layer 3 reaches non-empty geometry without the
wrapper's post-stock trace. The fsync diagnostic reproduces this, so the
missing line is not buffered output.

`DiagramEraser::updateSelection` clones and narrows its input through
`layoutGroup`/`selectLayoutGroup` at BN `0x76b784`--`0x76b78e`,
`selectByType(0xcc4, Remove)` at `0x76b7a6`--`0x76b7ae`, and
`itemsIntersecting` at `0x76b7da`. Those semantic Diagram filters can discard
raw ink selected from a plugin-created named layer. Core
`Eraser::updateSelection` at BN `0xa85ce4` instead only unions a non-empty
Selection into `this+0xd0` and returns whether it changed.

The return must be preserved. Core `Eraser::penMove` loads/calls `+0x7c` at BN
`0xa87c40`/`0xa87c4c`, then forwards its result to renderer vslot `+0x84` at
`0xa87c84`/`0xa87c88`. The adapter therefore bypasses the Diagram override
only for `cnt.layer.*` and directly returns the core update result. The
document/base layer retains exact stock behavior.

Removal is also virtual. Core `Eraser::penUp` loads vslot `+0x80` at BN
`0xa86f0a`, calls it at `0xa86f0e`, and uses its result to choose the real
transaction commit. `DiagramEraser::eraseSelection` forms `this+0xd0` at BN
`0x76c228` and calls `Diagram::removeEraseSelection` at `0x76c284`; the latter
reaches raw `DiagramData::eraseFromLayout` at `0x71667e` and `0x7166d2`
without another named-layer filter. The plugin's `+0x80` replacement is an
observer only: it checks the committed Selection, calls stock exactly once,
and returns the stock result unchanged.

### Binary-proven eraser-policy regression: core-only loses whole-stroke expansion

The custom-layer `+0x7c` bypass fixes the Diagram semantic narrowing, but it
also exposes a separate mode-preservation regression: an eraser configured as
Kobo's whole-stroke eraser reaches core `Eraser::updateSelection` with only the
polygon fragment produced by the layer-first `+0x78` adapter. The direct core
update faithfully commits that fragment; it does not perform the
stroke-boundary expansion which the bypassed firmware paths normally apply.
This is why the current `cnt.layer.*` path can behave like a precise/brush
eraser instead of removing the complete touched stroke.

The live mode is already stored on the concrete Eraser; no toolbar hook or
second UI state is needed. `ToolDispatcher::setPropertiesForTool` (ELF
`0x5c3718`, BN `0x5d3718`) reads the `eraser.policy` property. Its `stroke`
branch passes enum value `0` to `Eraser::setEraserPolicy` at BN
`0x5d3998`--`0x5d399c` (ELF `0x5c3998`--`0x5c399c`), while the `precise`
branch passes value `1` at BN `0x5d3b1e`--`0x5d3b22` (ELF
`0x5c3b1e`--`0x5c3b22`). The setter is ELF `0xa75a94`, BN `0xa85a94`, and
stores the enum at `Eraser+0xc4` at BN `0xa85a98`. The public
`Eraser::eraserPolicy() const` accessor is ELF `0xa75aa4`, BN `0xa85aa4`,
and loads the same field at BN `0xa85aa8`.

Both non-Diagram DrawingErasers prove what policy `0` requires:

- `snt::DrawingEraser::selectionFromPoints` (ELF `0x562838`, BN `0x572838`)
  reads `Eraser+0xc4` at BN `0x572848`. A nonzero policy returns the polygon
  hit unchanged; policy `0` reaches
  `InkSelection::adjustToStrokeBoundaries_(0.0f)` at BN `0x5728e6`, with the
  zero float literal at BN `0x572a78`.
- Global `DrawingEraser::selectionFromPoints` (ELF `0x421314`, BN
  `0x431314`) has the same branch at BN `0x431324`--`0x43132c` and the same
  zero-threshold adjustment at BN `0x431378`--`0x43137e`.
- The public wrapper suitable for the plugin is
  `atk::core::Selection::adjustToStrokeBoundaries(float)` at ELF
  `0x9e2d70`, BN `0x9f2d70`. The float travels in `s0` under this firmware's
  hard-float ABI.

The Diagram override independently checks the same policy field at BN
`0x76b7b4`--`0x76b7ba` (ELF `0x75b7b4`--`0x75b7ba`). A nonzero/precise
policy skips its whole-item work and proceeds to core update at BN
`0x76bb20`; policy `0` runs the larger semantic path. The plugin must continue
to bypass that semantic path for plugin-created layers because hardware
already proved that it can discard or stall on valid custom-layer raw ink,
but it must reproduce the missing policy-0 stroke normalization before the
core-only commit.

The implemented, build-verified narrow correction is therefore:

1. Read the concrete Eraser's live policy through the pinned public accessor
   for every `+0x78` callback, so a Kobo UI mode change takes effect without
   cached plugin state.
2. After custom-layer/Replace and eraser-polygon/Intersection, call
   `Selection::adjustToStrokeBoundaries(0.0f)` only when the selected ID has
   the `cnt.layer.*` prefix and the live policy is `0`.
3. Return that adjusted Selection directly to the existing custom-only core
   `+0x7c`; let the stock `+0x80` removal path continue unchanged.
4. Preserve policy `1` as the current precise polygon erase and preserve the
   document/base layer's exact stock `+0x7c` behavior. Missing symbols or an
   exception must clear the Selection and fail closed.

This placement is intentional: `+0x78` owns the mutable Selection and its
feedback geometry, whereas `+0x7c` receives a const Selection. It also avoids
double-normalizing the document layer, whose stock Diagram update still runs.
`Selection::adjustToStrokeBoundaries` mutates only the Selection it receives:
its public wrapper at BN `0x9f2d70` forwards that existing selection to
`PageSelection::adjustToStrokeBoundaries_` at BN `0xb06e54`; it performs no
new Page or layer hit test. Each stroke already has one layer ID, so expanding
the layer-scoped polygon selection to the touched stroke's boundaries remains
inside that stroke's layer. A second `selectLayer(..., Intersection)` is not
only redundant: its layer-lookup failure path can clear an otherwise valid
adjusted Selection, so the implementation deliberately performs no post-adjust
layer lookup.

**Hardware verification passed.** The successful post-test snapshot is
`backups/device-object-eraser-success-post-20260715-175128/`. Its trace shows
policy `0` with `boundary-adjusted=yes` on non-empty selections for both
custom IDs (lines 107--116 and 339--354), policy `1` with
`boundary-adjusted=no` (lines 143--164 and 249--270), a non-empty committed
Selection, and stock removal returning `removed=yes` at line 126. The user
confirmed complete-stroke Object Eraser behavior, precise Brush Eraser
behavior, and correct isolation after switching layers. The saved sidecar
still contains both IDs with the selected layer active, and the trace contains
no fail-closed, hook, crash, or routing errors.

This result proves the intended whole-raw-stroke behavior. It does not claim
parity with richer stock Diagram semantics for arbitrary recognized shapes,
text, borders, or other Diagram objects.
