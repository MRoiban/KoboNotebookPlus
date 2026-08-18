#include "export_naming.h"

#include <cstdio>
#include <cstring>

namespace cnt {
namespace export_naming {
namespace {

std::size_t const kMaximumFileNameBytes = 240;

FileName emptyFileName() {
    FileName result = {{0}, 0};
    return result;
}

std::size_t boundedLength(char const* value) {
    return value ? strnlen(value, 4096) : 0;
}

char asciiLower(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

bool asciiEqualIgnoringCase(
        char const* value,
        char const* expected,
        std::size_t length) {
    if (!value || !expected)
        return false;
    for (std::size_t i = 0; i < length; ++i) {
        if (asciiLower(value[i]) != expected[i])
            return false;
    }
    return true;
}

bool asciiAlphaNumeric(char value) {
    char const lower = asciiLower(value);
    return (lower >= 'a' && lower <= 'z')
        || (value >= '0' && value <= '9');
}

struct FormatSuffix {
    char bytes[17];
    std::size_t length;
};

FormatSuffix formatSuffix(
        char const* fileName,
        std::size_t fileNameLength) {
    FormatSuffix result = {{0}, 0};
    if (!fileName || fileNameLength == 0)
        return result;

    char const* const special[] = {
        "_png.zip",
        "_jpeg.zip",
        "_jpg.zip",
        ".pdf",
    };
    for (std::size_t i = 0; i < sizeof(special) / sizeof(special[0]); ++i) {
        std::size_t const length = strlen(special[i]);
        if (fileNameLength >= length
                && asciiEqualIgnoringCase(
                    fileName + fileNameLength - length,
                    special[i],
                    length)) {
            memcpy(result.bytes, special[i], length + 1);
            result.length = length;
            return result;
        }
    }

    std::size_t dot = fileNameLength;
    while (dot > 0 && fileName[dot - 1] != '.')
        --dot;
    if (dot == 0)
        return result;
    --dot;
    std::size_t const length = fileNameLength - dot;
    if (dot == 0 || length < 2 || length > 16)
        return result;
    for (std::size_t i = dot + 1; i < fileNameLength; ++i) {
        if (!asciiAlphaNumeric(fileName[i]))
            return result;
    }
    for (std::size_t i = 0; i < length; ++i)
        result.bytes[i] = asciiLower(fileName[dot + i]);
    result.bytes[length] = '\0';
    result.length = length;
    return result;
}

bool validDate(char const* date) {
    if (boundedLength(date) != 8)
        return false;
    for (std::size_t i = 0; i < 8; ++i) {
        if (date[i] < '0' || date[i] > '9')
            return false;
    }
    return true;
}

std::size_t utf8SequenceLength(
        char const* value,
        std::size_t remaining) {
    if (!value || remaining == 0)
        return 0;
    unsigned char const first = static_cast<unsigned char>(value[0]);
    if (first < 0x80)
        return 1;
    std::size_t expected = 0;
    if ((first & 0xe0) == 0xc0)
        expected = 2;
    else if ((first & 0xf0) == 0xe0)
        expected = 3;
    else if ((first & 0xf8) == 0xf0)
        expected = 4;
    if (expected == 0 || expected > remaining)
        return 0;
    for (std::size_t i = 1; i < expected; ++i) {
        unsigned char const continuation =
            static_cast<unsigned char>(value[i]);
        if ((continuation & 0xc0) != 0x80)
            return 0;
    }
    return expected;
}

bool invalidAsciiStemByte(unsigned char byte) {
    return byte < 0x20
        || byte == '\\'
        || byte == '/'
        || byte == ':'
        || byte == '*'
        || byte == '?'
        || byte == '"'
        || byte == '<'
        || byte == '>'
        || byte == '|';
}

std::size_t copySanitizedStem(
        char* destination,
        std::size_t capacity,
        char const* source,
        std::size_t sourceLength) {
    if (!destination || capacity == 0 || !source)
        return 0;

    std::size_t sourceOffset = 0;
    while (sourceOffset < sourceLength && source[sourceOffset] == ' ')
        ++sourceOffset;

    std::size_t written = 0;
    while (sourceOffset < sourceLength && written < capacity) {
        unsigned char const byte =
            static_cast<unsigned char>(source[sourceOffset]);
        if (byte < 0x80) {
            destination[written++] = invalidAsciiStemByte(byte)
                ? '_'
                : static_cast<char>(byte);
            ++sourceOffset;
            continue;
        }

        std::size_t const sequence = utf8SequenceLength(
            source + sourceOffset,
            sourceLength - sourceOffset);
        if (sequence == 0) {
            destination[written++] = '_';
            ++sourceOffset;
            continue;
        }
        if (sequence > capacity - written)
            break;
        memcpy(destination + written, source + sourceOffset, sequence);
        written += sequence;
        sourceOffset += sequence;
    }

    while (written > 0
            && (destination[written - 1] == ' '
                || destination[written - 1] == '.')) {
        --written;
    }
    return written;
}

void useFallbackStem(FileName* result) {
    static char const fallback[] = "Notebook";
    memcpy(result->bytes, fallback, sizeof(fallback) - 1);
    result->length = sizeof(fallback) - 1;
}

bool appendBytes(
        FileName* result,
        char const* bytes,
        std::size_t length) {
    if (!result || !bytes
            || length > kMaximumFileNameBytes - result->length) {
        return false;
    }
    memcpy(result->bytes + result->length, bytes, length);
    result->length += length;
    result->bytes[result->length] = '\0';
    return true;
}

} // namespace

bool canRewriteDestination(char const* destination) {
    static char const onboardPrefix[] = "/mnt/onboard/";
    static char const privateSegment[] = ".kobo";
    std::size_t const length = boundedLength(destination);
    std::size_t const prefixLength = sizeof(onboardPrefix) - 1;
    if (length <= prefixLength
            || memcmp(destination, onboardPrefix, prefixLength)) {
        return false;
    }

    char const* const relative = destination + prefixLength;
    std::size_t const relativeLength = length - prefixLength;
    std::size_t const privateLength = sizeof(privateSegment) - 1;
    return relativeLength < privateLength
        || memcmp(relative, privateSegment, privateLength)
        || (relativeLength > privateLength
            && relative[privateLength] != '/');
}

FileName buildFileName(
        char const* notebookName,
        char const* originalFileName,
        char const* date,
        int startPage,
        int endPage) {
    FileName result = emptyFileName();
    std::size_t const originalLength = boundedLength(originalFileName);
    FormatSuffix const suffix =
        formatSuffix(originalFileName, originalLength);
    if (suffix.length == 0 || !validDate(date))
        return result;

    char pageSuffix[32] = {};
    std::size_t pageSuffixLength = 0;
    if (startPage >= 1 && endPage >= startPage) {
        int const formatted = snprintf(
            pageSuffix,
            sizeof(pageSuffix),
            "-p%d-p%d",
            startPage,
            endPage);
        if (formatted <= 0
                || static_cast<std::size_t>(formatted)
                    >= sizeof(pageSuffix)) {
            return result;
        }
        pageSuffixLength = static_cast<std::size_t>(formatted);
    }

    std::size_t const fixedLength =
        1 + 8 + pageSuffixLength + suffix.length;
    if (fixedLength >= kMaximumFileNameBytes)
        return result;

    std::size_t const notebookLength = boundedLength(notebookName);
    char const* stem = notebookName;
    std::size_t stemLength = notebookLength;
    if (notebookLength == 0) {
        if (originalLength <= suffix.length)
            return result;
        stem = originalFileName;
        stemLength = originalLength - suffix.length;
    }

    result.length = copySanitizedStem(
        result.bytes,
        kMaximumFileNameBytes - fixedLength,
        stem,
        stemLength);
    if (result.length == 0
            || (result.length == 1 && result.bytes[0] == '.')
            || (result.length == 2
                && result.bytes[0] == '.'
                && result.bytes[1] == '.')) {
        useFallbackStem(&result);
    }
    result.bytes[result.length] = '\0';

    if (!appendBytes(&result, "-", 1)
            || !appendBytes(&result, date, 8)
            || !appendBytes(
                &result, pageSuffix, pageSuffixLength)
            || !appendBytes(
                &result, suffix.bytes, suffix.length)) {
        return emptyFileName();
    }
    return result;
}

FileName addCollisionSuffix(
        char const* fileName,
        unsigned int index) {
    std::size_t const fileNameLength = boundedLength(fileName);
    if (index < 2 || fileNameLength == 0
            || fileNameLength > kMaximumFileNameBytes) {
        FileName unchanged = emptyFileName();
        if (fileNameLength <= kMaximumFileNameBytes)
            appendBytes(&unchanged, fileName, fileNameLength);
        return unchanged;
    }

    FormatSuffix const suffix =
        formatSuffix(fileName, fileNameLength);
    if (suffix.length == 0 || fileNameLength <= suffix.length) {
        FileName unchanged = emptyFileName();
        appendBytes(&unchanged, fileName, fileNameLength);
        return unchanged;
    }

    char counter[16] = {};
    int const formatted = snprintf(
        counter, sizeof(counter), "-%u", index);
    if (formatted <= 0
            || static_cast<std::size_t>(formatted) >= sizeof(counter)) {
        return emptyFileName();
    }
    std::size_t const counterLength =
        static_cast<std::size_t>(formatted);
    if (counterLength + suffix.length >= kMaximumFileNameBytes)
        return emptyFileName();

    FileName result = emptyFileName();
    result.length = copySanitizedStem(
        result.bytes,
        kMaximumFileNameBytes - counterLength - suffix.length,
        fileName,
        fileNameLength - suffix.length);
    result.bytes[result.length] = '\0';
    if (!appendBytes(&result, counter, counterLength)
            || !appendBytes(
                &result, suffix.bytes, suffix.length)) {
        return emptyFileName();
    }
    return result;
}

} // namespace export_naming
} // namespace cnt
