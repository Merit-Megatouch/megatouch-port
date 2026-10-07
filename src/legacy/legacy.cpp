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
//  * Bitmaps are _RADBitmap { u32 tag; u32 width; u32 height; ... } — games read +4/+8.
//  * Colours are 8-bit "palette indices" from the old API: 0 = black, 5 = the transparency key.
//    Art is RGB565 (.dlt); pixels the RLE skips are the key colour (stored as 0x00000000).
//  * Animations (_MSmack, after RAD's Smacker) are .dlt files: u32 version 3, u16 w, h,
//    frames, delay, then run-length frames (src/common/merit_rle.h).
#include "../common/env.h"
#include "../common/merit_rle.h"
#include "legacy_internal.h"
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
struct _RADBitmap { uint32_t tag; uint32_t w; uint32_t h; uint32_t* px; };
struct _MSmack;
struct SoundBase;
struct Bitmap;

static const uint32_t kTag = 0x52414442;          // "RADB"
static const uint32_t kKey = 0x00000000;          // transparency key (palette index 5)

static uint32_t color_of(unsigned idx) {
    switch (idx) {
    case 5:   return kKey;
    case 0:   return 0xff000000;
    case 255: return 0xffffffff;
    default:  return 0xff000000 | (idx * 0x010101);   // unknown palette: grey ramp
    }
}

// ---------------------------------------------------------------------------------- screen
static const int SW = 640, SH = 480;              // the 2D API's screen
static uint32_t g_screen[SW * SH];
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
    for (auto& p : g_screen) p = 0xff000000;
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
static void autoclick() {
    static const char* spec = menv("AUTOCLICK");
    static size_t pos;
    if (!spec) return;
    while (spec[pos]) {
        unsigned t; int x, y, used = 0;
        if (sscanf(spec + pos, "%u:%d,%d%n", &t, &x, &y, &used) != 3) { spec = nullptr; return; }
        if (SDL_GetTicks() - g_start < t) return;
        pos += used + (spec[pos + used] == ';');
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
    if (!g_gl && g_dirty && SDL_GetTicks() - g_lastPresent >= 15) present();
}

static void sleep_ms(Uint32 ms) {
    Uint32 end = SDL_GetTicks() + ms;
    do { pump(); SDL_Delay(1); } while (!g_quit && SDL_GetTicks() < end);
}

// ---------------------------------------------------------------------------------- bitmaps
static _RADBitmap* bmp_new(uint32_t w, uint32_t h, uint32_t fill) {
    auto* b = static_cast<_RADBitmap*>(calloc(1, sizeof(_RADBitmap)));
    b->tag = kTag; b->w = w; b->h = h;
    b->px = static_cast<uint32_t*>(malloc((size_t)w * h * 4 + 4));
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
            if (trans && p == key) continue;
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
    if (!find_asset(name, lang, ".dlt", path)) { LOG("missing animation %s", name); return nullptr; }
    std::vector<uint8_t> d;
    if (!merit_read_gz(path.c_str(), d) || d.size() < 12) { LOG("unreadable %s", path.c_str()); return nullptr; }
    auto* a = new Anim;
    uint32_t ver; uint16_t hdr[4];
    memcpy(&ver, &d[0], 4); memcpy(hdr, &d[4], 8);
    size_t off = ver == 3 ? 12 : 4;                 // 3 = .dlt header, 2 = plain .spr layout
    unsigned count = ver == 3 ? hdr[2] : (unsigned)-1;
    if (ver == 3 && hdr[3]) a->delay = hdr[3];
    merit_read_frames(d, off, a->frames, count);
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
void BitmapFree(_RADBitmap* b) { if (b && b->tag == kTag) { b->tag = 0; free(b->px); free(b); } }
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
void BitmapSetPalette(_RADBitmap*) {}                       // 8-bit era; art is RGB565
void BitmapPaletteToPalette(_RADBitmap*, _RADBitmap*) {}

// Copies the dst-sized region of src starting at (sx, sy) into dst.
void BitmapToBitmap(_RADBitmap* dst, _RADBitmap* src, unsigned long sx, unsigned long sy) {
    if (dst && src) blit(src->px, src->w, src->h, (int)sx, (int)sy, dst->px, dst->w, dst->h, 0, 0, dst->w, dst->h, false, 0);
}
void BitmapToBitmapTrans(_RADBitmap* dst, _RADBitmap* src, unsigned long sx, unsigned long sy, unsigned char key) {
    if (dst && src) blit(src->px, src->w, src->h, (int)sx, (int)sy, dst->px, dst->w, dst->h, 0, 0, dst->w, dst->h, true, color_of(key));
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

// The loader's global state. Games read fields inline: +0x24 player count, +0x2038 current
// GameId; a SystemClass object lives at +0x22dc.
class MegacGlobals { public: static MegacGlobals* GetInstance(); };
MegacGlobals* MegacGlobals::GetInstance() {
    static unsigned char g[0x4000];
    static bool init;
    if (!init) {
        init = true;
        g[0x24] = (unsigned char)menv_int("PLAYERS", 1);
        int id = menv_int("GAME_ID", 0);
        memcpy(g + 0x2038, &id, 4);
    }
    return reinterpret_cast<MegacGlobals*>(g);
}

// "Quit game?" prompt: home play just quits.
class SystemClass { public: bool ConfirmExit(unsigned short, unsigned short, Bitmap*, bool); };
bool SystemClass::ConfirmExit(unsigned short, unsigned short, Bitmap*, bool) { return true; }
