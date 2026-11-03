# Custom notebook templates plugin

This directory contains the KoboNotebookPlus NickelHook plugin.

See the [repository README](../../README.md) for compatibility, safety,
firmware prerequisites, verification, build, and usage instructions.

The build is source-only. `package/libiinknote.so`, compiled ELF files, and
`KoboRoot.tgz` are deliberately excluded from version control because they
contain or depend on proprietary Kobo/MyScript firmware artifacts.


## Performance diagnostics and kill switches

Optional file:
`/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/perf-experiment.txt`.
Use whitespace-separated `key=value` tokens (spaces or newlines):

| Key | Values | Default when absent |
| --- | --- | --- |
| `move` | `position`, `stock` | `position`: navigation chrome only |
| `dup` | `setpart`, `stock` | `setpart`: select clone, then sync position |
| `layer-activate` | `none`, `stock` | `none`: no forced refresh |
| `layer-add` | `none`, `stock` | `none`: no forced refresh (the new layer is empty) |
| `settle` | `1`, `0` | `0`: paint observer disabled (also times notebook open) |
| `prewarm` | `1`, `0` | `1`: notebook-open pre-warm after boot |
| `profile-open` | `1`, `0` | `0`: sampling profiler for notebook open off |

Use `move=stock dup=stock layer-activate=stock layer-add=stock` to restore
the old refresh calls. Layer delete and cover changes always keep their full
refresh: they change what is drawn, and delete follows a modal dialog whose
ghosting the full refresh clears. Moves and duplicates also fall back automatically if the optional
`positionUpdated` pin is missing; layer activation needs no optional pin.
Synchronous `perf: notebook-edit` trace records are always emitted, with
`variant=position|setpart|none|stock|stock-fallback` identifying the actual
path. Add `settle=1` to enable extra `-settle` paint records; without it no
paint callback or event filters are installed. Settled paint is not a measure
of physical panel completion.

**Notebook-open pre-warm** (`prewarm`, on by default). Nickel loads its
notebook engine only at the first notebook open after boot, so that open paid
for loading and linking it, for this plugin's delayed hook setup, and for
decoding the page background. The plugin now, after boot:

1. at 12 s asks the kernel to read the iink/MyScript libraries ahead
   (`posix_fadvise`, asynchronous; ~2–5 ms on the GUI thread);
2. from 20 s, once there has been no touch or key input for 3 s, loads
   `/usr/local/Kobo/libiinknote.so` itself (~0.7 s, once; Nickel's own plugin
   loader then reuses it), which also runs the plugin's delayed hook setup;
3. 1.5 s later replays the page backgrounds of the two most recently opened
   notebooks (recorded in `templates/prewarm.txt`) through Kobo's own
   `BackgroundWidget::setBackgroundType` on a hidden widget, filling Qt's
   pixmap cache (~130 ms). Skipped if a notebook was already opened.

Measured cold first open, tap to settled screen, same notebook, 3 boots each:
**1876 → 1147 ms** median (−39%); a warm reopen is ~720 ms. `profile-open=1`
records 1 ms stack samples of the open to `templates/open-profile.txt`
(see `notes/NOTEBOOK_OPEN_PROFILE.md`).

The file is read at most once per operation, only while onboard is mounted as
vfat, with a 256-byte limit. Missing/unreadable files, malformed tokens,
unknown keys/values, duplicate keys, and oversized files silently use all
defaults (including diagnostics off). A valid partial file overrides only the
specified keys. Remove the file to restore the shipped defaults.
