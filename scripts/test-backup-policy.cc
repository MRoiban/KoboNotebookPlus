#include "backup_policy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using cnt::backup_policy::backupsToPrune;
using cnt::backup_policy::centralDirectoryRange;
using cnt::backup_policy::centralDirectorySignature;
using cnt::backup_policy::isBackupOf;

namespace {

void require(bool condition, char const* message) {
    if (condition)
        return;
    std::cerr << "backup policy test failed: " << message << '\n';
    std::exit(1);
}

void put16(std::vector<unsigned char>* bytes, std::size_t at, unsigned value) {
    (*bytes)[at] = value & 0xff;
    (*bytes)[at + 1] = (value >> 8) & 0xff;
}

void put32(std::vector<unsigned char>* bytes, std::size_t at, std::uint32_t value) {
    put16(bytes, at, value & 0xffff);
    put16(bytes, at + 2, value >> 16);
}

// A fake archive: `bodySize` bytes of entries, a central directory of
// `directorySize` bytes, then an end record with `comment` bytes of comment.
std::vector<unsigned char> archive(
        std::size_t bodySize,
        std::uint32_t entries,
        std::uint32_t directorySize,
        std::size_t comment) {
    std::vector<unsigned char> bytes(bodySize + directorySize + 22 + comment, 0);
    std::size_t const end = bodySize + directorySize;
    put32(&bytes, end, 0x06054b50u);
    put16(&bytes, end + 8, entries);
    put16(&bytes, end + 10, entries);
    put32(&bytes, end + 12, directorySize);
    put32(&bytes, end + 16, static_cast<std::uint32_t>(bodySize));
    put16(&bytes, end + 20, static_cast<unsigned>(comment));
    return bytes;
}

bool range(std::vector<unsigned char> const& bytes, std::uint64_t* offset,
        std::uint64_t* size, std::size_t tailSize = 0) {
    if (!tailSize || tailSize > bytes.size())
        tailSize = bytes.size();
    return centralDirectoryRange(
        bytes.data() + bytes.size() - tailSize, tailSize, bytes.size(), offset, size);
}

void testCentralDirectory() {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::vector<unsigned char> plain = archive(1000, 4, 4 * 46, 0);
    require(range(plain, &offset, &size), "plain archive is parsed");
    require(offset == 1000 && size == 4 * 46, "plain archive range");
    require(range(plain, &offset, &size, 22), "end record alone is enough");

    std::vector<unsigned char> commented = archive(500, 2, 120, 9);
    require(range(commented, &offset, &size), "commented archive is parsed");
    require(offset == 500 && size == 120, "commented archive range");

    // A signature inside the comment must not be taken for the record.
    std::vector<unsigned char> decoy = archive(500, 2, 120, 30);
    put32(&decoy, decoy.size() - 25, 0x06054b50u);
    require(range(decoy, &offset, &size), "decoy inside comment is skipped");
    require(offset == 500, "decoy does not change the range");

    std::vector<unsigned char> zip64 = archive(500, 2, 120, 0);
    put32(&zip64, zip64.size() - 6, 0xffffffffu);
    require(!range(zip64, &offset, &size), "ZIP64 marker is rejected");

    std::vector<unsigned char> overlapping = archive(500, 2, 120, 0);
    put32(&overlapping, overlapping.size() - 6, 600);
    require(!range(overlapping, &offset, &size), "directory past end record is rejected");

    std::vector<unsigned char> empty = archive(0, 0, 0, 0);
    require(!range(empty, &offset, &size), "empty archive is rejected");

    std::vector<unsigned char> multiDisk = archive(500, 2, 120, 0);
    put16(&multiDisk, multiDisk.size() - 18, 1);
    require(!range(multiDisk, &offset, &size), "multi-disk archive is rejected");

    std::vector<unsigned char> garbage(4096, 0x5a);
    require(!range(garbage, &offset, &size), "non-archive is rejected");
    require(!centralDirectoryRange(garbage.data(), 10, garbage.size(), &offset, &size),
        "short tail is rejected");
}

// One central-directory header for `name`.
void appendEntry(std::vector<unsigned char>* directory, char const* name,
        std::uint32_t crc, std::uint32_t size, unsigned time, std::size_t extra) {
    std::size_t const at = directory->size();
    std::size_t const nameLength = std::string(name).size();
    directory->resize(at + 46 + nameLength + extra, 0);
    put32(directory, at, 0x02014b50u);
    put16(directory, at + 12, time);
    put32(directory, at + 16, crc);
    put32(directory, at + 20, size);
    put32(directory, at + 24, size);
    put16(directory, at + 28, static_cast<unsigned>(nameLength));
    put16(directory, at + 30, static_cast<unsigned>(extra));
    for (std::size_t i = 0; i < nameLength; ++i)
        (*directory)[at + 46 + i] = static_cast<unsigned char>(name[i]);
    for (std::size_t i = 0; i < extra; ++i)
        (*directory)[at + 46 + nameLength + i] = static_cast<unsigned char>(time + i);
}

std::string signature(std::vector<unsigned char> const& directory) {
    std::string result;
    if (!centralDirectorySignature(directory.data(), directory.size(), &result))
        return std::string("<invalid>");
    return result;
}

void testSignature() {
    std::vector<unsigned char> base;
    appendEntry(&base, "pages/a/ink.bink", 0x11111111u, 100, 1, 0);
    appendEntry(&base, "pages/b/ink.bink", 0x22222222u, 200, 1, 4);
    std::string const reference = signature(base);
    require(reference != "<invalid>", "directory is parsed");

    std::vector<unsigned char> retimed;
    appendEntry(&retimed, "pages/a/ink.bink", 0x11111111u, 100, 9, 0);
    appendEntry(&retimed, "pages/b/ink.bink", 0x22222222u, 200, 9, 4);
    require(signature(retimed) == reference, "timestamps and extra fields are ignored");

    std::vector<unsigned char> inked;
    appendEntry(&inked, "pages/a/ink.bink", 0x11111112u, 100, 1, 0);
    appendEntry(&inked, "pages/b/ink.bink", 0x22222222u, 200, 1, 4);
    require(signature(inked) != reference, "changed CRC changes the signature");

    std::vector<unsigned char> grown;
    appendEntry(&grown, "pages/a/ink.bink", 0x11111111u, 101, 1, 0);
    appendEntry(&grown, "pages/b/ink.bink", 0x22222222u, 200, 1, 4);
    require(signature(grown) != reference, "changed size changes the signature");

    std::vector<unsigned char> reordered;
    appendEntry(&reordered, "pages/b/ink.bink", 0x22222222u, 200, 1, 4);
    appendEntry(&reordered, "pages/a/ink.bink", 0x11111111u, 100, 1, 0);
    require(signature(reordered) != reference, "entry order is significant");

    std::vector<unsigned char> renamed;
    appendEntry(&renamed, "pages/a/ink.bink", 0x11111111u, 100, 1, 0);
    appendEntry(&renamed, "pages/c/ink.bink", 0x22222222u, 200, 1, 4);
    require(signature(renamed) != reference, "renamed entry changes the signature");

    std::vector<unsigned char> truncated(base.begin(), base.end() - 3);
    require(signature(truncated) == "<invalid>", "truncated directory is rejected");
    std::vector<unsigned char> corrupt = base;
    corrupt[0] = 0;
    require(signature(corrupt) == "<invalid>", "bad header is rejected");
    require(signature(std::vector<unsigned char>()) == "<invalid>", "empty directory is rejected");
}

void testNames() {
    require(isBackupOf("20261103-012955274-bench.nebo.backup", "bench.nebo"), "plain name");
    require(isBackupOf("20261103-012955274-2-bench.nebo.backup", "bench.nebo"), "collision name");
    require(!isBackupOf("20261103-012955274-xbench.nebo.backup", "bench.nebo"), "longer name");
    require(!isBackupOf("20261103-012955274-bench.nebo.backup.tmp", "bench.nebo"), "temporary file");
    require(!isBackupOf("20261103-012955274-bench.nebo.backup", "ench.nebo"), "suffix only");
    require(!isBackupOf("2026110-0129552745-bench.nebo.backup", "bench.nebo"), "bad stamp");
    require(!isBackupOf("20261103-012955274-bench.nebo.backup", ""), "empty notebook name");
    require(isBackupOf("20261103-012955274-x-bench.nebo.backup", "x-bench.nebo"), "dash in name");
    require(!isBackupOf("20261103-012955274-x-bench.nebo.backup", "bench.nebo"),
        "non-numeric prefix is not a collision suffix");
}

void testPrune() {
    std::vector<std::string> names;
    names.push_back("20261103-000000003-a.nebo.backup");
    names.push_back("20261101-000000000-a.nebo.backup");
    names.push_back("20261103-000000003-1-a.nebo.backup");
    names.push_back("20261102-000000000-a.nebo.backup");
    names.push_back("20261103-000000003-b.nebo.backup");
    names.push_back("20261100-000000000-a.nebo.backup.tmp");
    names.push_back("notes.txt");
    std::vector<std::string> none;

    std::vector<std::string> pruned = backupsToPrune(names, "a.nebo", 2, none);
    require(pruned.size() == 2, "two oldest are pruned");
    require(pruned[0] == "20261102-000000000-a.nebo.backup", "older pruned first by order");
    require(pruned[1] == "20261101-000000000-a.nebo.backup", "oldest pruned");

    std::vector<std::string> keepAll = backupsToPrune(names, "a.nebo", 10, none);
    require(keepAll.empty(), "nothing pruned under the limit");

    std::vector<std::string> protectedNames;
    protectedNames.push_back("20261101-000000000-a.nebo.backup");
    pruned = backupsToPrune(names, "a.nebo", 1, protectedNames);
    require(pruned.size() == 2, "protected backup is not pruned");
    require(pruned[0] == "20261103-000000003-a.nebo.backup",
        "collision suffix is newer than the plain stamp");
    require(pruned[1] == "20261102-000000000-a.nebo.backup", "next oldest pruned");

    require(backupsToPrune(names, "b.nebo", 0, none).size() == 1, "other notebook");
    require(backupsToPrune(names, "c.nebo", 0, none).empty(), "unknown notebook");
}

} // namespace

int main() {
    testCentralDirectory();
    testSignature();
    testNames();
    testPrune();
    return 0;
}
