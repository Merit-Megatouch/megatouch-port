// Loader for Megatouch .spr / .spr.gz sprite animations.
//
// File:  u32 version (2), then frames { u32 dataBytes; u32 width; u32 height; data },
//        then an 8-byte zero trailer.
// Frame decoding (shared with the legacy .dlt loader) is in src/common/merit_rle.h.
#include <cstdio>
#include <vector>

#include "../common/merit_rle.h"
#include "backend.h"

TexData* load_spr(const char* path) {
    std::vector<uint8_t> d;
    if (!merit_read_gz(path, d)) return nullptr;
    std::vector<MeritFrame> mf;
    size_t good = merit_read_frames(d, 4, mf);
    if (mf.empty()) return nullptr;
    if (good != mf.size()) fprintf(stderr, "[mega] %zu bad frame(s) in %s\n", mf.size() - good, path);
    std::vector<std::vector<uint32_t>> frames;
    for (auto& f : mf) frames.push_back(std::move(f.argb));
    return spr_make_texture(mf[0].w, mf[0].h, frames);
}
