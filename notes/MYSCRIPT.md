# MyScript architecture and brush extension points

This note maps the MyScript stack shipped in Kobo firmware `4.38.23697`, with
an emphasis on how notebook strokes are captured, styled, rendered, stored,
exported, and how far a custom pen can realistically go.

The central finding is more nuanced than "the brushes are proprietary, so they
cannot be changed":

- the built-in stroke engines are proprietary MyScript implementations, but
  this build retains enough C++ symbols to identify their dispatch and major
  processing stages;
- Kobo chooses a brush declaratively with `-myscript-pen-brush` CSS;
- `libiink.so` contains an explicit registry for user-defined
  `IStrokerFactory`/`IStroker` implementations; and
- a custom stroker produces a MyScript `graphics::IPath`. It is therefore a
  vector-path extension point, not evidence of an arbitrary textured or
  particle rasterizer API.

No binary, Binary Ninja database, or device installation was modified during
this investigation. All addresses are static findings for this exact firmware.

## Installed components

The installed SDK is split into a C object engine, dynamically loaded engine
modules, and the higher-level Interactive Ink (`iink`) C++ API.

| File | Size | Observed role |
| --- | ---: | --- |
| `libMyScriptEngine.so` | 1,313,524 | Core object engine, properties, object loading/storing, and module/resource loading |
| `libMyScript2D.so` | 348,028 | 2D geometry module |
| `libMyScriptAnalyzer.so` | 464,684 | Analysis module |
| `libMyScriptDocument.so` | 1,023,188 | Interactive Ink/Nebo/Notes document containers and ink-to-document mapping |
| `libMyScriptGesture.so` | 399,016 | Gesture module |
| `libMyScriptInk.so` | 661,276 | Digital-ink stroke model plus BINK/JINK/JINKSON codecs |
| `libMyScriptMath.so` | 457,224 | Math recognition module |
| `libMyScriptPrediction.so` | 46,424 | Prediction module |
| `libMyScriptShape.so` | 349,740 | Shape recognition module |
| `libMyScriptText.so` | 2,629,524 | Text recognition module |
| `libiink.so` | 14,199,268 | Editor, tool CSS, renderers, strokers, content model, and language bindings |

All are 32-bit ARM EABI5 ELF shared objects. They are stripped, but dynamic
symbols remain. Relevant GNU build IDs are:

```text
libiink.so:              f960db22f0d4598d46d9a225042c067e49fec570
libMyScriptInk.so:       146b14c71b5136676786fa0ab5b2ff99c7fad2d1
libMyScriptDocument.so:  338832c4572c5aeeebdf5e8f4792b71943fb3781
```

`libiink.so` directly depends on `libMyScriptEngine.so`, but not on every
`libMyScript*.so` provider. `libMyScriptEngine.so` imports `dlopen`/`dlsym` and
exports a C object API including `voCreateEngine`, `voDestroyEngine`,
`voCreateObject`, `voGetInterface`, `voGetProperty`, `voGetType`,
`voStoreObject`, and `voLoadObject`.

Each provider exports a data symbol named after its module; for example,
`libMyScriptInk.so` exports `MyScriptInk`. This supports the following loading
model:

```text
Kobo notebook UI / export service
             |
             v
         libiink.so
  editor + CSS + rendering + C++ API
             |
             v
    libMyScriptEngine.so
  C object API + dlopen/dlsym loader
             |
             v
  libMyScriptInk.so, libMyScriptDocument.so, ...
  engine-loaded module descriptors and implementations
```

## Live stroke path

The complete path across Kobo and MyScript is:

```text
QTouchEvent::TouchPoint
  x, y, timestamp, raw pressure
             |
             v
libiinkuiref.so.1.0.0
  EditorWidget::calculateNormalizedPressure()
  Qt pointerDown / pointerMove / pointerUp signal
             |
             v
libiink.so
  EditorImpl::pointerDown_ / pointerMove_ / pointerUp_
  PendingStrokeFactory and InkStrokeBuilder
             |
             +--> stored stroke samples/channels
             |
             v
RendererImpl::getStroker(brush name)
  registered user stroker OR built-in stroker
             |
             v
graphics::IPath
  screen, thumbnail, or export rendering
```

Important `libiink.so` Binary Ninja addresses are:

| Binary Ninja | Function |
| --- | --- |
| `0x436560` | `EditorImpl::pointerMove_` |
| `0x4368b8` | public `EditorImpl::pointerMove` wrapper |
| `0x436a24` | `EditorImpl::pointerUp_` |
| `0x436fe8` | `EditorImpl::pointerDown_` |
| `0x43a8e4` | batched `EditorImpl::pointerMoves_` |
| `0x43ac58` | `EditorImpl::pointerEvents` |
| `0x4521fc` | `PendingStrokeFactory::pointerUp` |
| `0xbb431c` | `InkStrokeBuilder` implementation region |
| `0xbd6148` | `IInkStrokeBuilderPriv::addSampleXY` |
| `0xbd6394` | `IInkStrokeBuilderPriv::addSampleXYT` |
| `0xbd65ec` | `IInkStrokeBuilderPriv::addSampleXYFT` |

The `XY`, `XYT`, and `XYFT` helpers prove that the ink model can represent at
least position, time, and force channels. They do not prove that every Kobo
stroke or every document format persists all four values; that needs runtime
inspection of saved documents.

## Tool CSS and brush resolution

Kobo's popup does not contain the brush implementations. It selects a tool
style class, and `libiinkuiref.so.1.0.0` expands that class into CSS such as:

```css
.brush-0-0 {
    -myscript-pen-brush: CalligraphicBrush;
    -myscript-pen-width: 0.5;
    color: #000000;
}
```

Inside `libiink.so`, `ToolControllerImpl` owns style application:

| Binary Ninja | Function |
| --- | --- |
| `0x4c4ec8` | `ToolControllerImpl::getToolStyle` |
| `0x4c4f08` | `ToolControllerImpl::getToolStyleClasses` |
| `0x4c4f48` | `ToolControllerImpl::setToolStyle` |
| `0x4c5210` | `ToolControllerImpl::setToolStyleClasses` |

`RendererImpl::getStroker` at `0x4b10d0` resolves the CSS brush name. Its
known built-in names are:

```text
DynamicEnvelope
QuadBezier
Polyline
FountainPen
CalligraphicQuill
CalligraphicBrush
Qalam
Highlighter
Square
FeltPen
default
dash
```

`InternalStrokerWrapper::stroke` at `0x4d720c` dispatches the internal enum to
`FeltPen`, `FountainPen`, `CalligraphicQuill`, `CalligraphicBrush`, `Qalam`,
`Highlighter`, `Square`, `DynamicEnvelope`, `QuadBezier`, or `Polyline`, then
calls `atk::core::Stroker::strokePath`.

The deeper implementation still exposes useful symbols:

| Binary Ninja | Function/stage |
| --- | --- |
| `0x4d86c4` | `atk::core::Stroker::strokePath` path overload |
| `0xa75bd8` | `Stroker::getBrushWidthCoef` |
| `0xa95eb8` | `Stroker::preprocessPath` |
| `0xa75d80` | `Calligraphy` constructor |
| `0x434528` | `Calligraphy::operator()` |
| `0x4d7e18` | `Calligraphy::sendPolygonTo<IPath>` |
| `0x433048` | `PressureSimulator::sendTo<Calligraphy>` |
| `0x432860` | `PressureSimulator::sendTo<Smoother>` |
| `0x433ae8` | `Smoother::sendTo<Calligraphy>` |

This is enough to establish that built-ins combine preprocessing, smoothing,
pressure simulation, calligraphic polygon generation, and path output. It is
not enough to reproduce their algorithms accurately without substantially
more reverse engineering.

## The custom-stroker extension point

The renderer maintains a `std::map<string, shared_ptr<IStrokerFactory>>` for
user-defined strokers:

| Binary Ninja | Function |
| --- | --- |
| `0x4b15fc` | `RendererImpl::registerStroker(String, shared_ptr<IStrokerFactory>)` |
| `0x4b1014` | `RendererImpl::unregisterStroker` |
| `0x4b10d0` | `RendererImpl::getStroker` |
| `0x4d3c30` | `PageViewWrapper::getStroker` |
| `0x4d7040` | `UserDefinedStrokerWrapper::stroke` |
| `0x5046f0` | Java/JNI `NativeFunctions_registerStroker` |
| `0x509204` | Java/JNI `NativeFunctions_unregisterStroker` |

`registerStroker` rejects a null factory, then inserts or replaces the named
map entry. `getStroker` checks this map before its built-in-name dispatch. A
registered factory is wrapped in `UserDefinedStrokerWrapper`.

`UserDefinedStrokerWrapper::stroke` receives an internal path plus width,
pixel-size/smoothing/pressure parameters, converts the path points into a
`vector<graphics::InkPoint>`, and invokes the user `IStroker` virtual method.
The user implementation writes to a `graphics::IPath`. `StrokerWrapper` also
has setters for width, pixel size, smoothing, and real-pressure factor at
`0x4d6eb4` through `0x4d6ee4`.

The practical inference is:

1. register a name such as `MyBrush` on the renderer;
2. select it through `-myscript-pen-brush: MyBrush`; and
3. return custom vector geometry through `IPath`.

The registry-first lookup and CSS parser make this chain strongly supported by
static code, but it has not been exercised on the Kobo. The exact 32-bit C++
ABI, class declarations, `InkPoint` field layout, and renderer acquisition
path still need to be recovered or matched against a compatible MyScript SDK.

This interface can implement fundamentally different vector nib geometry. It
does not by itself establish support for textured graphite, watercolor
diffusion, particles, per-pixel blending, or persistent simulation state.
Those effects may exceed what `graphics::IPath` can express.

## Ink and document persistence

`libMyScriptInk.so` contains three identifiable digital-ink codecs:

| Format | Evidence |
| --- | --- |
| BINK | magic `BINK`, description `Binary digital ink`, MIME `application/vnd.myscript.bink` |
| JINK example format | description `MyScript Example Digital Ink Format` |
| JINKSON | description `JSON based digital ink`, MIME `application/vnd.myscript.jinkson`, extension `.jink` |

The module contains validation and error paths for stroke counts, sample
counts, X/Y values, stroke formats, named channels, tags, transforms, active
and origin stroke lists, and `-myscript-pen-brush` metadata.

`libMyScriptDocument.so` identifies higher-level containers including:

```text
MyScript Interactive Ink Document
application/vnd.myscript.iink
MyScript Nebo Document
MyScript Notes Document
ink.jink / ink.bink payloads
```

It also contains page/object/stroke mappings and the same pen-brush property.
The likely persistence model is therefore sampled ink plus style/document
metadata, with geometry regenerated by a renderer. That is an inference until
saved Kobo documents are decoded and compared before/after a controlled
stroke.

## What is realistically moddable

| Goal | Feasibility | Reason |
| --- | --- | --- |
| Reuse the visible popup frame | Medium | Kobo owns the Qt popup/controller, but its rows and state mapping are hard-coded; safest route is a parallel Kobo-styled popup or carefully extended model |
| Add more presets of existing brushes | Medium | CSS already expresses brush, width, and color; UI, selector persistence, and export CSS must all learn the new preset |
| Expose stock pressure curves | Medium-low risk | Existing Kobo menu and setters already implement eleven choices plus minimum pressure |
| Add custom pressure/velocity filtering | Medium to medium-high risk | Hook the Kobo sample path while continuing to use a stock MyScript stroker |
| Add a custom vector stroker | High risk, explicitly supported by architecture | Real registry exists, but headers/ABI, renderer lifetime, UI, persistence, and every rendering process must be handled |
| Add arbitrary raster/particle/wet-ink physics | Very high risk | Custom API outputs `IPath`; a parallel renderer/storage/export pipeline may be required |
| Replace recognition/document engine | Extreme | Crosses object engine, modules, document model, editor, renderer, and export services |

The menu and engine are separate projects. A new menu row does not register a
new renderer, and registering a renderer does not create a persistent Kobo
tool row.

## Cross-process requirement

Live editing happens in the notebook process, while the separate `iink`
executable applies editor CSS in `MyScriptExporterService::exportFile` and
`MyScriptThumbnailerService::thumbnail`.

A custom stroker registered only in Nickel may render correctly while drawing
but fail after reload, during thumbnail generation, or during export. A viable
mod needs one of these policies:

- register the same stroker in every process that constructs a MyScript
  renderer;
- materialize the custom result into a stock-compatible path/style before
  saving; or
- fall back to a named built-in stroker outside the live editor.

The first option is the most faithful but has the largest injection and ABI
surface. The second is attractive if the custom `IPath` can be committed as
ordinary document geometry. The third preserves file usability at the cost of
visual fidelity.

## Recommended proof-of-concept sequence

1. Decode one saved notebook and identify whether force/time/style are stored.
2. Reuse an existing stock brush and add only a custom pressure transform.
3. Verify draw, undo, erase, save/reopen, thumbnail, and export.
4. Recover the exact `IStrokerFactory`, `IStroker`, `InkPoint`, and `IPath`
   ABI from symbols, RTTI, vtables, or a version-matched MyScript SDK.
5. Register a trivial custom stroker that returns a simple vector path under a
   new name, without changing the popup yet.
6. Apply the name through temporary CSS and repeat the full lifecycle test.
7. Only then add a durable menu row and selector/persistence mapping.

## Binary Ninja provenance

The raw `libiink.so` view was analyzed as ARMv7/ELF at image base `0x10000`:

```text
SHA-256:       0dc02118b9b763450c228da1e632dadbcce5cfd2acb41a3ca76a2472aed71ac6
BN fingerprint: 7ab9ecbe55afc8a0db7a5d8b2893ed34ddbfdd660e9bceeb18656519d2a5380e
Functions:     37,715
```

The raw `libMyScriptInk.so` view was also analyzed at `0x10000`:

```text
SHA-256:       43a990653d54604f6147d4a59013b218840cf5c79895e3005fb4df9f4ba4e43d
BN fingerprint: 6b31f190c1a7e2e831ba3ed9501acca27a657eeedff37287acf2443e0cfc563f
Functions:     719
MyScriptInk data symbol: 0xbd480
```

For raw ELF views, Binary Ninja uses base `0x10000`; subtract `0x10000` to get
the ELF-relative VMA. Analysis reached the idle state in both views. No write
capabilities were enabled.

See also:

- `notes/CUSTOM_BRUSH_PHYSICS.md` for Kobo-side pressure and pointer hooks;
- `notes/BINARY_NINJA.md` for repeatable navigation entry points; and
- `notes/PEN_MENU.md` for the stock popup's construction and selector mapping.
