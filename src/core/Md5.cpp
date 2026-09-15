#include "core/Md5.h"

#include <cstdint>
#include <vector>

namespace {

// floor(abs(sin(i + 1)) * 2^32), from RFC 1321.
const uint32_t kSine[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

// How far each step rotates, four rounds of four.
const int kShift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

uint32_t rotateLeft(uint32_t value, int count) {
    return (value << count) | (value >> (32 - count));
}

}  // namespace

std::string Md5::hex(const std::string &text) {
    uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};

    // A one bit, zeros up to 56 bytes into a block, then the length in bits.
    std::vector<uint8_t> message(text.begin(), text.end());
    const uint64_t bits = (uint64_t)text.size() * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56) {
        message.push_back(0);
    }
    for (int i = 0; i < 8; ++i) {
        message.push_back((uint8_t)(bits >> (8 * i)));
    }

    for (size_t block = 0; block < message.size(); block += 64) {
        uint32_t words[16];
        for (int i = 0; i < 16; ++i) {
            const uint8_t *p = &message[block + (size_t)i * 4];
            words[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }
        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t mixed;
            int word;
            if (i < 16) {
                mixed = (b & c) | (~b & d);
                word = i;
            } else if (i < 32) {
                mixed = (d & b) | (~d & c);
                word = (5 * i + 1) % 16;
            } else if (i < 48) {
                mixed = b ^ c ^ d;
                word = (3 * i + 5) % 16;
            } else {
                mixed = c ^ (b | ~d);
                word = (7 * i) % 16;
            }
            mixed += a + kSine[i] + words[word];
            a = d;
            d = c;
            c = b;
            b += rotateLeft(mixed, kShift[i]);
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    }

    static const char kHex[] = "0123456789abcdef";
    std::string digest;
    digest.reserve(32);
    for (uint32_t word : state) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t byte = (uint8_t)(word >> (8 * i));
            digest += kHex[byte >> 4];
            digest += kHex[byte & 0x0F];
        }
    }
    return digest;
}
