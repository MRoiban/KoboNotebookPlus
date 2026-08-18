# Writable custom notebook covers

## Recovered implementation path

Kobo stores paper choice per page. The notebook's final page remains the source
for later page creation, so a cover at page zero does not propagate its paper.

MyScript's public `ContentPackage` interface can create a `Raw Content` part,
but it appends the part and exposes no reorder method. Firmware 4.38.23697's
bundled `libiink.so` contains the lower-level exported operation needed for a
true move:

```text
atk::core::Document::movePage(int from, int to)  ELF VMA 0x9f09e4
```

The plugin obtains the current `EditorImpl`, `ContentPartImpl`, and
`ContentPackageImpl`, creates a normal page through
`ContentPackageImpl::createPart("Raw Content")`, gets its underlying
`atk::core::Document`, and calls `movePage(last, 0)`. This retains every
existing page object and page ID; it does not clone pages or edit `index.bdom`,
`rel.json`, or ZIP members.

## User flow

1. Copy one native `1404 x 1872` PNG to
   `/mnt/onboard/.kobo/iink/custom-notebook-templates/covers/`.
2. Restart the Kobo.
3. In an open notebook, choose **Change notebook cover**.
4. Choose a cover from the covers-only picker.
5. The plugin saves and backs up the notebook, appends a writable page, applies
   the selected background, moves the page to index zero, saves, and validates
   the final page count and index.
6. If page zero already has a `Custom_Cover_*` background, only its background
   changes; a second page is not inserted.
7. In My Notebooks, Kobo/MyScript supplies a page-zero ink render on white but
   omits plugin-defined backgrounds. A guarded parser callback converts the
   render's darkness into an ink mask and composites it over the matching
   clean custom-cover PNG. A firmware-pinned `VolumePixmapView::loadCover()`
   hook may reuse that combined image for the notebook card while the `.nebo`
   mtime and size still match. The live lookup resolves the Volume's exact
   `.nebo` path, including subfolders. A notebook with no custom cover is
   passed through unchanged. Before the first ink render is available, the
   card immediately uses the clean cover PNG; the combined cover-and-writing
   image replaces it after Kobo renders the page.

## Safety properties

- Exact `libiinknote.so` and `libiink.so` symbol VMAs are checked before the
  cover hooks become active.
- Hook replacements remain transparent pass-throughs unless all checks pass.
- Cover detection opens the `.nebo` ZIP read-only and requires exactly one
  `Custom_Cover_*` page marker. Missing, malformed, busy, or ambiguous archives
  use Kobo's original thumbnail.
- Detection verdicts are cached in memory per canonical `.nebo` path, keyed to
  the notebook's modification time and size, so unchanged notebooks are not
  rescanned on every grid reload. Negative verdicts are cached the same way;
  indeterminate passes (archive busy or unreadable) are never cached. The
  notebook file is stat-checked before and after each scan and the verdict is
  only cached when both agree, and the plugin drops the entry itself after it
  changes a notebook's cover. Rendered page-zero previews are cached per
  notebook and cover type. They survive the final `.nebo` timestamp/size
  update which can occur after MyScript emits its completed render, and are
  replaced by the next render. A plugin-driven cover change clears them
  immediately. The cache is bounded to 64 MiB with least-recently-used
  eviction. Both caches sit behind one mutex that is never held across archive
  scans or firmware calls; all scanning stays synchronous on the calling
  thread.
- The source notebook is saved and copied to
  `/mnt/onboard/.kobo/iink/custom-notebook-templates/covers/backups/` before
  mutation.
- Backup names end in `.nebo.backup`, preventing Kobo from indexing them as
  duplicate notebooks. Legacy `.nebo` backups in that directory are renamed
  on plugin startup.
- The operation is refused if the path is not a real onboard `.nebo`, storage
  cannot be checked, free space is insufficient, the backup fails, or page and
  package counts disagree.
- The installation archive also restores the exact stock firmware
  `libiinknote.so`; NickelHook's failsafe and uninstall flag remain available.
- No boot, kernel, recovery, partition-table, or database writes are involved.

## Required disposable-notebook test

- Add a conspicuous cover PNG and restart.
- Create a new notebook with two differently marked pages.
- Choose the cover and verify it becomes page one in the UI.
- Write ink on the cover and on both original pages.
- Close and reopen the notebook; confirm order, ink, and backgrounds.
- Add a new page and confirm it inherits the former last page's paper, not the
  cover.
- Return to **Change notebook cover**, select another cover, and confirm the
  page count does not increase.
- Return to My Notebooks and confirm thumbnail generation.
- Connect USB and verify a new complete backup exists under `covers/backups`.
- Only after all checks pass, repeat on a nonessential unsynced notebook.
