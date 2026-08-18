# Incident: two automatic factory resets on 2026-07-14

Device: Kobo Elipsa 2E, fw 4.38.23697. Times local (Europe/Bucharest, UTC+3).

## Timeline (reconstructed from backups, file mtimes, and the post-reset DB)

| Time | Event | Evidence |
|---|---|---|
| 06:59 | `pre-pdf-import` backup taken (DB 12.5 MB) | backup mtimes |
| ~07:25 | `pre-page-management` DB snapshot (6.4 MB — DB shrank vs 06:59) | backup mtimes |
| 08:48 | Last notebook edit (`Leather.nebo`) | file mtime |
| **09:10** | **Factory reset #1**: fresh DB logs `TimeToUpdate` → `AppStart` → `WelcomeScreen` → `DesktopSetup` at 06:10:57Z | `AnalyticsEvents` in `backups/post-reset-before-restore-2026-07-14/KoboReader.sqlite` |
| 09:22–09:38 | Post-reset snapshot taken; content restored ("Recovered Pre-Reset") | backup dirs |
| 10:32 | `pre-page-management` full backup (newest surviving notebook set) | backup mtimes |
| 11:03 | Cover-resize work; originals backed up | backup mtimes |
| ~12:00 | Plugin package `645bb611…` copied to `.kobo/KoboRoot.tgz`, device ejected. **No confirmation the device was rebooted afterwards.** | session history |
| ~17:25 | Conf edit: appended `[DeveloperSettings] ReduceIInkUpdateAreaBy=1` and `[FeatureSettings] IInkPressureSettings=true`; `dot_clean`; clean eject. Conf file mtime on device was still 11:30 at mount time. | session history |
| after 17:25 | User reboots → **factory reset #2**, all user storage wiped | user report |
| 17:4x | Post-reset-2 volume verified clean (fsck exit 0, 28 MB used = fresh FAT32); content restored from backups | this session |

## Established facts

1. **Reset #1 predates the latency conf edit by ~8 hours.** The conf edit cannot
   explain the first reset; whatever caused #1 was already present this morning.
2. The first event the fresh DB records after reset #1 is **`TimeToUpdate`** —
   Kobo's analytics event for the update/upgrade path. The wipe went through
   the updater/factory path immediately before first `AppStart`.
3. The appended conf keys are standard QSettings sections read via
   `DevSettings`/`FeatureSettings` getters (the exact mechanism NickelMenu and
   community mods document as routine). There is no code path from an unknown
   INI key to a storage wipe; Nickel recreates a broken conf rather than
   resetting.
4. Host-side macOS logs (fsck_msdos / kernel msdosfs / diskarbitrationd)
   retained nothing for the volume — no direct evidence of FAT dirty-state at
   the 17:25 eject.
5. The post-reset-2 filesystem verifies clean and mounts fine, i.e. the wipe
   was an orderly reformat, not progressive corruption still present.
6. Multiple work sessions wrote to this FAT32 volume many times today
   (pdf-import, covers, page-management installs, restores ~830 MB), with
   several reboot cycles in between that did **not** reset.

## Hypotheses, ranked

**H1 — Pending/failed KoboRoot.tgz install at the fatal reboot (most likely for #2).**
The `645bb611…` package was copied at ~12:00 but no reboot was confirmed until
after 17:25. If the device was never rebooted in between, `KoboRoot.tgz` sat on
the FAT volume across another mount/eject cycle and installed only at the
evening reboot. A KoboRoot.tgz that the updater reads as corrupt (FAT-level
damage to its clusters, or an interrupted read) is a community-documented
trigger for the recovery path that reformats user storage. `TimeToUpdate`
firing first after reset #1 shows the updater path also ran at that wipe.
Caveat: the package hash was verified on-device immediately after the copy, so
corruption would have had to occur afterwards (or the updater failed for
another reason).

**H2 — FAT32 corruption crossing the boot-check threshold.**
Kobo's boot sequence checks the user partition and reformats it when the check
fails unrecoverably. macOS is a known aggressor here (Spotlight `.Spotlight-V100`
indexing was active on the volume; AppleDouble churn; `dot_clean -m` sweeps).
Heavy multi-session write traffic today fits, and it is the classic cause of
"my Kobo wiped itself after USB" reports. This can explain either or both
resets independently of any specific file content.

**H3 — The conf edit itself. Considered and rejected as a direct cause** (see
fact 3), but the edit session's mount/eject cycle is still part of H1/H2's
exposure (every mount cycle is a chance to corrupt FAT or to leave state the
next boot trips over).

## How to close the loop (next time the device is mounted)

1. Read `.kobo/KoboReader.sqlite` → `AnalyticsEvents`: if reset #2 also opens
   with `TimeToUpdate` before `WelcomeScreen`, the updater path did the wipe
   (supports H1).
2. Check whether the freshly-restored plugin installed correctly
   (`KoboRoot.tgz` gone, `plugin-status.txt` trace lines, menu present). A
   failed install now would strengthen H1.
3. Snapshot the post-reset DB **before** restoring anything (was missed for
   reset #2; do not miss it again).

## Procedure changes (adopted)

- **Before any device write session:** check for a leftover `.kobo/KoboRoot.tgz`
  and reconcile (either confirm the user rebooted to install it, or remove it)
  — never eject leaving a package pending across an unknown number of cycles
  plus unrelated edits.
- **After every install-copy:** explicitly confirm with the user that the
  reboot/install completed before doing further device work.
- **Snapshot `.kobo/KoboReader.sqlite` and `Kobo eReader.conf`** (cheap) at the
  start of every mount session.
- **Suppress Spotlight on the volume:** create `.metadata_never_index` at the
  volume root and avoid `dot_clean -m` sweeps (delete specific `._KoboRoot.tgz`
  sidecars only).
- Run `diskutil verifyVolume` (read-only) before large writes and before eject
  when the session did anything nontrivial.
- Keep full user-storage backups current before risky phases (this is what
  saved today).
