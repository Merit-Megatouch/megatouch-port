// Internals shared by the legacy engine's files: legacy.cpp (C API, screen, input, sound,
// VideoClass), bitmap.cpp (the C++ Bitmap and fonts), sprite.cpp (the sprite engine).
// Not exported from libmerit_legacy.so.
#pragma once
#include "../common/merit_rle.h"
#include "allegro.h"
#include "legacy_internal.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// +0 is the Allegro BITMAP (games blit it with Allegro directly), +4/+8 width/height.
// (Outside the hidden region: exported C-API functions take it.)
struct _RADBitmap { BITMAP* bmp; uint32_t w; uint32_t h; uint32_t* px; uint32_t tag; };
struct Anim;

#pragma GCC visibility push(hidden)

#define LOG(...) do { fprintf(stderr, "[legacy] " __VA_ARGS__); fputc('\n', stderr); } while (0)


static const int SW = 640, SH = 480;              // the 2D API's screen
static const uint32_t kTag = 0x52414442;          // "RADB"
// 32-bit pixels are Allegro's format (0x00RRGGBB); the transparency key is Allegro's 32-bit mask
// colour, magenta. 16-bit (Bitmap objects) use RGB565 with mask 0xF81F.
static const uint32_t kKey = 0x00ff00ff;
static const int kKey16 = 0xf81f;
inline uint32_t to_px(uint32_t argb) { return (argb >> 24) < 128 ? kKey : (argb & 0x00ffffff); }
inline void frames_to_px(std::vector<MeritFrame>& fr) { for (auto& f : fr) for (auto& p : f.argb) p = to_px(p); }
inline int px_to16(uint32_t p) {
    if (p == kKey) return kKey16;
    int c = makecol_depth(16, p >> 16 & 255, p >> 8 & 255, p & 255);
    return c == kKey16 ? kKey16 ^ 0x20 : c;       // opaque magenta must not become transparent
}

// The old API's 8-bit "palette indices".
inline uint32_t color_of(unsigned idx) {
    switch (idx) {
    case 5:   return kKey;
    // 0 and 254/255 were the transparent entries of the 8-bit palettes; the true-colour
    // conversion of the art left those pixels black (airhockey key 0, tennis keys 0xfe/0xff)
    case 0: case 254: case 255: return 0x00000000;
    default:  return idx * 0x010101u;                 // unknown palette: grey ramp
    }
}

// .dlt animations are delta-coded: frame 1 is a full picture, every later frame holds only
// the pixels that changed (the rest are skipped), so frames are composited in order.
struct Anim {
    std::vector<MeritFrame> frames;
    int w = 0, h = 0, delay = 4, cur = 0;
    Uint32 last = 0;
    _RADBitmap* target = nullptr;
    std::vector<uint32_t> canvas;                   // frames[0..cur] composited
};

namespace legacy {
// <name><ext>[.gz] in the language folder (first when lang), the game folder, gamegraphics/misc
bool find_asset(const char* name, bool lang, const char* ext, std::string& out);
Anim* anim_load(const char* name, bool lang);
void anim_seek(Anim* a, int frame);
// 32-bit buffer games draw into now: the open video buffer, else the screen
uint32_t* target_px();
BITMAP* target_bitmap();
BITMAP* vb_bitmap(int id);                        // -1 = the screen
void touched_target();                            // drawing on the screen: present soon
void sleep_ms(Uint32 ms);
Uint32 ticks();                                   // ms since the engine started
// touches for the sprite engine: down/up events in screen coordinates since the last call
struct Touch { int x, y; bool down; };
std::vector<Touch> take_touches();
bool quitting();
_RADBitmap* rad_new(uint32_t w, uint32_t h, uint32_t fill);   // C-API bitmap (BitmapAlloc)
}

#pragma GCC visibility pop
