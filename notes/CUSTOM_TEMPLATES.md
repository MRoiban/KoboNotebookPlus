# Custom notebook templates

## Result

Firmware 4.38.23697 does not expose a directory that Nickel scans for notebook
templates. Both halves of the feature are hard-coded in `libiinknote.so`:

1. `BackgroundOptionsWidget::backgroundOptions()` builds the visible menu as a
   static `QVector<BackgroundOption>`.
2. The initializer at Binary Ninja `0x487d0` builds a separate static
   `QMap<QString, QString>` from background identifiers to base PNG paths.

`BackgroundWidget::setBackgroundType()` at Binary Ninja `0x51cb0` looks up the
identifier, inserts `_<device>` before `.png`, and passes the resulting path to
`QPixmap`. On the Elipsa 2E, `<device>` is `condor`.

The corresponding annotations are saved locally in
`binary-ninja/libiinknote-4.38.23697-custom-templates.bndb`.

A direct binary patch for a genuinely additional row would require expanding
both hard-coded collections and adding new strings/code. A less invasive route
is now confirmed: the two internal callers of `backgroundOptions()` use its PLT
stub at Binary Ninja `0x47310`, so a NickelHook plugin can call the original and
append rows at runtime. The plugin can also add the matching identifiers and
external paths to the existing renderer map during startup. This preserves all
built-ins and does not require adding PNGs to the Qt resource tables.

The additive prototype is under `mods/custom-notebook-templates/`. It is pinned
to this firmware's `backgroundOptions()` ELF VMA (`0x78c9c`) and renderer-map
VMA (`0xa8b54`) and refuses to initialize unless the original map still has 36
entries. It was built successfully with NickelTC, packaged with the original
`libiinknote.so`, byte-verified after extraction, and staged on the device on
2026-07-13. Runtime confirmation succeeded: Test Paper appeared alongside all
built-ins and rendered correctly.

The follow-up one-file build automatically discovers ordinary lowercase PNGs
in
`/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/`. It derives the
menu label from the filename, generates a fitted white-padded `280 x 280`
picker icon, and maintains the required full-resolution `_condor.png` renderer
copy itself. A native `1404 x 1872` file named `Daily Planner.png` was installed
without a manifest or user-supplied icon and confirmed working on-device. The
resulting user workflow is copy one PNG, safely eject, and fully restart the
Kobo.

## External custom slot

The lower-risk design converts the existing `IsometricGrid` row into one
external slot while keeping its serialized identifier unchanged. Three
same-length strings are replaced in `libiinknote.so`:

| Purpose | Original | Replacement | Bytes |
| --- | --- | --- | --- |
| Renderer base path | `:/images/mynotes/isometric_grid_background.png` | `/mnt/onboard/.kobo/custom/template_custom1.png` | 46 |
| Menu icon | `:/images/mynotes/isometric_grid_bg_icon.png` | `/mnt/onboard/.kobo/custom/template_icon.png` | 43 |
| Menu label | `Isometric Grid` | `Custom Paper 1` | 14 |

The equal lengths are important because Kobo passes compile-time lengths to
`QString::fromLatin1_helper()`. In-place equal-length replacements preserve the
ELF size and avoid patching instructions or relocating data.

After the patched library is installed, the device files are:

```text
/mnt/onboard/.kobo/custom/template_custom1_condor.png
/mnt/onboard/.kobo/custom/template_icon.png
```

The first image must be a native `1404 x 1872` Condor paper PNG. The icon can
be a smaller PNG; it is loaded directly without a device suffix. Both files
live on user-visible storage, so they can be replaced over USB without another
firmware patch.

Create the offline patched library with:

```sh
scripts/make-custom-template-slot.py \
  extracted/rootfs/usr/local/Kobo/libiinknote.so \
  patches/work/libiinknote-custom-slot.so
```

The script is locked to the exact SHA-256 of the 4.38.23697 library, requires
each source string to occur once, and refuses any replacement that changes
length.

The verified offline output is:

```text
source SHA-256:  f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea
patched SHA-256: 8073e5b3dd4a32daf4402693d72e0fc26f418a336768f0576da04e098b29d3d5
source/output size: 626980 bytes
```

An ignored device-file layout has also been staged under
`patches/work/device-files/.kobo/custom/`. Reproduce it from the conspicuous
test template with:

```sh
mkdir -p patches/work/device-files/.kobo/custom
cp patches/assets/test-isometric-grid.png \
  patches/work/device-files/.kobo/custom/template_custom1_condor.png
magick patches/assets/test-isometric-grid.png \
  -thumbnail 280x280 -gravity center -background white -extent 280x280 \
  patches/work/device-files/.kobo/custom/template_icon.png
```

## Expected behavior and limitations

- New pages selected as `Custom Paper 1` still serialize
  `kobo.backgroundType = IsometricGrid`.
- Existing `IsometricGrid` pages will also use the external custom paper.
- If the external PNG is absent or unreadable, `QPixmap` will be empty; the
  likely result is a blank background. Confirm this on a disposable notebook.
- Syncing the notebook to an unpatched Kobo will show its normal Isometric Grid
  paper because the custom asset itself is not stored in the `.nebo` archive.
- Export and thumbnail behavior still requires a device test.
- Adding multiple custom slots can reuse the same method with other expendable
  built-in identifiers, but each slot needs three carefully length-matched
  replacements.

The external `IsometricGrid` slot patch was installed on the Elipsa 2E and the
custom paper was confirmed working. The first additive-plugin update restores
the original library atomically, installs the plugin, and adds a distinct
`Custom_TestPaper` manifest entry.
