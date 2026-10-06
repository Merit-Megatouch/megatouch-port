// Loader for Megatouch .spr / .spr.gz sprite animations.
//
// File:  u32 version (2), then frames { u32 dataBytes; u32 width; u32 height; data },
//        then an 8-byte zero trailer.
// Frame data is a stream of little-endian 16-bit words. The top 6 bits are an opcode
// and the low 10 bits a count n:
//   0  skip n rows                 3  end of row
//   1  skip n transparent pixels   4  end of frame
//   2  n opaque RGB565 pixels follow (one word each)
//   5  n translucent pixels follow, two words each: RGB565 colour, then a word whose
//      low byte is a 5-bit alpha (0..31)
#include <zlib.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "backend.h"

static inline uint32_t argb(uint16_t c, uint32_t a) {
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (a << 24) | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

static bool decode_frame(const uint8_t* p, size_t bytes, int w, int h, std::vector<uint32_t>& out) {
    out.assign((size_t)w * h, 0);
    size_t n = bytes / 2, i = 0;
    auto word = [&](size_t k) { return (uint16_t)(p[2 * k] | p[2 * k + 1] << 8); };
    int x = 0, y = 0;
    auto put = [&](uint32_t v) {
        if (x >= 0 && x < w && y >= 0 && y < h) out[(size_t)y * w + x] = v;
        x++;
    };
    while (i < n) {
        uint16_t c = word(i++);
        unsigned op = c >> 10, k = c & 0x3ff;
        switch (op) {
        case 0: y += k; break;
        case 1: x += k; break;
        case 2:
            if (i + k > n) return false;
            for (unsigned j = 0; j < k; j++) put(argb(word(i + j), 255));
            i += k;
            break;
        case 3: y++; x = 0; break;
        case 4: return true;
        case 5:
            if (i + 2 * k > n) return false;
            for (unsigned j = 0; j < k; j++) {
                uint32_t a = word(i + 2 * j + 1) & 0xff;
                a = a >= 31 ? 255 : a * 255 / 31;
                put(argb(word(i + 2 * j), a));
            }
            i += 2 * k;
            break;
        default:
            return false;
        }
    }
    return true;
}

TexData* load_spr(const char* path) {
    gzFile gz = gzopen(path, "rb");
    if (!gz) return nullptr;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    int r;
    while ((r = gzread(gz, buf, sizeof buf)) > 0) d.insert(d.end(), buf, buf + r);
    gzclose(gz);

    std::vector<std::vector<uint32_t>> frames;
    int w = 0, h = 0;
    size_t o = 4;
    while (o + 12 <= d.size()) {
        uint32_t sz, fw, fh;
        memcpy(&sz, &d[o], 4); memcpy(&fw, &d[o + 4], 4); memcpy(&fh, &d[o + 8], 4);
        if (fw == 0 || fh == 0 || o + 12 + sz > d.size()) break;
        if (frames.empty()) { w = fw; h = fh; }
        frames.emplace_back();
        if (!decode_frame(&d[o + 12], sz, w, h, frames.back())) {
            fprintf(stderr, "[trix] bad frame %zu in %s\n", frames.size() - 1, path);
        }
        o += 12 + sz;
    }
    if (frames.empty()) return nullptr;
    return spr_make_texture(w, h, frames);
}
