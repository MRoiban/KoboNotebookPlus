# Notebook format findings

Use one entry per suspected field or structure. Keep conclusions tentative until a controlled comparison reproduces them.

## Finding template

```text
Offset/path:
Old value:
New value:
Experiment:
Observed effect:
Confidence:
Counterevidence:
Next experiment:
```

## Questions to answer

- Where is the background choice stored?
- Where are pen presets defined?
- Which component renders and serializes each stroke?

## Initial corpus findings (2026-07-13)

The initial backup contains eight Basic notebooks and 297 pages. Working copies were extracted under the Git-ignored `samples/existing/` directory. No source notebook was modified.

### Container and component map

Each `.nebo` file is a normal deflated ZIP archive with this recurring layout:

```text
meta.json
rel.json
index.bdom
pages/<page-id>/meta.json
pages/<page-id>/page.bdom
pages/<page-id>/ink.bink
pages/<page-id>/style.css   # absent on four observed pages
```

Observed signatures and versions:

```text
Root format-version: 4.0
Document_Version: 1.11
Application: iink 2.0.6
Device identifier: condor
Canvas geometry: 1404 x 1872 at 228 DPI
BDOM header: 42 44 4f 4d 00 02 ("BDOM", version field 2)
BINK header: 42 49 4e 4b 00 05 ("BINK", version field 5)
```

All observed page metadata has `Type: "Raw Content"`. The Kobo database agrees: all eight rows use `ContentType = 6` and MIME type `application/vnd.myscript.nebo+raw`.

### Finding BG01: page background choice

```text
Path: pages/<page-id>/meta.json
Field: iink-user-metadata.kobo.backgroundType
Observed values: Dot2 (250 pages), DotWide (6 pages), MarginNarrow (41 pages)
Experiment: structural survey of eight notebooks / 297 pages
Observed effect: every page has exactly one backgroundType value
Confidence: High that this is the serialized background selection
Counterevidence: none in the current corpus
Next experiment: change one disposable page from Dot2 to another built-in background and diff only that page
```

The background value does not appear in `KoboReader.sqlite`. The database tracks the notebook and its page count, while the page-level choice lives inside the `.nebo` archive.

### Finding PEN01: active pen identifier

```text
Path: pages/<page-id>/ink.bink
Marker: ASCII string "active-pen-input"
Associated observed identifiers:
  fountain-2-0    281 pages
  highlighter-4-2   4 pages
  fountain-2-1      1 page
  fountain-1-0      1 page
No marker:           10 pages
Experiment: string-level structural survey of all 297 BINK members
Observed effect: 287 BINK files associate the active-pen marker with one of four identifiers
Confidence: High that BINK serializes the active pen identifier; medium on the numeric components' meaning
Hypothesis: <tool>-<width/preset index>-<color index>
Counterevidence: the current notebooks were not created as controlled pen comparisons
Next experiment: draw otherwise identical strokes with fountain-1-0 and fountain-2-0, then with fountain-2-0 and fountain-2-1
```

These pen identifiers do not appear in `KoboReader.sqlite`.

### Finding PEN02: reusable width definitions

There are 293 `style.css` members, and all 293 have the same SHA-256 hash:

```text
e82f2e2f79924de42a820d9affc197782eaf8fbf6dde2e9f7303dccc36d0aa89
```

The common stylesheet defines reusable MyScript pen widths, including:

```text
.pen-015  -> 0.080205
.pen-025  -> 0.187145
.pen-035  -> 0.334188
.pen-050  -> 0.481230
.pen-070  -> 0.748580
.pen-100  -> 1.069400

.brush-0125 -> 0.668375
.brush-0250 -> 1.336750
.brush-0500 -> 2.673500
.brush-0750 -> 4.010250
.brush-1125 -> 6.015375
.brush-1750 -> 9.357250
```

It also defines MyScript brush names such as `FeltPen`, `Highlighter`, and `Polyline`. Because the stylesheet is identical across pages using different active identifiers, it is a shared preset vocabulary rather than the sole record of the selected pen.

### Current answers

```text
Where is the background choice stored?
  In pages/<page-id>/meta.json at iink-user-metadata.kobo.backgroundType.

Where are pen presets defined?
  Reusable width/brush definitions are embedded in each page's style.css.
  The active page pen identifier is serialized in ink.bink.

Which component renders and serializes the stroke?
  The archive metadata identifies MyScript iink 2.0.6. Stroke data and active
  pen state are in BINK; page/document structure is in BDOM. Matching-firmware
  analysis shows that libiinkuiref.so.1.0.0 resolves pen identifiers to CSS,
  while libiinknote.so owns Kobo's notebook background catalogue and setters.
```

## Controlled testing-folder findings (2026-07-13)

Two new notebooks were captured in the verified `after-testing` snapshot. Both archives pass `unzip -t`.

### Finding BG02: test background identifiers

```text
Notebook named dot2:          2 pages, both backgroundType = DotNarrow
Notebook named MarginNarrow:  1 page, backgroundType = MarginNarrow
```

The archive filename is only a user-facing label; the internal page metadata is the authoritative background selection. In particular, `dot2.nebo` does not serialize `Dot2`—it serializes `DotNarrow` on both pages.

### Finding PEN03: controlled ballpoint width index

The page creation order in `rel.json` and page timestamps agrees with the requested test order:

```text
Dot page 1:     ballpoint-0-0
Dot page 2:     ballpoint-4-0
Margin page 1:  ballpoint-0-0
```

Each identifier occurs once in its page's `ink.bink`, associated with the `active-pen-input` marker. The varying file offsets are archive-data offsets, not stable patch locations.

```text
Hypothesis confirmed: the first numeric component is the ballpoint width/preset index.
Observed mapping: 0 = thinnest tested preset; 4 = thickest tested preset.
Firmware mapping: ballpoint width index 0 = 0.4; index 4 = 1.5.
Second numeric component: confirmed by matching firmware as the color index.
Confidence: High for both indices.
```

No readable floating-point width is stored beside the active identifier in BINK. The exact rendered width therefore appears to be resolved from the preset identifier by the MyScript/Kobo configuration rather than serialized as a readable CSS value at that location.

## Matching-firmware findings (2026-07-13)

### Finding PEN04: exact preset lookup table

The complete lookup table is embedded in `libiinkuiref.so.1.0.0` at `0x35cd8`.
This confirms both numeric fields and resolves every built-in preset:

```text
ballpoint widths:   0.4, 0.6, 0.8, 1.0, 1.5  (FeltPen)
fountain widths:    0.5, 1.0, 1.5, 2.0, 2.5  (FountainPen)
calligraphic widths:0.5, 1.0, 1.5, 2.0, 2.5  (CalligraphicQuill)
brush widths:       0.5, 1.0, 1.5, 2.0, 2.5  (CalligraphicBrush)
highlighter widths: 2, 4, 6, 8, 10            (FountainPen)

normal color index: 0=#000000, 1=#525252, 2=#868686, 3=#b2b2b2, 4=#dadada
highlighter alpha:  0=ff, 1=ad, 2=79, 3=4d, 4=25 (black base color)
```

This explains the controlled samples exactly: `ballpoint-0-0` is a 0.4 black
FeltPen, while `ballpoint-4-0` is a 1.5 black FeltPen.

### Finding BG03: built-in catalogue and native paper images

`libiinknote.so` owns the background catalogue and maps the serialized names to
Qt resource paths. For example, it pairs `DotNarrow` with
`:/images/mynotes/dot_narrow_background.png`. The large function
`BackgroundOptionsWidget::backgroundOptions()` at `0x78c9c` constructs the
selectable set, and `IInkNotePadWidget::setBackgroundType()` at `0x5b9fc`
applies the selected identifier.

The actual image payloads are embedded in `nickel`'s `default.rcc`. There are 36
Elipsa 2E variants with resource names ending in `_condor.png`; every extracted
file is a valid `1404 x 1872` grayscale/alpha PNG, matching the notebook canvas
exactly. They are locally available under the ignored directory
`extracted/resources/default-condor/images/mynotes/`.
