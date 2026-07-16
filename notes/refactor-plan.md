# KoboNotebookPlus modularization plan

## Completion status (2026-07-16)

The structural refactor is complete through the real-translation-unit and
explicit-entry milestones:

- `customnotebooktemplates.cc` and every `src/parts/*.cc.inc` fragment have
  been removed from the build.
- `plugin_entry.cc` owns the explicit process startup order; `hooks.cc` owns
  every exported firmware hook shim; `plugin_runtime.cc` owns the sole
  process-lifetime state pointer and inert layer/menu pin aggregates.
- Firmware symbol resolution is isolated in `firmware_resolver.cc`, with the
  read-only address inventory in `firmware_pins.h` and every firmware result
  still published through `FirmwareApi`.
- Feature implementations are ordinary `.cc/.h` services listed explicitly in
  the Makefile. There is no self-registration or cross-TU feature constructor.
- The source gate reports zero mutable objects in the firmware-pin inventory,
  two explicitly allowed process globals (`gPluginState` and NickelHook's
  `info`), and no namespace mutex globals.
- `make check` runs the host-safe layer-artifact, binary-parity, and globals
  verifier suites. The firmware ABI scripts and clean NickelTC build remain
  required release gates because Qt 5.2.1/ARM firmware modules cannot be
  faithfully host-compiled on the development Mac.

The former dormant, unreferenced cross-notebook page-move fragment was removed
rather than linked into a new translation unit; it had no menu entry or call
site and therefore no device behavior to preserve.

Goal: split `src/customnotebooktemplates.cc` (8,162 lines, one anonymous
namespace, ~448 file-scope statics, ~25 exported `extern "C"` hook shims)
into small modules with explicit ownership, without ever producing a build
whose ABI surface differs unintentionally from the verified alpha.

Ground truth this plan is built on (checked 2026-07-16):

- The three ABI scripts (`verify-layer-abi.py`, `verify-layer-preview-abi.py`,
  `verify-layer-artifacts.py`) operate on the **built `.so` and firmware
  libs**, not the source file. A source split does not break them.
- The Makefile is NickelHook's `NickelHook.mk`; adding translation units is
  `override SOURCES +=` and new `Q_OBJECT` headers are `override MOCS +=`.
  Multi-TU builds are low-friction.
- `-fvisibility=hidden -fvisibility-inlines-hidden` is already set; the
  exported hooks carry explicit `visibility("default")` attributes. The
  exported symbol set is therefore already deliberate and must stay
  byte-identical.
- Symbol resolution is mixed by design: `dlsym` for libzip, verified
  image-base offsets for local firmware symbols. The refactor must not
  "normalize" this.

## Preconditions (do these before touching structure)

1. **Land or park the layer-routing fix** (getCurrentTool/ToolDispatcher
   issue). Do not refactor and debug ABI behavior simultaneously.
2. Tag the verified build (`v0.0.1-alpha`) and keep its `.so` artifact.

## Phase 0 — Make the refactor falsifiable

Add `scripts/verify-binary-parity.py`: given two `.so` files, diff

- dynamic exported symbols (`nm -D --defined-only`): must be identical,
- undefined imports and `DT_NEEDED`: must be identical,
- section sizes: report drift (informational),
- the pinned hook/relocation sites already known to the ABI scripts.

Snapshot the alpha `.so` as the reference. Every phase below ends with:
clean NickelTC build → three ABI scripts → binary parity vs. reference →
commit. Update the reference only at device-verified milestones.

## Phase 1 — Split files with zero ABI risk (umbrella-include stage)

Do the file split *before* the linkage split. Move code verbatim into
`src/parts/*.cc.inc` fragments and have `customnotebooktemplates.cc` become
nothing but ordered `#include "parts/....cc.inc"` lines inside the existing
anonymous namespace.

Why this beats extracting real TUs first: it is still **one translation
unit** — internal linkage, inlining, and static initialization are exactly
as before, so the binary is provably unchanged — yet the repo now has
reviewable module boundaries and pure-move history. Every commit in this
phase must show zero edited lines under `git diff --color-moved=blocks`.

Suggested fragments (match the file's existing regions, don't invent new
seams): `abi_types`, `resolve`, `settings`, `fs_util`, `templates`,
`covers`, `cover_cache`, `pages`, `layers_state`, `layers_service`,
`layers_eraser`, `layers_preview`, `eraser_menu`, `visibility`, `menus`,
`hooks`.

## Phase 2 — FirmwareApi and the globals budget

Introduce `firmware_api.h`: one plain struct owning every resolved firmware
function pointer, pinned vtable offset, and image-base-resolved address,
populated by the existing resolution code moved verbatim. Startup refuses to
install any hook if a required member is null (extend the existing checks).
Opaque firmware structs move to `abi_types.h` unchanged.

Set an explicit **globals budget**: at the end of this phase every remaining
file-scope static must be (a) `const`/constexpr, (b) a mutex, or (c) a
member of exactly one service object created in `plugin_entry`. Track the
count (`grep -c '^static '`) in the phase notes; ~448 → target < 30.

## Phase 3 — Promote fragments to real translation units

One fragment per commit becomes a real `.cc/.h` pair added to `SOURCES`
(and `MOCS` for `Q_OBJECT` headers). Rules:

- Everything stays in per-file anonymous namespaces except the minimal
  interface in the header; rely on the existing `-fvisibility=hidden` and
  let the parity gate prove the exported set never changed.
- The ~25 `extern "C"` hook shims all live in one `hooks.cc`, names,
  signatures, and visibility attributes untouched. Hooks translate firmware
  arguments and call a service — no feature logic.
- Promotion order = blast radius order: `fs_util`/`settings` →
  `templates` → `covers`/`cover_cache` → `pages` → `visibility`/`menus` →
  eraser → **layers last** (vtable clones, eraser hooks, previews are the
  most ABI-sensitive code in the plugin).
- No cross-TU global constructors. `plugin_entry.cc` owns one explicit,
  ordered init/teardown list; anything lazy uses function-local statics.

Keep the layout flat — `src/` plus at most `src/parts/` during Phase 1.
Twelve directories for a one-maintainer plugin is ceremony; sixteen files
with clear names is enough.

## Phase 4 — Extensibility, the boring way

A "feature" is: an `init(FirmwareApi&, SettingsStore&) -> bool`, optional
teardown, and optional menu contributions returned as **data** (label, slot
receiver) that `menus.cc` wires explicitly. New features are added by
appending to the explicit list in `plugin_entry.cc`. No self-registration
objects (they reintroduce static-init-order risk), no runtime plugin
loading (unacceptable ABI risk on GCC 4.9/Qt 5.2).

## Phase 5 — Host tests for the pure modules

Extend the `test-verify-layer-artifacts.py` pattern into a `make check`
that builds the pure modules for the host and tests: layer JSON
serialization/reconciliation, preview-cache eviction bounds, path/manifest
handling, and the Home/My Books visibility filter. These are the modules
where regressions are silent on-device.

## Device testing cadence

Host gates run every phase; the device runs at three milestones only, each
following the mount-session safety checklist (check for a pending
`KoboRoot.tgz` first; snapshot `KoboReader.sqlite` and `Kobo eReader.conf`
on mount):

1. after Phase 2 (FirmwareApi live),
2. after non-layer features are real TUs,
3. after the layer split (full smoke: add/select/delete layer, per-layer
   pen+eraser, previews, hardware eraser size, covers, templates, page ops,
   My Books cleanliness).

A phase whose binary-parity diff is clean does not need the device.

## What this deliberately does not do

No behavior changes, no performance work, no new features, no renaming of
settings keys or on-disk layouts (`.kobo/custom/layers/`, `.nebo.backup`)
— those would invalidate the parity argument that makes the refactor safe.
