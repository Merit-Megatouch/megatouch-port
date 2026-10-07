// Autodesk FLIC (.fli/.flc) decoder: 8-bit palettized animations (brickbreaker, stairs and
// solitaire ship some art this way). Produces full ARGB frames; palette index 0 is returned
// with alpha 0 (the transparent colour of these sprites).
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

struct FlicFrame { int w = 0, h = 0; std::vector<uint32_t> argb; };

inline bool flic_decode(const std::vector<uint8_t>& d, std::vector<FlicFrame>& out, int* delay_ms = nullptr) {
    auto u16 = [&](size_t o) -> unsigned { return o + 2 <= d.size() ? (unsigned)(d[o] | d[o + 1] << 8) : 0; };
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= d.size() ? (uint32_t)(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24) : 0; };
    if (d.size() < 128) return false;
    unsigned magic = u16(4);
    if (magic != 0xaf12 && magic != 0xaf11) return false;
    int frames = (int)u16(6), w = (int)u16(8), h = (int)u16(10);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    if (delay_ms) *delay_ms = magic == 0xaf12 ? (int)u32(16) : (int)u32(16) * 1000 / 70;
    std::vector<uint8_t> idx((size_t)w * h, 0);
    uint32_t pal[256] = {};
    size_t o = magic == 0xaf12 && u32(80) ? u32(80) : 128;
    for (int f = 0; f < frames && o + 16 <= d.size(); f++) {
        uint32_t fsize = u32(o);
        if (fsize < 16 || o + fsize > d.size()) break;
        if (u16(o + 4) != 0xf1fa) { o += fsize; f--; continue; }    // prefix/other chunk
        int chunks = (int)u16(o + 6);
        size_t c = o + 16;
        for (int k = 0; k < chunks && c + 6 <= o + fsize; k++) {
            uint32_t csize = u32(c);
            unsigned type = u16(c + 4);
            size_t p = c + 6, end = c + csize;
            if (csize < 6 || end > d.size()) break;
            switch (type) {
            case 4: case 11: {                   // COLOR_256 / COLOR_64
                unsigned packets = u16(p); p += 2;
                int ci = 0;
                for (unsigned n = 0; n < packets && p < end; n++) {
                    ci += d[p++];
                    int cnt = d[p++]; if (!cnt) cnt = 256;
                    for (int i = 0; i < cnt && ci < 256 && p + 3 <= end; i++, ci++, p += 3) {
                        int r = d[p], g = d[p + 1], b = d[p + 2];
                        if (type == 11) { r = r * 255 / 63; g = g * 255 / 63; b = b * 255 / 63; }
                        pal[ci] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
                    }
                }
                break;
            }
            case 13: std::fill(idx.begin(), idx.end(), 0); break;   // BLACK
            case 16:                                                  // FLI_COPY
                for (size_t i = 0; i < idx.size() && p + i < end; i++) idx[i] = d[p + i];
                break;
            case 15:                                                  // BYTE_RUN
                for (int y = 0; y < h && p < end; y++) {
                    p++;                                              // packet count (unused)
                    int x = 0;
                    while (x < w && p < end) {
                        int8_t cnt = (int8_t)d[p++];
                        if (cnt > 0) { uint8_t v = d[p++]; for (int i = 0; i < cnt && x < w; i++) idx[(size_t)y * w + x++] = v; }
                        else for (int i = 0; i < -cnt && x < w && p < end; i++) idx[(size_t)y * w + x++] = d[p++];
                    }
                }
                break;
            case 12: {                                                // DELTA_FLI (byte)
                int y = (int)u16(p), lines = (int)u16(p + 2); p += 4;
                for (int l = 0; l < lines && y < h && p < end; l++, y++) {
                    int packets = d[p++], x = 0;
                    for (int n = 0; n < packets && p < end; n++) {
                        x += d[p++];
                        int8_t cnt = (int8_t)d[p++];
                        if (cnt > 0) for (int i = 0; i < cnt && p < end; i++, x++) { if (x < w) idx[(size_t)y * w + x] = d[p]; p++; }
                        else { uint8_t v = d[p++]; for (int i = 0; i < -cnt; i++, x++) if (x < w) idx[(size_t)y * w + x] = v; }
                    }
                }
                break;
            }
            case 7: {                                                 // DELTA_FLC (word)
                int lines = (int)u16(p); p += 2;
                int y = 0;
                for (int l = 0; l < lines && y < h && p + 2 <= end; ) {
                    int16_t op = (int16_t)u16(p); p += 2;
                    if ((op & 0xc000) == 0xc000) { y += -op; continue; }
                    if ((op & 0xc000) == 0x8000) { idx[(size_t)y * w + w - 1] = (uint8_t)(op & 0xff); continue; }
                    int x = 0;
                    for (int n = 0; n < op && p + 2 <= end; n++) {
                        x += d[p++];
                        int8_t cnt = (int8_t)d[p++];
                        if (cnt > 0) for (int i = 0; i < cnt && p + 2 <= end; i++, x += 2, p += 2) {
                            if (x < w) idx[(size_t)y * w + x] = d[p];
                            if (x + 1 < w) idx[(size_t)y * w + x + 1] = d[p + 1];
                        } else {
                            uint8_t a = d[p], b = d[p + 1]; p += 2;
                            for (int i = 0; i < -cnt; i++, x += 2) { if (x < w) idx[(size_t)y * w + x] = a; if (x + 1 < w) idx[(size_t)y * w + x + 1] = b; }
                        }
                    }
                    y++; l++;
                }
                break;
            }
            default: break;                                           // PSTAMP and others
            }
            c = end;
        }
        FlicFrame fr; fr.w = w; fr.h = h; fr.argb.resize(idx.size());
        for (size_t i = 0; i < idx.size(); i++) fr.argb[i] = idx[i] ? pal[idx[i]] : 0;
        out.push_back(std::move(fr));
        o += fsize;
    }
    return !out.empty();
}
