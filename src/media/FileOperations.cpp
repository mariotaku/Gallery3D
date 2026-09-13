#include "media/FileOperations.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Degrees to the EXIF orientation value. Only the three rotations the port
// understands; anything else means upright.
unsigned orientationForDegrees(float degrees) {
    int normalized = ((int)(degrees + 0.5f) % 360 + 360) % 360;
    switch (normalized) {
    case 90:
        return 6;
    case 180:
        return 3;
    case 270:
        return 8;
    default:
        return 1;
    }
}

}  // namespace

namespace FileOperations {

bool setExifOrientation(const std::string &path, float degrees) {
    // Find the orientation tag the same way Bitmap::readExif does, then write
    // over its value in place. Nothing else in the file moves.
    std::FILE *file = std::fopen(path.c_str(), "r+b");
    if (!file) {
        return false;
    }

    std::vector<uint8_t> header(65536);
    size_t read = std::fread(header.data(), 1, header.size(), file);
    if (read < 12 || header[0] != 0xFF || header[1] != 0xD8) {
        std::fclose(file);
        return false;
    }

    size_t pos = 2;
    while (pos + 4 <= read) {
        if (header[pos] != 0xFF) {
            break;
        }
        uint8_t marker = header[pos + 1];
        size_t length = ((size_t)header[pos + 2] << 8) | header[pos + 3];
        if (length < 2 || pos + 2 + length > read) {
            break;
        }
        if (marker == 0xE1 && length >= 16 && std::memcmp(&header[pos + 4], "Exif\0\0", 6) == 0) {
            const size_t tiffStart = pos + 10;
            const uint8_t *tiff = &header[tiffStart];
            size_t tiffLength = length - 8;
            bool bigEndian = tiff[0] == 'M';
            auto read16 = [&](size_t offset) -> unsigned {
                if (offset + 2 > tiffLength) {
                    return 0;
                }
                return bigEndian ? (unsigned)((tiff[offset] << 8) | tiff[offset + 1])
                                 : (unsigned)((tiff[offset + 1] << 8) | tiff[offset]);
            };
            auto read32 = [&](size_t offset) -> unsigned {
                if (offset + 4 > tiffLength) {
                    return 0;
                }
                if (bigEndian) {
                    return ((unsigned)tiff[offset] << 24) | ((unsigned)tiff[offset + 1] << 16) |
                           ((unsigned)tiff[offset + 2] << 8) | (unsigned)tiff[offset + 3];
                }
                return ((unsigned)tiff[offset + 3] << 24) | ((unsigned)tiff[offset + 2] << 16) |
                       ((unsigned)tiff[offset + 1] << 8) | (unsigned)tiff[offset];
            };

            unsigned ifdOffset = read32(4);
            unsigned entryCount = read16(ifdOffset);
            for (unsigned i = 0; i < entryCount; ++i) {
                size_t entry = (size_t)ifdOffset + 2 + (size_t)i * 12;
                if (entry + 12 > tiffLength) {
                    break;
                }
                if (read16(entry) != 0x0112) {
                    continue;
                }
                // A SHORT sits in the first two bytes of the value field.
                unsigned value = orientationForDegrees(degrees);
                uint8_t bytes[2];
                if (bigEndian) {
                    bytes[0] = (uint8_t)(value >> 8);
                    bytes[1] = (uint8_t)(value & 0xFF);
                } else {
                    bytes[0] = (uint8_t)(value & 0xFF);
                    bytes[1] = (uint8_t)(value >> 8);
                }
                long fileOffset = (long)(tiffStart + entry + 8);
                if (std::fseek(file, fileOffset, SEEK_SET) != 0 ||
                    std::fwrite(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
                    std::fclose(file);
                    return false;
                }
                std::fclose(file);
                return true;
            }
            break;
        }
        if (marker == 0xDA) {
            break;
        }
        pos += 2 + length;
    }

    std::fclose(file);
    return false;
}

}  // namespace FileOperations
