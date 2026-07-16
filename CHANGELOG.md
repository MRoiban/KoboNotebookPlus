# Changelog

All notable changes to KoboNotebookPlus will be documented in this file.

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
