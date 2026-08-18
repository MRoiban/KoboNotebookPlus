# Binary Ninja ABI analysis: MyScript page-export fast paths at notebook sleep

Firmware: Kobo 4.38.23697. This report uses two address forms throughout:

- **BN** is the address shown in Binary Ninja, whose ELF view starts at
  `0x10000`.
- **ELF VMA** is the value suitable for `firmware_pins.h`, and is always
  `BN - 0x10000`.

No binary, plugin source, or device was modified. BN Remote exposed no mutation
capabilities. All firmware queries were made through BN Remote/`bnctl` against
GUI-hosted Binary Ninja views.

## Analysis targets and an important database mismatch

The supplied `binary-ninja/iink-4.38.23697.bndb` is not a database for
`libiink.so`. It is a small Thumb executable named `iink`: its view is only
`0x10000..0x422e8`, with 516 functions, and its original-file SHA-256 is
`98bb2032f47f75bd1c3796f3cda2f016edc43a57435007035d6b5b8657dd7bb1`.
It cannot contain `exportToPNG` at BN `0x5230c4` / ELF `0x5130c4` or any
of the other `libiink.so` addresses. I opened it to verify the mismatch, then
opened the exact original `extracted/rootfs/usr/local/Kobo/libiink.so` as the
required fallback. The latter reached quiescent, complete analysis before the
final queries.

| View | Exact target | SHA-256 | BN view / status |
|---|---|---|---|
| main | `extracted/rootfs/usr/local/Kobo/libiink.so` | `0dc02118b9b763450c228da1e632dadbcce5cfd2acb41a3ca76a2472aed71ac6` | ELF `0x10000..0x21ddd08`, `linux-armv7`, 37,715 functions, complete, fingerprint `037dc1cda8898d30145cbbfe677a78e3a36cdd7424b2fdb5524d514c3ab06671` |
| UI reference implementation | `binary-ninja/libiinkuiref-4.38.23697.bndb` | `693aa804c31e3d783314ea2a61282730b0ada6f0b48029a3bc955ea78acd409a` | ELF `0x10000..0x617b8`, complete |
| note | `binary-ninja/libiinknote-4.38.23697.bndb` | `f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea` | ELF `0x10000..0xb9a18`, complete |

ARM code in these views is mixed ARMv7/Thumb2. Every function discussed below
is Thumb2. Runtime function pointers in vtables therefore carry the Thumb bit
in bit 0; the tabulated symbol addresses are the normalized even addresses.

## Executive findings

1. The stock concrete `ImagePainter` does render into a heap-owned in-memory
   `QImage`. `prepareImage()` creates it, `createCanvas()` renders into it, and
   `saveImage()` performs the PNG encode/write. Unfortunately `saveImage()`
   releases the painter's image immediately after saving, so `getImage()` after
   normal `BackendImageDrawer::drawImage()` returns an empty image.
2. A complete replacement `IImagePainter` is not attractive: its required
   `createCanvas()` result is a proprietary `graphics::ICanvas`. The practical
   fast path is a per-instance shadow vtable for the stock `ImagePainter`,
   replacing only `saveImage()`. That thunk can call the non-virtual
   `ImagePainter::getImage()` and retain the implicitly shared `QImage` without
   encoding it.
3. `atk::core::Box` is an abstract semantic notebook-content object, not a POD
   rectangle. Passing a real non-empty box changes the selection and extent
   before renderer construction; it is not a post-render crop. It cannot
   represent an arbitrary viewport, and fabricating one from four floats is
   unsafe.
4. Final bitmap dimensions are derived from the selected extent in millimetres,
   `export.image-resolution`, and `export.image-max-size`, or from the four
   `export.image.viewport.*` configuration values. The stock path's trailing
   `unsigned` is forwarded to the listener but ignored by the stock callback.
   It is not a DPI, scale, or size argument.

The in-memory interception removes PNG encode, file I/O, and PNG decode, but it
does not eliminate MyScript stroke rasterization. No ABI-level mechanism was
found that makes a full-page render avoid that work while retaining identical
content.

## 1. Export path and the exact PNG boundary

### 1.1 `PageController::exportToPNG`

| Symbol | BN | ELF VMA | Mode |
|---|---:|---:|---|
| `snt::PageController::exportToPNG(shared_ptr<Box>, string const&, shared_ptr<RendererListener>, unsigned)` | `0x5230c4` | `0x5130c4` | Thumb |
| stock anonymous listener callback | `0x48b090` | `0x47b090` | Thumb |
| `BackendImageDrawer::drawImage(...)` | `0x3f7b8c` | `0x3e7b8c` | Thumb |
| `atk::core::Renderer::layout()` | `0xa67450` | `0xa57450` | Thumb |
| `atk::core::Layout::page()` | `0x9e9530` | `0x9d9530` | Thumb |

At BN `0x523392..0x5233d0` / ELF `0x513392..0x5133d0`, the caller constructs
the virtual-listener ABI exactly as follows:

```cpp
// exportToPNG, disassembly/MLIL condensed
r0 = listener_object;
r1 = &shared_ptr<Renderer>;     // owner passed by reference
r2 = &Selection;
s0 = extent.left;
s1 = extent.top;
s2 = extent.right;
s3 = extent.bottom;
r3 = trailing_unsigned;
sp[0] = &output_path;
call listener_object->vptr[2];  // byte offset +0x08
```

`RendererListener` therefore has the ordinary Itanium destructor pair at
vtable offsets `+0x00/+0x04`, followed by its callback at `+0x08`. The callback
is synchronous: `exportToPNG` releases its temporary renderer, selection, and
other local owners only after the virtual call returns.

The stock anonymous callback at BN `0x48b090` / ELF `0x47b090` saves the
context, renderer, selection, and `s0..s3`, but never saves or reads incoming
`r3`. Its load-bearing sequence is:

```cpp
// sub_48b090, MLIL condensed
layout = renderer->layout();                     // BN 0x48b0a6 / ELF 0x47b0a6
page = layout.page();                            // BN 0x48b0b2 / ELF 0x47b0b2
drawer  = context->drawer_at_04;                 // raw pointer
backend = context->backend_at_24;                // raw pointer
drawer->drawImage(page, renderer, backend,
                  Extent{s0, s1, s2, s3},
                  selection, output_path);       // BN 0x48b116..0x48b136
                                                  // ELF 0x47b116..0x47b136
```

At the `drawImage` call, `r0` is the drawer, `r1` is the `Page` temporary,
`r2` identifies the renderer owner, `r3` is the backend, `s0..s3` hold the
extent, and stack words 0 and 1 carry `Selection` and path temporaries. Binary
Ninja's recovered high-level prototype loses some non-trivial aggregate ABI
detail; the register-level call above is the reliable interface.

The stock callback context is `0x28` bytes. It exposes a raw drawer pointer at
`+0x04` and a raw backend pointer at `+0x24`. The surrounding listener object
retains the corresponding shared owners for the complete synchronous call, so
these raw references remain valid.

### 1.2 `BackendImageDrawer` layout and ownership

| Symbol | BN | ELF VMA | Mode |
|---|---:|---:|---|
| `BackendImageDrawer` constructor | `0x3f71c8` | `0x3e71c8` | Thumb |
| exported `make_shared<BackendImageDrawer>` helper | `0x453eac` | `0x443eac` | Thumb |

The factory allocates `0x40` bytes and places the object at control-block
offset `0x0c`; the exact object size is therefore **`0x34` bytes**, alignment
4. Constructor stores and later draw-path loads give this layout:

| Offset | Size | Field |
|---:|---:|---|
| `+0x00` | 8 | `enable_shared_from_this` weak pointer state |
| `+0x08` | 8 | strong `shared_ptr<EngineImpl>` |
| `+0x10` | 8 | strong `shared_ptr<IImagePainter>` |
| `+0x18` | 4 | `float export.image-resolution` |
| `+0x1c` | 4 | `int export.image-max-size` |
| `+0x20` | 1 | complete viewport-config flag |
| `+0x24` | 4 | viewport x |
| `+0x28` | 4 | viewport y |
| `+0x2c` | 4 | viewport width |
| `+0x30` | 4 | viewport height |

The constructor retains both engine and painter control blocks. It does not
retain the configuration object: it snapshots the numeric values above. The
current plugin's explicit painter/drawer owners therefore match stock lifetime
expectations, and it is safe to destroy a private configuration clone after the
drawer has been constructed.

The constructor HLIL at BN `0x3f71c8` / ELF `0x3e71c8`, with array notation
reflecting 32-bit fields, is:

```cpp
result[6] = configuration->getNumber("export.image-resolution");
result[7] = configuration->getNumber("export.image-max-size");
result[8] = configuration->contains(viewport_x)
         && configuration->contains(viewport_y)
         && configuration->contains(viewport_width)
         && configuration->contains(viewport_height);
if (result[8]) {
    result[9]  = viewport_section->getNumber("x");
    result[10] = viewport_section->getNumber("y");
    result[11] = viewport_section->getNumber("width");
    result[12] = viewport_section->getNumber("height");
}
```

### 1.3 `IImagePainter` vtable ABI

The abstract interface vtable symbol is BN `0x5fdf0` / ELF `0x4fdf0`; its
address point is BN `0x5fdf8` / ELF `0x4fdf8`. The interface subobject is one
4-byte vptr. The vtable is:

| Slot from address point | Exact interface method | ARM hard-float ABI notes |
|---:|---|---|
| `+0x00` | complete destructor, D1 | `r0=this`; pure-interface entry is null |
| `+0x04` | deleting destructor, D0 | `r0=this`; pure-interface entry is null |
| `+0x08` | `void prepareImage(int width, int height, float scale)` | `r0=this`, `r1=width`, `r2=height`, `s0=scale` |
| `+0x0c` | `void saveImage(myscript::iink::String const& path)` | `r0=this`, `r1=&path` |
| `+0x10` | `shared_ptr<graphics::ICanvas> createCanvas()` | hidden result `r0`, `this` in `r1` |

The `void` on `prepareImage` is the interface semantics; BN sometimes infers
the concrete tail call's integer return value from `QImage::fill`.

The concrete `uireferenceimplementation::ImagePainter` primary vtable symbol
is BN `0x5fe38` / ELF `0x4fe38`; its primary address point is BN `0x5fe40` /
ELF `0x4fe40`:

| Slot | Implementation BN | ELF VMA | Exact method |
|---:|---:|---:|---|
| `+0x00` | `0x397e0` | `0x297e0` | D1 complete destructor |
| `+0x04` | `0x39948` | `0x29948` | D0 deleting destructor |
| `+0x08` | `0x38254` | `0x28254` | `prepareImage(int,int,float)` |
| `+0x0c` | `0x38354` | `0x28354` | `saveImage(String const&)` |
| `+0x10` | `0x38bb4` | `0x28bb4` | `createCanvas()` |
| `+0x14` | `0x397bc` | `0x297bc` | `QString context() const` from `MyScriptLeakDetectable`; not part of `IImagePainter` |

The secondary `MyScriptLeakDetectable` address point is BN `0x5fe60` / ELF
`0x4fe60`, with offset-to-top `-0x0c`. Its thunks are D1 BN `0x39940` / ELF
`0x29940`, D0 BN `0x39ab4` / ELF `0x29ab4`, and `context()` BN `0x397d8` /
ELF `0x297d8`. A shadow of the primary table must not overwrite the secondary
vptr at object `+0x0c`.

### 1.4 Concrete `ImagePainter` layout and methods

| Symbol | BN | ELF VMA | Mode |
|---|---:|---:|---|
| constructor | `0x38b48` | `0x28b48` | Thumb |
| `setImageLoader(shared_ptr<ImageLoader>)` | `0x38a60` | `0x28a60` | Thumb |
| `getImage() const` | `0x38b38` | `0x28b38` | Thumb |

The exact complete-object size is **`0x24` bytes**, alignment 4:

| Offset | Size | Field |
|---:|---:|---|
| `+0x00` | 4 | primary `IImagePainter` vptr |
| `+0x04` | 8 | `enable_shared_from_this<ImagePainter>` weak state |
| `+0x0c` | 4 | secondary `MyScriptLeakDetectable` vptr |
| `+0x10` | 4 | `QMutex` |
| `+0x14` | 8 | strong `shared_ptr<QImage>` |
| `+0x1c` | 8 | strong `shared_ptr<ImageLoader>` |

The constructor's final 16-byte zero store covers `+0x14..+0x23`; the
destructor releases the loader control block at `+0x20`, the image control
block at `+0x18`, then destroys the mutex and bases. The plugin's existing
`0x40`-byte storage reservation is conservative; `0x24` is the recovered
firmware size.

The existing construction pattern has the correct ownership shape: allocate
the complete object with `::operator new`, invoke C1 at BN `0x38b48` / ELF
`0x28b48`, create one `shared_ptr<IImagePainter>` whose custom deleter invokes
D0 at BN `0x39948` / ELF `0x29948`, and pass that native owner to the drawer
factory. D0 is appropriate here precisely because the object came from
`::operator new`; it destroys the complete object and frees the allocation.
The drawer copies that shared owner, while the renderer listener retains both
painter and drawer through the synchronous callback. A raw painter with no
real shared control block would not meet the factory's lifetime expectations.

`getImage()` is non-virtual. Its HLIL reduces to:

```cpp
// ImagePainter::getImage, BN 0x38b38 / ELF 0x28b38
QImage::QImage(hidden_result, this->image_owner_at_14->object);
return hidden_result;
```

This is a normal implicitly shared `QImage` copy, so a copied result remains
valid after the painter and its `shared_ptr<QImage>` are destroyed.

`createCanvas()` unconditionally loads the owner at `+0x1c`, calls the
`ImageLoader`'s `getEditor`, and constructs the private canvas around it. A
loader is therefore required; `ImageLoader` is not the bitmap sink.
`setImageLoader` copies and strongly retains the supplied loader control block
at `+0x1c/+0x20` and releases any previous owner.

### 1.5 Actual painter call order and where PNG I/O occurs

The critical `BackendImageDrawer::drawImage` calls are:

```cpp
// BN 0x3f7f96..0x3f7fb4 / ELF 0x3e7f96..0x3e7fb4
painter = this->painter_at_10;
painter->vptr[2](painter, width, height, image_resolution); // prepareImage
canvas = painter->vptr[4](painter);                         // createCanvas

// intervening RendererImpl bind + drawModel/drawSelection into canvas

// BN 0x3f8226..0x3f8242 / ELF 0x3e8226..0x3e8242
painter = this->painter_at_10;
painter->vptr[3](painter, output_path);                     // saveImage
```

Concrete `prepareImage` at BN `0x38254` / ELF `0x28254` allocates a
`shared_ptr<QImage>`, constructs `QImage(width, height, format)`, stores its
owner at `+0x14/+0x18`, and fills it. `createCanvas` then returns a MyScript
canvas backed by that image/editor.

Concrete `saveImage` at BN `0x38354` / ELF `0x28354` is the exact PNG/file
boundary:

```cpp
// ImagePainter::saveImage, HLIL condensed
if (this->image_at_14 == nullptr)
    return;
QString qpath = convert(path);
QFileInfo info(qpath);
QByteArray suffix = info.suffix().toLocal8Bit();
if (!this->image_at_14->save(qpath, suffix.constData()))
    throw ...;
this->image_at_14 = nullptr;
release(this->image_control_at_18);
this->image_control_at_18 = nullptr;
```

Thus the **concrete `ImagePainter::saveImage` virtual** performs both encoding
and file writing via `QImage::save`; `BackendImageDrawer` itself does neither.
Immediately afterward, however, `drawImage` asks MyScript's filesystem whether
the requested path exists and throws the diagnostic
`"imageDrawer did not create image: "` if it does not. Suppressing the save
must account for this postcondition.

### 1.6 Feasible in-memory interception

A wholly plugin-supplied `IImagePainter` is a **no-go** unless it reimplements
the proprietary `graphics::ICanvas` stack. Replacing `ImageLoader` also does
not expose a final-bitmap callback.

A stock `ImagePainter` with a per-instance shadow primary vtable is a viable
route:

1. Construct the stock object and install the normal stock `ImageLoader`.
2. Copy the primary vtable header, RTTI, and all six entries into static
   read-only-lifetime plugin storage.
3. Replace only slot `+0x0c`, `saveImage(String const&)`, with a Thumb plugin
   thunk. Preserve D1, D0, `prepareImage`, `createCanvas`, and `context`.
4. In the thunk, call pinned non-virtual `ImagePainter::getImage` with the
   hidden-result ABI (`r0=&QImage result`, `r1=this`) and store/copy that
   `QImage` into plugin state. Do not call stock `saveImage`.
5. Give `drawImage` a path that already exists according to MyScript's
   filesystem, so its post-save existence check succeeds. `/dev/null` is an
   attractive candidate but requires device validation; a controlled existing
   sentinel path is safer if MyScript rejects device nodes.
6. Restore the original primary vptr before releasing the synchronous export
   owners.

Calling `getImage()` after an unmodified export is too late, because stock
`saveImage()` has cleared `+0x14/+0x18`. Calling it inside an overridden save
slot captures the fully rendered bitmap at the only useful boundary.

**Confidence:** high for the ABI, layout, call order, and `QImage` lifetime;
medium for the operational technique until the path-existence postcondition
and shadow-vtable thunk are exercised on-device.

## 2. Non-empty `atk::core::Box`

### 2.1 It is not a rectangle

| Symbol | BN | ELF VMA | Mode |
|---|---:|---:|---|
| abstract `atk::core::Box` vtable | `0xd99850` | `0xd89850` | data |
| `snt::GridBox` constructor | `0x543c64` | `0x533c64` | Thumb |
| `PageController::box(string const&)` | `0x5135b0` | `0x5035b0` | Thumb |
| `PageController::box(int)` | `0x515e34` | `0x505e34` | Thumb |
| `GridBoxFactory::root()` | `0x545250` | `0x535250` | Thumb |
| `PageController::getTmpBoxForExport(...)` | `0x520bdc` | `0x510bdc` | Thumb |

The abstract `Box` vtable address point is BN `0xd99858` / ELF `0xd89858`; it
contains the destructor pair followed by numerous pure virtual semantic
operations. The concrete `GridBox` constructor writes the derived vptr at
`+0x00`, zeros the weak state at `+0x04/+0x08`, constructs `atk::core::Page` at
`+0x0c`, and begins its first derived shared pointer at `+0x24`:

```cpp
// GridBox ctor, HLIL/disassembly condensed
this->vptr = GridBox_vtable_address_point;
this->weak_object = 0;
this->weak_control = 0;
atk::core::Page::Page(this + 0x0c, page_arg);
copy_shared_ptr(this + 0x24, derived_arg);
```

The base `atk::core::Box` complete subobject is therefore exactly **`0x24`
bytes**, alignment 4:

| Offset | Field |
|---:|---|
| `+0x00` | polymorphic vptr |
| `+0x04/+0x08` | `enable_shared_from_this<Box>` weak state |
| `+0x0c..+0x23` | embedded `atk::core::Page` (`0x18` bytes) |

It is abstract, non-POD, and carries page and shared-ownership invariants.
Heap-constructing four floats and wrapping them in a `shared_ptr<Box>` is not
safe. There is no usable exported base constructor or rectangle factory.

### 2.2 How `exportToPNG` uses a real box

The box branch in `exportToPNG` occurs before renderer creation:

```cpp
// exportToPNG HLIL, condensed from BN 0x5230c4 / ELF 0x5130c4
if (box_owner != nullptr)
    composite = dynamic_cast<snt::CompositeBox *>(box_owner.get());

if (box_owner == nullptr || composite == nullptr) {
    root = gridBoxFactory->root();
    TreeHelper::visitAll(root, ...);                 // build full selection
} else {
    box_owner->virtual_at_24(&selection_tmp);        // semantic box selection
    selection = selection_tmp;
}

extent = selection.extent(ExtentPolicy(...));
Renderer renderer(..., selection, tile_config);
listener(renderer, selection, extent, flags, path);
```

The function specifically attempts `Box -> snt::CompositeBox`; null or an
unsuitable box falls back to root traversal. For an accepted non-empty box it
obtains the semantic selection from the object, then computes
`Selection::extent` before the temporary renderer and output canvas are made.

The extent uses MyScript's physical units. In `BackendImageDrawer::drawImage`,
the default pixel formula is:

```cpp
width  = round((extent.right  - extent.left) / 25.4f * image_resolution);
height = round((extent.bottom - extent.top)  / 25.4f * image_resolution);
```

The `25.4` conversion proves that the extent coordinates are millimetres and
the resolution is dots/pixels per inch.

This is not a full render followed by a Qt crop: both the semantic `Selection`
and its reduced `Extent` reach the MyScript renderer before image allocation
and `drawSelection`. It can reduce the output allocation and restrict the
content MyScript is asked to draw. The IL cannot prove how efficiently the
closed-source renderer traverses strokes internally, so the amount of CPU
saving for a small selection remains a runtime measurement.

It also does not solve arbitrary viewport clipping. A `Box` is a model object
such as a root/composite/grid box, not `{left,top,right,bottom}`. For the full
notebook page, the current empty-box/root behavior is the appropriate semantic
selection.

### 2.3 Firmware call sites

After complete analysis, there are zero code or data xrefs to BN `0x5230c4` /
ELF `0x5130c4`
(`exportToPNG`) in `libiink.so`. Symbol/import searches in the loaded `iink`,
`libiinkuiref`, and `libiinknote` views likewise found no stock PNG caller.
Consequently there is no qualifying firmware call site that can be shown to
pass a non-empty box to this exact PNG function; the plugin appears to be the
only caller in the inspected images.

The closest firmware analogue is the document export path:

| Symbol/call | BN | ELF VMA |
|---|---:|---:|
| `NeboBackend::export_` call path | `0x4994ec` | `0x4894ec` |
| `PageController::exportToDocx` | `0x52e198` | `0x51e198` |
| `getTmpBoxForExport` used by that export | `0x52e218` | `0x51e218` |

That path forwards a live `shared_ptr<Box>` into export selection and resolves
it through `getTmpBoxForExport`, falling back to the grid root when needed. It
confirms the semantic-selection interpretation, but it is not evidence of a
non-empty PNG call at runtime. This negative result is reported explicitly
rather than inventing a call site.

**Confidence:** high that `Box` is abstract semantic state and cannot be
fabricated as a rectangle; high that selection/extent are applied before
rasterization; medium on the magnitude of renderer work saved by a smaller
real box.

## 3. Output bitmap size

### 3.1 Default extent/DPI/max-size path

The drawer constructor reads these exact configuration keys:

| Key | String BN | ELF VMA |
|---|---:|---:|
| `export.image-resolution` | `0xc227c0` | `0xc127c0` |
| `export.image-max-size` | `0xc227d8` | `0xc127d8` |
| `export.image.viewport.x` | `0xc227f0` | `0xc127f0` |
| `export.image.viewport.y` | `0xc22808` | `0xc12808` |
| `export.image.viewport.width` | `0xc22820` | `0xc12820` |
| `export.image.viewport.height` | `0xc2283c` | `0xc1283c` |
| `export.image.viewport` section | `0xc2285c` | `0xc1285c` |

Without a complete viewport configuration, `drawImage` uses the millimetre to
DPI formula quoted above. It then applies an aspect-preserving cap:

```cpp
max_dimension = max(width, height);
if (max_dimension > image_max_size) {
    scale = float(image_max_size) / float(max_dimension);
    width  = round(width  * scale);
    height = round(height * scale);
}
```

The observed `1024x1024` is therefore consistent with a square full-page
extent hitting `export.image-max-size == 1024`. It is not a literal final-size
constant in `exportToPNG`.

`exportToPNG` does independently put `0x400` into both width/height fields of
the temporary renderer's tile configuration at BN `0x523308..0x523338` / ELF
`0x513308..0x513338`. Those are renderer tile/configuration fields. The final
`QImage` width and height are recomputed later by `BackendImageDrawer` from
extent/export configuration, so the two 1024 values must not be conflated.

### 3.2 Viewport configuration path

The constructor sets its `+0x20` flag only if all four viewport keys exist. If
set, `drawImage` takes output dimensions directly from rounded viewport width
and height, while x/y participate in the render transform. This provides a
private route to an exact `1404x1872` canvas independent of the default
max-size cap.

```cpp
// BackendImageDrawer::drawImage, alternate sizing branch
if (this->viewport_configured_at_20) {
    width  = round(this->viewport_width_at_2c);
    height = round(this->viewport_height_at_30);
    // viewport x/y feed the subsequent transform
}
```

Stock export code already demonstrates the safe configuration pattern: clone,
modify, then pass the clone to the drawer.

| Required symbol | BN | ELF VMA | Purpose |
|---|---:|---:|---|
| `make_shared<ConfigurationImpl>` copy helper | `0x453d94` | `0x443d94` | allocate/copy private configuration |
| `ConfigurationImpl` copy constructor | `0x3fa8b4` | `0x3ea8b4` | invoked by helper; validation pin |
| `ConfigurationImpl::setNumber(String const&, double)` | `0x3f9f30` | `0x3e9f30` | set the six numeric export keys |
| existing `myscript::iink::String` constructor | `0x421c3c` | `0x411c3c` | build key strings |

The copy factory allocates `0x60` bytes with its object at control-block offset
`0x0c`, so the concrete cloned `ConfigurationImpl` is `0x54` bytes. The stock
sequence at BN `0x44e1dc..0x44e3ae` / ELF `0x43e1dc..0x43e3ae` is equivalent
to:

```cpp
private_config = make_shared<ConfigurationImpl>(*live_config);
private_config->setNumber(key, value);
drawer = make_shared<BackendImageDrawer>(engine, painter, private_config);
```

Because the drawer snapshots its values and does not retain the configuration,
this avoids mutating the editor's live configuration. Exact native dimensions
can be requested by setting viewport x/y to `0/0` and width/height to
`1404/1872`. Alternatively, resolution and max-size can be changed for an
extent-derived size, but that is less deterministic for an exact screen canvas.

The transform meaning of viewport x/y and the interaction with page aspect
ratio need visual runtime confirmation. Also, `1404x1872` is about 2.5 times
the pixels of `1024x1024`; skipping Qt scaling may cost substantially more
MyScript raster time and memory.

### 3.3 Trailing `unsigned`

The value is copied into `r3` for the listener virtual call at BN `0x5233c6` /
ELF `0x5133c6`. The stock callback at BN `0x48b090` / ELF `0x47b090` never
reads it and calls a `BackendImageDrawer::drawImage` signature with no such
argument. It therefore has **no effect in this PNG path**. Passing 0 is correct;
changing it cannot request DPI, scale, flags, or native dimensions.

**Confidence:** high for all dimension sources and the ignored trailing
argument; medium that the private viewport configuration produces the desired
screen mapping without a transform adjustment, pending an on-device image
comparison.

## Concrete pin set for a future implementation

Existing pins for `exportToPNG`, the stock callback, drawer factory,
`ImagePainter` constructor/D0/`setImageLoader`, renderer layer restriction, and
view transform remain valid. A build of the recommended fast path would add:

### Required for in-memory capture

| Pin | BN | ELF VMA | Why |
|---|---:|---:|---|
| concrete `ImagePainter` vtable symbol | `0x5fe38` | `0x4fe38` | validate/copy primary address point at symbol `+8` |
| `ImagePainter::getImage() const` | `0x38b38` | `0x28b38` | copy rendered `QImage` in save thunk |
| `ImagePainter::prepareImage` | `0x38254` | `0x28254` | vtable/ABI validation |
| `ImagePainter::saveImage` | `0x38354` | `0x28354` | validate replaced slot |
| `ImagePainter::createCanvas` | `0x38bb4` | `0x28bb4` | validate preserved slot |
| `BackendImageDrawer::drawImage` | `0x3f7b8c` | `0x3e7b8c` | code-shape/ABI validation |

The abstract interface vtable at BN `0x5fdf0` / ELF `0x4fdf0` is valuable for
static verification but does not need to be called. The implementation should
compare the resolved concrete vtable entries against the expected pins before
installing the shadow table and fail closed to the existing PNG route on a
mismatch.

### Required only for native-size configuration

| Pin | BN | ELF VMA | Why |
|---|---:|---:|---|
| configuration copy factory | `0x453d94` | `0x443d94` | private clone |
| `ConfigurationImpl::setNumber` | `0x3f9f30` | `0x3e9f30` | viewport/resolution keys |
| configuration copy constructor | `0x3fa8b4` | `0x3ea8b4` | validation; factory calls it |

The already-pinned `String` constructor BN `0x421c3c` / ELF `0x411c3c` is also
required to construct key strings.

### Optional semantic-box access only

Do not add a fabricated `Box` constructor. If a later feature intentionally
exports a model box, use `PageController::box(string const&)` BN `0x5135b0` /
ELF `0x5035b0` and retain the returned native shared owner. That is unrelated
to rectangular viewport clipping.

## Go/no-go and risk assessment

| Candidate | Verdict | Benefit | Principal risks |
|---|---|---|---|
| **(a) In-memory painter** | **GO**, using stock `ImagePainter` plus a one-slot per-instance shadow vtable; **NO-GO** for a ground-up `IImagePainter` | Removes PNG encode, file write/read, and PNG decode; returns an implicitly shared `QImage` | private vtable ABI; Thumb thunk; MyScript post-save path-existence check; must restore vptr and fail closed |
| **(b) Viewport `Box`** | **NO-GO** for arbitrary viewport/full-page acceleration; **GO only** for a real semantic content box | Smaller real selections reduce canvas extent and constrain requested content | abstract non-POD object; only `CompositeBox`-compatible objects take the selected branch; actual stroke-traversal saving is unknown |
| **(c) Native-size export** | **GO technically**, but defer until measured | Avoids Qt smooth scaling and can produce exact `1404x1872` output | roughly 2.5x pixel count, higher MyScript raster/memory cost, private config ABI, viewport transform needs verification |

Recommended implementation order:

1. Add only the in-memory pins and implement the stock-painter save-slot
   interception with an automatic fallback to the current PNG path. Keep the
   existing full-page selection and 1024 cap for the first timing comparison.
2. Validate that the captured `QImage` survives painter destruction and that
   an already-existing output path satisfies `drawImage` without a write.
3. Measure raster, encode/write, and decode/scale time separately at the sleep
   boundary. This determines whether the ABI work removes enough latency.
4. Add a private configuration clone and test `1404x1872` only if Qt scaling is
   a material remaining cost. Compare total latency and peak memory against the
   1024 capture.
5. Do not pursue a fabricated viewport `Box`. Consider real boxes only for a
   future explicit semantic-selection feature.

## Open questions requiring on-device runtime verification

- Does MyScript's `IFileSystem::exists_` accept `/dev/null`, or should the
  plugin use a controlled pre-existing sentinel file/path for the no-save call?
- Does the shadow save thunk observe the expected primary vptr and Thumb-tagged
  entries on the production device build before every interception?
- Is the captured `QImage` format, alpha, orientation, and lifetime identical
  to the image obtained by PNG decode after all native owners are released?
- Can a save-suppressed `drawImage` complete normally with the pre-existing
  path, and does it leave any error or telemetry state despite no file write?
- What fraction of the current cache-miss latency is rasterization versus PNG
  encode/write/decode and Qt scaling on the actual notebook workloads?
- Do cloned configuration objects have any copied listeners or notifications
  with observable side effects when `setNumber` is called, despite not touching
  the live configuration?
- Do viewport `{0,0,1404,1872}` and the existing view transform produce the
  expected full-page composition, or do x/y need compensation for page origin,
  rotation, or aspect ratio?
- Is native-size export faster end to end after scaling is removed, or does its
  2.5x pixel count make the sleep-boundary export slower?
- For a genuine non-empty composite box, how much stroke traversal does the
  MyScript renderer avoid in practice versus merely reducing output allocation?
- Does the device firmware hash and every new resolver code-shape check match
  the analyzed 4.38.23697 build; if any check fails, does the plugin reliably
  fall back to the verified PNG path?
