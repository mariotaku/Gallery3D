#include "graphics/EmbeddedProfile.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace {

// A profile is a few kilobytes, a few hundred for a large one. A chunk that
// claims more than this is malformed and is not inflated without end.
const size_t kMaxProfile = 16 * 1024 * 1024;

uint32_t bigEndian32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

uint32_t littleEndian32(const uint8_t *bytes) {
    return ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[1] << 8) | (uint32_t)bytes[0];
}

// A JPEG splits a profile across APP2 segments, each opening with
// "ICC_PROFILE", its number and the count of segments, both counted from 1.
std::vector<uint8_t> fromJpeg(const uint8_t *data, size_t size) {
    static const char kTag[12] = {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', '\0'};
    std::map<int, std::vector<uint8_t>> parts;
    int count = 0;
    size_t at = 2;
    while (at + 4 <= size && data[at] == 0xFF) {
        const uint8_t marker = data[at + 1];
        // The profile comes before the image data.
        if (marker == 0xDA || marker == 0xD9) {
            break;
        }
        const size_t length = ((size_t)data[at + 2] << 8) | data[at + 3];
        if (length < 2 || length > size - at - 2) {
            break;
        }
        if (marker == 0xE2 && length >= 16 && std::memcmp(&data[at + 4], kTag, sizeof(kTag)) == 0) {
            count = data[at + 17];
            parts[data[at + 16]].assign(&data[at + 18], &data[at + 2 + length]);
        }
        at += 2 + length;
    }
    std::vector<uint8_t> profile;
    if (count == 0 || (int)parts.size() != count) {
        return profile;
    }
    for (int part = 1; part <= count; ++part) {
        const auto found = parts.find(part);
        if (found == parts.end()) {
            return std::vector<uint8_t>();
        }
        profile.insert(profile.end(), found->second.begin(), found->second.end());
    }
    return profile;
}

// A zlib stream, inflated whole, or empty when it is not a complete one.
std::vector<uint8_t> inflated(const uint8_t *data, size_t size) {
    std::vector<uint8_t> out(std::min<size_t>(size * 4 + 1024, kMaxProfile));
    z_stream stream {};
    if (inflateInit(&stream) != Z_OK) {
        return std::vector<uint8_t>();
    }
    stream.next_in = (Bytef *)data;
    stream.avail_in = (uInt)size;
    int result = Z_OK;
    while (result == Z_OK || (result == Z_BUF_ERROR && stream.avail_out == 0)) {
        if (stream.total_out == out.size()) {
            if (out.size() >= kMaxProfile) {
                break;
            }
            out.resize(std::min(out.size() * 2, kMaxProfile));
        }
        stream.next_out = out.data() + stream.total_out;
        stream.avail_out = (uInt)(out.size() - stream.total_out);
        result = inflate(&stream, Z_NO_FLUSH);
    }
    const bool complete = result == Z_STREAM_END;
    const size_t total = stream.total_out;
    inflateEnd(&stream);
    if (!complete) {
        return std::vector<uint8_t>();
    }
    out.resize(total);
    return out;
}

// PNG keeps the profile in an iCCP chunk before the image data: a name of up
// to 79 characters, a zero, a compression method that is always zero, and the
// profile deflated.
std::vector<uint8_t> fromPng(const uint8_t *data, size_t size) {
    size_t at = 8;
    while (at + 12 <= size) {
        const uint32_t length = bigEndian32(&data[at]);
        if (length > size - at - 12) {
            break;
        }
        const uint8_t *type = &data[at + 4];
        if (std::memcmp(type, "IDAT", 4) == 0 || std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        if (std::memcmp(type, "iCCP", 4) == 0) {
            const uint8_t *chunk = &data[at + 8];
            const uint8_t *end = chunk + length;
            const uint8_t *nameEnd = std::find(chunk, std::min(end, chunk + 80), (uint8_t)0);
            if (nameEnd == end || end - nameEnd < 2 || nameEnd[1] != 0) {
                return std::vector<uint8_t>();
            }
            return inflated(nameEnd + 2, (size_t)(end - (nameEnd + 2)));
        }
        at += 12 + (size_t)length;
    }
    return std::vector<uint8_t>();
}

// WebP keeps the profile whole in an ICCP chunk of its RIFF container. Chunks
// are padded to an even length.
std::vector<uint8_t> fromWebp(const uint8_t *data, size_t size) {
    size_t at = 12;
    while (at + 8 <= size) {
        const uint32_t length = littleEndian32(&data[at + 4]);
        if (length > size - at - 8) {
            break;
        }
        if (std::memcmp(&data[at], "ICCP", 4) == 0) {
            return std::vector<uint8_t>(&data[at + 8], &data[at + 8] + length);
        }
        at += 8 + (size_t)length + (length & 1u);
    }
    return std::vector<uint8_t>();
}

// TIFF keeps the profile whole under tag 34675 of the first IFD.
std::vector<uint8_t> fromTiff(const uint8_t *data, size_t size) {
    const bool bigEndian = data[0] == 'M';
    auto read16 = [&](size_t at) -> uint32_t {
        if (at + 2 > size) {
            return 0;
        }
        return bigEndian ? ((uint32_t)data[at] << 8) | data[at + 1] : ((uint32_t)data[at + 1] << 8) | data[at];
    };
    auto read32 = [&](size_t at) -> uint32_t {
        if (at + 4 > size) {
            return 0;
        }
        return bigEndian ? bigEndian32(&data[at]) : littleEndian32(&data[at]);
    };
    const uint32_t ifd = read32(4);
    const uint32_t entries = read16(ifd);
    for (uint32_t i = 0; i < entries; ++i) {
        const size_t entry = (size_t)ifd + 2 + (size_t)i * 12;
        if (entry + 12 > size) {
            break;
        }
        if (read16(entry) == 34675) {
            const uint32_t length = read32(entry + 4);
            // A value of four bytes or fewer sits in the entry itself.
            const size_t offset = (length <= 4) ? entry + 8 : (size_t)read32(entry + 8);
            if (length == 0 || length > kMaxProfile || offset > size || length > size - offset) {
                return std::vector<uint8_t>();
            }
            return std::vector<uint8_t>(data + offset, data + offset + length);
        }
    }
    return std::vector<uint8_t>();
}

}  // namespace

std::vector<uint8_t> EmbeddedProfile::of(const void *bytes, size_t size) {
    const uint8_t *data = (const uint8_t *)bytes;
    if (bytes == nullptr || size < 12) {
        return std::vector<uint8_t>();
    }
    static const uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (data[0] == 0xFF && data[1] == 0xD8) {
        return fromJpeg(data, size);
    }
    if (std::memcmp(data, kPngSignature, sizeof(kPngSignature)) == 0) {
        return fromPng(data, size);
    }
    if (std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0) {
        return fromWebp(data, size);
    }
    if (std::memcmp(data, "II*\0", 4) == 0 || std::memcmp(data, "MM\0*", 4) == 0) {
        return fromTiff(data, size);
    }
    return std::vector<uint8_t>();
}
