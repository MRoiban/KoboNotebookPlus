#include "export_naming.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

using cnt::export_naming::addCollisionSuffix;
using cnt::export_naming::buildFileName;
using cnt::export_naming::canRewriteDestination;
using cnt::export_naming::FileName;

namespace {

void require(
        FileName const& actual,
        char const* expected,
        char const* message) {
    if (expected
            && actual.length == strlen(expected)
            && !memcmp(actual.bytes, expected, actual.length)
            && actual.bytes[actual.length] == '\0') {
        return;
    }
    std::cerr << "export naming test failed: " << message
              << "\nexpected: " << (expected ? expected : "<null>")
              << "\nactual:   " << actual.bytes << '\n';
    std::exit(1);
}

void require(bool condition, char const* message) {
    if (condition)
        return;
    std::cerr << "export naming test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    require(
        canRewriteDestination(
            "/mnt/onboard/Exported Notebooks/Leather.pdf"),
        "visible onboard destination can be renamed");
    require(
        canRewriteDestination(
            "/mnt/onboard/My Exports/Leather.pdf"),
        "other visible onboard destination can be renamed");
    require(
        !canRewriteDestination(
            "/mnt/onboard/.kobo/google_drive/Exported Notebooks/Leather.pdf"),
        "Google Drive staging destination must be preserved");
    require(
        !canRewriteDestination(
            "/mnt/onboard/.kobo/dropbox/Exported Notebooks/Leather.pdf"),
        "Dropbox staging destination must be preserved");
    require(
        !canRewriteDestination(
            "/mnt/onboard/.kobo"),
        "private support root must be preserved");
    require(
        !canRewriteDestination("relative/Leather.pdf"),
        "relative destination must be preserved");
    require(
        !canRewriteDestination("/tmp/Leather.pdf"),
        "non-onboard destination must be preserved");

    require(
        buildFileName(
            "Leather", "Leather.pdf", "26072026", 0, 0),
        "Leather-26072026.pdf",
        "full export name");
    require(
        buildFileName(
            "Leather", "Leather.pdf", "26072026", 49, 66),
        "Leather-26072026-p49-p66.pdf",
        "range export name");
    require(
        buildFileName(
            "Field Notes", "Field Notes_png.zip", "27072026", 3, 3),
        "Field Notes-27072026-p3-p3_png.zip",
        "PNG ZIP format suffix");
    require(
        buildFileName(
            "", "Fallback Name.pdf", "01012027", 0, 0),
        "Fallback Name-01012027.pdf",
        "destination fallback stem");
    require(
        buildFileName(
            "  Bad:/Name?.  ", "old.pdf", "01012027", 0, 0),
        "Bad__Name_-01012027.pdf",
        "FAT-safe sanitization");
    require(
        buildFileName(
            "Notebook", "old.PDF", "01012027", 0, 0),
        "Notebook-01012027.pdf",
        "case-insensitive suffix");
    require(
        buildFileName(
            "Notebook", "old.pdf", "2026-07-27", 0, 0),
        "",
        "invalid date rejection");
    require(
        addCollisionSuffix(
            "Leather-26072026-p49-p66.pdf", 2),
        "Leather-26072026-p49-p66-2.pdf",
        "PDF collision suffix");
    require(
        addCollisionSuffix(
            "Leather-26072026_png.zip", 12),
        "Leather-26072026-12_png.zip",
        "PNG ZIP collision suffix");

    char longName[301] = {};
    for (int i = 0; i < 100; ++i) {
        longName[i * 3] = '\xe2';
        longName[i * 3 + 1] = '\x82';
        longName[i * 3 + 2] = '\xac';
    }
    FileName const bounded = buildFileName(
        longName, "old.pdf", "01012027", 0, 0);
    require(bounded.length <= 240, "filename byte bound");
    require(
        bounded.length < 3
            || memcmp(
                bounded.bytes + bounded.length - 3,
                "\xef\xbf\xbd",
                3),
        "UTF-8 truncation must not manufacture replacement bytes");
    return 0;
}
