# Changelog

All notable changes to KoboNotebookPlus will be documented in this file.

## [0.0.2-alpha] - 2026-07-17

### Fixed

- Preserve the configured eraser width whenever Nickel switches between pen,
  object eraser, brush eraser, or the stylus hardware eraser.
- Reapply the eraser policy after firmware tool activation, so the highlighted
  object/brush mode and the engine's actual erasing behavior stay synchronized.
- Coalesce a guarded zero-delay replay after tool activation to cover cached
  erasers which Nickel restyles or replaces later in the same event cycle.
- Verify that eraser state replay leaves the selected notebook layer unchanged.

### Changed

- Replace the 8,000-line umbrella translation unit and all `.cc.inc` fragments
  with explicit firmware-resolution, runtime, hook, entry-point, visibility,
  menu, page, eraser, and layer translation units.
- Centralize firmware addresses in an immutable pin inventory and keep only two
  audited process globals (`gPluginState` and NickelHook's `info`).
- Add `make check` for artifact, binary-parity, globals-budget, and release-
  package verifier tests.
- Add GitHub CI which verifies firmware ABI pins, performs a clean NickelTC
  build, checks preview ABI and loader-visible parity, validates the exact
  two-file installer, and records SHA-256 checksums.

### Verification

- Tested on Kobo Elipsa 2E (`condor`) firmware `4.38.23697`, including pen and
  hardware eraser switching, both eraser policies and widths, layer selection,
  isolated erasing, previews, covers, templates, page tools, and library asset
  filtering.
- The release installer remains firmware-specific and contains only the ARM
  plugin plus the checksum-pinned stock `libiinknote.so`.

## [0.0.1-alpha] - 2026-07-16

### Added

- Native-style Layers popup anchored below the Advanced Notebook toolbar.
- Native MyScript document layers with add, select, and delete controls.
- Independent writing and erasing on the selected layer, including active-layer
  restoration when reopening notebooks and changing tools.
- Notebook-ratio layer preview thumbnails with bounded caching and deferred
  generation.
- Brush and object eraser modes with a native five-step eraser-size selector.
- Persistent eraser-size preferences synchronized with the stylus hardware
  eraser.
- Automatic `1404 x 1872` custom paper templates and writable first-page
  notebook covers.
- Cover-aware notebook thumbnails and persisted cover previews.
- Page duplication and reordering controls with backup and rollback safeguards.
- Support-image filtering so plugin PNG assets stay out of Home/My Books while
  remaining available to notebook template and cover pickers.
- Firmware ABI, relocation, artifact, and ARM verification tooling for
  reproducible NickelTC builds.

### Compatibility and cautions

- Experimental alpha tested only on Kobo Elipsa 2E (`condor`) firmware
  `4.38.23697`.
- Uses private firmware APIs pinned to exact symbols, vtables, and instruction
  sequences. Do not install it on another device or firmware revision.
- Back up the Kobo database, settings, and notebooks before installation, and
  test first with disposable notebooks.
- Kobo firmware and MyScript libraries are not release assets and must be
  supplied from the user's matching firmware when creating an install package.
- KoboNotebookPlus is not affiliated with or endorsed by Rakuten Kobo or
  MyScript.
