// libmerit_legacy.so — stand-in for the cabinet loader's legacy 2D engine (pre-2009 games).
//
// Legacy games call the loader directly through an immediate-mode C API inherited from the
// DOS era: load a bitmap, blit it to the screen, register named touch zones, wait for input.
// Everything here was reconstructed from how the games call it (no loader source exists);
// see docs/reference/legacy.md. Fourplay is the reference game.
//
// Model:
//  * The screen is a 640x480 ARGB buffer, presented through SDL whenever time passes
//    (Delay, InputCharOrDelay, SystemTimer, AnimationStillDelay, voice polling).
//  * Bitmaps are _RADBitmap { BITMAP*; u32 width; u32 height; ... } — games read +4/+8 and blit +0
//    with Allegro (src/legacy/allegro.cpp). The screen is Allegro's `screen` (32-bit memory bitmap).
//  * Colours are 8-bit "palette indices" from the old API: 0 = black, 5 = the transparency key.
//    Art is RGB565 (.dlt); pixels the RLE skips are the key colour (stored as 0x00000000).
//  * Animations (_MSmack, after RAD's Smacker) are .dlt files: u32 version 3, u16 w, h,
//    frames, delay, then run-length frames (src/common/merit_rle.h).
#include "../common/env.h"
#include "../common/merit_rle.h"
#include "legacy_internal.h"
#include "allegro.h"
#include "legacy_ttf.h"
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "engine.h"
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);
#include "../common/flic.h"
#include <algorithm>

// ---------------------------------------------------------------------------------- types
extern "C" { extern volatile int mouse_x, mouse_y, mouse_b, mouse_pos, mouse_button_presses_cached; }
struct _MSmack;
struct SoundBase;

// ---------------------------------------------------------------------------------- screen
static uint32_t g_fallbackScreen[SW * SH];
static uint32_t* g_screen = g_fallbackScreen;     // Allegro's `screen` bitmap once it is set up
static bool g_gl = menv("GL") != nullptr;
bool gl_mode() { return g_gl; }        // Merit3D games: an OpenGL window instead
static SDL_GLContext g_glctx;
static int g_mouseX, g_mouseY;
static bool g_mouseDown;
static SDL_Window* g_win;
static SDL_Renderer* g_ren;
static SDL_Texture* g_tex;
static bool g_dirty = true, g_quit;
static Uint32 g_start, g_lastPresent, g_quitAt;

struct Zone { std::string name; int x, y, w, h; };
static int g_shownVB = -1;                       // the buffer last shown (base for the z list)
static bool g_zdirty;
void zlist_redraw();
static std::vector<legacy::Touch> g_touches;      // for the sprite engine (take_touches)
static std::vector<Zone> g_zones;
static std::vector<std::string> g_pending;       // touched zone names not yet read

static void audio_init();
static bool touch_debug() { static bool on = menv("DEBUG_TOUCH") != nullptr; return on; }

// Allegro's `screen` is our framebuffer; gfx_driver reports the mode size
__attribute__((constructor)) static void legacy_screen_ctor() {
    screen = legacy::al_wrap32(g_fallbackScreen, SW, SH);
    legacy::al_set_mode(legacy::screen_w(), legacy::screen_h());
    // Merit3D/AllegroGL games call OpenGL straight away: the loader had the context open
    if (g_gl) legacy::video_init();
}

static void video_init() {
    if (g_win) return;
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    const char* title = menv("TITLE") ? menv("TITLE") : "Megatouch";
    if (g_gl) {
        // the cabinet loader set up an AllegroGL mode before starting a Merit3D game
        int w = menv_int("WIDTH", 1024), h = menv_int("HEIGHT", 768);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_OPENGL);
        if (!g_win) { LOG("SDL_CreateWindow: %s", SDL_GetError()); _exit(1); }
        g_glctx = SDL_GL_CreateContext(g_win);
        if (!g_glctx) { LOG("SDL_GL_CreateContext: %s", SDL_GetError()); _exit(1); }
        SDL_GL_MakeCurrent(g_win, g_glctx);
        SDL_GL_SetSwapInterval(1);
        g_start = SDL_GetTicks();
        audio_init();
        LOG("OpenGL window %dx%d", w, h);
        return;
    }
    g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SW * 2, SH * 2, SDL_WINDOW_RESIZABLE);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(g_ren, SW, SH);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, SH);
    g_start = SDL_GetTicks();
    for (int i = 0; i < SW * SH; i++) g_screen[i] = 0;
    audio_init();
    LOG("screen %dx%d", SW, SH);
}

static void present() {
    static uint32_t out[SW * SH];
    // the transparency key copied by an opaque blit shows as black (palette index 0 on the cabinet)
    for (int i = 0; i < SW * SH; i++) { uint32_t p = g_screen[i] & 0xffffff; out[i] = p == kKey ? 0xff000000 : p | 0xff000000; }
    SDL_UpdateTexture(g_tex, nullptr, out, SW * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
    SDL_RenderPresent(g_ren);
    g_dirty = false;
    g_lastPresent = SDL_GetTicks();
    // MEGA_SHOT_DIR / MEGA_SHOT_EVERY (presents) for headless checks
    static const char* dir = menv("SHOT_DIR");
    static int every = menv_int("SHOT_EVERY", 30), n;
    if (dir && ++n % every == 0) {
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(out, SW, SH, 32, SW * 4, SDL_PIXELFORMAT_ARGB8888);
        char path[1024];
        snprintf(path, sizeof path, "%s/frame%05d.bmp", dir, n);
        SDL_SaveBMP(s, path);
        SDL_FreeSurface(s);
    }
}

// Scripted taps for headless tests: MEGA_AUTOCLICK="ms:x,y;ms:x,y" (milliseconds since start).
static Uint32 g_autoRelease;
static void autoclick() {
    static const char* spec = menv("AUTOCLICK");
    static size_t pos;
    if (!spec) return;
    if (g_autoRelease && SDL_GetTicks() >= g_autoRelease) {
        g_mouseDown = false; g_autoRelease = 0;
        g_touches.push_back({g_mouseX, g_mouseY, false});
    }
    while (spec[pos]) {
        unsigned t; int x, y, used = 0;
        if (sscanf(spec + pos, "%u:%d,%d%n", &t, &x, &y, &used) != 3) { spec = nullptr; return; }
        if (SDL_GetTicks() - g_start < t) return;
        pos += used + (spec[pos + used] == ';');
        // games that poll the mouse (MouseX/Y, mouse_b, GetTouchCoord) see a 150 ms press
        g_mouseX = x; g_mouseY = y; g_mouseDown = true; g_autoRelease = SDL_GetTicks() + 150;
        g_touches.push_back({x, y, true});
        for (auto& z : g_zones)
            if (x >= z.x && x < z.x + z.w && y >= z.y && y < z.y + z.h) { g_pending.push_back(z.name); break; }
    }
}

// Handles window events, touches and presents if the screen changed.
static void pump() {
    video_init();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: g_quit = true; g_quitAt = SDL_GetTicks(); g_pending.push_back("ESCAPE"); break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_ESCAPE) g_pending.push_back("ESCAPE");
            if (e.key.keysym.sym == SDLK_F11 && !g_gl) {
                bool fs = SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            break;
        case SDL_MOUSEMOTION:
            g_mouseX = e.motion.x; g_mouseY = e.motion.y;
            break;
        case SDL_MOUSEBUTTONUP:
            g_mouseDown = false;
            g_touches.push_back({e.button.x, e.button.y, false});
            break;
        case SDL_MOUSEBUTTONDOWN: {
            // SDL already reports mouse positions in logical (640x480) coordinates
            int x = e.button.x, y = e.button.y;
            g_mouseX = x; g_mouseY = y; g_mouseDown = true;
            g_touches.push_back({x, y, true});
            const char* hit = nullptr;
            // the most recently added zone wins where zones overlap
            for (auto it = g_zones.rbegin(); it != g_zones.rend(); ++it)
                if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) {
                    g_pending.push_back(it->name);
                    hit = it->name.c_str();
                    break;
                }
            if (touch_debug()) LOG("t=%u touch %d,%d -> %s (%zu zones, %zu queued)",
                                   SDL_GetTicks() - g_start, x, y, hit ? hit : "(no zone)",
                                   g_zones.size(), g_pending.size());
            break;
        }
        }
    }
    autoclick();
    if (g_quit && SDL_GetTicks() - g_quitAt > 3000) { LOG("window closed"); _exit(0); }
    // headless checks also re-present a still screen once a second so screenshots keep coming
    static const bool shots = menv("SHOT_DIR");
    if (g_zdirty) zlist_redraw();
    mouse_x = g_mouseX; mouse_y = g_mouseY; mouse_b = g_mouseDown ? 1 : 0; mouse_pos = g_mouseX << 16 | g_mouseY;
    if (g_mouseDown) mouse_button_presses_cached |= 1;
    if (!g_gl && (g_dirty || (shots && SDL_GetTicks() - g_lastPresent >= 1000)) && SDL_GetTicks() - g_lastPresent >= 15) present();
}

static void sleep_ms(Uint32 ms) {
    Uint32 end = SDL_GetTicks() + ms;
    do { pump(); SDL_Delay(1); } while (!g_quit && SDL_GetTicks() < end);
}

// ---------------------------------------------------------------------------------- bitmaps
static _RADBitmap* bmp_new(uint32_t w, uint32_t h, uint32_t fill) {
    auto* b = static_cast<_RADBitmap*>(calloc(1, sizeof(_RADBitmap)));
    if (w == 0 || h == 0 || w > 4096 || h > 4096) { LOG("bitmap %ux%u requested; using 1x1", w, h); w = h = 1; }
    b->tag = kTag; b->w = w; b->h = h;
    b->bmp = create_bitmap_ex(32, (int)w, (int)h);
    b->px = static_cast<uint32_t*>(b->bmp->dat);
    for (size_t i = 0; i < (size_t)w * h; i++) b->px[i] = fill;
    return b;
}

// Copies a w x h block; `trans` skips pixels equal to `key`.
static void blit(const uint32_t* src, int sw, int sh, int sx, int sy,
                 uint32_t* dst, int dw, int dh, int dx, int dy, int w, int h, bool trans, uint32_t key) {
    for (int y = 0; y < h; y++) {
        int ys = sy + y, yd = dy + y;
        if (ys < 0 || ys >= sh || yd < 0 || yd >= dh) continue;
        for (int x = 0; x < w; x++) {
            int xs = sx + x, xd = dx + x;
            if (xs < 0 || xs >= sw || xd < 0 || xd >= dw) continue;
            uint32_t p = src[ys * sw + xs];
            // the requested key colour, or a pixel the .dlt run-length skipped (transparent
            // whatever palette index the game uses for it: 5 in Fourplay, 0x60 in Conquest)
            if (trans && (p == key || p == kKey)) continue;
            dst[yd * dw + xd] = p;
        }
    }
}

// ---------------------------------------------------------------------------------- .dlt files
// .dlt animations are delta-coded: frame 1 is a full picture, every later frame holds only
// the pixels that changed (the rest are skipped), so frames are composited in order.
static void anim_overlay(Anim* a, int f) {
    const MeritFrame& fr = a->frames[f];
    for (int y = 0; y < fr.h && y < a->h; y++)
        for (int x = 0; x < fr.w && x < a->w; x++) {
            uint32_t p = fr.argb[(size_t)y * fr.w + x];
            if (p != kKey) a->canvas[(size_t)y * a->w + x] = p;
        }
}

// Rebuilds the canvas for frame `f` (from frame 1 when going backwards or wrapping).
static void anim_seek(Anim* a, int f) {
    if (a->full) {
        f = std::clamp(f, 0, (int)a->frames.size() - 1);
        a->canvas = a->frames[f].argb;
        a->cur = f;
        return;
    }
    if (a->canvas.empty() || f < a->cur || f == 0) {
        a->canvas.assign((size_t)a->w * a->h, kKey);
        a->cur = 0;
        anim_overlay(a, 0);
    }
    while (a->cur < f) anim_overlay(a, ++a->cur);
}

// <name><ext>[.gz] in the language folder (when asked), the game's folder (the working dir),
// then the legacy games' shared folder gamegraphics/misc.
static bool find_asset(const char* name, bool lang, const char* ext, std::string& out) {
    std::string l = menv("LANGUAGE") ? menv("LANGUAGE") : "english";
    std::vector<std::string> dirs = lang ? std::vector<std::string>{l + "/", ""} : std::vector<std::string>{"", l + "/"};
    dirs.push_back("../misc/");
    dirs.push_back("../misc/" + l + "/");
    const char* exts[] = {".gz", ""};
    for (auto& d : dirs)
        for (auto* z : exts) {
            std::string p = d + name + ext + z;
            struct stat st;
            if (stat(p.c_str(), &st) == 0) { out = p; return true; }
        }
    return false;
}

static Anim* anim_load(const char* name, bool lang) {
    std::string path;
    if (find_asset(name, lang, ".flc", path) || find_asset(name, lang, ".fli", path)) {
        std::vector<uint8_t> d;
        std::vector<FlicFrame> ff;
        int delay = 0;
        if (!merit_read_gz(path.c_str(), d) || !flic_decode(d, ff, &delay)) { LOG("bad FLIC %s", path.c_str()); return nullptr; }
        auto* a = new Anim;
        a->full = true;
        a->w = ff[0].w; a->h = ff[0].h;
        if (delay > 0) a->delay = std::max(1, delay * 60 / 1000);
        for (auto& f : ff) { MeritFrame m; m.w = f.w; m.h = f.h; m.argb = std::move(f.argb); a->frames.push_back(std::move(m)); }
        frames_to_px(a->frames);
        return a;
    }
    if (!find_asset(name, lang, ".dlt", path) && !find_asset(name, lang, ".spr", path) && !find_asset(name, lang, "", path)) {
        LOG("missing animation %s", name); return nullptr;
    }
    std::vector<uint8_t> d;
    if (!merit_read_gz(path.c_str(), d) || d.size() < 12) { LOG("unreadable %s", path.c_str()); return nullptr; }
    auto* a = new Anim;
    uint32_t ver; uint16_t hdr[4];
    memcpy(&ver, &d[0], 4); memcpy(hdr, &d[4], 8);
    size_t off, count;
    if (!merit_container(d, off, count)) { LOG("unknown version in %s", path.c_str()); delete a; return nullptr; }
    if (ver == 3 && hdr[3]) a->delay = hdr[3];
    merit_read_frames(d, off, a->frames, count);
    frames_to_px(a->frames);
    if (a->frames.empty()) { LOG("no frames in %s", path.c_str()); delete a; return nullptr; }
    a->w = ver == 3 ? hdr[0] : a->frames[0].w;
    a->h = ver == 3 ? hdr[1] : a->frames[0].h;
    return a;
}

static void anim_to(Anim* a, _RADBitmap* b) {
    if (!a || !b || a->frames.empty()) return;
    if (a->canvas.empty()) anim_seek(a, a->cur);
    for (size_t i = 0; i < (size_t)b->w * b->h; i++) b->px[i] = kKey;
    blit(a->canvas.data(), a->w, a->h, 0, 0, b->px, b->w, b->h, 0, 0, a->w, a->h, false, 0);
}

// ---------------------------------------------------------------------------------- sound
struct Pcm { std::vector<int16_t> s; };             // 44.1 kHz stereo
struct Voice { int id; const short* s; size_t n; size_t pos; float vol; bool loop; };
static SDL_AudioDeviceID g_audio;
static std::mutex g_amx;
static std::map<std::string, Pcm*> g_waves;
static std::vector<Voice> g_voices;
static int g_nextVoice = 1;

static void audio_cb(void*, Uint8* stream, int len) {
    auto* out = reinterpret_cast<int16_t*>(stream);
    int n = len / 2;
    std::vector<int> mix(n, 0);
    std::lock_guard<std::mutex> lk(g_amx);
    for (auto& v : g_voices)
        for (int i = 0; i < n; i++) {
            if (v.pos >= v.n) { if (!v.loop || !v.n) break; v.pos = 0; }
            mix[i] += (int)(v.s[v.pos++] * v.vol);
        }
    for (int i = 0; i < n; i++) out[i] = (int16_t)SDL_clamp(mix[i], -32768, 32767);
    for (size_t i = 0; i < g_voices.size();)
        if (!g_voices[i].loop && g_voices[i].pos >= g_voices[i].n) g_voices.erase(g_voices.begin() + i); else i++;
}

static void audio_init() {
    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    want.callback = audio_cb;
    g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio) SDL_PauseAudioDevice(g_audio, 0); else LOG("no audio: %s", SDL_GetError());
}

static Pcm* wave_load(const char* name) {
    auto it = g_waves.find(name);
    if (it != g_waves.end()) return it->second;
    std::string path;
    Pcm* p = nullptr;
    bool found = find_asset(name, false, ".wav", path) || find_asset(name, false, "", path) || find_asset(name, false, ".ogg", path);
    if (found && path.size() > 4 && (path.compare(path.size() - 4, 4, ".ogg") == 0 || path.find(".ogg.") != std::string::npos)) {
        // Ogg Vorbis (Merit2d/Merit3d games) through stb_vorbis, resampled to 44.1 kHz stereo
        std::vector<uint8_t> d;
        if (merit_read_gz(path.c_str(), d)) {
            int ch = 0, rate = 0;
            short* out = nullptr;
            int frames = stb_vorbis_decode_memory(d.data(), (int)d.size(), &ch, &rate, &out);
            if (frames > 0 && out) {
                SDL_AudioCVT cvt;
                int need = SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, ch, rate, AUDIO_S16SYS, 2, 44100);
                size_t blen = (size_t)frames * ch * 2;
                std::vector<Uint8> tmp(blen * (cvt.len_mult > 0 ? cvt.len_mult : 1));
                memcpy(tmp.data(), out, blen);
                cvt.buf = tmp.data(); cvt.len = (int)blen;
                if (need > 0) SDL_ConvertAudio(&cvt);
                int outLen = need > 0 ? cvt.len_cvt : (int)blen;
                p = new Pcm;
                p->s.assign(reinterpret_cast<int16_t*>(tmp.data()), reinterpret_cast<int16_t*>(tmp.data() + outLen));
            }
            free(out);
        }
        found = false;
    }
    if (found) {
        std::vector<uint8_t> d;
        if (merit_read_gz(path.c_str(), d)) {
            SDL_AudioSpec ws; Uint8* buf; Uint32 blen;
            if (SDL_LoadWAV_RW(SDL_RWFromConstMem(d.data(), (int)d.size()), 1, &ws, &buf, &blen)) {
                SDL_AudioCVT cvt;
                int need = SDL_BuildAudioCVT(&cvt, ws.format, ws.channels, ws.freq, AUDIO_S16SYS, 2, 44100);
                cvt.len = (int)blen;
                std::vector<Uint8> tmp((size_t)blen * (cvt.len_mult > 0 ? cvt.len_mult : 1));
                memcpy(tmp.data(), buf, blen);
                cvt.buf = tmp.data();
                if (need > 0) SDL_ConvertAudio(&cvt);
                int outLen = need > 0 ? cvt.len_cvt : (int)blen;   // no conversion: len_cvt is unset
                p = new Pcm;
                p->s.assign(reinterpret_cast<int16_t*>(tmp.data()), reinterpret_cast<int16_t*>(tmp.data() + outLen));
                SDL_FreeWAV(buf);
            }
        }
    }
    if (!p) LOG("missing sound %s", name);
    g_waves[name] = p;
    return p;
}

// ================================================================================== the API
// Signatures (and so mangled names) must match the games' imports exactly.

_RADBitmap* MouseBmp;                                // cursor bitmap passed to MouseAdd; unused

_RADBitmap* BitmapAlloc(unsigned long w, unsigned long h, unsigned char) { video_init(); return bmp_new(w, h, color_of(0)); }
// golf's BMAP_BitmapAlloc: the same frame, read back as {?, w, h} and passed to Bitmap*()
_RADBitmap* allocframe(unsigned long w, unsigned long h, int) { return BitmapAlloc(w, h, 0); }
void BitmapFree(_RADBitmap* b) { if (b && b->tag == kTag) { b->tag = 0; destroy_bitmap(b->bmp); free(b); } }
void BitmapClear(_RADBitmap* b, unsigned char c, int, int, int) {
    if (!b) return;
    uint32_t v = color_of(c);
    for (size_t i = 0; i < (size_t)b->w * b->h; i++) b->px[i] = v;
}
void BitmapFilledBox(_RADBitmap* b, unsigned long x, unsigned long y, unsigned long w, unsigned long h, unsigned char c) {
    if (!b) return;
    uint32_t v = color_of(c);
    for (unsigned long j = y; j < y + h && j < b->h; j++)
        for (unsigned long i = x; i < x + w && i < b->w; i++) b->px[j * b->w + i] = v;
}
void BitmapChangeColor(_RADBitmap* b, unsigned char from, unsigned char to) {
    if (!b) return;
    uint32_t f = color_of(from), t = color_of(to);
    for (size_t i = 0; i < (size_t)b->w * b->h; i++) if (b->px[i] == f) b->px[i] = t;
}
void BitmapSetPalette(_RADBitmap*) {}
void BitmapGetPalette(_RADBitmap*) {}
unsigned long BitmapWidth(_RADBitmap* b) { return b ? b->w : 0; }
unsigned long BitmapHeight(_RADBitmap* b) { return b ? b->h : 0; }
_RADBitmap* SaveBackBmp;                             // a global the games keep a saved-screen bitmap in                       // 8-bit era; art is RGB565
void BitmapPaletteToPalette(_RADBitmap*, _RADBitmap*) {}

// Copies the dst-sized region of src starting at (sx, sy) into dst.
// (x, y) is where src goes in dst when src fits inside dst; otherwise it is the corner of the
// region of src cut out into dst (airhockey's Object_Draw_Clip uses both forms).
static void bmp_to_bmp(_RADBitmap* dst, _RADBitmap* src, long x, long y, bool trans, uint32_t key) {
    if (!dst || !src) return;
    if (src->w <= dst->w && src->h <= dst->h)
        blit(src->px, src->w, src->h, 0, 0, dst->px, dst->w, dst->h, (int)x, (int)y, src->w, src->h, trans, key);
    else
        blit(src->px, src->w, src->h, (int)x, (int)y, dst->px, dst->w, dst->h, 0, 0, dst->w, dst->h, trans, key);
}
void BitmapToBitmap(_RADBitmap* dst, _RADBitmap* src, unsigned long x, unsigned long y) {
    bmp_to_bmp(dst, src, (long)x, (long)y, false, 0);
}
void BitmapToBitmapTrans(_RADBitmap* dst, _RADBitmap* src, unsigned long x, unsigned long y, unsigned char key) {
    bmp_to_bmp(dst, src, (long)x, (long)y, true, color_of(key));
}
void BitmapToScreen(_RADBitmap* b, unsigned long x, unsigned long y) {
    if (!b) return;
    blit(b->px, b->w, b->h, 0, 0, g_screen, SW, SH, (int)x, (int)y, b->w, b->h, false, 0);
    g_dirty = true;
}
void BitmapToScreenTrans(_RADBitmap* b, unsigned long x, unsigned long y, int key) {
    if (!b) return;
    blit(b->px, b->w, b->h, 0, 0, g_screen, SW, SH, (int)x, (int)y, b->w, b->h, true, color_of(key));
    g_dirty = true;
}
unsigned long BitmapFromScreen(_RADBitmap* b, unsigned long x, unsigned long y) {
    if (b) blit(g_screen, SW, SH, (int)x, (int)y, b->px, b->w, b->h, 0, 0, b->w, b->h, false, 0);
    return 0;
}
void ScreenFilledBox(unsigned long x, unsigned long y, unsigned long w, unsigned long h, int c) {
    uint32_t v = color_of(c);
    for (unsigned long j = y; j < y + h && j < (unsigned long)SH; j++)
        for (unsigned long i = x; i < x + w && i < (unsigned long)SW; i++) g_screen[j * SW + i] = v;
    g_dirty = true;
}

// Draws `n` into `dst` with digit bitmaps digits[0..9] (digits[10] = comma).
// align: 1 = right-aligned (others: centred); key: transparency key; commas: 1 = group thousands.
void ShowNumber(unsigned long n, _RADBitmap* dst, _RADBitmap** digits, unsigned char align, unsigned char key, unsigned char commas) {
    if (!dst || !digits) return;
    std::string s = std::to_string(n), t;
    for (size_t i = 0; i < s.size(); i++) {
        if (commas && i && (s.size() - i) % 3 == 0) t += ',';
        t += s[i];
    }
    int total = 0;
    for (char c : t) { _RADBitmap* g = digits[c == ',' ? 10 : c - '0']; if (g) total += g->w; }
    int x = align == 1 ? (int)dst->w - total : ((int)dst->w - total) / 2;
    for (char c : t) {
        _RADBitmap* g = digits[c == ',' ? 10 : c - '0'];
        if (!g) continue;
        int y = ((int)dst->h - (int)g->h) / 2;
        blit(g->px, g->w, g->h, 0, 0, dst->px, dst->w, dst->h, x, y, g->w, g->h, true, color_of(key));
        x += g->w;
    }
}

// --- animations
_MSmack* AnimationOpen(unsigned char lang, char* name, unsigned char) { video_init(); return reinterpret_cast<_MSmack*>(anim_load(name, lang)); }
void AnimationClose(_MSmack* m) { delete reinterpret_cast<Anim*>(m); }
unsigned long AnimationWidth(_MSmack* m) { auto* a = reinterpret_cast<Anim*>(m); return a ? a->w : 1; }
unsigned long AnimationHeight(_MSmack* m) { auto* a = reinterpret_cast<Anim*>(m); return a ? a->h : 1; }
unsigned long AnimationFrames(_MSmack* m) { auto* a = reinterpret_cast<Anim*>(m); return a ? a->frames.size() : 0; }
void AnimationGoto(_MSmack* m, unsigned long f) {        // 1-based frame number
    auto* a = reinterpret_cast<Anim*>(m);
    if (!a) return;
    anim_seek(a, f ? (int)((f - 1) % a->frames.size()) : 0);
    if (a->target) anim_to(a, a->target);
}
// Binds `b` as the animation's output and decodes the current frame into it.
void AnimationToBitmap(_MSmack* m, _RADBitmap* b) {
    auto* a = reinterpret_cast<Anim*>(m);
    if (!a) return;
    a->target = b;
    a->last = SDL_GetTicks();
    anim_to(a, b);
}
void AnimationAdvanceNoPalette(_MSmack* m, unsigned char) {
    auto* a = reinterpret_cast<Anim*>(m);
    if (!a) return;
    anim_seek(a, (a->cur + 1) % (int)a->frames.size());
    a->last = SDL_GetTicks();
    if (a->target) anim_to(a, a->target);
}
void AnimationAdvance(_MSmack* m, unsigned char f) { AnimationAdvanceNoPalette(m, f); }
void ClearPreSmack() {}                               // preloaded animations: we load on demand
// Blocks until it is time for the animation's next frame.
void AnimationDelay(_MSmack* m) {
    auto* a = reinterpret_cast<Anim*>(m);
    if (!a) return;
    Uint32 due = a->last + (Uint32)a->delay * 1000 / 60;
    while (!g_quit && SDL_GetTicks() < due) { pump(); SDL_Delay(1); }
}
// Non-zero while it is not yet time for the next frame (delay in 1/60 s).
int AnimationStillDelay(_MSmack* m) {
    auto* a = reinterpret_cast<Anim*>(m);
    pump();
    if (!a) return 0;
    bool wait = SDL_GetTicks() - a->last < (Uint32)a->delay * 1000 / 60;
    if (wait) SDL_Delay(1);
    return wait;
}
_RADBitmap* AnimationLoadToBitmap(char* name, unsigned char lang) {
    video_init();
    Anim* a = anim_load(name, lang);
    if (!a) return bmp_new(1, 1, kKey);
    _RADBitmap* b = bmp_new(a->w, a->h, kKey);
    anim_to(a, b);
    delete a;
    return b;
}
// Plays frames [first, last] of an animation at (x, y) and returns when done.
void AnimationPlay(unsigned long x, unsigned long y, unsigned long first, unsigned long last, char* name, unsigned char lang) {
    Anim* a = anim_load(name, lang);
    if (!a) return;
    _RADBitmap* b = bmp_new(a->w, a->h, kKey);
    unsigned long end = last >= a->frames.size() ? a->frames.size() - 1 : last;
    for (unsigned long f = first; f <= end && !g_quit; f++) {
        anim_seek(a, (int)f);
        anim_to(a, b);
        BitmapToScreenTrans(b, x, y, 5);
        sleep_ms((Uint32)a->delay * 1000 / 60);
    }
    BitmapFree(b);
    delete a;
}

// --- input and time
void MouseAdd(char* name, unsigned long x, unsigned long y, unsigned long w, unsigned long h, _RADBitmap*) {
    if (touch_debug()) LOG("zone %s x=%lu y=%lu w=%lu h=%lu", name, x, y, w, h);
    g_zones.push_back({name ? name : "", (int)x, (int)y, (int)w, (int)h});
}
void MouseRemoveAll() { g_zones.clear(); }
void MouseRemove(char* name) {
    for (size_t i = 0; i < g_zones.size();) if (name && g_zones[i].name == name) g_zones.erase(g_zones.begin() + i); else i++;
}
static std::vector<std::vector<Zone>> g_zoneStack;
void MousePushAndRemoveAll() { g_zoneStack.push_back(g_zones); g_zones.clear(); }
void MousePop() { if (!g_zoneStack.empty()) { g_zones = g_zoneStack.back(); g_zoneStack.pop_back(); } }
void ClearTouch() { pump(); if (touch_debug() && !g_pending.empty()) LOG("ClearTouch drops %zu", g_pending.size()); g_pending.clear(); }
// Waits up to `ms` for a touch on a registered zone and copies its name into `buf`.
void InputCharOrDelay(char* buf, unsigned long ms) {
    Uint32 end = SDL_GetTicks() + ms;
    do {
        pump();
        if (!g_pending.empty()) {
            strcpy(buf, g_pending.front().c_str());
            if (touch_debug()) LOG("t=%u game read %s", SDL_GetTicks() - g_start, buf);
            g_pending.erase(g_pending.begin());
            return;
        }
        SDL_Delay(1);
    } while (SDL_GetTicks() < end);
    if (buf) strcpy(buf, "DELAY");                 // timeout marker games test for (goal: strstr("getout", buf))
}
void Delay(unsigned long ms) { sleep_ms(ms); }
unsigned long SystemTimer() { pump(); return SDL_GetTicks() - g_start; }
static unsigned long g_rng = 1;
void Randomize(unsigned long seed) { g_rng = seed ? seed : 1; }
unsigned long Random(unsigned long n) {
    g_rng = g_rng * 1103515245 + 12345;
    return n ? (g_rng >> 16) % n : 0;
}

// --- sound
void AddPreWave(char* name, unsigned short, SoundBase**, bool) { video_init(); if (name) wave_load(name); }
void ClearPreWave() {
    std::lock_guard<std::mutex> lk(g_amx);
    g_voices.clear();
    for (auto& w : g_waves) delete w.second;
    g_waves.clear();
}
// vol 0..255; returns a voice id for voice_get_position.
int PlayPreWave(char* name, unsigned short, bool, int vol, int, int) {
    video_init();
    Pcm* p = name ? wave_load(name) : nullptr;
    if (!p || !g_audio) return -1;
    std::lock_guard<std::mutex> lk(g_amx);
    int id = g_nextVoice++;
    g_voices.push_back({id, p->s.data(), p->s.size(), 0, SDL_clamp(vol, 0, 255) / 255.0f, false});
    return id;
}
// Allegro: sample position of a playing voice, -1 once it has finished.
extern "C" int voice_get_position(int voice) {
    pump();
    SDL_Delay(1);
    std::lock_guard<std::mutex> lk(g_amx);
    for (auto& v : g_voices) if (v.id == voice) return (int)(v.pos / 2);
    return -1;
}

// Allegro: voice volume 0..255
extern "C" void voice_set_volume(int voice, int vol) { legacy::set_voice_volume(voice, vol); }

// ------------------------------------------------------------------ shared with merit3d.cpp
namespace legacy {
void video_init() { ::video_init(); }
void pump() { ::pump(); }
void screen_touched() { g_dirty = true; }
// GL mode: MEGA_SHOT_DIR screenshots of the GL back buffer every MEGA_SHOT_EVERY swaps
void gl_frame_done() {
    static const char* dir = menv("SHOT_DIR");
    static int every = menv_int("SHOT_EVERY", 60), n;
    if (!dir || !g_gl || ++n % every) return;
    int w = screen_w(), h = screen_h();
    std::vector<uint8_t> px((size_t)w * h * 4);
    using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
    static auto rp = reinterpret_cast<ReadPixels>(SDL_GL_GetProcAddress("glReadPixels"));
    if (!rp) return;
    rp(0, 0, w, h, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px.data());
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    for (int y = 0; y < h; y++) memcpy(static_cast<uint8_t*>(s->pixels) + y * s->pitch, &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    char path[1024];
    snprintf(path, sizeof path, "%s/frame%05d.bmp", dir, n);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}
void warp_mouse(int x, int y) { g_mouseX = x; g_mouseY = y; }
bool gl_mode() { return g_gl; }
SDL_Window* window() { return g_win; }
int screen_w() { return g_gl ? menv_int("WIDTH", 1024) : SW; }
int screen_h() { return g_gl ? menv_int("HEIGHT", 768) : SH; }
int mouse_x() { return g_mouseX; }
int mouse_y() { return g_mouseY; }
bool mouse_down() { return g_mouseDown; }
int play_pcm(const std::string&, const short* s, size_t n, int vol, bool loop) {
    ::video_init();
    if (!g_audio || !s || !n) return -1;
    std::lock_guard<std::mutex> lk(g_amx);
    int id = g_nextVoice++;
    g_voices.push_back({id, s, n, 0, SDL_clamp(vol, 0, 255) / 255.0f, loop});
    return id;
}
void stop_voice(int voice) {
    std::lock_guard<std::mutex> lk(g_amx);
    for (size_t i = 0; i < g_voices.size(); i++) if (g_voices[i].id == voice) { g_voices.erase(g_voices.begin() + i); break; }
}
void set_voice_volume(int voice, int vol) {
    std::lock_guard<std::mutex> lk(g_amx);
    for (auto& v : g_voices) if (v.id == voice) v.vol = SDL_clamp(vol, 0, 255) / 255.0f;
}
bool voice_playing(int voice) {
    std::lock_guard<std::mutex> lk(g_amx);
    for (auto& v : g_voices) if (v.id == voice) return true;
    return false;
}
bool find_asset(const char* name, bool lang, const char* ext, std::string& out) { return ::find_asset(name, lang, ext, out); }
Anim* anim_load(const char* name, bool lang) { return ::anim_load(name, lang); }
void anim_seek(Anim* a, int f) { ::anim_seek(a, f); }
void sleep_ms(Uint32 ms) { ::sleep_ms(ms); }
Uint32 ticks() { return SDL_GetTicks() - g_start; }
std::vector<Touch> take_touches() { ::pump(); std::vector<Touch> t; t.swap(g_touches); return t; }
bool quitting() { return g_quit; }
_RADBitmap* rad_new(uint32_t w, uint32_t h, uint32_t fill) { ::video_init(); return bmp_new(w, h, fill); }
// a sound file by name (preloaded or not), vol 0..255; returns a voice id or -1
int play_wave(const char* name, int vol, bool loop) {
    ::video_init();
    Pcm* p = name ? wave_load(name) : nullptr;
    if (!p || !g_audio) return -1;
    std::lock_guard<std::mutex> lk(g_amx);
    int id = g_nextVoice++;
    g_voices.push_back({id, p->s.data(), p->s.size(), 0, SDL_clamp(vol, 0, 255) / 255.0f, loop});
    return id;
}
bool key_down(int sdl_scancode) { ::pump(); const Uint8* k = SDL_GetKeyboardState(nullptr); return k && k[sdl_scancode]; }
}

// --- loader objects
namespace xml_gameinfo { enum GameIds : int {}; }
namespace xml_gameoptions { enum GameOptionIndex : int {}; }

// Operator game options (NVRAM). Games call NVRAMMap::GamesOpt(&NVRAMData, index); the values
// were set in the operator menu. Defaults: 0, logged once per index (MEGA_DEBUG_NVRAM=1).
class NVRAMMap { public: unsigned char GamesOpt(xml_gameoptions::GameOptionIndex) const; };
unsigned char NVRAMData[0x8000];
unsigned char NVRAMMap::GamesOpt(xml_gameoptions::GameOptionIndex i) const {
    static bool dbg = menv("DEBUG_NVRAM") != nullptr;
    if (dbg) LOG("GamesOpt(%d) -> 0", (int)i);
    return 0;
}

class MegacGlobals { public: static MegacGlobals* GetInstance(); };
// MegacGlobals lives in megatouch-host (src/host/loader_services.cpp), shared with GameDevice games.



// =============================================================================================
// The loader's C++ layer (most legacy games): Bitmap, VideoClass, MouseManager, BmpFont,
// UniversalTranslator/TextSystem. Reconstructed from the games' call sites (static vs member
// from the pushes; tools/callsites.py) and the fields they read inline.
//
// Bitmap objects are 0x94 bytes, allocated by the games. Fields games touch inline:
//   +0x0a/0x0c/0x0e colour effect (short r,g,b), +0x54 its flag   +0x30/0x32 scale, +0x34 flag
//   +0x4c width, +0x50 height (int)
// Our own data lives at +0x88 (BmpData*) / +0x8c (tag).
// =============================================================================================
#include <unistd.h>
struct _IO_FILE;
struct FontBase;
namespace Locale { enum Languages : int {}; }

namespace {

// --- video buffers (VideoClass): the 2D screen is VB "screen"; games draw into an open VB and
// show it. Ids from CreateVB/OpenVB are kept as separate 640x480 buffers.
std::map<int, std::vector<uint32_t>> g_vbs;
int g_curVB = -1;                            // -1: draw straight to the screen
const int kScrapVB = 1000;
uint32_t g_rgb = 0;                          // SetRGB colour for Rect
int g_nextVB = 1;

uint32_t* vb_pixels(int id) {
    if (id < 0) return g_screen;
    auto& v = g_vbs[id];
    if (v.empty()) v.assign(g_screen, g_screen + SW * SH);   // a new buffer starts as the screen
    return v.data();
}
uint32_t* target() { return vb_pixels(g_curVB); }
bool g_vbTouched;                            // something drew into a video buffer since the last world frame
void touched_target() { if (g_curVB < 0) g_dirty = true; else g_vbTouched = true; }
}
namespace legacy {
BITMAP* vb_bitmap(int id) {
    if (id < 0) return screen;
    static std::map<int, BITMAP*> wraps;
    BITMAP*& b = wraps[id];
    if (!b) b = al_wrap32(vb_pixels(id), SW, SH);
    return b;
}
BITMAP* target_bitmap() { return vb_bitmap(g_curVB); }
uint32_t* target_px() { return vb_pixels(g_curVB); }
void touched_target() { ::touched_target(); }
}

class Bitmap;
class FontBase;

// --- VideoClass
class VideoClass {
public:
    void OpenVB(int);
    void ShowVB(int);
    void CloseVB();
    void OpenScrapVB();
    int CreateVB(short, short);
    void DestroyVB(int);
    void CopyVBRegion(int, int, int, int, int, int, int);
    void CopyFromScreen(int, int, int, int, int, int, bool);
    static int GetVB();
    static void ShowVBRegion(int, int, int, int, int, int);
    static void Rect(int, int, int, int);
    static void SetRGB(short, short, short);
    static void ClearVB(int);
    static void RemoveFromVBZ(unsigned long);
};
void VideoClass::RemoveFromVBZ(unsigned long id) { legacy::zlist_remove(id); }
namespace legacy {
void zlist_changed() { g_zdirty = true; }
int base_vb() {
    if (!g_vbTouched) return -2;
    g_vbTouched = false;
    return g_curVB >= 0 ? g_curVB : g_shownVB >= 0 ? g_shownVB : -2;
}
}
// the shown buffer plus the z-ordered items over it
void zlist_redraw() {
    g_zdirty = false;
    if (g_shownVB < 0) return;
    memcpy(g_screen, vb_pixels(g_shownVB), (size_t)SW * SH * 4);
    legacy::zlist_compose(screen);
    g_dirty = true;
}
void VideoClass::OpenVB(int id) { video_init(); g_curVB = id; vb_pixels(id); }
void VideoClass::CloseVB() { g_curVB = -1; }
void VideoClass::OpenScrapVB() { g_curVB = kScrapVB; vb_pixels(kScrapVB); }
int VideoClass::GetVB() { return g_curVB; }
int VideoClass::CreateVB(short, short) { int id = g_nextVB++; vb_pixels(id); return id; }
void VideoClass::DestroyVB(int id) { g_vbs.erase(id); if (g_curVB == id) g_curVB = -1; }
void VideoClass::ClearVB(int id) { uint32_t* p = vb_pixels(id); for (int i = 0; i < SW * SH; i++) p[i] = 0; }
// Presents a whole buffer (-1: the open one).
void VideoClass::ShowVB(int id) {
    if (id == -1) id = g_curVB;
    if (id >= 0) { memcpy(g_screen, vb_pixels(id), (size_t)SW * SH * 4); g_shownVB = id; legacy::zlist_compose(screen); }
    g_dirty = true;
    present();
    pump();
}
void VideoClass::ShowVBRegion(int x, int y, int w, int h, int dx, int dy) {
    if (g_curVB >= 0) blit(vb_pixels(g_curVB), SW, SH, x, y, g_screen, SW, SH, dx, dy, w, h, false, 0);
    g_dirty = true;
    pump();
}
// Copies a region of the open buffer (usually the scrap) into buffer `dst`.
void VideoClass::CopyVBRegion(int sx, int sy, int w, int h, int dx, int dy, int dst) {
    uint32_t* s = target();
    blit(s, SW, SH, sx, sy, vb_pixels(dst), SW, SH, dx, dy, w, h, false, 0);
    if (dst < 0) g_dirty = true;
}
void VideoClass::CopyFromScreen(int sx, int sy, int w, int h, int dx, int dy, bool) {
    blit(g_screen, SW, SH, sx, sy, target(), SW, SH, dx, dy, w, h, false, 0);
}
void VideoClass::SetRGB(short r, short g, short b) { g_rgb = (uint32_t)(r & 255) << 16 | (g & 255) << 8 | (b & 255); }
void VideoClass::Rect(int x, int y, int w, int h) {
    uint32_t* t = target();
    for (int j = y; j < y + h && j < SH; j++)
        for (int i = x; i < x + w && i < SW; i++) if (i >= 0 && j >= 0) t[j * SW + i] = g_rgb;
    touched_target();
}

// --- MouseManager: named zones; CheckLoc reports a touch by writing the zone name into
// MegacGlobals+0x5ae4 and returning non-zero.
class MouseManager {
public:
    void AddZone(char*, int, int, int, int);
    void RemoveZone(char*);
    void RemoveAll(unsigned char);
    bool CheckLoc(unsigned char, bool);
};
void MouseManager::AddZone(char* name, int x, int y, int w, int h) {
    video_init();
    if (touch_debug()) LOG("zone %s %d,%d %dx%d", name, x, y, w, h);
    g_zones.push_back({name ? name : "", x, y, w, h});
}
void MouseManager::RemoveZone(char* name) { MouseRemove(name); }
void MouseManager::RemoveAll(unsigned char) { g_zones.clear(); }
bool MouseManager::CheckLoc(unsigned char, bool) {
    pump();
    if (g_pending.empty()) return false;
    auto* glob = reinterpret_cast<char*>(MegacGlobals::GetInstance());
    snprintf(glob + 0x5ae4, 0x80, "%s", g_pending.front().c_str());
    if (touch_debug()) LOG("CheckLoc -> %s", g_pending.front().c_str());
    g_pending.erase(g_pending.begin());
    return true;
}

// --- translation: the loader's UniversalTranslator / TextSystem over the host's Translator
class Translator {
public:
    static bool LoadTranslations(char const*, bool);
    static bool LoadTranslations(xml_gameinfo::GameIds);
    static char const* Translate(char const*);
    static char* nTranslate(char*, unsigned int, char const*);
};
// Translate into a caller buffer (the Translate<N> template in game code calls this)
char* Translator::nTranslate(char* out, unsigned int size, char const* key) {
    if (out && size) snprintf(out, size, "%s", Translate(key ? key : ""));
    return out;
}
class UniversalTranslator {
public:
    UniversalTranslator();
    ~UniversalTranslator();
    void SetupTransSys(char const*, unsigned char, Locale::Languages);
    void ReleaseTransSys();
    bool nTrans(char*, char const*, int, bool);
};
UniversalTranslator::UniversalTranslator() {}
UniversalTranslator::~UniversalTranslator() {}
void UniversalTranslator::SetupTransSys(char const* name, unsigned char, Locale::Languages) { Translator::LoadTranslations(name, false); }
void UniversalTranslator::ReleaseTransSys() {}
bool UniversalTranslator::nTrans(char* out, char const* key, int len, bool) {
    if (!out || len <= 0) return false;
    const char* t = Translator::Translate(key ? key : "");
    snprintf(out, len, "%s", t);
    return t != key;
}
// TextSystem / TextDesc / textSystem: text_system.cpp

// --- small helpers
char FileLoc[256];                            // game data path prefix; empty = the working dir
bool IsUAEGame(xml_gameinfo::GameIds) { return false; }
char* CommaStr(char* out, unsigned long n) {
    std::string s = std::to_string(n), t;
    for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) t += ','; t += s[i]; }
    if (out) strcpy(out, t.c_str());
    return out;
}
char* _strrev(char* s) {
    if (!s) return s;
    for (size_t i = 0, j = strlen(s); i + 1 < j; i++, j--) { char c = s[i]; s[i] = s[j - 1]; s[j - 1] = c; }
    return s;
}
// "Quit game?" prompt: home play just quits. CheckKey: the operator key-switch, never turned.
class SystemClass { public: bool ConfirmExit(unsigned short, unsigned short, Bitmap*, bool); bool CheckKey(); };
bool SystemClass::ConfirmExit(unsigned short, unsigned short, Bitmap*, bool) { return true; }
bool SystemClass::CheckKey() { pump(); return false; }

// ------------------------------------------------------------------------------ more C API
// (inferred from call sites in airhockey, quickcell, puckshot, funkymonkey)
unsigned short MouseX() { pump(); return (unsigned short)g_mouseX; }
unsigned short MouseY() { pump(); return (unsigned short)g_mouseY; }
// Current touch position; *down = finger on the glass.
void GetTouchCoord(int* x, int* y, int* down) {
    pump();
    if (x) *x = g_mouseX;
    if (y) *y = g_mouseY;
    if (down) *down = g_mouseDown;
}
bool ScreenIsTouched() { pump(); return g_mouseDown; }
bool IsScreenTouched() { pump(); return g_mouseDown; }
void MouseRemoveAllNoFlush() { g_zones.clear(); }
void QueFlush(bool) { pump(); g_pending.clear(); }
// PlayWave("name", flags, vol 0..255, freq, pan, bool): like PlayPreWave without preloading
int PlayWave(char* name, unsigned short flags, int vol, int freq, int pan, bool b) {
    return PlayPreWave(name, flags, b, vol, freq, pan);
}
void ClearAPreWave(char* name) {
    if (!name) return;
    std::lock_guard<std::mutex> lk(g_amx);
    auto it = g_waves.find(name);
    if (it == g_waves.end()) return;
    const short* data = it->second->s.data();
    for (size_t i = 0; i < g_voices.size();) if (g_voices[i].s == data) g_voices.erase(g_voices.begin() + i); else i++;
    delete it->second;
    g_waves.erase(it);
}
void Delay_PE(unsigned long ms) { sleep_ms(ms); }

// pixels / boxes / lines in palette-index colours
unsigned char BitmapGetPixel(_RADBitmap* b, unsigned long x, unsigned long y) {
    if (!b || x >= b->w || y >= b->h) return 0;
    uint32_t c = b->px[y * b->w + x];
    if (c == kKey) return 5;
    return (unsigned char)(((c >> 16 & 255) + (c >> 8 & 255) + (c & 255)) / 3);
}
void BitmapSetPixel(_RADBitmap* b, unsigned long x, unsigned long y, unsigned char c) {
    if (b && x < b->w && y < b->h) b->px[y * b->w + x] = color_of(c);
}
static void box_px(uint32_t* px, int pw, int ph, int x, int y, int w, int h, uint32_t v, bool fill) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) {
        if (i < 0 || j < 0 || i >= pw || j >= ph) continue;
        if (fill || j == y || j == y + h - 1 || i == x || i == x + w - 1) px[j * pw + i] = v;
    }
}
void BitmapBox(_RADBitmap* b, unsigned long x, unsigned long y, unsigned long w, unsigned long h, unsigned char c) {
    if (b) box_px(b->px, b->w, b->h, x, y, w, h, color_of(c), false);
}
void ScreenBoxC(unsigned long x, unsigned long y, unsigned long w, unsigned long h, unsigned char c) {
    box_px(g_screen, SW, SH, x, y, w, h, color_of(c), false);
    g_dirty = true;
}
void ScreenLineC(unsigned long x1, unsigned long y1, unsigned long x2, unsigned long y2, int c) {
    int r = 0, g = 0, b = 0;
    uint32_t v = color_of(c & 255);
    r = v >> 16 & 255; g = v >> 8 & 255; b = v & 255;
    screen->vtable->line(screen, x1, y1, x2, y2, makecol_depth(32, r, g, b));
    g_dirty = true;
}
void ScreenLineC(unsigned long x1, unsigned long y1, unsigned long x2, unsigned long y2, int r, int g, int b) {
    screen->vtable->line(screen, x1, y1, x2, y2, makecol_depth(32, r, g, b));
    g_dirty = true;
}
void ScreenClear(int c) { for (int i = 0; i < SW * SH; i++) g_screen[i] = color_of(c & 255); g_dirty = true; }
void ScreenFadeOutClear(unsigned char) {
    for (int step = 0; step < 8; step++) {
        for (int i = 0; i < SW * SH; i++) { uint32_t p = g_screen[i]; g_screen[i] = (p >> 1) & 0x7f7f7f; }
        g_dirty = true;
        sleep_ms(30);
    }
    for (int i = 0; i < SW * SH; i++) g_screen[i] = 0;
    g_dirty = true;
}
void BitmapRectToScreen(_RADBitmap* b, unsigned long x, unsigned long y, unsigned long w, unsigned long h) {
    if (b) blit(b->px, b->w, b->h, x, y, g_screen, SW, SH, x, y, w, h, false, 0);
    g_dirty = true;
}
// Scaled copy (into dst if given, else a new bitmap). Transparent pixels stay transparent.
_RADBitmap* ArbScaleBmp(_RADBitmap* dst, _RADBitmap* src, unsigned short w, unsigned short h, unsigned char) {
    if (!src || !w || !h) return dst;
    if (!dst) dst = bmp_new(w, h, kKey);
    for (unsigned j = 0; j < h && j < dst->h; j++)
        for (unsigned i = 0; i < w && i < dst->w; i++)
            dst->px[j * dst->w + i] = src->px[(j * src->h / h) * src->w + i * src->w / w];
    return dst;
}
// 8-bit palette conversions: art here is true colour already
_RADBitmap* BitmapConvPal(_RADBitmap* src, _RADBitmap* dst, unsigned char) {
    if (!src) return dst;
    if (!dst) { dst = bmp_new(src->w, src->h, kKey); memcpy(dst->px, src->px, (size_t)src->w * src->h * 4); }
    return dst;
}
void BitmapPaletteMerge(_RADBitmap*, _RADBitmap*) {}
// Tints the bitmap towards (r, g, b) by `amount` percent (non-transparent pixels)
void BitmapColorize(_RADBitmap* b, int r, int g, int bl, int amount) {
    if (!b) return;
    int a = amount ? SDL_clamp(amount, 0, 100) : 100;
    for (size_t i = 0; i < (size_t)b->w * b->h; i++) {
        uint32_t p = b->px[i];
        if (p == kKey) continue;
        int lum = ((p >> 16 & 255) + (p >> 8 & 255) + (p & 255)) / 3;
        int tr = r * lum / 255, tg = g * lum / 255, tb = bl * lum / 255;
        int pr = p >> 16 & 255, pg = p >> 8 & 255, pb = p & 255;
        b->px[i] = (uint32_t)(pr + (tr - pr) * a / 100) << 16 | (uint32_t)(pg + (tg - pg) * a / 100) << 8 | (uint32_t)(pb + (tb - pb) * a / 100);
    }
}
// BitmapText: the 8x8 system font; draws with the default bitmap font in white
void BitmapText(_RADBitmap* b, unsigned long x, unsigned long y, char* text) {
    if (b && text) legacy::ttf_draw(b->bmp, text, x, y, 0, 0, 255, 255, 255, 12, 0, false, "bureau", 0, 0);
}
// BitmapTextTTF(bmp, text, x, y, w, h, dr, dg, db, size, align, bool, family, spacing)
// dr..db: colour as offsets from white (255 + d): (0,0,-255) yellow, (0,0,0) white.
void BitmapTextTTF(_RADBitmap* b, char const* text, int x, int y, int w, int h, int c1, int c2, int c3,
                   int size, int align, bool bold, char* family, int spacing) {
    if (!b || !text) return;
    text = Translator::Translate(text);
    static bool dbg = menv("DEBUG_TEXT") != nullptr;
    if (dbg) LOG("BitmapTextTTF '%s' box %d,%d %dx%d c %d,%d,%d size %d align %d %d %s %d", text, x, y, w, h, c1, c2, c3, size, align, bold, family ? family : "-", spacing);
    legacy::ttf_draw(b->bmp, text, x, y, w, h, SDL_clamp(255 + c1, 0, 255), SDL_clamp(255 + c2, 0, 255),
                     SDL_clamp(255 + c3, 0, 255), size, align, bold, family, 0, 0);
}

// Head-to-head cabinet linking: never linked here (one cabinet, player 0, we are the master)
extern "C" {
unsigned char LinkedWithCnt;
unsigned char WhoWeArePlaying[16];
unsigned char LinkFanTimeSetting, LinkShowDeckSetting, LinkEasyMode, LinkCheckerzRules, LinkContinueSetting, LinkWaitTimer;
unsigned char BonusPlay;
int SavedLanguage;
void* RankFunc;
}
unsigned char GetMyId() { return 0; }
unsigned char GetMaster() { return 1; }
void SetMaster(unsigned char) {}
int GetPackets(char*, int) { return 0; }
bool SendPilePacket(char*, int) { return true; }
void ProcessRTPackets() { pump(); }
void InFormOfIDUsage(unsigned char, unsigned char) {}
int Heartbeat_Check(int, int) { return 1; }
void Heartbeat_Stop() {}
void DisplayDefeated(int, int) {}
// Meritthon (multi-game tournament) round banner: not in a Meritthon
void MeritthonDisplayRound(int, int, int, unsigned long) {}
// Help file for the current language: gamedata/help/<game><suffix>
bool GetHelpFileName(char (&out)[255], char const* game, Locale::Languages, char const* suffix) {
    snprintf(out, sizeof out, "/usr/local/gamedata/help/%s%s", game ? game : "", suffix ? suffix : "");
    struct stat st;
    return stat(out, &st) == 0;
}

// ------------------------------------------------------------------------------ linked play
// Cabinet-to-cabinet linking (head-to-head, card tournaments). Not linked: NetGlob is null and
// MegacGlobals+0x30 (link mode) is 0, so games skip most of this; the rest are no-ops.
static unsigned char g_cardG[0x10000];
extern "C" {
void* NetGlob;                                   // NetGlobals*; null = not linked
void* CardG = g_cardG;                           // card-link state block (read only when linked)
unsigned char LocalGameOverFlag, DLLReady, ISRDone;
}
class NetGlobals {
public:
    static unsigned long GetLeaderScore();
    static unsigned char GetRank(unsigned char);
    static int GetTrueLinkCount();
    static int GetState(int);
    static int GetHeadIdx(unsigned char);
    static void ProcNetClicks();
};
extern "C" int PlrScore[8];                      // megatouch-host
unsigned long NetGlobals::GetLeaderScore() { return PlrScore[0]; }
unsigned char NetGlobals::GetRank(unsigned char) { return 1; }
int NetGlobals::GetTrueLinkCount() { return 1; }
int NetGlobals::GetState(int) { return 0; }
int NetGlobals::GetHeadIdx(unsigned char) { return 0; }
void NetGlobals::ProcNetClicks() { pump(); }
void Card_PollLinks() { pump(); }
void Card_ActiveDelay(int ms) { sleep_ms(ms); }  // delay while servicing the links
void Card_ProcessInfo() {}
void Card_LinkWaitForAll(unsigned long) {}
void Card_ProcessRTPackets(unsigned char) {}
bool Card_ParseGameOverFlag() { return false; }
void LinkWait_Advance(int) { pump(); }
void LinkWait_End() {}
// WatchDog::GetInstance() returns LockingProxy<WatchDog> (by hidden pointer): { WatchDog* }, with
// the WatchDog's DebugMutex (libdebug_shared) at +4 locked; the game's inline ~LockingProxy
// unlocks it (checkerz).
struct WatchDogProxy { unsigned char* obj; };
class WatchDog { public: static WatchDogProxy GetInstance(); void IncHeartbeat(); };
WatchDogProxy WatchDog::GetInstance() {
    alignas(16) static unsigned char inst[4 + 512];
    static bool init;
    using Ctor = void (*)(void*);
    using Lock = void (*)(void*, const char*, const char*, unsigned long);
    static auto ctor = reinterpret_cast<Ctor>(dlsym(RTLD_DEFAULT, "_ZN10DebugMutexC1Ev"));
    static auto lock = reinterpret_cast<Lock>(dlsym(RTLD_DEFAULT, "_ZN10DebugMutex4LockEPKcS1_m"));
    static auto initf = reinterpret_cast<void (*)(void*, const char*)>(dlsym(RTLD_DEFAULT, "_ZN10DebugMutex4InitEPKc"));
    if (!init) { init = true; if (ctor) ctor(inst + 4); if (initf) initf(inst + 4, "WatchDog"); }
    if (lock) lock(inst + 4, "watchdog", "GetInstance", 0);
    return WatchDogProxy{inst};
}
void WatchDog::IncHeartbeat() {}

// Slides bitmap g from (x1, y1) to (x2, y2) in `steps` frames over what is on the screen
// (royal: a card flying to the pile), leaving it drawn at the end.
void GAME_MoveGraphic(void* g, unsigned char steps, unsigned long x1, unsigned long y1,
                      unsigned long x2, unsigned long y2, unsigned char key) {
    auto* b = static_cast<_RADBitmap*>(g);
    if (!b || b->tag != kTag) return;
    std::vector<uint32_t> saved(g_screen, g_screen + SW * SH);
    int n = steps ? steps : 1;
    for (int i = 1; i <= n; i++) {
        memcpy(g_screen, saved.data(), (size_t)SW * SH * 4);
        int x = (int)x1 + ((int)x2 - (int)x1) * i / n, y = (int)y1 + ((int)y2 - (int)y1) * i / n;
        blit(b->px, b->w, b->h, 0, 0, g_screen, SW, SH, x, y, b->w, b->h, true, color_of(key));
        g_dirty = true;
        sleep_ms(20);
    }
}

void AnimationBackToStart(_MSmack* m) { AnimationGoto(m, 1); }   // frames are 1-based
extern "C" void voice_start(int) {}
extern "C" void voice_stop(int voice) { legacy::stop_voice(voice); }
// Allegro returns the SAMPLE* a voice plays (NULL when idle); callers only test it
extern "C" void* voice_check(int voice) { static char sample[64]; return legacy::voice_playing(voice) ? sample : nullptr; }
extern "C" void voice_ramp_volume(int voice, int, int vol) { legacy::set_voice_volume(voice, vol); }
extern "C" void voice_set_frequency(int, int) {}
extern "C" void voice_sweep_frequency(int, int, int) {}
extern "C" void voice_set_pan(int, int) {}
// Seniors-edition help overlay: not shown
void SeniorsHelp(int, bool) {}

// Screen mode queries (the loader's liblayout talks to a layout daemon over IPC; not loaded).
// ScreenInfo::GetInstance() + 0x58 is the layout::ScreenControl games query.
namespace xml_screencontrol { enum Resolution : int {}; enum Engine : int {}; enum FullScreenMode : int {}; enum WindowStackPosition : int {}; enum DisplayColorDepth : int {}; }
namespace layout { class ScreenControl { public: int GetCurrentWidth() const; int GetCurrentHeight() const; }; }
int layout::ScreenControl::GetCurrentWidth() const { return legacy::screen_w(); }
int layout::ScreenControl::GetCurrentHeight() const { return legacy::screen_h(); }
class ScreenInfo {
public:
    static ScreenInfo* GetInstance();
    void SwitchVideoMode(xml_screencontrol::Resolution, xml_screencontrol::Engine, xml_screencontrol::FullScreenMode,
                         xml_screencontrol::WindowStackPosition, xml_screencontrol::DisplayColorDepth);
};
ScreenInfo* ScreenInfo::GetInstance() { static unsigned char inst[0x100]; return reinterpret_cast<ScreenInfo*>(inst); }
void ScreenInfo::SwitchVideoMode(xml_screencontrol::Resolution, xml_screencontrol::Engine, xml_screencontrol::FullScreenMode,
                                 xml_screencontrol::WindowStackPosition, xml_screencontrol::DisplayColorDepth) {}

// ------------------------------------------------------------------------------ FLIC player
// open_sprfli(path) / next_sprfli_frame(path, loop) / get_sprfli_bitmap(path) / ..._palette /
// close_sprfli(path): Allegro-style FLIC playback keyed by file name (cardbandits deals its deck
// from cards.flc). Frames come out as 32-bit Allegro bitmaps; palette index 0 is transparent.
struct RGB_ { unsigned char r, g, b, filler; };   // Allegro RGB
namespace {
struct SprFli { std::vector<FlicFrame> frames; int cur = -1; BITMAP* bmp = nullptr; RGB_ pal[256]; };
std::map<std::string, SprFli*>& g_flis_() { static auto* m = new std::map<std::string, SprFli*>; return *m; }   // never destroyed: no exit-time teardown
#define g_flis g_flis_()
}
int open_sprfli(char const* path) {
    if (!path) return -1;
    std::vector<uint8_t> d;
    std::string p = path;
    auto* f = new SprFli;
    bool ok = merit_read_gz(p.c_str(), d) && flic_decode(d, f->frames);
    if (!ok) {
        // the ION build converted the .flc art to .dlt/.spr: same frames, same name
        std::string base = p;
        size_t dot = base.rfind('.');
        size_t slash = base.rfind('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) base.resize(dot);
        if (Anim* a = ::anim_load(base.c_str(), false)) {
            for (size_t i = 0; i < a->frames.size(); i++) {
                ::anim_seek(a, (int)i);
                FlicFrame fr; fr.w = a->w; fr.h = a->h; fr.argb.resize(a->canvas.size());
                // black was palette index 0, the FLIC's transparent colour
                for (size_t k = 0; k < a->canvas.size(); k++)
                    fr.argb[k] = (a->canvas[k] == kKey || !(a->canvas[k] & 0xffffff)) ? 0 : (a->canvas[k] | 0xff000000);
                f->frames.push_back(std::move(fr));
            }
            delete a;
            ok = !f->frames.empty();
        }
    }
    if (!ok) { delete f; return -1; }
    f->bmp = create_bitmap_ex(32, f->frames[0].w, f->frames[0].h);
    memset(f->pal, 0, sizeof f->pal);
    delete g_flis[p];
    g_flis[p] = f;
    return 0;                                       // FLI_OK
}
int next_sprfli_frame(char const* path, int loop) {
    auto it = g_flis.find(path ? path : "");
    if (it == g_flis.end()) return -1;
    SprFli* f = it->second;
    if (f->cur + 1 >= (int)f->frames.size()) { if (!loop) return 1; f->cur = -1; }   // FLI_EOF
    const FlicFrame& fr = f->frames[++f->cur];
    uint32_t* px = static_cast<uint32_t*>(f->bmp->dat);
    for (size_t i = 0; i < fr.argb.size(); i++) px[i] = to_px(fr.argb[i]);
    return 0;
}
BITMAP* get_sprfli_bitmap(char const* path) {
    auto it = g_flis.find(path ? path : "");
    return it == g_flis.end() ? nullptr : it->second->bmp;
}
RGB_* get_sprfli_palette(char const* path) {
    auto it = g_flis.find(path ? path : "");
    return it == g_flis.end() ? nullptr : it->second->pal;
}
void close_sprfli(char const* path) {
    auto it = g_flis.find(path ? path : "");
    if (it == g_flis.end()) return;
    destroy_bitmap(it->second->bmp);
    delete it->second;
    g_flis.erase(it);
}
