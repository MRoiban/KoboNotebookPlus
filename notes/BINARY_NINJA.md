# Binary Ninja workflow

Use the original extracted libraries as inputs and save analysis databases under
the Git-ignored `binary-ninja/` directory.

Binary Ninja loads both shared libraries below at image base `0x10000`. The
primary addresses in this guide are therefore the addresses to enter in Binary
Ninja; the parenthesized values are the corresponding ELF-relative VMAs/file
offsets recorded by the firmware survey.

## 1. Background catalogue

Open:

```text
extracted/rootfs/usr/local/Kobo/libiinknote.so
```

The loader should identify a 32-bit little-endian ARM ELF. Dynamic C++ symbols
are present even though the library is otherwise stripped. Save the database as:

```text
binary-ninja/libiinknote-4.38.23697.bndb
```

Start at these symbols/addresses:

```text
0x88c9c  BackgroundOptionsWidget::backgroundOptions()             (ELF 0x78c9c)
0x6b9fc  IInkNotePadWidget::setBackgroundType(QString const&)     (ELF 0x5b9fc)
0x4974c  BackgroundOptionsController::setBackgroundType(QString const&) (ELF 0x3974c)
0x8f288  qInitResources_iinknote_styles()                         (ELF 0x7f288)
0x92860  "DotNarrow"                                              (ELF 0x82860)
0x9286c  ":/images/mynotes/dot_narrow_background.png"             (ELF 0x8286c)
0x96fec  "kobo.backgroundType"                                    (ELF 0x86fec)
```

Additional custom-template findings:

```text
0x487d0  builds the hard-coded identifier -> paper-path QMap   (ELF 0x387d0)
0x51cb0  BackgroundWidget::setBackgroundType(QString const&)  (ELF 0x41cb0)
0x47310  backgroundOptions() PLT stub; two internal callers
0xb8b54  renderer QMap object                                 (ELF VMA 0xa8b54)
```

The PLT stub is referenced at `0x49a68` from
`BackgroundOptionsController::BackgroundOptionsController(QString)` and at
`0x8bf7e` from `BackgroundOptionsWidget::BackgroundOptionsWidget(QString
const&)`. This makes runtime catalogue extension possible without expanding the
ARM function body. The `BackgroundOption` implementation in `libnickel.so`
stores `type`, `path`, and `text` as three consecutive `QString` values at
offsets 0, 4, and 8.

The renderer setter inserts the device suffix before `.png` and loads the
result with `QPixmap`. See `notes/CUSTOM_TEMPLATES.md` for the external-slot
design derived from this behavior.

Recommended actions:

1. Open High Level IL for `backgroundOptions()`.
2. Find references to `DotNarrow` and the adjacent resource path.
3. Rename any recovered helper only with a `_candidate` suffix until its role is
   confirmed by another identifier or caller.
4. Follow the returned catalogue into `setBackgroundType()` and record the
   exact value passed to the note engine.
5. For pen-style integration in this same database, inspect the imported
   `IInkEditorCss::setCss` stubs at `0x457cc` and `0x45fa8`. Their call sites
   are annotated at `0x71158`, `0x6ea40`, and `0x69492`.

## 2. Pen preset resolver

Open:

```text
extracted/rootfs/usr/local/Kobo/libiinkuiref.so.1.0.0
```

Save the database as:

```text
binary-ninja/libiinkuiref-4.38.23697.bndb
```

Start at:

```text
0x33970  IInkEditorCss::getToolStyles()          (ELF 0x23970)
0x3398c  IInkEditorCss::setCss(Editor, float)    (ELF 0x2398c)
0x33e94  IInkEditorCss::setCss(Editor)           (ELF 0x23e94)
0x45cd8  complete 125-entry pen CSS table        (ELF/file offset 0x35cd8)
```

The CSS table is already enough to prove the serialized identifier mapping.
Use its references to understand when Kobo applies the table and whether the
float overload scales the widths before passing CSS to MyScript.

For construction of the visible pen popup itself, inspect
`IInkToolMenuController` in `libiinknote.so` and see `notes/PEN_MENU.md`.

For the adjacent stylus-pressure pipeline, built-in pressure curves, pointer
sample boundary, and feasibility of custom brush dynamics, see
`notes/CUSTOM_BRUSH_PHYSICS.md`.

## 2a. MyScript editor, renderer, and stroker registry

Open the original shared object directly:

```text
extracted/rootfs/usr/local/Kobo/libiink.so
```

Binary Ninja identifies an ARMv7 ELF at image base `0x10000`. Analysis of this
build produced 37,715 functions. Start with these named functions:

```text
0x436560  EditorImpl::pointerMove_
0x436a24  EditorImpl::pointerUp_
0x436fe8  EditorImpl::pointerDown_
0x43ac58  EditorImpl::pointerEvents
0x4c4f48  ToolControllerImpl::setToolStyle
0x4b10d0  RendererImpl::getStroker
0x4b1014  RendererImpl::unregisterStroker
0x4b15fc  RendererImpl::registerStroker
0x4d7040  UserDefinedStrokerWrapper::stroke
0x4d720c  InternalStrokerWrapper::stroke
0x4d86c4  atk::core::Stroker::strokePath
0xa95eb8  atk::core::Stroker::preprocessPath
0xbd65ec  IInkStrokeBuilderPriv::addSampleXYFT
```

`RendererImpl::getStroker` is the decisive custom-brush entry point. Follow
its map lookup before the built-in-name comparisons, then compare
`UserDefinedStrokerWrapper::stroke` with `InternalStrokerWrapper::stroke`.
The former calls a user `IStroker` with converted ink points and a
`graphics::IPath` output; the latter dispatches to MyScript's stock engines.

The built-in string table begins near:

```text
0xc28340  DynamicEnvelope
0xc28350  QuadBezier
0xc2835c  Polyline
0xc28368  FountainPen
0xc28374  CalligraphicQuill
0xc28388  CalligraphicBrush
0xc2839c  Qalam
0xc283a4  Highlighter
0xc283b0  FeltPen
0xc283b8  default
0xc283c0  dash
```

Raw-view provenance:

```text
File SHA-256: 0dc02118b9b763450c228da1e632dadbcce5cfd2acb41a3ca76a2472aed71ac6
BN fingerprint: 7ab9ecbe55afc8a0db7a5d8b2893ed34ddbfdd660e9bceeb18656519d2a5380e
```

## 2b. MyScript digital-ink provider

Open:

```text
extracted/rootfs/usr/local/Kobo/libMyScriptInk.so
```

This ARMv7 ELF has one meaningful exported data symbol, `MyScriptInk`, at
Binary Ninja address `0xbd480`. The module is loaded through
`libMyScriptEngine.so`; do not expect a conventional set of exported ink C
functions.

Useful strings establish the codec boundary:

```text
0x9df88  BINK
0x9df9c  Binary digital ink
0x9dfb0  application/vnd.myscript.bink
0x9ea2c  MyScript Example Digital Ink Format
0xa55c8  JSON based digital ink
0xa55e0  application/vnd.myscript.jinkson
0xa5604  .jink
```

Raw-view provenance:

```text
File SHA-256: 43a990653d54604f6147d4a59013b218840cf5c79895e3005fb4df9f4ba4e43d
BN fingerprint: 6b31f190c1a7e2e831ba3ed9501acca27a657eeedff37287acf2443e0cfc563f
Functions: 719
```

See `notes/MYSCRIPT.md` for the complete layer map, persistence formats,
custom-stroker interpretation, and cross-process constraints.

## 3. Export and thumbnail rendering

Open:

```text
extracted/rootfs/usr/local/Kobo/iink
```

Save the database as:

```text
binary-ninja/iink-4.38.23697.bndb
```

Binary Ninja identifies this executable as `linux-thumb2` and loads it at
image base `0x10000`. Start at:

```text
0x1b1e4  imported IInkEditorCss::setCss(Editor) stub       (ELF 0x0b1e4)
0x1f7d6  MyScriptExporterService::exportFile call site     (ELF 0x0f7d6)
0x237ac  MyScriptThumbnailerService::thumbnail call site   (ELF 0x137ac)
```

Only the default, no-float overload is imported here. Both rendering services
apply it before producing exports or thumbnails; all three locations are
annotated in the saved database.

## 4. Main executable

Open `extracted/rootfs/usr/local/Kobo/nickel` only when inspecting resource
registration. Its `qInitResources_resources()` function has ELF-relative VMA
`0x282d4`; record Binary Ninja's load base before translating that address.
Notebook paper extraction does not require manual carving in Binary Ninja;
`scripts/extract-nickel-rcc.py` performs the same resource-table traversal
reproducibly.

## First modification target

Use `IsometricGrid`, not a compressed dot or ruled slot. Its Condor paper PNG is
stored uncompressed and has a fixed 60,231-byte payload region. The offline test
replacement is 43,302 bytes, so it fits without moving any later RCC entries.
The patched `nickel` remains exactly the same total size as the original.
