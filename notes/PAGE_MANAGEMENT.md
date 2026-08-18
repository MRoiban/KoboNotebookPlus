# Full page management

Target firmware: Kobo 4.38.23697 (`libiink.so` build
`f960db22f0d4598d46d9a225042c067e49fec570`).

This feature manages the page currently shown in a basic notebook. The stock
page scrubber remains the page selector. Page management is exposed directly as
four Kobo-native overflow-menu rows, in this order:

- `Duplicate page` clones the current page immediately after the original.
- `Move page earlier` moves the current page one position toward the front.
- `Move page later` moves the current page one position toward the back.
- `Move page to…` opens the same hierarchical page-number picker as Export
  range and inserts the current page at the selected one-based position.

On the Kobo Elipsa 2E running firmware 4.38.23697, plugin-created top-level Qt
windows are painted but receive no touch input through Nickel's platform/window
routing. An application-modal `QDialog` therefore locks the entire UI, and a
`QInputDialog` has the same unsupported top-level-window path. Plugin UI must
reuse Kobo-native chrome: `createIInkMenuItem` menu rows, Kobo's native pickers,
and `IInkNotePadWidget::showErrorPopup`.

Exact-position reorder uses the proven Kobo-native Export range picker and the
same transactional reorder path as the one-step actions. Cross-notebook move
remains compiled behind `kEnableCrossNotebookMove = false` and also requires
the `clonePart` resource-independence runtime matrix to pass on a disposable
device before it can be enabled.

No operation modifies firmware, boot files, the database, or documents outside
the two explicitly selected `.nebo` archives.

## Verified firmware primitives

The addresses below are ELF VMAs. Binary Ninja loads this image at `+0x10000`.

| Operation | ELF VMA | Evidence |
|---|---:|---|
| `EditorImpl::getEngine()` | `0x425bb0` | Returns the editor's engine `shared_ptr` from offsets `0x2c/0x30`. |
| `EngineImpl::openPackage(String, PackageOpenOption)` | `0x4470fc` | Option `0` takes the existing-package path; options `1`, `2`, and `3` are create, create-new, and truncate-existing paths. |
| `ContentPackageImpl::clonePart(shared_ptr<ContentPart>)` | `0x3f3d5c` | Obtains the source page and calls `atk::core::Document::importPage` on the destination document. |
| `ContentPackageImpl::removePart(shared_ptr<ContentPart>)` | `0x3f27b0` | Resolves the page index and calls `myscript::document::Document::removePage_`. |
| `ContentPackageImpl::save()` | `0x3f1960` | Existing, proven save path. |
| `ContentPackageImpl::getPart(int)` | `0x3f4850` | Existing, proven page lookup. |
| `ContentPackageImpl::indexOfPart(...)` | `0x3f2238` | Existing, proven page-index lookup. |
| `atk::core::Document::movePage(int,int)` | `0x9f09e4` | Already proven by writable-cover insertion. |

MyScript's public storage documentation agrees with the firmware: a part can
be cloned from the same package or another open package, and a move between
packages is clone-to-destination followed by deletion from the source. Source
changes must be saved before cloning or unsaved changes may not be imported.

`ContentPackageImpl::removePart()` detaches the part immediately and compacts
the remaining indices. Calling `indexOfPart()` with that detached part throws
`EngineError`; it does not return `-1`. Removal therefore follows the stock
delete shape: capture the old index and count, remove the part, verify the new
count, then obtain and select the surviving part at the compacted index without
querying the removed handle again.

Opening an already-open path is not independent serialization proof. MyScript
may return a new package wrapper backed by the same cached in-memory `Document`
from its process-global path registry. Serialization is instead verified by
opening the `.nebo` ZIP read-only and counting `pages/*/meta.json` entries
without inflating their contents.

## Transaction rules

### Reorder

1. Save the editor.
2. Validate the current index, target index, page count, and common document.
3. Scan the current archive directly for a plugin cover marker and refuse an
   indeterminate scan. If a cover exists, refuse to move page zero or move
   another page to page zero.
4. Copy the source notebook to the hidden page-manager backup directory.
5. Call `movePage(current, target)`.
6. Save and verify the selected part is at the target.
7. Finish through `IInkNotePadWidget::save()`, invalidate the plugin cover-scan
   entry, select the live part, and refresh.

### Duplicate

1. Save the editor and validate the current page context.
2. Scan the current archive directly for a plugin cover marker and refuse an
   indeterminate scan. If page zero is the cover, refuse to duplicate it.
3. Validate the page-count limit and make a source backup.
4. Clone the current part into the same package.
5. Verify count increased by one and the clone was appended.
6. Move the clone to `current + 1`.
7. Save and verify count and index.
8. Finish through `IInkNotePadWidget::save()`, invalidate the plugin cover-scan
   entry, select the clone, and refresh.
9. If validation fails, remove the known-attached clone directly, save, and
   verify the old count without querying the detached clone handle.

### Move between notebooks

1. Save the source editor and validate the current page context.
2. Refuse to move the only page of a notebook.
3. Back up both source and destination archives.
4. Write a small transaction journal with state `prepared`.
5. Obtain the live engine from the source editor.
6. Open the destination with `PackageOpenOption::EXISTING` (`0`); never use a
   create or truncate option.
7. Clone the source part into the destination and save it.
8. Invalidate the destination cover-scan entry, release the plugin's
   destination part and package references, then count serialized page entries
   directly in the destination ZIP. Mark the journal `destination-verified`.
9. Capture the source index and count, remove the original part, and never
   query that detached handle again.
10. Verify the in-memory count, obtain the replacement at the compacted stock
    index, select it, and save the source package.
11. Invalidate the source cover-scan entry and verify the source ZIP page-entry
    count independently.
12. Finish through `IInkNotePadWidget::save()`, invalidate the source scan entry
    again, mark the journal `complete`, and refresh.

The destination is committed and validated before source deletion. A crash,
exception, or power loss before step 10 therefore leaves a copy in both
notebooks, never a missing page. If source deletion fails, the operation is
reported as a safe copy and the backups and journal are retained.

## Guards

- Firmware symbols and their VMAs must match exactly before the menu appears.
- Accept only regular, non-symlink `.nebo` files under `/mnt/onboard/`.
- Exclude `.kobo/custom`, backup files, temporary files, and the current
  notebook from destination selection.
- Cap page counts at 4096 and destination choices at 512.
- Check free space before making backups or cloning.
- Keep backups hidden from Kobo's notebook scanner by ending them in
  `.nebo.backup`, not `.nebo`.
- Catch all MyScript exceptions at the plugin boundary and show a non-fatal
  notebook error.
- Treat a plugin cover as positional: the `Custom_Cover_*` page must remain at
  index zero. Reorder cannot move it away or move another page ahead of it, and
  duplicate cannot clone it.
- Treat a busy or unreadable cover scan as an operation refusal, not as proof
  that no cover exists.
- Reject a second menu action while `_cnt_page_operation_active` is true, and
  clear the property on every return path after the synchronous operation.
- Use only Kobo-native chrome for page-management UI; plugin-created top-level
  Qt windows do not participate in Nickel's touch routing.
- Never auto-retry a source deletion during startup. An interrupted move is a
  recoverable duplicate and requires explicit user review.

## Test gate

Deployment is blocked until disposable notebooks pass all of these cases:

1. Move first, middle, last, cover, and custom-background pages one position
   earlier and later, including the friendly first/last boundary popups. Use
   `Move page to…` for both forward and backward insertion, the current
   position, picker cancellation, and multi-level selection in a notebook with
   more than ten pages.
2. Duplicate blank, inked, converted-text, diagram, and custom-background
   pages; restart and reopen after each.
3. Confirm all four overflow-menu rows accept touch and no modal or top-level
   plugin window is created.
4. Verify the notebook grid preview changes only when page zero changes.

The deferred cross-notebook gate must move each page type between root and
subfolder notebooks, survive interruption after destination save with both
copies intact, and reject an only page, the current notebook, a malformed
archive, and insufficient space. Its runtime matrix must also prove that cloned
page resources remain independent after either package is closed, reopened,
edited, and deleted.
