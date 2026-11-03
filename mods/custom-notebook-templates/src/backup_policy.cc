#include "backup_policy.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace cnt {
namespace backup_policy {

namespace {

std::uint32_t le16(unsigned char const* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8;
}

std::uint32_t le32(unsigned char const* p) {
    return le16(p) | le16(p + 2) << 16;
}

bool digits(std::string const& text, std::size_t begin, std::size_t count) {
    if (begin + count > text.size())
        return false;
    for (std::size_t i = begin; i < begin + count; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    return true;
}

bool endsWith(std::string const& text, std::size_t from, std::string const& suffix) {
    return text.size() >= from && text.size() - from == suffix.size()
        && !text.compare(from, suffix.size(), suffix);
}

// "yyyyMMdd-HHmmsszzz" is 18 characters.
enum { kStampLength = 18 };

// Parses a backup name. `collision` is 0 without a "-N" suffix.
bool parse(
        std::string const& name,
        std::string const& notebookFileName,
        unsigned long* collision) {
    if (notebookFileName.empty()
            || !digits(name, 0, 8) || name.size() <= kStampLength
            || name[8] != '-' || !digits(name, 9, 9))
        return false;
    std::string const tail = notebookFileName + ".backup";
    if (name[kStampLength] != '-')
        return false;
    if (endsWith(name, kStampLength + 1, tail)) {
        *collision = 0;
        return true;
    }
    std::size_t end = kStampLength + 1;
    while (end < name.size() && name[end] >= '0' && name[end] <= '9')
        ++end;
    if (end == kStampLength + 1 || end - (kStampLength + 1) > 6
            || end >= name.size() || name[end] != '-'
            || !endsWith(name, end + 1, tail))
        return false;
    *collision = std::strtoul(name.c_str() + kStampLength + 1, nullptr, 10);
    return true;
}

struct Entry {
    std::string name;
    unsigned long collision;
};

bool newerFirst(Entry const& a, Entry const& b) {
    int const stamp = a.name.compare(0, kStampLength, b.name, 0, kStampLength);
    if (stamp != 0)
        return stamp > 0;
    if (a.collision != b.collision)
        return a.collision > b.collision;
    return a.name > b.name;
}

} // namespace

bool centralDirectoryRange(
        unsigned char const* tail,
        std::size_t tailSize,
        std::uint64_t fileSize,
        std::uint64_t* offset,
        std::uint64_t* size) {
    if (!tail || tailSize < 22 || tailSize > fileSize || !offset || !size)
        return false;
    for (std::size_t i = tailSize - 22 + 1; i-- > 0;) {
        unsigned char const* const record = tail + i;
        if (le32(record) != 0x06054b50u)
            continue;
        // The real record's comment must end exactly at end of file.
        if (i + 22 + le16(record + 20) != tailSize)
            continue;
        std::uint32_t const disk = le16(record + 4);
        std::uint32_t const directoryDisk = le16(record + 6);
        std::uint32_t const entriesHere = le16(record + 8);
        std::uint32_t const entries = le16(record + 10);
        std::uint32_t const directorySize = le32(record + 12);
        std::uint32_t const directoryOffset = le32(record + 16);
        if (disk != 0 || directoryDisk != 0 || entriesHere != entries
                || entries == 0xffffu || directorySize == 0xffffffffu
                || directoryOffset == 0xffffffffu)
            return false;
        std::uint64_t const recordPosition = fileSize - tailSize + i;
        if (entries == 0 || directorySize < 46u * entries
                || directorySize > kMaxCentralDirectoryBytes
                || static_cast<std::uint64_t>(directoryOffset) + directorySize
                    > recordPosition)
            return false;
        *offset = directoryOffset;
        *size = directorySize;
        return true;
    }
    return false;
}

bool centralDirectorySignature(
        unsigned char const* directory,
        std::size_t size,
        std::string* signature) {
    if (!directory || !signature || size == 0)
        return false;
    std::string result;
    std::size_t at = 0;
    while (at < size) {
        if (size - at < 46 || le32(directory + at) != 0x02014b50u)
            return false;
        unsigned char const* const header = directory + at;
        std::size_t const nameLength = le16(header + 28);
        std::size_t const extraLength = le16(header + 30);
        std::size_t const commentLength = le16(header + 32);
        std::size_t const recordLength = 46 + nameLength + extraLength + commentLength;
        if (nameLength == 0 || recordLength > size - at)
            return false;
        // CRC-32 and both sizes (16..27), then the name.
        result.append(reinterpret_cast<char const*>(header + 16), 12);
        result.append(reinterpret_cast<char const*>(header + 28), 2);
        result.append(reinterpret_cast<char const*>(header + 46), nameLength);
        at += recordLength;
    }
    signature->swap(result);
    return true;
}

bool isBackupOf(std::string const& backupName, std::string const& notebookFileName) {
    unsigned long collision = 0;
    return parse(backupName, notebookFileName, &collision);
}

std::vector<std::string> backupsToPrune(
        std::vector<std::string> const& names,
        std::string const& notebookFileName,
        std::size_t keep,
        std::vector<std::string> const& protectedNames) {
    std::vector<Entry> entries;
    for (std::size_t i = 0; i < names.size(); ++i) {
        Entry entry;
        entry.name = names[i];
        if (parse(entry.name, notebookFileName, &entry.collision))
            entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), newerFirst);
    std::vector<std::string> result;
    for (std::size_t i = keep; i < entries.size(); ++i) {
        if (std::find(protectedNames.begin(), protectedNames.end(), entries[i].name)
                == protectedNames.end())
            result.push_back(entries[i].name);
    }
    return result;
}

} // namespace backup_policy
} // namespace cnt
