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
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#define LOG(...) do { fprintf(stderr, "[legacy] " __VA_ARGS__); fputc('\n', stderr); } while (0)

// ---------------------------------------------------------------------------------- types
// +0 is the Allegro BITMAP (games blit it with Allegro directly), +4/+8 width/height
struct _RADBitmap { BITMAP* bmp; uint32_t w; uint32_t h; uint32_t* px; uint32_t tag; };
extern "C" { extern volatile int mouse_x, mouse_y, mouse_b, mouse_pos; }
struct _MSmack;
struct SoundBase;
struct Bitmap;

static const uint32_t kTag = 0x52414442;          // "RADB"
// Pixels are in Allegro's 32-bit format (0x00RRGGBB) so games' own Allegro drawing and ours
// share one framebuffer. The transparency key is Allegro's 32-bit mask colour, magenta.
static const uint32_t kKey = 0x00ff00ff;          // transparent (palette index 5 / skipped RLE)
static inline uint32_t to_px(uint32_t argb) { return (argb >> 24) < 128 ? kKey : (argb & 0x00ffffff); }
static void frames_to_px(std::vector<MeritFrame>& fr) { for (auto& f : fr) for (auto& p : f.argb) p = to_px(p); }

static uint32_t color_of(unsigned idx) {
    switch (idx) {
    case 5:   return kKey;
    // 0 and 254/255 were the transparent entries of the 8-bit palettes; the true-colour
    // conversion of the art left those pixels black (airhockey key 0, tennis keys 0xfe/0xff)
    case 0: case 254: case 255: return 0x00000000;
    default:  return idx * 0x010101u;                 // unknown palette: grey ramp
    }
}

// ---------------------------------------------------------------------------------- screen
static const int SW = 640, SH = 480;              // the 2D API's screen
static uint32_t g_fallbackScreen[SW * SH];
static uint32_t* g_screen = g_fallbackScreen;     // Allegro's `screen` bitmap once it is set up
static bool g_gl = menv("GL") != nullptr;        // Merit3D games: an OpenGL window instead
static SDL_GLContext g_glctx;
static int g_mouseX, g_mouseY;
static bool g_mouseDown;
static SDL_Window* g_win;
static SDL_Renderer* g_ren;
static SDL_Texture* g_tex;
static bool g_dirty = true, g_quit;
static Uint32 g_start, g_lastPresent, g_quitAt;

struct Zone { std::string name; int x, y, w, h; };
static std::vector<Zone> g_zones;
static std::vector<std::string> g_pending;       // touched zone names not yet read

static void audio_init();
static bool touch_debug() { static bool on = menv("DEBUG_TOUCH") != nullptr; return on; }

// Allegro's `screen` is our framebuffer; gfx_driver reports the mode size
__attribute__((constructor)) static void legacy_screen_ctor() {
    screen = legacy::al_wrap32(g_fallbackScreen, SW, SH);
    legacy::al_set_mode(legacy::screen_w(), legacy::screen_h());
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
    for (int i = 0; i < SW * SH; i++) out[i] = g_screen[i] | 0xff000000;
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
    if (g_autoRelease && SDL_GetTicks() >= g_autoRelease) { g_mouseDown = false; g_autoRelease = 0; }
    while (spec[pos]) {
        unsigned t; int x, y, used = 0;
        if (sscanf(spec + pos, "%u:%d,%d%n", &t, &x, &y, &used) != 3) { spec = nullptr; return; }
        if (SDL_GetTicks() - g_start < t) return;
        pos += used + (spec[pos + used] == ';');
        // games that poll the mouse (MouseX/Y, mouse_b, GetTouchCoord) see a 150 ms press
        g_mouseX = x; g_mouseY = y; g_mouseDown = true; g_autoRelease = SDL_GetTicks() + 150;
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
            break;
        case SDL_MOUSEBUTTONDOWN: {
            // SDL already reports mouse positions in logical (640x480) coordinates
            int x = e.button.x, y = e.button.y;
            g_mouseX = x; g_mouseY = y; g_mouseDown = true;
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
    mouse_x = g_mouseX; mouse_y = g_mouseY; mouse_b = g_mouseDown ? 1 : 0; mouse_pos = g_mouseX << 16 | g_mouseY;
    if (!g_gl && (g_dirty || (shots && SDL_GetTicks() - g_lastPresent >= 1000)) && SDL_GetTicks() - g_lastPresent >= 15) present();
}

static void sleep_ms(Uint32 ms) {
    Uint32 end = SDL_GetTicks() + ms;
    do { pump(); SDL_Delay(1); } while (!g_quit && SDL_GetTicks() < end);
}

// ---------------------------------------------------------------------------------- bitmaps
static _RADBitmap* bmp_new(uint32_t w, uint32_t h, uint32_t fill) {
    auto* b = static_cast<_RADBitmap*>(calloc(1, sizeof(_RADBitmap)));
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
struct Anim {
    std::vector<MeritFrame> frames;
    int w = 0, h = 0, delay = 4, cur = 0;
    Uint32 last = 0;
    _RADBitmap* target = nullptr;
    std::vector<uint32_t> canvas;                   // frames[0..cur] composited
};

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
    if (find_asset(name, false, ".wav", path)) {
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
const uint32_t kBmpTag = 0x58504d42;        // "BMPX"
struct BmpData { int w = 0, h = 0; std::vector<uint32_t> px; Anim* anim = nullptr; };

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
void touched_target() { if (g_curVB < 0) g_dirty = true; }
}

class Bitmap {
public:
    Bitmap(int, int, unsigned char, unsigned char);
    virtual ~Bitmap();                       // games delete through the vtable (slot 1)
    bool LoadPCX(char*, int, int, unsigned char);
    bool LoadData(char*, unsigned char, int);
    bool LoadCompressedData(char*, _IO_FILE*);
    bool LoadTGA_32(char*);
    void Display(int, int, int);
    void DisplayRegion(int, int, int, int, int, int, int);
    void CopyFromCurrent(int, int);
    void CopyToBitmap(Bitmap*, int, int, int, int, int, int);
    bool SmackAnimationLoad(char*, unsigned char);
    void SmackAnimationJumpTo(int);
    void SmackAnimationRewind();
    void SmackAnimationDisplay(int, int, unsigned char);
    void SmackAnimationUnload();
    void DisplaySmack(unsigned short, unsigned short, unsigned char);
    void SetDim(int, int);
    void CreateColoredSmackTextBox(char const*, FontBase*, signed char, unsigned short, unsigned short,
                                   short, short, short, unsigned char, bool);
};

static BmpData* bd(Bitmap* b) {
    auto* raw = reinterpret_cast<unsigned char*>(b);
    uint32_t tag; memcpy(&tag, raw + 0x8c, 4);
    BmpData* d = nullptr;
    if (tag == kBmpTag) memcpy(&d, raw + 0x88, sizeof d);
    if (!d) {
        d = new BmpData;
        memcpy(raw + 0x88, &d, sizeof d);
        tag = kBmpTag; memcpy(raw + 0x8c, &tag, 4);
    }
    return d;
}
static void bmp_resize(Bitmap* b, int w, int h, uint32_t fill) {
    BmpData* d = bd(b);
    d->w = w < 0 ? 0 : w; d->h = h < 0 ? 0 : h;
    d->px.assign((size_t)d->w * d->h, fill);
    auto* raw = reinterpret_cast<unsigned char*>(b);
    memcpy(raw + 0x4c, &d->w, 4); memcpy(raw + 0x50, &d->h, 4);
}

Bitmap::Bitmap(int w, int h, unsigned char, unsigned char) {
    video_init();
    memset(reinterpret_cast<unsigned char*>(this) + 4, 0, 0x94 - 4);   // keep the vtable pointer
    bmp_resize(this, w, h, kKey);
}
Bitmap::~Bitmap() { BmpData* d = bd(this); delete d->anim; delete d; memset(reinterpret_cast<unsigned char*>(this) + 0x88, 0, 8); }
void Bitmap::SetDim(int w, int h) { bmp_resize(this, w, h, kKey); }

// Games name files with or without their extension; try the cabinet's image extensions too.
static bool read_file(const char* name, std::vector<uint8_t>& d) {
    std::string path;
    for (const char* ext : {"", ".img", ".pcx", ".tga", ".dlt", ".spr"})
        if (find_asset(name, false, ext, path)) return merit_read_gz(path.c_str(), d);
    return false;
}

// PCX: run-length rows. 8-bit with a 256-colour palette at the end, or 24-bit as three
// planes per row (R, G, B lines).
bool Bitmap::LoadPCX(char* name, int, int, unsigned char) {
    std::vector<uint8_t> d;
    if (!name || !read_file(name, d)) { LOG("missing PCX %s", name ? name : "?"); return false; }
    if (d.size() < 128 || d[0] != 0x0a) { LOG("not a PCX: %s", name); return false; }
    int w = (d[8] | d[9] << 8) - (d[4] | d[5] << 8) + 1, h = (d[10] | d[11] << 8) - (d[6] | d[7] << 8) + 1;
    int planes = d[65], bpl = d[66] | d[67] << 8;
    bool pal8 = planes == 1 && d.size() >= 128 + 769 && d[d.size() - 769] == 12;
    const uint8_t* pal = pal8 ? &d[d.size() - 768] : nullptr;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || (planes != 1 && planes != 3)) { LOG("unsupported PCX %s", name); return false; }
    bmp_resize(this, w, h, kKey);
    BmpData* b = bd(this);
    size_t o = 128, end = pal8 ? d.size() - 769 : d.size();
    std::vector<uint8_t> line((size_t)bpl * planes);
    for (int y = 0; y < h; y++) {
        size_t x = 0;
        while (x < line.size() && o < end) {
            uint8_t c = d[o++];
            int n = 1;
            if ((c & 0xc0) == 0xc0 && o < end) { n = c & 0x3f; c = d[o++]; }
            while (n-- && x < line.size()) line[x++] = c;
        }
        for (int i = 0; i < w; i++) {
            uint32_t v;
            if (planes == 3) v = (uint32_t)line[i] << 16 | line[bpl + i] << 8 | line[2 * bpl + i];
            else if (pal) { const uint8_t* p = pal + line[i] * 3; v = (uint32_t)p[0] << 16 | p[1] << 8 | p[2]; }
            else v = line[i] * 0x010101u;
            b->px[(size_t)y * w + i] = v;
        }
    }
    return true;
}

// .img: u32 width, u32 height, raw RGB565 (magenta 0xF81F transparent)
bool Bitmap::LoadData(char* name, unsigned char, int) {
    std::vector<uint8_t> d;
    if (!name || !read_file(name, d) || d.size() < 8) { LOG("missing image %s", name ? name : "?"); return false; }
    uint32_t w, h; memcpy(&w, &d[0], 4); memcpy(&h, &d[4], 4);
    if (w > 4096 || h > 4096 || 8 + (size_t)w * h * 2 > d.size()) { LOG("bad image %s", name); return false; }
    bmp_resize(this, w, h, kKey);
    BmpData* b = bd(this);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint16_t c = d[8 + 2 * i] | d[9 + 2 * i] << 8;
        b->px[i] = c == 0xf81f ? kKey : (merit_rgb565(c, 255) & 0x00ffffff);
    }
    return true;
}
bool Bitmap::LoadCompressedData(char* name, _IO_FILE*) {
    // a .dlt picture (first frame) when it is one, else the raw .img layout
    std::string path;
    if (name && find_asset(name, false, "", path)) {
        std::vector<uint8_t> d;
        if (merit_read_gz(path.c_str(), d)) {
            size_t off, count;
            if (merit_container(d, off, count)) {
                std::vector<MeritFrame> f;
                merit_read_frames(d, off, f, 1);
                if (!f.empty()) {
                    frames_to_px(f);
                    bmp_resize(this, f[0].w, f[0].h, kKey);
                    bd(this)->px = f[0].argb;
                    return true;
                }
            }
        }
    }
    return LoadData(name, 0, -1);
}
bool Bitmap::LoadTGA_32(char* name) {
    std::vector<uint8_t> d;
    if (!name || !read_file(name, d) || d.size() < 18) return false;
    int idlen = d[0], type = d[2], w = d[12] | d[13] << 8, h = d[14] | d[15] << 8, bpp = d[16], desc = d[17];
    if (type != 2 || (bpp != 32 && bpp != 24)) { LOG("unsupported TGA %s", name); return false; }
    int bytes = bpp / 8;
    bmp_resize(this, w, h, kKey);
    BmpData* b = bd(this);
    size_t o = 18 + idlen;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w && o + bytes <= d.size(); x++, o += bytes) {
            int yy = (desc & 0x20) ? y : h - 1 - y;
            uint32_t a = bytes == 4 ? d[o + 3] : 255;
            b->px[(size_t)yy * w + x] = a < 128 ? kKey : ((uint32_t)d[o + 2] << 16 | d[o + 1] << 8 | d[o]);
        }
    return true;
}

// Draws into the open video buffer (or the screen); transparent pixels skipped. A non-zero
// flag also treats pure black as transparent (PCX sprites have no other transparency).
void Bitmap::Display(int x, int y, int flag) {
    BmpData* b = bd(this);
    if (b->w <= 0) return;
    blit(b->px.data(), b->w, b->h, 0, 0, target(), SW, SH, x, y, b->w, b->h, true, flag ? 0u : kKey);
    touched_target();
}
void Bitmap::DisplayRegion(int x, int y, int sx, int sy, int w, int h, int) {
    BmpData* b = bd(this);
    blit(b->px.data(), b->w, b->h, sx, sy, target(), SW, SH, x, y, w, h, true, kKey);
    touched_target();
}
void Bitmap::CopyFromCurrent(int x, int y) {
    BmpData* b = bd(this);
    blit(target(), SW, SH, x, y, b->px.data(), b->w, b->h, 0, 0, b->w, b->h, false, 0);
}
void Bitmap::CopyToBitmap(Bitmap* dst, int dx, int dy, int sx, int sy, int w, int h) {
    if (!dst) return;
    BmpData* s = bd(this); BmpData* d = bd(dst);
    blit(s->px.data(), s->w, s->h, sx, sy, d->px.data(), d->w, d->h, dx, dy, w, h, true, kKey);
}

// Animations attached to a bitmap (.dlt "smacks").
bool Bitmap::SmackAnimationLoad(char* name, unsigned char lang) {
    BmpData* b = bd(this);
    delete b->anim;
    b->anim = name ? anim_load(name, lang) : nullptr;
    if (!b->anim) return false;
    bmp_resize(this, b->anim->w, b->anim->h, kKey);
    b->anim->target = nullptr;
    return true;
}
void Bitmap::SmackAnimationUnload() { BmpData* b = bd(this); delete b->anim; b->anim = nullptr; }
void Bitmap::SmackAnimationRewind() { BmpData* b = bd(this); if (b->anim) anim_seek(b->anim, 0); }
void Bitmap::SmackAnimationJumpTo(int f) {
    BmpData* b = bd(this);
    if (b->anim && !b->anim->frames.empty()) anim_seek(b->anim, f < 0 ? 0 : f % (int)b->anim->frames.size());
}
// Shows the current frame at (x, y), then steps to the next one.
void Bitmap::SmackAnimationDisplay(int x, int y, unsigned char) {
    BmpData* b = bd(this);
    if (!b->anim) return;
    Anim* a = b->anim;
    if (a->canvas.empty()) anim_seek(a, a->cur);
    blit(a->canvas.data(), a->w, a->h, 0, 0, target(), SW, SH, x, y, a->w, a->h, true, kKey);
    touched_target();
    anim_seek(a, (a->cur + 1) % (int)a->frames.size());
}
void Bitmap::DisplaySmack(unsigned short x, unsigned short y, unsigned char f) { SmackAnimationDisplay(x, y, f); }

// --- fonts and text boxes. BmpFont(w, h, n) picks a cabinet bitmap font (gamedata/fonts/*.dlt:
// one glyph per frame, frame = character code) by cell size.
class BmpFont { public: BmpFont(short, short, unsigned short); };

// FontBase objects are polymorphic: games call their virtual methods (slots 0x10, 0x2c, 0x4c,
// 0x54, ...). Until each slot's meaning is known, every slot is a stand-in that logs its first
// call with its arguments (MEGA_DEBUG_FONT=1 logs every call) and returns 0. Slots get real
// implementations in font_slot() as they are identified.
namespace {
int font_slot(int slot, void* self, const int* a);
template <int N> int font_vfn(void* self, int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9) {
    int a[10] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9};
    return font_slot(N, self, a);
}
template <int... I> struct Seq {};
template <int N, int... I> struct MakeSeq : MakeSeq<N - 1, N - 1, I...> {};
template <int... I> struct MakeSeq<0, I...> { typedef Seq<I...> type; };
template <int... I> void* const* make_font_vtable(Seq<I...>) {
    static void* const vt[] = {reinterpret_cast<void*>(&font_vfn<I>)...};
    return vt;
}
void* const* font_vtable() { static void* const* vt = make_font_vtable(MakeSeq<48>::type()); return vt; }
void font_attach(void* obj) { void* const* vt = font_vtable(); memcpy(obj, &vt, sizeof vt); }
}
namespace {
struct GlyphFont { int w = 0, h = 0; std::vector<MeritFrame> glyphs; std::vector<int> adv; };
std::map<const void*, GlyphFont*> g_fonts;
GlyphFont* g_defaultFont;

GlyphFont* font_load(const char* file) {
    std::string path = std::string("/usr/local/gamedata/fonts/") + file;
    std::vector<uint8_t> d;
    if (!merit_read_gz(path.c_str(), d)) return nullptr;
    size_t off, count;
    if (!merit_container(d, off, count)) return nullptr;
    auto* f = new GlyphFont;
    merit_read_frames(d, off, f->glyphs, count);
    if (f->glyphs.empty()) { delete f; return nullptr; }
    frames_to_px(f->glyphs);
    f->w = f->glyphs[0].w; f->h = f->glyphs[0].h;
    for (auto& g : f->glyphs) {           // proportional advance: rightmost opaque column + 2
        int r = -1;
        for (int y = 0; y < g.h; y++) for (int x = g.w - 1; x > r; x--) if (g.argb[(size_t)y * g.w + x] != kKey) { r = x; break; }
        f->adv.push_back(r < 0 ? f->w / 2 : r + 2);
    }
    return f;
}
GlyphFont* font_for(short w, short h) {
    char names[4][32];
    snprintf(names[0], 32, "b%dx%d.dlt.gz", w, h); snprintf(names[1], 32, "%dx%d.dlt.gz", w, h);
    snprintf(names[2], 32, "%dx%dw.dlt.gz", w, h); snprintf(names[3], 32, "%dx%d.dlt.gz", h, h);
    for (auto& n : names) if (GlyphFont* f = font_load(n)) return f;
    return nullptr;
}
GlyphFont* default_font() {
    if (!g_defaultFont) g_defaultFont = font_load("12x16.dlt.gz");    // white glyphs, fits the games' text boxes
    if (!g_defaultFont) g_defaultFont = font_load("b24x24.dlt.gz");
    return g_defaultFont;
}
}
BmpFont::BmpFont(short w, short h, unsigned short) {
    video_init();
    font_attach(this);
    GlyphFont* f = font_for(w, h);
    if (!f) { LOG("no bitmap font %dx%d, using the default", w, h); f = default_font(); }
    g_fonts[this] = f;
}

// A w x h bitmap with `text` centred, glyphs tinted with (r, g, b) unless all are 0.
void Bitmap::CreateColoredSmackTextBox(char const* text, FontBase* font, signed char, unsigned short w,
                                       unsigned short h, short r, short g, short bl, unsigned char, bool) {
    GlyphFont* f = nullptr;
    auto it = g_fonts.find(font);
    f = it != g_fonts.end() ? it->second : default_font();
    if (!f) return;
    const unsigned char* t = reinterpret_cast<const unsigned char*>(text ? text : "");
    int tw = 0;
    for (const unsigned char* p = t; *p; p++) tw += *p < f->adv.size() ? f->adv[*p] : f->w;
    int W = w ? w : tw, H = h ? h : f->h;
    bmp_resize(this, W, H, kKey);
    BmpData* b = bd(this);
    int x = (W - tw) / 2, y = (H - f->h) / 2;
    bool tint = r || g || bl;
    for (const unsigned char* p = t; *p; p++) {
        if (*p < f->glyphs.size()) {
            const MeritFrame& gl = f->glyphs[*p];
            for (int yy = 0; yy < gl.h; yy++)
                for (int xx = 0; xx < gl.w; xx++) {
                    uint32_t c = gl.argb[(size_t)yy * gl.w + xx];
                    if (c == kKey) continue;
                    if (tint) {
                        uint32_t lum = ((c >> 16 & 255) + (c >> 8 & 255) + (c & 255)) / 3;
                        c = (uint32_t)(r * lum / 255 & 255) << 16 | (uint32_t)(g * lum / 255 & 255) << 8 | (uint32_t)(bl * lum / 255 & 255);
                    }
                    int X = x + xx, Y = y + yy;
                    if (X >= 0 && X < W && Y >= 0 && Y < H) b->px[(size_t)Y * W + X] = c;
                }
        }
        x += *p < f->adv.size() ? f->adv[*p] : f->w;
    }
}

// The loader's graphics globals: FontBase* fonts at +0x04..+0x80 (games use +0x08, +0x1c, +0x40).
class MegacGraphics { public: static MegacGraphics* GetInstance(); };
MegacGraphics* MegacGraphics::GetInstance() {
    static unsigned char g[0x400];
    static bool init;
    if (!init) {
        init = true;
        for (int off = 0x04; off <= 0x80; off += 4) {
            auto* f = new unsigned char[256]();
            font_attach(f);
            g_fonts[f] = default_font();
            memcpy(g + off, &f, sizeof f);
        }
    }
    return reinterpret_cast<MegacGraphics*>(g);
}

namespace {
struct FontState { int align = 0; int spacing = 0; };
std::map<const void*, FontState> g_fontState;

int text_width(GlyphFont* f, const unsigned char* t, int spacing) {
    int w = 0;
    for (; *t; t++) w += (*t < f->adv.size() ? f->adv[*t] : f->w) + spacing;
    return w;
}
void text_draw(GlyphFont* f, const unsigned char* t, int x, int y, int spacing) {
    uint32_t* dst = target();
    for (; *t; t++) {
        if (*t < f->glyphs.size()) {
            const MeritFrame& g = f->glyphs[*t];
            blit(g.argb.data(), g.w, g.h, 0, 0, dst, SW, SH, x, y, g.w, g.h, true, kKey);
        }
        x += (*t < f->adv.size() ? f->adv[*t] : f->w) + spacing;
    }
    touched_target();
}

// Slots identified so far (byte offsets): 0x18 width of a string, 0x24 alignment
// (0 left, 1 centre, 2 right), 0x2c draw text (text, x, y, width, ...), 0x54 spacing.
int font_slot(int slot, void* self, const int* a) {
    static bool all = menv("DEBUG_FONT") != nullptr;
    static std::map<int, bool> seen;
    if (all || !seen[slot]) {
        seen[slot] = true;
        LOG("font %p slot 0x%x args %d %d %d %d %d %d %d %d", self, slot * 4, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
    auto it = g_fonts.find(self);
    GlyphFont* f = it != g_fonts.end() && it->second ? it->second : default_font();
    FontState& st = g_fontState[self];
    if (!f) return 0;
    switch (slot * 4) {
    case 0x18: {                         // width of a string
        const char* t = reinterpret_cast<const char*>(a[0]);
        return t ? text_width(f, reinterpret_cast<const unsigned char*>(t), st.spacing) : 0;
    }
    case 0x24: st.align = a[0]; return 0;
    case 0x54: st.spacing = a[0] > 4 ? 0 : a[0]; return 0;   // values seen (10, 12) look like sizes, not gaps
    case 0x2c: {                         // draw: text, x, y, width (0 = at x)
        const unsigned char* t = reinterpret_cast<const unsigned char*>(a[0]);
        if (!t) return 0;
        int x = a[1], y = a[2], w = a[3], tw = text_width(f, t, st.spacing);
        if (w > 0) x += st.align == 1 ? (w - tw) / 2 : st.align == 2 ? w - tw : 0;
        else x -= st.align == 1 ? tw / 2 : st.align == 2 ? tw : 0;
        text_draw(f, t, x, y, st.spacing);
        return tw;
    }
    }
    return 0;
}
}

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
};
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
    if (id >= 0) memcpy(g_screen, vb_pixels(id), (size_t)SW * SH * 4);
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
class TextSystem { public: void LoadTranslations(char const*, bool); void SetCurrentLanguage(Locale::Languages); };
void TextSystem::LoadTranslations(char const* name, bool) { Translator::LoadTranslations(name, false); }
void TextSystem::SetCurrentLanguage(Locale::Languages) {}
TextSystem textSystem;

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
// BitmapTextTTF(bmp, text, x, y, w, h, c1, c2, c3, size, align, bool, family, spacing)
// c1..c3: colour adjustments (0,0,-255 / 0,-100,-255 / 0,0,0 seen); drawn white for now.
void BitmapTextTTF(_RADBitmap* b, char const* text, int x, int y, int w, int h, int c1, int c2, int c3,
                   int size, int align, bool bold, char* family, int spacing) {
    if (!b || !text) return;
    static bool dbg = menv("DEBUG_TEXT") != nullptr;
    if (dbg) LOG("BitmapTextTTF '%s' box %d,%d %dx%d c %d,%d,%d size %d align %d %d %s %d", text, x, y, w, h, c1, c2, c3, size, align, bold, family ? family : "-", spacing);
    legacy::ttf_draw(b->bmp, text, x, y, w, h, 255, 255, 255, size, align, bold, family, 0, 0);
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
