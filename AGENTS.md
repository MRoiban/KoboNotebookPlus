# Agent Instructions

## Resource cleanup

- If you start or use Colima during a task, run `colima stop` when you are finished with it to release CPU and memory resources.

## Kobo device safety (read before touching the device)

On 2026-11-02 the Kobo Elipsa 2E factory-reset itself and lost its books,
notebooks, and plugins. It had already happened once before
(KoboNotebookPlus `notes/INCIDENT-2026-07-14-factory-resets.md`). These rules
exist so it never happens again.

**It happened again the same evening** (the second reset of 2026-11-02, and the
fourth overall). Since the first reset, the only device changes had been: copying
notebooks, 4 books, a font and the templates folder onto the mounted drive
from the Mac; renaming `.kobo/ssh-disabled` to `ssh-enabled` on the mounted
drive; then a clean `diskutil eject` and a reboot. No configfs or USB work ran.
Both resets came on the first boot after the Mac wrote to the drive. The cause
is not known; suspects are macOS writes (`.fseventsd`, `.Spotlight-V100`)
failing this Kobo's boot `dosfsck`, leftover rootfs plugins (KoboNotebookPlus
pinned to 23697, running on 23852), or failing eMMC.

**Root cause found for the Mac-side damage (reproduced 2026-11-02):** macOS 27
mounts FAT drives with its new FSKit driver (`mount` shows `msdos … fskit`).
When it writes several files with similar long names into one folder, it gives
two of them the **same 8.3 short name** (`SCRATC~2.NEB` for both
`Scratchpad 001.nebo` and `Scratchpad 003.nebo`). macOS's `fsck_msdos` reports
no problems. The Kobo's `fsck.fat 4.1` sees a duplicate directory entry,
renames one copy to `FSCK0000.000`, and rcS then runs `rm -rf /mnt/onboard/FSCK*`.
Mac-copied files silently disappear, and the Kobo kernel's vfat driver is left
working on a directory with clashing names. The second reset followed exactly
such a copy (the notebooks in `Skratchpads/`, plus the templates folder, which
has many similar `*.nebo.backup` names). Reproduce it with the Kobo's own
`/bin/dosfsck` under qemu-arm on a FAT32 image written by the Mac.

**Never write to the Kobo drive from macOS 27 (FSKit), not even with Finder.**
That includes letting Spotlight index it, since Spotlight creates many
similarly named files. Copy files over SSH instead (Linux's vfat driver
generates unique short names), or from Linux or Windows. Mount the Kobo
read-only on the Mac (`diskutil mount readOnly`).

### What caused it

1. **USB gadget experiments in configfs.** On this MediaTek kernel (4.9.77,
   Android configfs) only **one** gadget directory can exist: every gadget
   registers `/devices/virtual/android_usb/android0`. An extra gadget
   (`/sys/kernel/config/usb_gadget/kscreen`) made Nickel's own `mkdir g1` fail.
   Tapping **Connect** then showed "connected" but never reached the computer,
   and the user had to unplug in that broken state.
2. **Removing a gadget that had been bound crashed the kernel.** Deleting the
   config symlink and running `rmdir` on the function and gadget directories
   rebooted the Kobo instantly, with the FAT user partition mounted read-write.
3. **Repeated reboots and unclean states on the FAT partition.** At boot,
   `/etc/init.d/rcS` runs `dosfsck -a -w` on `/mnt/onboard` up to four times.
   If every pass fails, it **factory-resets the device**. The stress above (an
   unclean reboot plus a broken USB session) is the most likely reason the
   check failed on the next boot.

### Rules

- **USB gadget work is limited to the shared-`g1` design in
  kobo-screen's `kobo/src/usb_gadget.cc`.** The user re-authorized it on 2026-11-02, after
  the reset. Under `/sys/kernel/config/usb_gadget/` it may only do what
  Nickel's own `/usr/local/Kobo/udev/usb` does: `mkdir`, attribute writes, one
  `symlink`, and `UDC` bind/unbind.
  - **Never `rmdir`, `unlink`, or `rm` anything in configfs.** That is what
    crashed the kernel.
  - **Never create a second gadget directory.** Only `g1` may exist.
  - **Never bind while `configs/c.1/mass_storage.0` exists**, or the Mac could
    mount the FAT partition while the Kobo has it mounted.
  - **Any failed configfs write disables USB for the rest of the boot.** Do not
    retry in a loop.
  - Test changes one step at a time, with a fresh backup made first, and stop
    at the first unexpected result. See kobo-screen's `docs/USB.md`.
- **No kernel-level experiments** on the user's Kobo: no sysfs or configfs
  writes (beyond the USB rule above), no module loading, and no `/dev` writes,
  with one exception. **Touch input may be written to `/dev/input/event2`**
  (Elan touchscreen) through kobo-touch (github.com/MRoiban/kobo-touch). Never inject
  events on the power key (`event4`) or `event0`.
- **When driving the UI** (kobo-touch): screenshot before and after every
  action. Never tap through factory reset, sign out, "Connect" (USB drive),
  firmware or storage settings, or delete dialogs without asking the user
  first.
- **Never read `/tmp/nickel-hardware-status`.** It is Nickel's hardware-event
  FIFO, and `cat`/`tail` steal its events (plug, USB, power).
- **Never read or write the bare `/mnt/onboard` mount point while the partition
  is unmounted** (during USB-drive mode). Check `/proc/mounts` first.
- **Back up before any change on the device.** Copy `.kobo/` (database and
  config) and `My Notebooks/` to the Mac and verify checksums. Never work from
  a backup older than the current session.
- **Keep reboots to a minimum**, and only ever do a clean `sync; reboot`. Do not
  install through `KoboRoot.tgz` unless the user asks; it is linked to the July
  reset. Install plugins over SSH (kobo-screen's `kobo/scripts/push-over-ssh.sh`).
- **Treat a dirty FAT partition as an emergency.** After any crash, unexpected
  reboot, or failed USB session, stop and have the user eject or unplug
  cleanly. Check the boot log for `dosfsck` errors before doing anything else.
- **When a test misbehaves on the device, stop and report.** Don't try
  another variant.
- **On the Mac, never write to the mounted Kobo drive** except for an explicit,
  user-approved copy. Use `cp -X` (no `._` files), keep `.metadata_never_index`
  in place, and always finish with `diskutil eject`.


### Firmware note for this plugin

The plugin supports firmware 4.38.23697 and 4.38.23852 (binary audit
2026-11-03; deployed and exercised on the device the same day). In 23852 only
`libnickel.so.1.0.0` moved: `libiink.so`, `libiinkuiref.so.1.0.0` and `iink`
are byte-identical to 23697, and `libiinknote.so` differs only inside one
embedded QSS resource. The 31 libnickel pins live in
`mods/custom-notebook-templates/src/nickel_pins.cc`, one column per firmware.
At init the plugin selects a column only if every pin matches exactly, and
logs `libnickel firmware <version> profile verified (31 pins)`. If none
matches, every libnickel feature fails closed. Any other firmware still needs
a fresh binary audit before install (see "For this plugin" below).

## Autonomous on-device development (kobo-touch)

Agents can build, install, run and check changes on the Kobo **without a human**,
using SSH plus [kobo-touch](https://github.com/MRoiban/kobo-touch), which takes
screenshots and injects taps and swipes. Follow this loop, and stop to ask the user
only in the cases listed under "Needs a human".

### Tools (on the dev machine)

- `ssh kobo`: a root shell on the Kobo (alias in `~/.ssh/config`). Stock sshd
  forces every login into a shell that reads **stdin**, so send commands as
  `echo '…' | ssh kobo sh -s`; arguments after `ssh kobo` are ignored, and
  `scp`/`sftp` do not work.
- Copy files with `push-over-ssh.sh <local> <remote> kobo` (from kobo-touch's
  `scripts/`). It writes a temp file, checks SHA-256, then renames it into place.
- `kobo-touch` (T3 project `kobo-touch`, `/home/jarvis/.t3/projects/kobo-touch/kobo-touch`):
  `screen FILE`, `tap X Y`, `swipe X1 Y1 X2 Y2 [MS]`, `hold X Y [MS]`, `tree`,
  `find`, and `--shot FILE` after any touch. Screen pixels are portrait
  1404×1872 on the Elipsa 2E.
- Faster screenshots and the widget tree need the kobo-screen plugin with
  `debug=true` and `export KOBO_HOST=$(kobo-touch find | head -1)`.

### The loop

1. **Reachable?** Run `nc -z -w3 "$KOBO_HOST" 22`, and if that fails,
   `kobo-touch find`. If nothing answers, the Kobo is asleep or off Wi-Fi:
   stop and ask the user to wake it.
2. **Back up before changing the device:**
   `echo 'cd /mnt/onboard && tar cf - .kobo "My Notebooks" .adds' | ssh kobo sh -s > backup-$(date +%s).tar`
3. **Build** with NickelTC (`docker run --rm -v "$PWD:/work" -w /work ghcr.io/pgaskin/nickeltc:1 make …`).
4. **Deploy** the plugin with `push-over-ssh.sh <lib>.so /usr/local/Kobo/imageformats/<lib>.so kobo`.
5. **Restart Nickel by rebooting:** `echo 'sync; (sleep 2; reboot) >/dev/null 2>&1 &' | ssh kobo sh -s`,
   then wait about 60 s with `until nc -z -w3 "$KOBO_HOST" 22; do sleep 5; done`.
6. **Did it load?** Run `echo 'logread | grep -i "<plugin name>" | tail -40; ls /usr/local/Kobo/imageformats' | ssh kobo sh -s`.
   A `<lib>.so.failsafe` name means NickelHook's failsafe is still armed (first
   30 s), or that init crashed and the plugin was disabled.
7. **Exercise the feature** with kobo-touch: screenshot, then `tree` to find
   widgets, then tap by widget geometry, then screenshot again. Read the
   screenshots, and treat them as the evidence of what happened.
8. **Before the next reboot**, check the storage read-only:
   `echo 'dosfsck -n /dev/mmcblk0p12' | ssh kobo sh -s`. A dirty bit is normal
   while mounted; duplicate entries or cross-linked clusters mean stop.

Useful coordinates (Elipsa 2E). Bottom bar: Home (140,1819), My Books
(421,1819), My Notebooks (702,1819), Discover (982,1819), More (1263,1819).
Inside a notebook: next page `swipe 1150 1000 250 1000`, previous page
`swipe 250 1000 1150 1000`. Prefer `tree` geometry over hard-coded coordinates.

### Recovery an agent can do alone

- **Plugin misbehaves:** `echo 'rm /usr/local/Kobo/imageformats/<lib>.so*; sync; reboot' | ssh kobo sh -s`.
- **Nickel frozen but SSH alive:** reboot over SSH (step 5).
- **IP changed:** use `kobo-touch find`.

### Needs a human (stop and ask)

- **The Kobo is asleep, or Wi-Fi is off.** Only the power button wakes it; never
  inject the power key.
- **Nickel crash-loops.** Nickel starts Wi-Fi, so SSH is gone too. The user has
  to recover it over USB.
- **Anything physical:** plugging or unplugging USB, Nickel's Connect prompt.
- **Destructive or account UI:** factory reset, sign out, deleting books or
  notebooks, firmware updates, storage settings.
- **Writing to the mounted Kobo drive from macOS 27:** never do this (see the
  device safety rules above).

To keep the device available: `[DeveloperSettings] ForceWifiOn=true` is set in
`/mnt/onboard/.kobo/Kobo/Kobo eReader.conf` (since 2026-11-02; a backup of the
previous file is on jarvis in `~/Backups/kobo/`), so Wi-Fi stays up while the
Kobo is awake. It still sleeps after `[PowerOptions] AutoSleepMinutes=60` of
inactivity. Injected taps probably reset that timer (not yet verified). Keep it on the charger.

### For this plugin

- **Library:** `libcustomnotebooktemplates.so`. Its log tag is `CustomNotebookTemplates`
  (`logread`), and its status file is
  `/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/plugin-status.txt`.
- **Firmware pins:** audited for **4.38.23697** and **4.38.23852** (the device
  runs 23852). After any firmware update, redo the binary audit before
  deploying. Pull the exact device libraries over SSH, which makes the audit
  reproducible without a human:
  `echo 'cd /usr/local/Kobo && tar cf - libnickel.so.1.0.0 libiink.so libiinknote.so libiinkuiref.so.1.0.0 iink' | ssh kobo sh -s | tar xf - -C extracted/rootfs/usr/local/Kobo/`
- Then run `python3 scripts/verify-layer-abi.py` inside NickelTC with the ARM
  binutils on `PATH` (`nm`, `objdump` and `readelf` symlinked to
  `arm-nickel-linux-gnueabihf-*`). It selects the libnickel profile by SHA-256
  and also checks that the `nickel_pins.cc` column matches the library. A new
  firmware needs a new profile there and a new column in `nickel_pins.cc`.
- Run `make -C mods/custom-notebook-templates check` before every deploy.
