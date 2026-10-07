// The part of Allegro 4.0 that legacy games call directly, implemented natively.
//
// On the cabinet these came from liballeg-4.0.0.so plus Allegro's assembler drawing core, which was
// a static library linked into the (protected) loader executable — so the shipped .so alone cannot
// draw anything. Games use only memory bitmaps here, so this is a small C implementation: BITMAP and
// GFX_VTABLE have the 4.0 layouts (games inline vtable calls and read line[]), `screen` is the
// 640x480 32-bit framebuffer that src/legacy presents, and the colour helpers, blenders, mouse
// globals, timers and file search behave as Allegro's did.
//
// Not implemented (rarely used, see tools/legacy-missing.py): fonts/textout, the GUI dialog
// engine, config files, 3D/zbuffer, datafiles.
#include "allegro.h"
#include "legacy_internal.h"
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// write_bank / read_bank for memory bitmaps: Allegro's inline bmp_write_line() calls these with
// edx = bitmap, eax = line and expects the line address in eax (no other register touched).
extern "C" void al_stub_bank() __attribute__((visibility("hidden")));
extern "C" void al_stub_unbank() __attribute__((visibility("hidden")));
asm(".text\n"
    ".globl al_stub_bank\n.type al_stub_bank,@function\n"
    "al_stub_bank:\n  movl 0x40(%edx,%eax,4), %eax\n  ret\n"
    ".globl al_stub_unbank\n.type al_stub_unbank,@function\n"
    "al_stub_unbank:\n  ret\n");

struct RGB { unsigned char r, g, b, filler; };
static const unsigned long BMP_ID_SUB = 0x20000000;

extern "C" {
BITMAP* screen;
int _color_depth = 32;
int _rgb_r_shift_15 = 10, _rgb_g_shift_15 = 5, _rgb_b_shift_15 = 0;
int _rgb_r_shift_16 = 11, _rgb_g_shift_16 = 5, _rgb_b_shift_16 = 0;
int _rgb_r_shift_24 = 16, _rgb_g_shift_24 = 8, _rgb_b_shift_24 = 0;
int _rgb_r_shift_32 = 16, _rgb_g_shift_32 = 8, _rgb_b_shift_32 = 0, _rgb_a_shift_32 = 24;
int _rgb_scale_5[32], _rgb_scale_6[64];
RGB _current_palette[256];
RGB desktop_palette[256];
volatile int mouse_x, mouse_y, mouse_b, mouse_pos;
volatile char key[128];
int gfx_capabilities;
int* allegro_errno = &errno;
char allegro_error[256];
void* system_driver;
void* mouse_driver;
void* keyboard_driver;
int keyboard_needs_poll;
void* font;
struct GfxDriver {                                // GFX_DRIVER: games read w/h at +0x68/+0x6c
    int id; const char *name, *desc, *ascii_name;
    void* fn[22];
    int w, h, linear;
    long bank_size, bank_gran, vid_mem, vid_phys_base;
    int windowed;
};
static GfxDriver g_driver;
GfxDriver* gfx_driver;
}

// ------------------------------------------------------------------------------ pixels
static int depth_of(BITMAP* b) { return b->vtable->color_depth; }
static int bpp_of(int d) { return d == 8 ? 1 : d <= 16 ? 2 : d == 24 ? 3 : 4; }
static int mask_of(int d) { return d == 8 ? 0 : d == 15 ? 0x7c1f : d == 16 ? 0xf81f : 0xff00ff; }

static inline int raw_get(BITMAP* b, int x, int y) {
    unsigned char* p = b->line[y];
    switch (depth_of(b)) {
    case 8: return p[x];
    case 15: case 16: return reinterpret_cast<uint16_t*>(p)[x];
    case 24: p += x * 3; return p[0] | p[1] << 8 | p[2] << 16;
    default: return (int)reinterpret_cast<uint32_t*>(p)[x];
    }
}
static inline void raw_put(BITMAP* b, int x, int y, int c) {
    unsigned char* p = b->line[y];
    switch (depth_of(b)) {
    case 8: p[x] = (unsigned char)c; break;
    case 15: case 16: reinterpret_cast<uint16_t*>(p)[x] = (uint16_t)c; break;
    case 24: p += x * 3; p[0] = c; p[1] = c >> 8; p[2] = c >> 16; break;
    default: reinterpret_cast<uint32_t*>(p)[x] = (uint32_t)c; break;
    }
}

extern "C" int makecol_depth(int d, int r, int g, int b);
extern "C" int getr_depth(int d, int c);
extern "C" int getg_depth(int d, int c);
extern "C" int getb_depth(int d, int c);
extern "C" int geta_depth(int d, int c);

static int pal_nearest(int r, int g, int b) {
    int best = 0, bd = 1 << 30;
    for (int i = 1; i < 256; i++) {
        int dr = _current_palette[i].r * 4 - r, dg = _current_palette[i].g * 4 - g, db = _current_palette[i].b * 4 - b;
        int dd = dr * dr + dg * dg + db * db;
        if (dd < bd) { bd = dd; best = i; }
    }
    return best;
}

// colour c of depth `from` in depth `to`, keeping the mask colour masked
static inline int convert(int c, int from, int to) {
    if (from == to) return c;
    if (c == mask_of(from)) return mask_of(to);
    int r = getr_depth(from, c), g = getg_depth(from, c), b = getb_depth(from, c);
    if (to == 8) return pal_nearest(r, g, b);
    int out = makecol_depth(to, r, g, b);
    if (from == 32 && to == 32) out |= c & 0xff000000;
    return out;
}

static inline bool clip_ok(BITMAP* b, int x, int y) {
    return x >= b->cl && x < b->cr && y >= b->ct && y < b->cb;
}

// ------------------------------------------------------------------------------ blending
enum Blend { kTrans, kAlpha, kBurn };
static Blend g_blend = kTrans;
static int g_blendR, g_blendG, g_blendB, g_blendA = 255;

static int blend_px(int d, int src, int dst, int n) {
    int sr = getr_depth(d, src), sg = getg_depth(d, src), sb = getb_depth(d, src);
    int dr = getr_depth(d, dst), dg = getg_depth(d, dst), db = getb_depth(d, dst);
    if (g_blend == kBurn) {
        return makecol_depth(d, std::max(0, dr - (255 - sr) * n / 255), std::max(0, dg - (255 - sg) * n / 255),
                             std::max(0, db - (255 - sb) * n / 255));
    }
    return makecol_depth(d, dr + (sr - dr) * n / 255, dg + (sg - dg) * n / 255, db + (sb - db) * n / 255);
}

// ------------------------------------------------------------------------------ vtable functions
static void v_set_clip(BITMAP*) {}
static void v_acquire(BITMAP*) {}
static int v_getpixel(BITMAP* b, int x, int y) {
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) return -1;
    return raw_get(b, x, y);
}
static void v_putpixel(BITMAP* b, int x, int y, int c) { if (clip_ok(b, x, y)) raw_put(b, x, y, c); }
static void v_hline(BITMAP* b, int x1, int y, int x2, int c) {
    if (x1 > x2) std::swap(x1, x2);
    if (y < b->ct || y >= b->cb) return;
    x1 = std::max(x1, b->cl); x2 = std::min(x2, b->cr - 1);
    for (int x = x1; x <= x2; x++) raw_put(b, x, y, c);
}
static void v_vline(BITMAP* b, int x, int y1, int y2, int c) {
    if (y1 > y2) std::swap(y1, y2);
    if (x < b->cl || x >= b->cr) return;
    y1 = std::max(y1, b->ct); y2 = std::min(y2, b->cb - 1);
    for (int y = y1; y <= y2; y++) raw_put(b, x, y, c);
}
static void v_line(BITMAP* b, int x1, int y1, int x2, int y2, int c) {
    int dx = abs(x2 - x1), dy = -abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, e = dx + dy;
    for (;;) {
        v_putpixel(b, x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x1 += sx; }
        if (e2 <= dx) { e += dx; y1 += sy; }
    }
}
static void v_rectfill(BITMAP* b, int x1, int y1, int x2, int y2, int c) {
    if (y1 > y2) std::swap(y1, y2);
    for (int y = y1; y <= y2; y++) v_hline(b, x1, y, x2, c);
}
static int v_triangle(BITMAP* b, int x1, int y1, int x2, int y2, int x3, int y3, int c) {
    int miny = std::min({y1, y2, y3}), maxy = std::max({y1, y2, y3});
    int px[3] = {x1, x2, x3}, py[3] = {y1, y2, y3};
    for (int y = miny; y <= maxy; y++) {
        int lo = 1 << 30, hi = -(1 << 30);
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3;
            int ya = py[i], yb = py[j], xa = px[i], xb = px[j];
            if (ya == yb) { if (y == ya) { lo = std::min({lo, xa, xb}); hi = std::max({hi, xa, xb}); } continue; }
            if (y < std::min(ya, yb) || y > std::max(ya, yb)) continue;
            int x = xa + (xb - xa) * (y - ya) / (yb - ya);
            lo = std::min(lo, x); hi = std::max(hi, x);
        }
        if (lo <= hi) v_hline(b, lo, y, hi, c);
    }
    return 0;
}

// sprite drawing, clipped to dst's clip rectangle. mode: 0 masked, 1 trans, 2 lit
template <int Mode>
static void sprite_core(BITMAP* dst, BITMAP* spr, int x, int y, bool hflip, bool vflip, int lit = 0) {
    int sd = depth_of(spr), dd = depth_of(dst), mask = mask_of(sd);
    for (int j = 0; j < spr->h; j++) {
        int ty = y + j;
        if (ty < dst->ct || ty >= dst->cb) continue;
        int syy = vflip ? spr->h - 1 - j : j;
        for (int i = 0; i < spr->w; i++) {
            int tx = x + i;
            if (tx < dst->cl || tx >= dst->cr) continue;
            int sxx = hflip ? spr->w - 1 - i : i;
            int c = raw_get(spr, sxx, syy);
            if (sd == 8 ? c == 0 : (c & 0xffffff) == (mask & 0xffffff)) continue;
            if (Mode == 0) { raw_put(dst, tx, ty, convert(c, sd, dd)); continue; }
            if (Mode == 1) {
                int n = g_blendA;
                if (g_blend == kAlpha && sd == 32) n = (unsigned)c >> 24;
                int cc = convert(c & (sd == 32 ? 0xffffff : ~0), sd, dd);
                raw_put(dst, tx, ty, blend_px(dd, cc, raw_get(dst, tx, ty), n));
                continue;
            }
            int cc = convert(c, sd, dd);
            raw_put(dst, tx, ty, blend_px(dd, makecol_depth(dd, g_blendR, g_blendG, g_blendB), cc, lit));
        }
    }
}
static void v_draw_sprite(BITMAP* d, BITMAP* s, int x, int y) { sprite_core<0>(d, s, x, y, false, false); }
static void v_draw_sprite_v(BITMAP* d, BITMAP* s, int x, int y) { sprite_core<0>(d, s, x, y, false, true); }
static void v_draw_sprite_h(BITMAP* d, BITMAP* s, int x, int y) { sprite_core<0>(d, s, x, y, true, false); }
static void v_draw_sprite_vh(BITMAP* d, BITMAP* s, int x, int y) { sprite_core<0>(d, s, x, y, true, true); }
static void v_draw_trans(BITMAP* d, BITMAP* s, int x, int y) { sprite_core<1>(d, s, x, y, false, false); }
static void v_draw_lit(BITMAP* d, BITMAP* s, int x, int y, int c) { sprite_core<2>(d, s, x, y, false, false, c); }
static void v_draw_character(BITMAP* d, BITMAP* s, int x, int y, int c) {
    for (int j = 0; j < s->h; j++) for (int i = 0; i < s->w; i++)
        if (raw_get(s, i, j)) v_putpixel(d, x + i, y + j, c);
}
static void v_rle_unsupported(BITMAP*, const void*, int, int) {}
static void v_rle_lit_unsupported(BITMAP*, const void*, int, int, int) {}
static void v_glyph_unsupported(BITMAP*, const void*, int, int, int) {}

static void blit_core(BITMAP* s, BITMAP* d, int sx, int sy, int dx, int dy, int w, int h, bool masked) {
    int sd = depth_of(s), dd = depth_of(d), mask = mask_of(sd);
    bool back = s->dat == d->dat && (dy > sy || (dy == sy && dx > sx));
    for (int jj = 0; jj < h; jj++) {
        int j = back ? h - 1 - jj : jj;
        if (!masked && sd == dd) {
            memmove(d->line[dy + j] + dx * bpp_of(dd), s->line[sy + j] + sx * bpp_of(sd), (size_t)w * bpp_of(dd));
            continue;
        }
        for (int ii = 0; ii < w; ii++) {
            int i = back ? w - 1 - ii : ii;
            int c = raw_get(s, sx + i, sy + j);
            if (masked && (sd == 8 ? c == 0 : (c & 0xffffff) == (mask & 0xffffff))) continue;
            raw_put(d, dx + i, dy + j, convert(c, sd, dd));
        }
    }
}
static void v_blit(BITMAP* s, BITMAP* d, int sx, int sy, int dx, int dy, int w, int h) { blit_core(s, d, sx, sy, dx, dy, w, h, false); }
static void v_masked_blit(BITMAP* s, BITMAP* d, int sx, int sy, int dx, int dy, int w, int h) { blit_core(s, d, sx, sy, dx, dy, w, h, true); }
static void v_clear_to_color(BITMAP* b, int c) { v_rectfill(b, b->cl, b->ct, b->cr - 1, b->cb - 1, c); }
static void v_pivot(BITMAP* d, BITMAP* s, int x, int y, int, int, int, int, int) { v_draw_sprite(d, s, x, y); }

static GFX_VTABLE make_vtable(int depth) {
    GFX_VTABLE v{};
    v.color_depth = depth; v.mask_color = mask_of(depth);
    v.unwrite_bank = reinterpret_cast<void*>(al_stub_unbank);
    v.set_clip = v_set_clip; v.acquire = v_acquire; v.release = v_acquire;
    v.getpixel = v_getpixel; v.putpixel = v_putpixel; v.vline = v_vline; v.hline = v_hline; v.hfill = v_hline;
    v.line = v_line; v.rectfill = v_rectfill; v.triangle = v_triangle;
    v.draw_sprite = v_draw_sprite; v.draw_256_sprite = v_draw_sprite; v.draw_sprite_v_flip = v_draw_sprite_v;
    v.draw_sprite_h_flip = v_draw_sprite_h; v.draw_sprite_vh_flip = v_draw_sprite_vh;
    v.draw_trans_sprite = v_draw_trans; v.draw_trans_rgba_sprite = v_draw_trans; v.draw_lit_sprite = v_draw_lit;
    v.draw_rle_sprite = v_rle_unsupported; v.draw_trans_rle_sprite = v_rle_unsupported;
    v.draw_trans_rgba_rle_sprite = v_rle_unsupported; v.draw_lit_rle_sprite = v_rle_lit_unsupported;
    v.draw_character = v_draw_character; v.draw_glyph = v_glyph_unsupported;
    v.blit_from_memory = v.blit_to_memory = v.blit_from_system = v.blit_to_system = v_blit;
    v.blit_to_self = v.blit_to_self_forward = v.blit_to_self_backward = v.blit_between_formats = v_blit;
    v.masked_blit = v_masked_blit; v.clear_to_color = v_clear_to_color; v.pivot_scaled_sprite_flip = v_pivot;
    return v;
}
static GFX_VTABLE g_vt8 = make_vtable(8), g_vt15 = make_vtable(15), g_vt16 = make_vtable(16),
                  g_vt24 = make_vtable(24), g_vt32 = make_vtable(32);
static GFX_VTABLE* vtable_for(int d) {
    switch (d) { case 8: return &g_vt8; case 15: return &g_vt15; case 16: return &g_vt16; case 24: return &g_vt24; default: return &g_vt32; }
}
extern "C" { GFX_VTABLE __linear_vtable8, __linear_vtable15, __linear_vtable16, __linear_vtable24, __linear_vtable32; }

// ------------------------------------------------------------------------------ bitmaps
static BITMAP* bmp_struct(int depth, int w, int h) {
    auto* b = static_cast<BITMAP*>(calloc(1, sizeof(BITMAP) + sizeof(unsigned char*) * (h > 0 ? h : 1)));
    b->w = w; b->h = h; b->clip = 1; b->cl = 0; b->ct = 0; b->cr = w; b->cb = h;
    b->vtable = vtable_for(depth);
    b->write_bank = b->read_bank = reinterpret_cast<void*>(al_stub_bank);
    return b;
}
namespace legacy {
void al_set_mode(int w, int h) { g_driver.w = w; g_driver.h = h; g_driver.vid_mem = w * h * 4; }
BITMAP* al_wrap32(uint32_t* px, int w, int h) {
    BITMAP* b = bmp_struct(32, w, h);
    b->dat = px;
    b->id = BMP_ID_SUB;                           // memory not owned
    for (int y = 0; y < h; y++) b->line[y] = reinterpret_cast<unsigned char*>(px + (size_t)y * w);
    return b;
}
}

extern "C" {
BITMAP* create_bitmap_ex(int depth, int w, int h) {
    if (w < 0 || h < 0) return nullptr;
    BITMAP* b = bmp_struct(depth, w, h);
    size_t pitch = (size_t)w * bpp_of(depth);
    b->dat = calloc(1, pitch * (h ? h : 1) + 4);
    for (int y = 0; y < h; y++) b->line[y] = static_cast<unsigned char*>(b->dat) + y * pitch;
    return b;
}
BITMAP* create_bitmap(int w, int h) { return create_bitmap_ex(_color_depth, w, h); }
BITMAP* create_sub_bitmap(BITMAP* p, int x, int y, int w, int h) {
    if (!p) return nullptr;
    x = std::max(0, x); y = std::max(0, y);
    w = std::min(w, p->w - x); h = std::min(h, p->h - y);
    if (w <= 0 || h <= 0) return nullptr;
    BITMAP* b = bmp_struct(depth_of(p), w, h);
    b->dat = p->dat; b->id = BMP_ID_SUB;
    b->x_ofs = p->x_ofs + x; b->y_ofs = p->y_ofs + y;
    for (int j = 0; j < h; j++) b->line[j] = p->line[y + j] + x * bpp_of(depth_of(p));
    return b;
}
void destroy_bitmap(BITMAP* b) {
    if (!b || b == screen) return;
    if (!(b->id & BMP_ID_SUB)) free(b->dat);
    free(b);
}
int bitmap_color_depth(BITMAP* b) { return depth_of(b); }
int bitmap_mask_color(BITMAP* b) { return b->vtable->mask_color; }

void set_clip(BITMAP* b, int x1, int y1, int x2, int y2) {
    if (!x1 && !y1 && !x2 && !y2) { b->clip = 0; b->cl = b->ct = 0; b->cr = b->w; b->cb = b->h; return; }
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    b->clip = 1;
    b->cl = std::max(0, x1); b->ct = std::max(0, y1);
    b->cr = std::min(b->w, x2 + 1); b->cb = std::min(b->h, y2 + 1);
}

// clips a blit against the source bitmap and the destination clip rectangle
static bool clip_blit(BITMAP* s, BITMAP* d, int& sx, int& sy, int& dx, int& dy, int& w, int& h) {
    if (!s || !d) return false;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    w = std::min(w, s->w - sx); h = std::min(h, s->h - sy);
    if (dx < d->cl) { int t = d->cl - dx; w -= t; sx += t; dx = d->cl; }
    if (dy < d->ct) { int t = d->ct - dy; h -= t; sy += t; dy = d->ct; }
    w = std::min(w, d->cr - dx); h = std::min(h, d->cb - dy);
    return w > 0 && h > 0;
}
void blit(BITMAP* s, BITMAP* d, int sx, int sy, int dx, int dy, int w, int h) {
    if (clip_blit(s, d, sx, sy, dx, dy, w, h)) blit_core(s, d, sx, sy, dx, dy, w, h, false);
    if (d == screen) legacy::screen_touched();
}
void masked_blit(BITMAP* s, BITMAP* d, int sx, int sy, int dx, int dy, int w, int h) {
    if (clip_blit(s, d, sx, sy, dx, dy, w, h)) blit_core(s, d, sx, sy, dx, dy, w, h, true);
    if (d == screen) legacy::screen_touched();
}
void stretch_blit(BITMAP* s, BITMAP* d, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {
    if (!s || !d || dw <= 0 || dh <= 0) return;
    for (int j = 0; j < dh; j++) for (int i = 0; i < dw; i++) {
        int x = sx + i * sw / dw, y = sy + j * sh / dh;
        if (x < 0 || y < 0 || x >= s->w || y >= s->h) continue;
        v_putpixel(d, dx + i, dy + j, convert(raw_get(s, x, y), depth_of(s), depth_of(d)));
    }
}
void stretch_sprite(BITMAP* d, BITMAP* s, int x, int y, int w, int h) {
    if (!s || !d || w <= 0 || h <= 0) return;
    int sd = depth_of(s), mask = mask_of(sd);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        int c = raw_get(s, i * s->w / w, j * s->h / h);
        if (sd == 8 ? c == 0 : (c & 0xffffff) == (mask & 0xffffff)) continue;
        v_putpixel(d, x + i, y + j, convert(c, sd, depth_of(d)));
    }
}
void clear_to_color(BITMAP* b, int c) { if (b) v_clear_to_color(b, c); }
void clear_bitmap(BITMAP* b) { if (b) v_clear_to_color(b, 0); if (b == screen) legacy::screen_touched(); }
void clear(BITMAP* b) { clear_bitmap(b); }
void rect(BITMAP* b, int x1, int y1, int x2, int y2, int c) {
    v_hline(b, x1, y1, x2, c); v_hline(b, x1, y2, x2, c); v_vline(b, x1, y1, y2, c); v_vline(b, x2, y1, y2, c);
}
void triangle(BITMAP* b, int x1, int y1, int x2, int y2, int x3, int y3, int c) { v_triangle(b, x1, y1, x2, y2, x3, y3, c); }
void do_line(BITMAP* b, int x1, int y1, int x2, int y2, int d, void (*proc)(BITMAP*, int, int, int)) {
    int dx = abs(x2 - x1), dy = -abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, e = dx + dy;
    for (;;) {
        proc(b, x1, y1, d);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x1 += sx; }
        if (e2 <= dx) { e += dx; y1 += sy; }
    }
}

// ------------------------------------------------------------------------------ colours
void set_color_depth(int d) { _color_depth = d; }
int get_color_depth() { return _color_depth; }
int makecol_depth(int d, int r, int g, int b) {
    r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    switch (d) {
    case 8: return pal_nearest(r, g, b);
    case 15: return (r >> 3) << _rgb_r_shift_15 | (g >> 3) << _rgb_g_shift_15 | (b >> 3) << _rgb_b_shift_15;
    case 16: return (r >> 3) << _rgb_r_shift_16 | (g >> 2) << _rgb_g_shift_16 | (b >> 3) << _rgb_b_shift_16;
    case 24: return r << _rgb_r_shift_24 | g << _rgb_g_shift_24 | b << _rgb_b_shift_24;
    default: return r << _rgb_r_shift_32 | g << _rgb_g_shift_32 | b << _rgb_b_shift_32;
    }
}
int makeacol_depth(int d, int r, int g, int b, int a) {
    int c = makecol_depth(d, r, g, b);
    return d == 32 ? (int)((unsigned)c | (unsigned)(std::clamp(a, 0, 255)) << _rgb_a_shift_32) : c;
}
int makecol(int r, int g, int b) { return makecol_depth(_color_depth, r, g, b); }
int makeacol(int r, int g, int b, int a) { return makeacol_depth(_color_depth, r, g, b, a); }
int makecol8(int r, int g, int b) { return pal_nearest(r, g, b); }
int makecol15(int r, int g, int b) { return makecol_depth(15, r, g, b); }
int makecol16(int r, int g, int b) { return makecol_depth(16, r, g, b); }
int makecol24(int r, int g, int b) { return makecol_depth(24, r, g, b); }
int makecol32(int r, int g, int b) { return makecol_depth(32, r, g, b); }
int getr_depth(int d, int c) {
    switch (d) {
    case 8: return _rgb_scale_6[_current_palette[c & 255].r & 63];
    case 15: return _rgb_scale_5[(c >> _rgb_r_shift_15) & 31];
    case 16: return _rgb_scale_5[(c >> _rgb_r_shift_16) & 31];
    case 24: return (c >> _rgb_r_shift_24) & 255;
    default: return (c >> _rgb_r_shift_32) & 255;
    }
}
int getg_depth(int d, int c) {
    switch (d) {
    case 8: return _rgb_scale_6[_current_palette[c & 255].g & 63];
    case 15: return _rgb_scale_5[(c >> _rgb_g_shift_15) & 31];
    case 16: return _rgb_scale_6[(c >> _rgb_g_shift_16) & 63];
    case 24: return (c >> _rgb_g_shift_24) & 255;
    default: return (c >> _rgb_g_shift_32) & 255;
    }
}
int getb_depth(int d, int c) {
    switch (d) {
    case 8: return _rgb_scale_6[_current_palette[c & 255].b & 63];
    case 15: return _rgb_scale_5[(c >> _rgb_b_shift_15) & 31];
    case 16: return _rgb_scale_5[(c >> _rgb_b_shift_16) & 31];
    case 24: return (c >> _rgb_b_shift_24) & 255;
    default: return (c >> _rgb_b_shift_32) & 255;
    }
}
int geta_depth(int d, int c) { return d == 32 ? ((unsigned)c >> _rgb_a_shift_32) & 255 : 255; }
int getr(int c) { return getr_depth(_color_depth, c); }
int getg(int c) { return getg_depth(_color_depth, c); }
int getb(int c) { return getb_depth(_color_depth, c); }
int geta(int c) { return geta_depth(_color_depth, c); }
int getr32(int c) { return getr_depth(32, c); }
int getg32(int c) { return getg_depth(32, c); }
int getb32(int c) { return getb_depth(32, c); }

void set_trans_blender(int r, int g, int b, int a) { g_blend = kTrans; g_blendR = r; g_blendG = g; g_blendB = b; g_blendA = a; }
void set_alpha_blender() { g_blend = kAlpha; g_blendA = 255; }
void set_burn_blender(int r, int g, int b, int a) { g_blend = kBurn; g_blendR = r; g_blendG = g; g_blendB = b; g_blendA = a; }
void drawing_mode(int, BITMAP*, int, int) {}

void set_palette(const RGB* p) { if (p) memcpy(_current_palette, p, sizeof _current_palette); }
void set_palette_range(const RGB* p, int from, int to, int) {
    if (p) for (int i = std::max(0, from); i <= std::min(255, to); i++) _current_palette[i] = p[i];
}
void get_palette(RGB* p) { if (p) memcpy(p, _current_palette, sizeof _current_palette); }
void set_color(int i, const RGB* c) { if (c && i >= 0 && i < 256) _current_palette[i] = *c; }
void fade_in(const RGB* p, int) { set_palette(p); }
void vsync() {}

// 24-bit BMP of any bitmap (games save screenshots/thumbnails)
int save_bitmap(const char* name, BITMAP* b, const RGB*) {
    if (!b) return -1;
    FILE* f = fopen(name, "wb");
    if (!f) return -1;
    int pitch = (b->w * 3 + 3) & ~3;
    uint32_t size = 54 + pitch * b->h;
    unsigned char hdr[54] = {'B', 'M'};
    auto le32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) hdr[o + i] = v >> (8 * i); };
    le32(2, size); le32(10, 54); le32(14, 40); le32(18, b->w); le32(22, b->h);
    hdr[26] = 1; hdr[28] = 24; le32(34, pitch * b->h);
    fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> row(pitch);
    for (int y = b->h - 1; y >= 0; y--) {
        for (int x = 0; x < b->w; x++) {
            int c = raw_get(b, x, y), d = depth_of(b);
            row[x * 3] = getb_depth(d, c); row[x * 3 + 1] = getg_depth(d, c); row[x * 3 + 2] = getr_depth(d, c);
        }
        fwrite(row.data(), 1, pitch, f);
    }
    fclose(f);
    return 0;
}

// ------------------------------------------------------------------------------ input
int install_mouse() { return 2; }
void remove_mouse() {}
int install_keyboard() { return 0; }
void remove_keyboard() {}
int install_timer() { return 0; }
void remove_timer() {}
void position_mouse(int x, int y) { legacy::warp_mouse(x, y); mouse_x = x; mouse_y = y; mouse_pos = x << 16 | y; }
void set_mouse_sprite(BITMAP*) {}
void set_mouse_sprite_focus(int, int) {}
void show_mouse(BITMAP*) {}
void scare_mouse() {}
void unscare_mouse() {}
int poll_mouse() { legacy::pump(); return 0; }
int poll_keyboard() { legacy::pump(); return 0; }
int keypressed() { legacy::pump(); return 0; }
int readkey() { return 0; }
void clear_keybuf() {}
int install_sound(int, int, const char*) { return 0; }

// install_int / install_int_ex: SDL timers instead of Allegro's timer driver
struct AlTimer { void (*fn)(); SDL_TimerID id; };
static std::vector<AlTimer> g_timers;
static Uint32 timer_cb(Uint32 interval, void* p) { reinterpret_cast<void (*)()>(p)(); return interval; }
void remove_int(void (*fn)()) {
    for (size_t i = 0; i < g_timers.size(); i++)
        if (g_timers[i].fn == fn) { SDL_RemoveTimer(g_timers[i].id); g_timers.erase(g_timers.begin() + i); return; }
}
int install_int_ex(void (*fn)(), long ticks) {                   // ticks of the 1.193 MHz PIT
    SDL_InitSubSystem(SDL_INIT_TIMER);
    remove_int(fn);
    int ms = std::max(1, (int)(ticks / 1193));
    g_timers.push_back({fn, SDL_AddTimer(ms, timer_cb, reinterpret_cast<void*>(fn))});
    return 0;
}
int install_int(void (*fn)(), long ms) { return install_int_ex(fn, ms * 1193); }
void rest(long ms) { SDL_Delay(ms); legacy::pump(); }

// ------------------------------------------------------------------------------ file search
struct al_ffblk { int attrib; long time; long size; char name[512]; void* ff_data; };
struct FindState { DIR* dir; std::string folder, pattern; int attrib; };
static const int FA_DIREC = 0x10;
int al_findnext(al_ffblk* f) {
    auto* st = static_cast<FindState*>(f->ff_data);
    if (!st) { errno = ENOENT; return -1; }
    while (dirent* e = readdir(st->dir)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (fnmatch(st->pattern.c_str(), e->d_name, FNM_CASEFOLD) != 0) continue;
        std::string full = st->folder + e->d_name;
        struct stat sb;
        if (stat(full.c_str(), &sb) != 0) continue;
        bool dir = S_ISDIR(sb.st_mode);
        if (dir && !(st->attrib & FA_DIREC)) continue;
        f->attrib = dir ? FA_DIREC : 0; f->time = sb.st_mtime; f->size = sb.st_size;
        snprintf(f->name, sizeof f->name, "%s", e->d_name);
        return 0;
    }
    errno = ENOENT;
    return -1;
}
void al_findclose(al_ffblk* f) {
    if (auto* st = static_cast<FindState*>(f->ff_data)) { closedir(st->dir); delete st; f->ff_data = nullptr; }
}
int al_findfirst(const char* pattern, al_ffblk* f, int attrib) {
    std::string p = pattern ? pattern : "*";
    size_t slash = p.rfind('/');
    std::string folder = slash == std::string::npos ? "./" : p.substr(0, slash + 1);
    std::string pat = slash == std::string::npos ? p : p.substr(slash + 1);
    if (pat == "*.*") pat = "*";
    DIR* d = opendir(folder.c_str());
    f->ff_data = nullptr;
    if (!d) { errno = ENOENT; return -1; }
    f->ff_data = new FindState{d, folder, pat, attrib};
    if (al_findnext(f) != 0) { al_findclose(f); errno = ENOENT; return -1; }
    return 0;
}
int exists(const char* name) { struct stat sb; return stat(name, &sb) == 0; }
int file_exists(const char* name, int, int*) { return exists(name); }
long file_time(const char* name) { struct stat sb; return stat(name, &sb) == 0 ? sb.st_mtime : 0; }
long file_size(const char* name) { struct stat sb; return stat(name, &sb) == 0 ? sb.st_size : 0; }
char* fix_filename_slashes(char* p) { for (char* c = p; *c; c++) if (*c == '\\') *c = '/'; return p; }
}

__attribute__((constructor)) static void allegro_ctor() {
    for (int i = 0; i < 32; i++) _rgb_scale_5[i] = i * 255 / 31;
    for (int i = 0; i < 64; i++) _rgb_scale_6[i] = i * 255 / 63;
    __linear_vtable8 = g_vt8; __linear_vtable15 = g_vt15; __linear_vtable16 = g_vt16;
    __linear_vtable24 = g_vt24; __linear_vtable32 = g_vt32;
    g_driver.id = 'M' << 24 | 'E' << 16 | 'G' << 8 | 'A';
    g_driver.name = g_driver.desc = g_driver.ascii_name = "Megatouch port";
    g_driver.linear = 1; g_driver.windowed = 1;
    gfx_driver = &g_driver;
}

// ------------------------------------------------------------------------------ AllegroGL glue
// The loader opened the GL window before the game started (merit3d.md §5); games only flip.
extern "C" {
void allegro_gl_flip(void) {
    if (SDL_Window* w = legacy::window()) SDL_GL_SwapWindow(w);
    legacy::gl_frame_done();
    legacy::pump();
}
// a function-pointer variable in libagl (NULL: libmerit3d then uses glXGetProcAddress itself)
void* (*__aglXGetProcAddressARB)(const unsigned char*) = nullptr;
// swaps rows top<->bottom in place; one row = 2*row16 bytes (merit3d.md §5.5)
void FlipBitmapVert(void* pixels, int row16, int height) {
    if (!pixels || row16 <= 0 || height <= 1) return;
    size_t row = (size_t)row16 * 2;
    std::vector<unsigned char> tmp(row);
    auto* p = static_cast<unsigned char*>(pixels);
    for (int y = 0; y < height / 2; y++) {
        memcpy(tmp.data(), p + y * row, row);
        memmove(p + y * row, p + (size_t)(height - 1 - y) * row, row);
        memcpy(p + (size_t)(height - 1 - y) * row, tmp.data(), row);
    }
}
// cabinet liballeg extension: buttons pressed since the game last cleared it
volatile int mouse_button_presses_cached;
// audio streams (libmvideo's video sound): none, so videos play silently
void* play_audio_stream(int, int, int, int, int, int) { return nullptr; }
void* get_audio_stream_buffer(void*) { return nullptr; }
void free_audio_stream_buffer(void*) {}
void stop_audio_stream(void*) {}
}
