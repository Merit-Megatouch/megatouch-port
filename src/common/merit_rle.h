// Megatouch run-length image frames, shared by .spr (GameDevice games) and .dlt (legacy games).
//
// Frame data is a stream of little-endian 16-bit words. The top 6 bits are an opcode
// and the low 10 bits a count n:
//   0  skip n rows                 3  end of row
//   1  skip n transparent pixels   4  end of frame
//   2  n opaque RGB565 pixels follow (one word each)
//   5  n translucent pixels follow, two words each: RGB565 colour, then a word whose
//      low byte is a 5-bit alpha (0..31)
// Containers (a frame record is { u32 dataBytes; u32 w; u32 h; data }):
//   version 2 (.spr)  u32 2, frame records...
//   version 3 (.dlt)  u32 3, u16 width, height, frameCount, frameDelay, records (delta-coded)
//   version 4 (.spr)  u32 4, u16 frameCount, u32 offset[frameCount] (absolute), records
//                     (newer GameDevice games; *_DSK / *_DYN images)
#pragma once
#include <zlib.h>
#include <cstdint>
#include <cstring>
#include <vector>

struct MeritFrame { int w = 0, h = 0; std::vector<uint32_t> argb; };

inline uint32_t merit_rgb565(uint16_t c, uint32_t a) {
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (a << 24) | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

// Decodes one frame into ARGB8888; skipped pixels stay 0 (transparent).
inline bool merit_rle_decode(const uint8_t* p, size_t bytes, int w, int h, std::vector<uint32_t>& out) {
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
            for (unsigned j = 0; j < k; j++) put(merit_rgb565(word(i + j), 255));
            i += k;
            break;
        case 3: y++; x = 0; break;
        case 4: return true;
        case 5:
            if (i + 2 * k > n) return false;
            for (unsigned j = 0; j < k; j++) {
                uint32_t a = word(i + 2 * j + 1) & 0xff;
                a = a >= 31 ? 255 : a * 255 / 31;
                put(merit_rgb565(word(i + 2 * j), a));
            }
            i += 2 * k;
            break;
        default:
            return false;
        }
    }
    return true;
}

// Reads a (gzip-compressed or plain) file whole.
inline bool merit_read_gz(const char* path, std::vector<uint8_t>& d) {
    gzFile gz = gzopen(path, "rb");
    if (!gz) return false;
    uint8_t buf[65536];
    int r;
    d.clear();
    while ((r = gzread(gz, buf, sizeof buf)) > 0) d.insert(d.end(), buf, buf + r);
    gzclose(gz);
    return !d.empty();
}

// Decodes the frame records starting at `o`; returns how many decoded cleanly.
inline size_t merit_read_frames(const std::vector<uint8_t>& d, size_t o, std::vector<MeritFrame>& frames, size_t limit = (size_t)-1) {
    size_t bad = 0;
    while (o + 12 <= d.size() && frames.size() < limit) {
        uint32_t sz, fw, fh;
        memcpy(&sz, &d[o], 4); memcpy(&fw, &d[o + 4], 4); memcpy(&fh, &d[o + 8], 4);
        if (fw == 0 || fh == 0 || fw > 4096 || fh > 4096 || o + 12 + sz > d.size()) break;
        frames.emplace_back();
        frames.back().w = fw; frames.back().h = fh;
        if (!merit_rle_decode(&d[o + 12], sz, fw, fh, frames.back().argb)) bad++;
        o += 12 + sz;
    }
    return frames.size() - bad;
}

// Where the frame records start and how many there are (-1 = until the end of the data).
inline bool merit_container(const std::vector<uint8_t>& d, size_t& off, size_t& count) {
    if (d.size() < 4) return false;
    uint32_t ver; memcpy(&ver, &d[0], 4);
    switch (ver) {
    case 2: off = 4; count = (size_t)-1; return true;
    case 3: { if (d.size() < 12) return false; uint16_t n; memcpy(&n, &d[8], 2); off = 12; count = n; return true; }
    case 4: { if (d.size() < 6) return false; uint16_t n; memcpy(&n, &d[4], 2); off = 6 + 4 * (size_t)n; count = n; return true; }
    default: return false;
    }
}
