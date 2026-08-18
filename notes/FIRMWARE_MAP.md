# Firmware map

Record paths, hashes, file types, architecture details, strings, and Binary Ninja cross-references from the firmware version that exactly matches the device.

## Firmware identity

```text
Device firmware version:
Archive path:
Archive SHA-256:
Root filesystem path:
Architecture and bitness:
```

## Firmware 4.38.23697

```text
Device firmware version: 4.38.23697 (ae95f6a7d7)
Hardware family: kobo11
Archive path: firmware/original/kobo-elipsa2e-4.38.23697.zip
Archive size: 136533375 bytes
Archive MD5: 7a73311519583e75401f5e2e27011d0f
Archive SHA-256: c399fd885211de921307bcbfe12ba9255d77082f42742864fb582c00197a2b9a
Archive integrity: unzip -t passed
Update extraction path: extracted/update
Update extraction size: 139 MB
Root filesystem path: extracted/rootfs
Root filesystem size: 236 MB / 1679 files
Architecture and bitness: ELF 32-bit LSB ARM, EABI5, armhf dynamic linker
Internal manifest: all five listed boot/firmware images passed MD5 verification
```

### Initial Binary Ninja targets

| Component | Evidence | Relevant strings/resources | Role hypothesis | Confidence |
| --- | --- | --- | --- | --- |
| `usr/local/Kobo/nickel` | Main Kobo executable; ARM EABI5; stripped | Registers the top-level Qt `resources` bundle at `0x282d4`; embeds 23 child RCC bundles | Resource container and application bootstrap | High |
| `usr/local/Kobo/libnickel.so.1.0.0` | Main Kobo UI library; ARM EABI5; stripped; Build ID `38e89c617f414b62c6e06156bcf3645778041717` | General Kobo UI logic; notebook-specific catalogue is in `libiinknote.so` | Kobo-side UI integration | Medium |
| `usr/local/Kobo/iink` | MyScript executable; ARM EABI5/Thumb-2; stripped; Build ID `d9ba627daf4ee05fdf8542418c6452e0a478ca7f` | Imports the default `IInkEditorCss::setCss` overload from export and thumbnail services | Export/thumbnail rendering integration | High |
| `usr/local/Kobo/libiink.so` | MyScript iink library; ARM EABI5; stripped; Build ID `f960db22f0d4598d46d9a225042c067e49fec570` | Editor pointer methods, tool CSS, built-in stroker dispatch, and user-defined `IStrokerFactory` registry | Core editor and path renderer | High |
| `usr/local/Kobo/libiinknote.so` | Note-specific iink library; ARM EABI5; stripped; Build ID `03b40320ba071485bffeb1a2e5889c4dcf50098b` | Background identifiers, image paths, `kobo.backgroundType`, and menu/controller functions | Notebook background catalogue and selection logic | High |
| `usr/local/Kobo/libiinkuiref.so.1.0.0` | iink UI reference library; ARM EABI5; stripped; Build ID `08a61b64818dc76a54a14ac4d8d2932bdd44770c` | Complete pen CSS table at ELF/file offset `0x35cd8` (Binary Ninja `0x45cd8`); `IInkEditorCss` functions | Pen identifier-to-style resolution | High |
| `usr/local/Kobo/libMyScriptInk.so` | MyScript Ink library; ARM EABI5; stripped; Build ID `146b14c71b5136676786fa0ab5b2ff99c7fad2d1` | `MyScriptInk` module symbol; BINK, JINK, and JINKSON codecs; stroke/channel/tag validation | Digital-ink model and serialization provider | High |
| `usr/local/Kobo/libMyScriptDocument.so` | MyScript Document library; ARM EABI5; stripped; Build ID `338832c4572c5aeeebdf5e8f4792b71943fb3781` | Interactive Ink/Nebo/Notes document names, `ink.jink`/`ink.bink`, page/object/stroke mappings | Higher-level document container provider | High |

### Pen preset implementation

`libiinkuiref.so.1.0.0` contains one complete CSS string at VMA/file offset
`0x35cd8`. The table confirms the serialized identifier shape
`<tool>-<width-index>-<color-index>`.

| Identifier prefix | MyScript brush | Widths for indices 0, 1, 2, 3, 4 |
| --- | --- | --- |
| `ballpoint` | `FeltPen` | 0.4, 0.6, 0.8, 1.0, 1.5 |
| `fountain` | `FountainPen` | 0.5, 1.0, 1.5, 2.0, 2.5 |
| `calligraphic` | `CalligraphicQuill` | 0.5, 1.0, 1.5, 2.0, 2.5 |
| `brush` | `CalligraphicBrush` | 0.5, 1.0, 1.5, 2.0, 2.5 |
| `highlighter` | `FountainPen` | 2, 4, 6, 8, 10 |

For the first four tools, color indices 0 through 4 map to `#000000`,
`#525252`, `#868686`, `#b2b2b2`, and `#dadada`. Highlighter indices map
to black with alpha values `ff`, `ad`, `79`, `4d`, and `25`.

The saved Binary Ninja database loads this library at image base `0x10000`, so
the GUI address is the ELF-relative address plus `0x10000`.

Binary Ninja entry points:

| Binary Ninja | ELF-relative | Symbol | Why it matters |
| --- | --- | --- | --- |
| `0x33970` | `0x23970` | `IInkEditorCss::getToolStyles()` | Returns the preset-style collection |
| `0x3398c` | `0x2398c` | `IInkEditorCss::setCss(Editor, float)` | Main CSS construction/application path |
| `0x33e94` | `0x23e94` | `IInkEditorCss::setCss(Editor)` | Smaller overload/wrapper |
| `0x45cd8` | `0x35cd8` | Complete 125-entry CSS table | All 5 tools × 5 widths × 5 colors |

Binary Ninja finds one direct code reference to the CSS table: `0x3397c`
inside `IInkEditorCss::getToolStyles()`. The main `setCss(Editor, float)`
function has no callers inside this module, which is consistent with an
exported entry point consumed by another Kobo/UI component. The next resolver
step is therefore to find imports or call sites for this symbol in the
dependent libraries rather than searching deeper in this table owner.

### MyScript renderer and ink modules

`libiink.so` is the higher-level C++ editor/renderer. It directly depends on
`libMyScriptEngine.so`; the engine in turn uses `dlopen`/`dlsym` to load module
providers such as `libMyScriptInk.so` and `libMyScriptDocument.so`.

The renderer has an explicit user-defined stroker registry:

| Binary Ninja | Function | Why it matters |
| --- | --- | --- |
| `0x4b15fc` | `RendererImpl::registerStroker` | Installs/replaces a named `IStrokerFactory` |
| `0x4b1014` | `RendererImpl::unregisterStroker` | Removes a named factory |
| `0x4b10d0` | `RendererImpl::getStroker` | Checks registered names before built-in brush dispatch |
| `0x4d7040` | `UserDefinedStrokerWrapper::stroke` | Converts points and calls the custom stroker with an `IPath` output |
| `0x4d720c` | `InternalStrokerWrapper::stroke` | Dispatches the built-in MyScript stroker enum |

The built-ins recognized in this firmware include `DynamicEnvelope`,
`QuadBezier`, `Polyline`, `FountainPen`, `CalligraphicQuill`,
`CalligraphicBrush`, `Qalam`, `Highlighter`, `Square`, `FeltPen`, `default`,
and `dash`.

This makes a custom vector-path stroker architecturally possible, but still a
high-risk Kobo mod: the firmware does not ship headers, ABI/layout recovery is
unfinished, and the same custom renderer must be available to the separate
thumbnail/export process. See `notes/MYSCRIPT.md` and
`notes/CUSTOM_BRUSH_PHYSICS.md`.

### Background catalogue implementation

`libiinknote.so` owns the identifier-to-resource catalogue. Its read-only data
contains entries such as `DotNarrow` at ELF-relative `0x82860`, followed by
`:/images/mynotes/dot_narrow_background.png` at ELF-relative `0x8286c`, plus
the complete set of ruled, margin, grid, planning, music, handwriting, and
storyboard backgrounds.

The saved Binary Ninja database also loads this library at image base
`0x10000`.

Binary Ninja entry points:

| Binary Ninja | ELF-relative | Symbol | Why it matters |
| --- | --- | --- | --- |
| `0x88c9c` | `0x78c9c` | `BackgroundOptionsWidget::backgroundOptions()` | Large (`0x3040` bytes) function that builds the selectable catalogue |
| `0x4974c` | `0x3974c` | `BackgroundOptionsController::setBackgroundType(QString const&)` | Controller setter |
| `0x6b9fc` | `0x5b9fc` | `IInkNotePadWidget::setBackgroundType(QString const&)` | Applies a selected background to the note pad |
| `0x8f288` | `0x7f288` | `qInitResources_iinknote_styles()` | Registers note-specific styles, not the paper PNG payloads |
| `0x92860` | `0x82860` | `"DotNarrow"` | Catalogue identifier string |
| `0x9286c` | `0x8286c` | `":/images/mynotes/dot_narrow_background.png"` | Resource path paired with `DotNarrow` |
| `0x96fec` | `0x86fec` | `"kobo.backgroundType"` | Serialized notebook metadata key |

The renderer uses a second hard-coded structure that is independent of the
visible catalogue:

| Binary Ninja | ELF-relative | Location | Why it matters |
| --- | --- | --- | --- |
| `0x487d0` | `0x387d0` | Static initializer | Builds the identifier-to-paper-path `QMap` |
| `0x51cb0` | `0x41cb0` | `BackgroundWidget::setBackgroundType(...)` | Looks up the identifier, inserts `_<device>` before `.png`, and loads it with `QPixmap` |

Because `QPixmap` receives the constructed path directly, one existing row can
be redirected to an absolute `/mnt/onboard` path. The verified offline design
converts `IsometricGrid` into an external `Custom Paper 1` slot using three
same-length string replacements. See `notes/CUSTOM_TEMPLATES.md` and
`scripts/make-custom-template-slot.py`.

### Pen CSS integration in `libiinknote.so`

The notebook library imports both `IInkEditorCss::setCss` overloads. Binary
Ninja resolves the no-float import stub at `0x457cc` and the float-overload
stub at `0x45fa8`. Their call sites are:

| Binary Ninja call site | ELF-relative | Containing function | Observed role |
| --- | --- | --- | --- |
| `0x71158` | `0x61158` | `IInkNotePadWidget` constructor | Applies the default editor CSS while constructing the note pad |
| `0x6ea40` | `0x5ea40` | `sub_6e8e8` | Reapplies the default CSS; helper purpose still needs caller analysis |
| `0x69492` | `0x59492` | `IInkNotePadWidget::setLineSpacing(...)` | Calls the float overload when line spacing changes |

These cross-references show that pen CSS is applied by the notebook UI layer,
including during construction and after a layout change. The three call sites
are commented in `libiinknote-4.38.23697.bndb`.

`sub_6e8e8` has one direct caller, the 16-byte `sub_6f350`, and Binary Ninja
finds no direct callers or data references for that wrapper. This is not enough
evidence to rename either function; continue with the `iink` executable's
imports/call sites instead.

### Export and thumbnail CSS integration in `iink`

Binary Ninja identifies `iink` as `linux-thumb2` and loads it at image base
`0x10000`. It imports only the default, no-float `IInkEditorCss::setCss`
overload.

| Binary Ninja | ELF-relative | Location | Observed role |
| --- | --- | --- | --- |
| `0x1b1e4` | `0x0b1e4` | Imported `setCss(Editor)` stub | Entry point used by both rendering services |
| `0x1f7d6` | `0x0f7d6` | `MyScriptExporterService::exportFile(...)` | Applies default editor CSS before export rendering |
| `0x237ac` | `0x137ac` | `MyScriptThumbnailerService::thumbnail(...)` | Applies default editor CSS before thumbnail rendering |

This confirms the preset CSS is used consistently for live notebook editing,
file export, and thumbnail generation. The three locations are commented in
`iink-4.38.23697.bndb`.

### Embedded notebook resources

`nickel` registers its top-level Qt resource table from
`qInitResources_resources()` at `0x282d4`. For this firmware, the table
addresses are:

```text
resource data:   0x00028b60
resource names:  0x015041a8
resource tree:   0x01504828
RCC version:     1
embedded RCCs:   23
```

The reusable extractor is `scripts/extract-nickel-rcc.py`. The notebook paper
payloads for the Elipsa 2E are in the embedded `default.rcc` under resource
names ending in `_condor.png`. Extraction produced 36 valid PNG files, each
`1404 x 1872`, under the ignored directory
`extracted/resources/default-condor/`. The similarly named generic assets in
`daylight.rcc` are `1440 x 1920` and do not match this device's canvas.

Reproduce the extraction with:

```sh
scripts/extract-nickel-rcc.py \
  extracted/rootfs/usr/local/Kobo/nickel \
  --bundle default.rcc \
  --prefix images/mynotes/ \
  --contains _condor.png \
  --output extracted/resources/default-condor
```

### First size-preserving replacement candidate

`images/mynotes/isometric_grid_background_condor.png` is the safest initial
paper slot. Unlike most paper images, it is stored without Qt zlib compression
and provides a 60,231-byte fixed payload region. The generated test asset at
`patches/assets/test-isometric-grid.png` is a native-size `LA` PNG and occupies
43,302 bytes.

The replacement mode in `scripts/extract-nickel-rcc.py` writes the new length
and payload, then zero-pads the unused capacity so every later RCC offset and
the total ELF size remain unchanged. An ignored offline copy has been produced
and successfully re-extracted for byte-for-byte verification. It has not been
packaged or installed on the device.

## Candidate components

| Component | Evidence | Relevant strings/resources | Role hypothesis | Confidence |
| --- | --- | --- | --- | --- |
| | | | | |

Keep reverse-engineering labels suffixed with `candidate` until device behavior confirms them.

## Binary Ninja analysis

Store local Binary Ninja databases under `binary-ninja/`. They are analysis
artifacts and should not be committed. The local databases currently contain
comments at the background catalogue, pen resolver, notebook integration, and
export/thumbnail locations documented above. Publish reusable findings as both
Binary Ninja GUI and ELF-relative addresses, plus byte patterns, patch
definitions, and notes.
