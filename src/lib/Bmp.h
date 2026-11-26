// Uncompressed BMP pictures (24 or 32 bits per pixel). Pure C++, host-testable.
#pragma once
#include <cstdint>
#include <cstring>

namespace bmp {

struct Info {
    int width = 0, height = 0, bytesPerPixel = 3;
    bool topDown = false;
    uint32_t dataOffset = 0, rowBytes = 0;
};

inline uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

// Reads the first 54 bytes of a file. False for anything this cannot show.
inline bool parse(const uint8_t* h, size_t len, Info& info) {
    if (len < 54 || h[0] != 'B' || h[1] != 'M') return false;
    uint32_t headerSize = u32(h + 14);
    int32_t w = (int32_t)u32(h + 18), hgt = (int32_t)u32(h + 22);
    uint16_t bpp = u16(h + 28);
    uint32_t compression = u32(h + 30);
    if (headerSize < 40 || w <= 0 || hgt == 0 || w > 4096 || hgt > 4096 || hgt < -4096) return false;
    if (!(bpp == 24 && compression == 0) && !(bpp == 32 && (compression == 0 || compression == 3))) return false;
    info.width = w;
    info.topDown = hgt < 0;
    info.height = hgt < 0 ? -hgt : hgt;
    info.bytesPerPixel = bpp / 8;
    info.dataOffset = u32(h + 10);
    info.rowBytes = ((uint32_t)w * info.bytesPerPixel + 3) & ~3u;
    return true;
}

// File position of picture row y (0 = top).
inline uint32_t rowOffset(const Info& info, int y) {
    int stored = info.topDown ? y : info.height - 1 - y;
    return info.dataOffset + (uint32_t)stored * info.rowBytes;
}

// Header of a 24-bit picture stored top to bottom (so rows can be appended as they arrive).
inline void header(uint8_t out[54], int width, int height) {
    memset(out, 0, 54);
    uint32_t rowBytes = ((uint32_t)width * 3 + 3) & ~3u, image = rowBytes * (uint32_t)height;
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) out[at + i] = (uint8_t)(v >> (8 * i)); };
    out[0] = 'B';
    out[1] = 'M';
    put32(2, 54 + image);
    put32(10, 54);
    put32(14, 40);
    put32(18, (uint32_t)width);
    put32(22, (uint32_t)(-height));
    out[26] = 1;
    out[28] = 24;
    put32(34, image);
    put32(38, 2835);                // 72 dpi
    put32(42, 2835);
}

// RGB row -> BMP row (BGR, padded to 4 bytes).
inline size_t packRow(const uint8_t* rgb, int width, uint8_t* out) {
    size_t n = 0;
    for (int x = 0; x < width; x++) {
        out[n++] = rgb[x * 3 + 2];
        out[n++] = rgb[x * 3 + 1];
        out[n++] = rgb[x * 3];
    }
    while (n % 4) out[n++] = 0;
    return n;
}

}  // namespace bmp
