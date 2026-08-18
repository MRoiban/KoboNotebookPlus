# Experiments

Change one variable at a time and use disposable notebooks. Keep Wi-Fi and sync disabled until an experiment explicitly tests synchronization.

## Experiment template

```text
Experiment:
Date:
Firmware:
Patch hash:
Changed files:
Expected result:
Actual result:
Boot result:
Save result:
Reopen result:
Export result:
Sync result:
Database integrity result:
Rollback tested:
Notes:
```

## RE01 — Existing testing folder

```text
Experiment: Compare two Dot pages at the thinnest and thickest ballpoint presets, plus one MarginNarrow page at the thinnest preset
Date: 2026-07-13
Firmware: 4.38.23697 (ae95f6a7d7)
Patch hash: N/A — observation only
Changed files: Two new .nebo archives and normal Kobo database/config bookkeeping
Expected result: The two Dot pages differ in pen-width state; the Margin page shares the thin pen state but differs in background state
Actual result: ballpoint-0-0 -> ballpoint-4-0 across the Dot pages; Margin page uses ballpoint-0-0; backgrounds serialize as DotNarrow and MarginNarrow
Boot result: Not applicable
Save result: Both archives pass unzip integrity testing
Reopen result: Not tested during offline analysis
Export result: Not tested
Sync result: Not tested
Database integrity result: ok; database now records 10 Basic notebooks / 300 pages, including the new 1-page and 2-page notebooks
Rollback tested: Not applicable — no modification installed
Notes: Complete findings are in notes/FORMAT.md. Private extracted samples remain Git-ignored.
```

## RE02 — Offline IsometricGrid replacement

```text
Experiment: Replace the native Condor IsometricGrid paper payload in an offline copy of nickel
Date: 2026-07-13
Firmware: 4.38.23697 (ae95f6a7d7)
Patch hash: patched nickel SHA-256 835bdb8e974cc55c3d17d57e578ddf4e03c23d9cf53bf7ed5b5d814b9305124a
Changed files: Local ignored copy patches/work/nickel-test-template only
Expected result: Selecting Isometric Grid will display diagonal lines and the text TEST TEMPLATE
Actual result: Offline verification passed; the patched resource re-extracts byte-for-byte as the replacement PNG
Boot result: Not tested — package has not been installed
Save result: Not tested
Reopen result: Not tested
Export result: Not tested
Sync result: Not tested; keep Wi-Fi and notebook sync disabled for live test
Database integrity result: Not applicable — no database modified
Rollback tested: Original matching firmware and full user-visible backup are preserved; live rollback not yet tested
Notes: Original nickel SHA-256 36bb8d1b784686d9d7f87f7fef295b81d8017914493d8b4d1d8c65318cce21b3.
       Replacement PNG SHA-256 7745cebc0457ea154812c030da022e1b5ff7ef4a2e34d4ba839c5d1931950d41.
       Target slot is uncompressed, 60231 bytes; replacement is 43302 bytes with 16929 bytes padding.
       Original and patched nickel are both 27219100 bytes. No file has been copied to the Kobo.
```

## RE03 — External custom-template slot

```text
Experiment: Repoint the IsometricGrid renderer and icon paths to /mnt/onboard and relabel the row Custom Paper 1
Date: 2026-07-13
Firmware: 4.38.23697 (ae95f6a7d7)
Patch hash: patched libiinknote.so SHA-256 8073e5b3dd4a32daf4402693d72e0fc26f418a336768f0576da04e098b29d3d5
Changed files: Installed libiinknote.so update plus .kobo/custom/template_custom1_condor.png and template_icon.png
Expected result: The template menu shows Custom Paper 1 and loads template_custom1_condor.png plus template_icon.png from .kobo/custom
Actual result: Offline patch passed; install archive and both PNGs were copied to the Kobo, verified byte-for-byte on-volume, flushed, and safely ejected to trigger installation
Boot result: Installation triggered; awaiting observation on the device
Save result: Not tested
Reopen result: Not tested
Export result: Not tested
Sync result: Not tested; keep Wi-Fi and notebook sync disabled
Database integrity result: Not applicable
Rollback tested: Not yet; patches/output/KoboRoot-rollback.tgz is preserved on the Mac
Notes: The IsometricGrid serialized identifier remains unchanged. Install archive SHA-256 3a82b89eaafdbbe518a256b4a039ab7126f2c85a9f93d9f66fef82d0c1c5160c. See notes/CUSTOM_TEMPLATES.md.
```
