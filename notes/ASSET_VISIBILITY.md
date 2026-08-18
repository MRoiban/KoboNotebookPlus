# Notebook support assets and Nickel library indexing

## 2026-07-25 timing-independent storage fix

The dynamic `FeatureSettings::excludeSyncFolders()` hook is valid but can be
installed after Nickel has already obtained the exclusion list for its first
filesystem scan. The canonical support root is therefore now
`/mnt/onboard/.kobo/iink/custom-notebook-templates/`. Nickel hard-codes
`(\.kobo|koboExtStorage)/iink` in `findFilesToParse()` before consulting
plugins, so images under the new root cannot become Home/My Books content even
when scanning starts first.

At plugin initialization, an existing `/mnt/onboard/.kobo/custom/` tree is
moved to the new root and absolute manifest paths are rewritten. The legacy
scanner and backing-file hooks remain enabled only to reconcile already
imported rows safely. Device deployment also moves the tree before the reboot,
so stale legacy rows point to absent paths while Nickel performs its normal
removal workflow.

## Root cause on firmware 4.38.23697

The notebook plugin intentionally stores templates, covers, rendered cover
previews, layer previews, JSON sidecars, and its trace beneath
`/mnt/onboard/.kobo/custom/`. Nickel does **not** ignore that parent by
default.

The read-only pre-install database snapshot at
`backups/device-layer-popup-performance-preinstall-20260716-011034/`
contains 59 `content` rows whose IDs point beneath `.kobo/custom`: 29
top-level support images (`ContentType=6`, `image/png`), their 29 page rows
(`ContentType=9`), and `plugin-status.txt`. This is why PNG templates, covers,
and generated previews appear in Home/My Books.

Binary Ninja and ARM disassembly were checked against
`libnickel.so.1.0.0` SHA-256
`577235f308b4dea1c150caf87a376ecf34c8ecab04aacee0806feddd9e268744`.
Addresses below are ELF VMAs; add `0x10000` for the Binary Ninja view.

- `FeatureSettings::excludeSyncFolders()` is exported at `0xa04650`, size
  `0xb8`, and returns the existing `ExcludeSyncFolders` setting.
- `SyncFileSystemCommand::findFilesToParse()` at `0x89b450` calls that getter
  through its PLT/GOT entry at `0x89b518`, so `nh_dlhook` can interpose this
  call. The relocation is an `R_ARM_JUMP_SLOT`; this is not the failed
  intra-libiink direct-call case.
- The result is split into expressions at `0x89b51c..0x89b53e`. Each is
  wrapped as `^/mnt/(sd|onboard)/%1$` beginning at `0x89b966` and matched
  before descending into a directory.
- Stock exclusions enumerate selected `.kobo` children (`articles`,
  `assets`, `images`, `kepub`, `screensaver`, `slideshow`, `dict`,
  `certificates`, `webcache`, `webstorage`, `sunpinyin`, `iink`, and
  `markups`), but omit `.kobo/custom`.

Moving data to a nested `.adds` directory is not a solution. The stock
expression `\.add` is anchored to the storage root, so it matches only
`/mnt/onboard/.add`, not `.kobo/custom/.adds`; the former is also outside the
allowed plugin write scope.

## Failed first fix and deletion root cause

The first deployed fix installed only an early libnickel GOT hook for
`FeatureSettings::excludeSyncFolders()` and appended this expression:

```text
(\.kobo|koboExtStorage)/custom
```

The scanner exclusion itself worked, but the assumption that stale-row
reconciliation was database-only was wrong. The first sync removed every
existing `.kobo/custom` library row and physically deleted its backing PNG or
text file. The JSON sidecars and `.nebo.backup` files survived because Nickel
had never imported them as supported content.

Binary Ninja proof, with BN addresses equal to the ELF VMA plus `0x10000`:

- `SyncFileSystemCommand::finalizeSync()` at ELF `0x89c004` calls
  `VolumeManager::removeVolume()` at `0x89c46a`.
- `removeVolume()` at `0xa6f70c` forces `removeBackingFile=true` at
  `0xa6f70e` and tail-calls `removeBook()`.
- `removeBook()` at `0xa6f514` forwards that flag to
  `removeCommonBookData()` at `0xa6f540`.
- `removeCommonBookData()` is exported at `0xa6ec74`, size `0x8a0`. Its flag
  test at `0xa6ed50..0xa6ed54` enters the physical-delete path only for true.
- That path obtains the Volume content ID, strips its seven-character
  `file://` prefix, constructs a `QFile`, and calls static `QFile::remove()`
  at `0xa6ef5e`. Thus a content ID such as
  `file:///mnt/onboard/.kobo/custom/templates/Notes.png` becomes the exact
  mounted source path and is deleted.
- `removeImagesByVolume(..., true)` removes only derived `.kobo-images` cache
  entries; it is not the source-file deletion.

The mounted post-incident database contained zero `.kobo/custom` rows, and
the corresponding source PNG directories contained zero PNGs. The pre-sync
snapshot proves the deleted names and sizes. Recovery copies exist for all
user-facing templates and covers; only deterministic preview derivatives
need regeneration.

## Hardened fix

The plugin now installs two validated GOT hooks, in this order, directly from
`initialize()`:

1. Interpose
   `VolumeManager::removeCommonBookData(Device const&, Volume&, bool)`.
   The wrapper reads `Content::getId()` and forces the final boolean to false
   only when the ID begins with an exact plugin support prefix under
   `/mnt/(onboard|sd)/(.kobo|koboExtStorage)/custom/`. All other content and
   every other argument remain stock.
2. Only after that preservation hook validates, interpose
   `FeatureSettings::excludeSyncFolders()` and append the existing exclusion
   expression.

For the observed imported support MIME paths, passing false at the narrow
`removeCommonBookData` seam skips the flag-gated backing-file deletion while
`removeBook()` continues its normal image-cache, repository, and database
cleanup. Stale My Books rows therefore disappear without deleting the source.
The wrapper does not hook `QFile::remove()` or any global filesystem API.

Hook setup is fail-closed: all symbols are resolved before mutation; the
preservation hook is installed first; and the exclusion hook is not enabled
if preservation cannot be validated. If the second hook fails, both GOT slots
are written back to their `dlsym()` stock targets. (`nh_dlhook` returns the
stock symbol address rather than the displaced GOT value, so this code does
not claim to preserve an unrelated pre-existing interposer.) A failure keeps
the custom scanner exclusion disabled instead of exposing support files to
destructive stale-row cleanup.

`scripts/verify-layer-abi.py` pins the exact libnickel hash, both exported
symbols and sizes, `Content::getId()`, the boolean branch, the PLT caller, and
the explicit `QFile::remove()` block.

## 2026-07-16 rejected deployment

The exclusion-only candidate was rebuilt from a clean tree in NickelTC with GCC
4.9.4, Qt 5.2.1, `-Wall -Wextra -Werror`, then packaged by the `koboroot`
target.

- `scripts/verify-layer-abi.py`: 109 symbols, 5 vtable sizes, 31 vtable
  slots, and 120 instruction pins verified.
- Layer-preview ABI verifier and all three artifact-verifier unit tests pass.
- `KoboRoot.tgz` SHA-256:
  `e54ed7a1ed021d373f4b8b1fec0d6ff1ad0c7f9e72bc4bcecc3f617013faa0cd`
- Built and packaged plugin SHA-256 (byte-identical):
  `5d8a720cc71558c445358d5fff16688a21f34fee272450e3d5f8af690c03fbe4`
- Source and packaged stock `libiinknote.so` SHA-256 (byte-identical):
  `f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea`
- Both plugin copies identify as ELF32 little-endian ARM EABI5 shared
  objects with the hard-float ABI. The archive contains exactly the plugin
  and stock `libiinknote.so` at their intended Kobo paths.
- ARM disassembly of `_cnt_exclude_sync_folders_hook` shows the stock getter
  indirect call, a `';'` split/duplicate check, conditional delimiter and
  pattern append, and the mutex-guarded one-shot trace. Disassembly of
  `initialize()` shows the pinned resolve/store/hook/validate order and the
  prior-target rollback path before notebook-hook initialization.

This archive was deployed and is rejected. It hid the support rows but also
deleted their source files during reconciliation. Its hashes are retained only
for incident identification; do not reinstall it.
