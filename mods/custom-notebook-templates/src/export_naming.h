#pragma once

#include <cstddef>

namespace cnt {
namespace export_naming {

// FAT permits 255 bytes per component, but leave a small safety margin for
// Kobo's exporter. Keeping the result in fixed storage also avoids introducing
// a second libstdc++ string/stream ABI into Nickel's export path.
struct FileName {
    char bytes[241];
    std::size_t length;

    bool isEmpty() const {
        return length == 0;
    }
};

// Rewriting the output path is safe only for user-visible onboard storage.
// Provider-managed staging paths below .kobo retain the original destination
// in their upload transaction; diverting the exporter to another filename
// makes the provider upload the untouched zero-byte placeholder.
bool canRewriteDestination(char const* destination);

// Build only the final UTF-8 filename component. The caller supplies the
// notebook display stem (normally the .nebo basename), Kobo's original output
// filename so its format suffix can be preserved, and an eight-digit ddMMyyyy
// date. An empty result means Kobo's original destination should be retained.
FileName buildFileName(
    char const* notebookName,
    char const* originalFileName,
    char const* date,
    int startPage,
    int endPage);

// Insert a collision counter before the format suffix. Index 2 turns
// "Leather-26072026.pdf" into "Leather-26072026-2.pdf" and preserves the
// special "_png.zip" suffix used by Kobo to select PNG ZIP export.
FileName addCollisionSuffix(
    char const* fileName,
    unsigned int index);

} // namespace export_naming
} // namespace cnt
