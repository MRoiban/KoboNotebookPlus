# KoboNotebookPlus

KoboNotebookPlus is an experimental, firmware-pinned NickelHook plugin for
Kobo Advanced Notebooks. It adds custom paper templates and writable covers,
page operations, native document layers with previews and independent
writing/erasing, and adjustable eraser size.

> [!CAUTION]
> This project calls private Kobo and MyScript C++ APIs by firmware address.
> It has been tested only on a Kobo Elipsa 2E (`condor`) running firmware
> `4.38.23697`. A mismatched build can crash Nickel, corrupt notebooks, cause
> data loss, or force device recovery. Back up the Kobo database, settings,
> and notebooks first, and test only with disposable notebooks.

The latest source includes a fix which keeps plugin support images out of My
Books while preserving their backing files during Nickel's stale-row cleanup.
That recovery build has passed its host-side ABI, relocation, packaging, and
ARM disassembly checks; final reboot/runtime verification is still pending.
There is no stable binary release yet.

## Features

- Native-style Layers popup below the notebook toolbar.
- Add, select, and delete native MyScript document layers.
- Independent pen and eraser routing for the selected layer.
- Per-layer notebook-ratio preview thumbnails with a bounded cache.
- Brush/object eraser modes with a five-step eraser-size row.
- Hardware eraser synchronization with the configured eraser size.
- Automatic `1404 x 1872` paper templates and notebook covers.
- Writable first-page covers and notebook page duplication/reordering.
- Plugin support files excluded from Home/My Books without editing the Kobo
  database.

## Compatibility

| Item | Supported target |
| --- | --- |
| Device | Kobo Elipsa 2E (`condor`) |
| Firmware | `4.38.23697` |
| Qt | 5.2.1 |
| Compiler | NickelTC GCC 4.9.4, ARM hard-float |
| Status | Experimental, source-only |

Every private symbol, vtable slot, instruction sequence, and hook relocation
used by the current build is pinned by the verification scripts. Supporting a
different firmware requires a fresh binary audit; changing only the version
check is unsafe.

## Source-only repository

This repository intentionally does not contain Kobo firmware, MyScript
libraries, Binary Ninja databases, device backups, notebooks, generated
plugins, or `KoboRoot.tgz`. The install archive embeds a stock
`libiinknote.so`, which must be extracted from firmware obtained by the user
and must not be redistributed here.

Clone with the NickelHook submodule:

```sh
git clone --recurse-submodules https://github.com/MRoiban/KoboNotebookPlus.git
cd KoboNotebookPlus
```

Place matching, user-extracted firmware libraries at:

```text
extracted/rootfs/usr/local/Kobo/libiink.so
extracted/rootfs/usr/local/Kobo/libiinknote.so
extracted/rootfs/usr/local/Kobo/libnickel.so.1.0.0
extracted/rootfs/usr/local/Kobo/libiinkuiref.so.1.0.0
mods/custom-notebook-templates/package/libiinknote.so
```

The expected stock `libiinknote.so` SHA-256 for the supported firmware is:

```text
f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea
```

## Verify and build

Requirements are Docker, Python 3, `objdump`, and the matching firmware files
listed above. Run the ABI gates before every build:

```sh
python3 scripts/verify-layer-abi.py
python3 scripts/verify-layer-preview-abi.py
python3 scripts/test-verify-layer-artifacts.py
```

Build only with NickelTC, keeping clean and compile steps sequential:

```sh
docker run --rm -v "$PWD:/work" -w /work \
  ghcr.io/pgaskin/nickeltc:1 \
  make -C mods/custom-notebook-templates clean

docker run --rm -v "$PWD:/work" -w /work \
  ghcr.io/pgaskin/nickeltc:1 \
  make -C mods/custom-notebook-templates -j2

docker run --rm -v "$PWD:/work" -w /work \
  ghcr.io/pgaskin/nickeltc:1 \
  make -C mods/custom-notebook-templates koboroot
```

Before installing, verify that the package contains the compiled ARM plugin
and the exact stock library—nothing else. Never install a package built for a
different firmware.

## Usage

Copy a full-size paper PNG to:

```text
/mnt/onboard/.kobo/custom/templates/Example.png
```

Copy a full-size cover PNG to:

```text
/mnt/onboard/.kobo/custom/covers/Example.png
```

Restart the Kobo after changing templates or covers. Inside an Advanced
Notebook, use the notebook menu for Covers, Layers, and page operations. The
eraser popup contains the stock mode choices plus the configured size row.

Layer metadata and preview caches live under `.kobo/custom/layers/`; native
ink ownership remains inside the `.nebo` document. Backups created by cover,
page, and layer operations use `.nebo.backup`, so Nickel does not import them
as duplicate notebooks.

## Repository layout

```text
mods/custom-notebook-templates/  Plugin source and NickelTC Makefile
scripts/                         ABI and artifact verification tools
third_party/NickelHook/          Upstream NickelHook submodule
```

## Credits and legal

KoboNotebookPlus is not affiliated with or endorsed by Rakuten Kobo or
MyScript. Kobo and related marks belong to their respective owners.

[NickelHook](https://github.com/pgaskin/NickelHook) is maintained by Patrick
Gaskin and is included as a separate MIT-licensed submodule. KoboNotebookPlus
does not yet have a project-specific license; public visibility does not grant
permission to redistribute or reuse its source. No cover or template artwork
is included because its publication license has not been established.
