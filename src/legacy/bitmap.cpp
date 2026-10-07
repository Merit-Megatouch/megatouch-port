// The loader's C++ Bitmap class, bitmap fonts (BmpFont/FontBase), TrueType text boxes and String.
//
// A Bitmap keeps its pixels in a 16-bit Allegro BITMAP at +0x2c at all times, because games use
// it directly (_getpixel16, blit, masked_blit, rectfill...). Compress/DeCompress are therefore
// no-ops. Layout and semantics: docs/reference/sprite-engine.md (§3 Bitmap, §5.4).
#include "bitmap.h"
#include "legacy_ttf.h"
#include "../common/env.h"
#include <SDL2/SDL_image.h>
#include <sys/stat.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

using legacy::find_asset;

// The loader's text calls take translation keys ("IntroText1", "TransTag_..."): megatouch-host's
// Translator maps them through the game's .utf8 table (unknown keys come back unchanged).
class Translator { public: static char const* Translate(char const*); };
static const char* tr(const char* s) { return s ? Translator::Translate(s) : s; }

static inline uint16_t* row16(BITMAP* b, int y) { return reinterpret_cast<uint16_t*>(b->line[y]); }
static inline uint32_t* row32(BITMAP* b, int y) { return reinterpret_cast<uint32_t*>(b->line[y]); }
static inline int depth_of(BITMAP* b) { return b->vtable->color_depth; }

// ------------------------------------------------------------------------------ Bitmap core
Bitmap::Bitmap(int w_, int h_, unsigned char, unsigned char bpp) {
    legacy::video_init();
    memset(reinterpret_cast<unsigned char*>(this) + 4, 0, sizeof(Bitmap) - 4);   // keep the vptr
    depth = bpp ? bpp : 16;
    resize(w_, h_);
}
Bitmap::~Bitmap() {
    delete anim;
    anim = nullptr;
    if (al) destroy_bitmap(al);
    al = nullptr;
}
void Bitmap::resize(int w_, int h_) {
    if (al) destroy_bitmap(al);
    w = std::max(0, w_); h = std::max(0, h_);
    al = create_bitmap_ex(16, w, h);
    for (int y = 0; y < h; y++) std::fill(row16(al, y), row16(al, y) + w, (uint16_t)kKey16);
}
void Bitmap::SetDim(int w_, int h_) { resize(w_, h_); }
void Bitmap::from_frame(const MeritFrame& f) {
    resize(f.w, f.h);
    for (int y = 0; y < f.h; y++)
        for (int x = 0; x < f.w; x++) row16(al, y)[x] = (uint16_t)px_to16(f.argb[(size_t)y * f.w + x]);
}

// Draws a region onto a 32- or 16-bit target, honouring +0x22 opaque, the transparency key, the
// colour effect (+0x54 with +0x0a..0x0e) and black-as-transparent for old PCX art.
void Bitmap::draw_to(BITMAP* dst, int sx, int sy, int cw, int ch, int dx, int dy, bool black_trans) {
    if (!dst || !ready()) return;
    if (sx < 0) { cw += sx; dx -= sx; sx = 0; }
    if (sy < 0) { ch += sy; dy -= sy; sy = 0; }
    cw = std::min(cw, al->w - sx); ch = std::min(ch, al->h - sy);
    if (dx < dst->cl) { int t = dst->cl - dx; cw -= t; sx += t; dx = dst->cl; }
    if (dy < dst->ct) { int t = dst->ct - dy; ch -= t; sy += t; dy = dst->ct; }
    cw = std::min(cw, dst->cr - dx); ch = std::min(ch, dst->cb - dy);
    if (cw <= 0 || ch <= 0) return;
    bool opaque = flags & 0x02;
    bool tint = (effect == 2 || effect == 4) && (fx_r || fx_g || fx_b);
    int tr = 255 + fx_r, tg = 255 + fx_g, tb = 255 + fx_b;
    int sd = depth_of(al), dd = depth_of(dst);
    for (int j = 0; j < ch; j++) {
        for (int i = 0; i < cw; i++) {
            int c = sd == 16 ? row16(al, sy + j)[sx + i] : (int)row32(al, sy + j)[sx + i];
            int r, g, b;
            if (sd == 16) {
                if (!opaque && (c == kKey16 || (black_trans && c == 0))) continue;
                r = getr_depth(16, c); g = getg_depth(16, c); b = getb_depth(16, c);
            } else {
                if (!opaque && ((c & 0xffffff) == (int)kKey || (black_trans && !(c & 0xffffff)))) continue;
                r = c >> 16 & 255; g = c >> 8 & 255; b = c & 255;
            }
            if (tint) { r = r * tr / 255; g = g * tg / 255; b = b * tb / 255; }
            if (dd == 32) row32(dst, dy + j)[dx + i] = (uint32_t)(r << 16 | g << 8 | b);
            else if (dd == 16) row16(dst, dy + j)[dx + i] = (uint16_t)makecol_depth(16, r, g, b);
            else dst->vtable->putpixel(dst, dx + i, dy + j, makecol_depth(dd, r, g, b));
        }
    }
}

void Bitmap::Display(int x, int y, int f) {
    if (!ready()) return;
    BITMAP* dst = legacy::target_bitmap();
    if (scale_on && scale_x > 0 && scale_y > 0 && (scale_x != 100 || scale_y != 100)) {
        // scaled: through a temporary (scale values are percentages)
        int nw = w * scale_x / 100, nh = h * scale_y / 100;
        Bitmap tmp(0, 0, 0, 16);
        ScaleInto(&tmp, nw, nh, 0);
        tmp.flags = flags; tmp.effect = effect; tmp.fx_r = fx_r; tmp.fx_g = fx_g; tmp.fx_b = fx_b;
        tmp.draw_to(dst, 0, 0, nw, nh, x, y, f != 0);
    } else {
        draw_to(dst, 0, 0, w, h, x, y, f != 0);
    }
    legacy::touched_target();
}
void Bitmap::DisplayRegion(int sx, int sy, int cw, int ch, int dx, int dy, int f) {
    draw_to(legacy::target_bitmap(), sx, sy, cw, ch, dx, dy, f != 0 && false);
    legacy::touched_target();
}
void Bitmap::DisplayZ(int x, int y, int, unsigned char) { Display(x, y, 0); }
namespace {
struct ZItem { unsigned long id; Bitmap* b; int x, y, z; };
std::vector<ZItem> g_zlist;
unsigned long g_zid = 1;
}
int Bitmap::DrawtoVBZ(int x, int y, int z) {
    g_zlist.push_back({g_zid, this, x, y, z});
    legacy::zlist_changed();
    return (int)g_zid++;
}
namespace legacy {
void zlist_remove(unsigned long id) {
    for (size_t i = 0; i < g_zlist.size(); i++) if (g_zlist[i].id == id) { g_zlist.erase(g_zlist.begin() + i); zlist_changed(); return; }
}
bool zlist_compose(BITMAP* dst) {
    if (g_zlist.empty()) return false;
    std::vector<ZItem> v = g_zlist;
    std::stable_sort(v.begin(), v.end(), [](const ZItem& a, const ZItem& b) { return a.z < b.z; });
    for (auto& it : v) it.b->draw_to(dst, 0, 0, it.b->w, it.b->h, it.x, it.y, false);
    return true;
}
}

static void grab(Bitmap* b, BITMAP* src, int x, int y) {
    if (!b->al || !src) return;
    blit(src, b->al, x, y, 0, 0, b->w, b->h);
}
void Bitmap::CopyFromCurrent(int x, int y) { grab(this, legacy::target_bitmap(), x, y); }
void Bitmap::CopyFromScreen(int x, int y, bool) { grab(this, screen, x, y); }
void Bitmap::CopyFromVB(int x, int y, int vb) { grab(this, vb < 0 ? legacy::target_bitmap() : legacy::vb_bitmap(vb), x, y); }
void Bitmap::CopyToBitmap(Bitmap* dst, int sx, int sy, int cw, int ch, int dx, int dy) {
    if (dst && dst->ready() && ready()) masked_blit(al, dst->al, sx, sy, dx, dy, cw, ch);
}

void Bitmap::Clear() { if (al) for (int y = 0; y < h; y++) std::fill(row16(al, y), row16(al, y) + w, (uint16_t)kKey16); }
// pixel-wise helpers on the 16-bit image (32 after DeCompress32)
template <class F> static void each_px(Bitmap* b, F f) {
    if (!b->al) return;
    int d = depth_of(b->al);
    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->w; x++) {
            int c = d == 16 ? row16(b->al, y)[x] : (int)row32(b->al, y)[x];
            bool key = d == 16 ? c == kKey16 : (c & 0xffffff) == (int)kKey;
            if (key) continue;
            int r = getr_depth(d, c), g = getg_depth(d, c), bl = getb_depth(d, c);
            f(r, g, bl);
            int n = makecol_depth(d, std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(bl, 0, 255));
            if (d == 16) row16(b->al, y)[x] = (uint16_t)(n == kKey16 ? n ^ 0x20 : n);
            else row32(b->al, y)[x] = (uint32_t)n;
        }
}
void Bitmap::GreyBmp() { each_px(this, [](int& r, int& g, int& b) { r = g = b = (r * 30 + g * 59 + b * 11) / 100; }); }
void Bitmap::Colorize(int dr, int dg, int db) {
    each_px(this, [&](int& r, int& g, int& b) { r = r * (255 + dr) / 255; g = g * (255 + dg) / 255; b = b * (255 + db) / 255; });
}
// true: black becomes transparent; false: transparent becomes black
void Bitmap::ReplaceTrans(bool to_trans) {
    if (!al || depth_of(al) != 16) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t& c = row16(al, y)[x];
            if (to_trans && c == 0) c = kKey16;
            else if (!to_trans && c == kKey16) c = 0;
        }
}
void Bitmap::ScaleInto(Bitmap* dst, int nw, int nh, unsigned char) {
    if (!dst || !al || nw <= 0 || nh <= 0) return;
    BITMAP* src = al;
    if (dst == this) { src = create_bitmap_ex(depth_of(al), w, h); blit(al, src, 0, 0, 0, 0, w, h); }
    int sw = src->w, sh = src->h;
    dst->resize(nw, nh);
    stretch_blit(src, dst->al, 0, 0, sw, sh, 0, 0, nw, nh);
    if (src != al) destroy_bitmap(src);
}
void Bitmap::Scale(int nw, int nh, unsigned char f) { ScaleInto(this, nw, nh, f); }
// copy of the non-transparent bounding box into dst
void Bitmap::Crop(Bitmap* dst, unsigned char) {
    if (!dst || !al) return;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (row16(al, y)[x] != kKey16) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 < 0) { dst->resize(0, 0); return; }
    dst->resize(x1 - x0 + 1, y1 - y0 + 1);
    blit(al, dst->al, x0, y0, 0, 0, dst->w, dst->h);
}
void Bitmap::Compress(bool, int, unsigned short*) {}
// +0x2c is NULL while the bitmap holds only compressed data (setCData): decode the RLE
// (+0x48 bytes, +0x4c x +0x50, as the game filled them) into a 16-bit bitmap.
void Bitmap::DeCompress() {
    if (al || !cdata || w <= 0 || h <= 0 || w > 4096 || h > 4096) return;
    MeritFrame f; f.w = w; f.h = h;
    size_t bytes = csize > 0 ? (size_t)csize : (size_t)w * h * 4;
    merit_rle_decode(reinterpret_cast<const uint8_t*>(cdata), bytes, w, h, f.argb);
    for (auto& p : f.argb) p = to_px(p);
    from_frame(f);
}
// 32-bit pixels from now on (games then use 32-bit getpixel/putpixel on +0x2c)
void Bitmap::DeCompress32() {
    if (!al) DeCompress();
    if (!al || depth_of(al) == 32) return;
    BITMAP* n = create_bitmap_ex(32, w, h);
    blit(al, n, 0, 0, 0, 0, w, h);                 // mask colour stays the mask colour
    destroy_bitmap(al);
    al = n;
    depth = 32;
}
void Bitmap::FreeMemory() {}
unsigned short* Bitmap::CData(int) { return cdata; }
void Bitmap::setCData(unsigned short* p) {
    cdata = p;
    if (p && al) { destroy_bitmap(al); al = nullptr; }   // decoded on DeCompress
}
void Bitmap::freeCData() { free(cdata); cdata = nullptr; }
Bitmap* Bitmap::NextAnim() { return next; }
Bitmap* Bitmap::PrevAnim() { return prev; }
void Bitmap::NextAnim(Bitmap* b) { next = b; }
void Bitmap::PrevAnim(Bitmap* b) { prev = b; }

// ------------------------------------------------------------------------------ loaders
// Games name files with or without their extension; try the cabinet's image extensions too.
static bool read_file(const char* name, std::vector<uint8_t>& d, std::string* found = nullptr) {
    std::string path;
    for (const char* ext : {"", ".img", ".pcx", ".tga", ".jpg", ".png", ".dlt", ".spr"})
        if (find_asset(name, false, ext, path)) { if (found) *found = path; return merit_read_gz(path.c_str(), d); }
    return false;
}

// PCX: run-length rows. 8-bit with a 256-colour palette at the end, or 24-bit as three planes.
bool Bitmap::LoadPCX(char* name, int, int, unsigned char) {
    std::vector<uint8_t> d;
    if (!name || !read_file(name, d)) { LOG("missing PCX %s", name ? name : "?"); return false; }
    if (d.size() < 128 || d[0] != 0x0a) { LOG("not a PCX: %s", name); return false; }
    int pw = (d[8] | d[9] << 8) - (d[4] | d[5] << 8) + 1, ph = (d[10] | d[11] << 8) - (d[6] | d[7] << 8) + 1;
    int planes = d[65], bpl = d[66] | d[67] << 8;
    bool pal8 = planes == 1 && d.size() >= 128 + 769 && d[d.size() - 769] == 12;
    const uint8_t* pal = pal8 ? &d[d.size() - 768] : nullptr;
    if (pw <= 0 || ph <= 0 || pw > 4096 || ph > 4096 || (planes != 1 && planes != 3)) { LOG("unsupported PCX %s", name); return false; }
    resize(pw, ph);
    size_t o = 128, end = pal8 ? d.size() - 769 : d.size();
    std::vector<uint8_t> line((size_t)bpl * planes);
    for (int y = 0; y < ph; y++) {
        size_t x = 0;
        while (x < line.size() && o < end) {
            uint8_t c = d[o++];
            int n = 1;
            if ((c & 0xc0) == 0xc0 && o < end) { n = c & 0x3f; c = d[o++]; }
            while (n-- && x < line.size()) line[x++] = c;
        }
        for (int i = 0; i < pw; i++) {
            uint32_t v;
            if (planes == 3) v = (uint32_t)line[i] << 16 | line[bpl + i] << 8 | line[2 * bpl + i];
            else if (pal) { const uint8_t* p = pal + line[i] * 3; v = (uint32_t)p[0] << 16 | p[1] << 8 | p[2]; }
            else v = line[i] * 0x010101u;
            row16(al, y)[i] = (uint16_t)px_to16(v);
        }
    }
    return true;
}
// .img: u32 width, u32 height, raw RGB565 (magenta 0xF81F transparent)
bool Bitmap::LoadData(char* name, unsigned char, int) {
    std::vector<uint8_t> d;
    if (!name || !read_file(name, d) || d.size() < 8) { LOG("missing image %s", name ? name : "?"); return false; }
    uint32_t iw, ih; memcpy(&iw, &d[0], 4); memcpy(&ih, &d[4], 4);
    if (iw > 4096 || ih > 4096 || 8 + (size_t)iw * ih * 2 > d.size()) { LOG("bad image %s", name); return false; }
    resize(iw, ih);
    for (uint32_t y = 0; y < ih; y++) memcpy(row16(al, y), &d[8 + (size_t)y * iw * 2], iw * 2);
    return true;
}
bool Bitmap::LoadCompressedData(char* name, _IO_FILE* fp) {
    // From an open archive (chess pieces.all): u32 1 (compressed) | 0 (raw), u32 size,
    // u32 w, u32 h, then size bytes of RLE (or w*h RGB565).
    if (FILE* f = reinterpret_cast<FILE*>(fp)) {
        uint32_t hd[4];
        if (fread(hd, 4, 4, f) != 4 || hd[2] > 4096 || hd[3] > 4096) { LOG("bad archive record for %s", name ? name : "?"); return false; }
        std::vector<uint8_t> d(hd[0] ? hd[1] : (size_t)hd[2] * hd[3] * 2);
        if (!d.empty() && fread(d.data(), 1, d.size(), f) != d.size()) return false;
        MeritFrame fr; fr.w = (int)hd[2]; fr.h = (int)hd[3];
        if (hd[0]) merit_rle_decode(d.data(), d.size(), fr.w, fr.h, fr.argb);
        else { fr.argb.resize((size_t)fr.w * fr.h); for (size_t i = 0; i < fr.argb.size(); i++) fr.argb[i] = merit_rgb565(d[2 * i] | d[2 * i + 1] << 8, 255); }
        for (auto& p : fr.argb) p = to_px(p);
        from_frame(fr);
        return true;
    }
    // a .dlt/.spr picture (first frame) when it is one, else the raw .img layout
    std::string path;
    if (name && find_asset(name, false, "", path)) {
        std::vector<uint8_t> d;
        size_t off, count;
        if (merit_read_gz(path.c_str(), d) && merit_container(d, off, count)) {
            std::vector<MeritFrame> f;
            merit_read_frames(d, off, f, 1);
            if (!f.empty()) { frames_to_px(f); from_frame(f[0]); return true; }
        }
    }
    return LoadData(name, 0, -1);
}
bool Bitmap::LoadTGA_32(char* name) {
    std::vector<uint8_t> d;
    if (!name || !read_file(name, d) || d.size() < 18) return false;
    int idlen = d[0], type = d[2], tw = d[12] | d[13] << 8, th = d[14] | d[15] << 8, bpp = d[16], desc = d[17];
    if (type != 2 || (bpp != 32 && bpp != 24)) { LOG("unsupported TGA %s", name); return false; }
    int bytes = bpp / 8;
    MeritFrame f; f.w = tw; f.h = th; f.argb.assign((size_t)tw * th, kKey);
    size_t o = 18 + idlen;
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw && o + bytes <= d.size(); x++, o += bytes) {
            int yy = (desc & 0x20) ? y : th - 1 - y;
            uint32_t a = bytes == 4 ? d[o + 3] : 255;
            f.argb[(size_t)yy * tw + x] = a < 128 ? kKey : ((uint32_t)d[o + 2] << 16 | d[o + 1] << 8 | d[o]);
        }
    from_frame(f);
    return true;
}
bool Bitmap::LoadTGA(char* name, int) { return LoadTGA_32(name); }
// JPEG/PNG through SDL_image
static bool load_image(Bitmap* b, char* name) {
    std::vector<uint8_t> d;
    if (!name || !read_file(name, d)) { LOG("missing image %s", name ? name : "?"); return false; }
    SDL_Surface* s = IMG_Load_RW(SDL_RWFromConstMem(d.data(), (int)d.size()), 1);
    if (!s) { LOG("cannot decode %s: %s", name, IMG_GetError()); return false; }
    SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s);
    if (!c) return false;
    MeritFrame f; f.w = c->w; f.h = c->h; f.argb.resize((size_t)c->w * c->h);
    for (int y = 0; y < c->h; y++) {
        const uint32_t* p = reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(c->pixels) + y * c->pitch);
        for (int x = 0; x < c->w; x++) f.argb[(size_t)y * c->w + x] = to_px(p[x]);
    }
    SDL_FreeSurface(c);
    b->from_frame(f);
    return true;
}
bool Bitmap::LoadJPEG(char* name, int, int, unsigned char) { return load_image(this, name); }
bool Bitmap::LoadPNG(char* name, int) { return load_image(this, name); }
bool Bitmap::LoadRawPNG(char* name) { return load_image(this, name); }

// ------------------------------------------------------------------------------ animations
static void show_anim_frame(Bitmap* b) {
    Anim* a = b->anim;
    if (!a) return;
    if (a->canvas.empty()) legacy::anim_seek(a, a->cur);
    MeritFrame f; f.w = a->w; f.h = a->h; f.argb = a->canvas;
    b->from_frame(f);
}
bool Bitmap::SmackAnimationLoad(char* name, unsigned char lang) {
    delete anim;
    anim = name ? legacy::anim_load(name, lang) : nullptr;
    if (!anim) return false;
    frames = (unsigned short)anim->frames.size();
    legacy::anim_seek(anim, 0);
    show_anim_frame(this);
    return true;
}
void Bitmap::SmackAnimationUnload() { delete anim; anim = nullptr; }
void Bitmap::SmackAnimationRewind() { if (anim) { legacy::anim_seek(anim, 0); show_anim_frame(this); } }
void Bitmap::SmackAnimationJumpTo(int f) {
    if (anim && !anim->frames.empty()) { legacy::anim_seek(anim, f < 0 ? 0 : f % (int)anim->frames.size()); show_anim_frame(this); }
}
void Bitmap::SmackAnimationAdvance(unsigned char) {
    if (anim && !anim->frames.empty()) { legacy::anim_seek(anim, (anim->cur + 1) % (int)anim->frames.size()); show_anim_frame(this); }
}
// Shows the current frame at (x, y), then steps to the next one.
void Bitmap::SmackAnimationDisplay(int x, int y, unsigned char) {
    if (!anim) return;
    show_anim_frame(this);
    draw_to(legacy::target_bitmap(), 0, 0, w, h, x, y, false);
    legacy::touched_target();
    legacy::anim_seek(anim, (anim->cur + 1) % (int)anim->frames.size());
}
void Bitmap::SmackAnimationChangeFRate(int) {}
void Bitmap::DisplaySmack(unsigned short x, unsigned short y, unsigned char f) {
    if (anim) SmackAnimationDisplay(x, y, f);
    else if (rad) { ConvertSmack(0); Display(x, y, 0); }
}
bool Bitmap::LoadSmack(char* name, unsigned char lang, unsigned char) { return SmackAnimationLoad(name, lang); }
// the RAD image the game stored at +0x44 becomes this bitmap
void Bitmap::ConvertSmack(unsigned char) {
    if (!rad || rad->tag != kTag) return;
    MeritFrame f; f.w = rad->w; f.h = rad->h; f.argb.assign(rad->px, rad->px + (size_t)rad->w * rad->h);
    from_frame(f);
}
bool Bitmap::DeltaOpen(char* name) {
    if (!SmackAnimationLoad(name, 0)) return false;
    flags |= 0x80;
    return true;
}
// next frame of the streamed animation into the bitmap; false after the last one
bool Bitmap::DeltaAdvance(bool, int* frame) {
    if (!anim || anim->cur + 1 >= (int)anim->frames.size()) return false;
    legacy::anim_seek(anim, anim->cur + 1);
    show_anim_frame(this);
    if (frame) *frame = anim->cur;
    return true;
}

// ------------------------------------------------------------------------------ drawing
static int rgb16(int r, int g, int b) {
    int c = makecol_depth(16, std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
    return c == kKey16 ? c ^ 0x20 : c;
}
static int col(Bitmap* b, int r, int g, int bl) { return depth_of(b->al) == 16 ? rgb16(r, g, bl) : makecol_depth(depth_of(b->al), r, g, bl); }
void Bitmap::DrawPoint(int x, int y, int r, int g, int b) { if (al) al->vtable->putpixel(al, x, y, col(this, r, g, b)); }
void Bitmap::DrawLine(int x1, int y1, int x2, int y2, int r, int g, int b) { if (al) al->vtable->line(al, x1, y1, x2, y2, col(this, r, g, b)); }
void Bitmap::DrawRectangle(int x, int y, int x2, int y2, int r, int g, int b) {
    if (!al) return;
    int c = col(this, r, g, b);
    al->vtable->hline(al, x, y, x2, c); al->vtable->hline(al, x, y2, x2, c);
    al->vtable->vline(al, x, y, y2, c); al->vtable->vline(al, x2, y, y2, c);
}
static void ellipse(Bitmap* bm, int cx, int cy, int rx, int ry, int c, bool fill) {
    if (!bm->al || rx < 0 || ry < 0) return;
    for (int y = -ry; y <= ry; y++) {
        double t = ry ? 1.0 - (double)y * y / ((double)ry * ry) : 1.0;
        int hx = (int)std::lround(rx * std::sqrt(std::max(0.0, t)));
        if (fill) bm->al->vtable->hline(bm->al, cx - hx, cy + y, cx + hx, c);
        else { bm->al->vtable->putpixel(bm->al, cx - hx, cy + y, c); bm->al->vtable->putpixel(bm->al, cx + hx, cy + y, c); }
    }
}
void Bitmap::DrawCircle(int x, int y, int rad_, int r, int g, int b, bool fill) { ellipse(this, x, y, rad_, rad_, col(this, r, g, b), fill); }
void Bitmap::DrawEllipse(int x, int y, int rx, int ry, int r, int g, int b) { ellipse(this, x, y, rx, ry, col(this, r, g, b), false); }
void Bitmap::DrawArc(int x, int y, float a1, float a2, int rad_, int r, int g, int b) {
    if (!al) return;
    int c = col(this, r, g, b);
    for (float a = a1; a <= a2; a += 0.01f) al->vtable->putpixel(al, x + (int)(rad_ * std::cos(a)), y - (int)(rad_ * std::sin(a)), c);
}
void Bitmap::DrawTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int r, int g, int b) {
    if (al) al->vtable->triangle(al, x1, y1, x2, y2, x3, y3, col(this, r, g, b));
}
void Bitmap::DrawSpline(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int r, int g, int b) {
    if (!al) return;
    int c = col(this, r, g, b), px = x1, py = y1;
    for (int i = 1; i <= 32; i++) {
        double t = i / 32.0, u = 1 - t;
        int x = (int)(u * u * u * x1 + 3 * u * u * t * x2 + 3 * u * t * t * x3 + t * t * t * x4);
        int y = (int)(u * u * u * y1 + 3 * u * u * t * y2 + 3 * u * t * t * y3 + t * t * t * y4);
        al->vtable->line(al, px, py, x, y, c);
        px = x; py = y;
    }
}

// ------------------------------------------------------------------------------ bitmap fonts
// BmpFont(w, h, n) picks a cabinet bitmap font (gamedata/fonts/*.dlt: one glyph per frame,
// frame = character code) by cell size. FontBase objects are polymorphic: games call virtual
// slots on them (sprite-engine.md §5.4); every slot logs its first call (MEGA_DEBUG_FONT=1: all).
class BmpFont { public: BmpFont(short, short, unsigned short); };
namespace {
struct GlyphFont { int w = 0, h = 0; std::vector<MeritFrame> glyphs; std::vector<int> adv; };
struct FontState { GlyphFont* font = nullptr; int align = 0; int spacing = 0; int r = 0, g = 0, b = 0; };
std::map<const void*, FontState> g_fonts;
GlyphFont* g_defaultFont;

GlyphFont* font_load(const char* file) {
    static std::map<std::string, GlyphFont*> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    std::string path = std::string("/usr/local/gamedata/fonts/") + file;
    std::vector<uint8_t> d;
    GlyphFont* f = nullptr;
    size_t off, count;
    if (merit_read_gz(path.c_str(), d) && merit_container(d, off, count)) {
        f = new GlyphFont;
        merit_read_frames(d, off, f->glyphs, count);
        if (f->glyphs.empty()) { delete f; f = nullptr; }
    }
    if (f) {
        frames_to_px(f->glyphs);
        f->w = f->glyphs[0].w; f->h = f->glyphs[0].h;
        for (auto& g : f->glyphs) {           // proportional advance: rightmost opaque column + 2
            int r = -1;
            for (int y = 0; y < g.h; y++) for (int x = g.w - 1; x > r; x--) if (g.argb[(size_t)y * g.w + x] != kKey) { r = x; break; }
            f->adv.push_back(r < 0 ? f->w / 2 : r + 2);
        }
    }
    cache[file] = f;
    return f;
}
GlyphFont* font_named(const char* name) {
    if (!name || !*name) return nullptr;
    std::string n = name;
    for (const char* ext : {"", ".dlt.gz", ".dlt", ".spr.gz"})
        if (GlyphFont* f = font_load((n + ext).c_str())) return f;
    return nullptr;
}
GlyphFont* font_for(short w, short h) {
    char names[4][32];
    snprintf(names[0], 32, "b%dx%d.dlt.gz", w, h); snprintf(names[1], 32, "%dx%d.dlt.gz", w, h);
    snprintf(names[2], 32, "%dx%dw.dlt.gz", w, h); snprintf(names[3], 32, "%dx%d.dlt.gz", h, h);
    for (auto& n : names) if (GlyphFont* f = font_load(n)) return f;
    return nullptr;
}
GlyphFont* default_font() {
    if (!g_defaultFont) g_defaultFont = font_load("12x16.dlt.gz");    // white glyphs
    if (!g_defaultFont) g_defaultFont = font_load("b24x24.dlt.gz");
    return g_defaultFont;
}
FontState& state_of(const void* font) {
    FontState& st = g_fonts[font];
    if (!st.font) st.font = default_font();
    return st;
}
int adv_of(GlyphFont* f, unsigned char c, int spacing) { return (c < f->adv.size() ? f->adv[c] : f->w) + spacing; }
int text_width(GlyphFont* f, const unsigned char* t, int spacing) {
    int w = 0;
    for (; *t; t++) w += adv_of(f, *t, spacing);
    return w;
}
// glyphs onto a 16/32-bit Allegro bitmap; colour offsets from white tint the (white) glyphs
void glyphs_draw(BITMAP* dst, GlyphFont* f, const unsigned char* t, size_t n, int x, int y, int spacing, int dr, int dg, int db) {
    int d = depth_of(dst);
    for (size_t k = 0; k < n && t[k]; k++) {
        unsigned char ch = t[k];
        if (ch < f->glyphs.size()) {
            const MeritFrame& g = f->glyphs[ch];
            for (int yy = 0; yy < g.h; yy++)
                for (int xx = 0; xx < g.w; xx++) {
                    uint32_t c = g.argb[(size_t)yy * g.w + xx];
                    if (c == kKey) continue;
                    int X = x + xx, Y = y + yy;
                    if (X < dst->cl || X >= dst->cr || Y < dst->ct || Y >= dst->cb) continue;
                    int r = (c >> 16 & 255) * (255 + dr) / 255, gg = (c >> 8 & 255) * (255 + dg) / 255, b = (c & 255) * (255 + db) / 255;
                    int v = d == 16 ? rgb16(r, gg, b) : makecol_depth(d, r, gg, b);
                    dst->vtable->putpixel(dst, X, Y, v);
                }
        }
        x += adv_of(f, ch, spacing);
    }
}
// word-wraps text to max_w (0 = one line)
std::vector<std::string> wrap(GlyphFont* f, const char* text, int max_w, int spacing) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto width = [&](const std::string& s) { return text_width(f, reinterpret_cast<const unsigned char*>(s.c_str()), spacing); };
    auto flush_word = [&]() {
        if (word.empty()) return;
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (max_w > 0 && !cur.empty() && width(trial) > max_w) { lines.push_back(cur); cur = word; }
        else cur = trial;
        word.clear();
    };
    for (const char* p = text ? text : ""; *p; p++) {
        if (*p == '\n') { flush_word(); lines.push_back(cur); cur.clear(); }
        else if (*p == ' ' && max_w > 0) flush_word();
        else word += *p;
    }
    flush_word();
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    return lines;
}
// renders wrapped glyph text into b, resizing it to fit (sprite-engine.md §5.4)
std::string strip_markup(const char* t) {
    std::string out;
    for (const char* p = t ? t : ""; *p; p++) {
        if (*p == '<' && (isalpha((unsigned char)p[1]) || p[1] == '/')) { const char* e = strchr(p, '>'); if (e) { p = e; continue; } }
        out += *p;
    }
    return out;
}
void smack_text(Bitmap* b, const char* text_in, const void* font, int just, int max_w, int max_h, int dr, int dg, int db) {
    std::string clean = strip_markup(tr(text_in));
    const char* text = clean.c_str();
    FontState& st = state_of(font);
    GlyphFont* f = st.font;
    if (!f) return;
    auto lines = wrap(f, text, max_w, st.spacing);
    int tw = 0;
    for (auto& l : lines) tw = std::max(tw, text_width(f, reinterpret_cast<const unsigned char*>(l.c_str()), st.spacing));
    int W = std::max(1, tw), H = std::max(1, (int)lines.size() * f->h);
    if (max_h > 0) H = std::min(H, std::max((int)max_h, f->h));
    b->resize(W, H);
    for (size_t i = 0; i < lines.size(); i++) {
        auto* t = reinterpret_cast<const unsigned char*>(lines[i].c_str());
        int lw = text_width(f, t, st.spacing);
        int x = just > 0 ? W - lw : just < 0 ? 0 : (W - lw) / 2;
        glyphs_draw(b->al, f, t, lines[i].size(), x, (int)i * f->h, st.spacing, dr, dg, db);
    }
}

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

// Slots (byte offsets): 0x08 colour (0, r, g, b), 0x18 Load(font, glyphs, flag), 0x24 alignment
// (0 left, 1 centre, 2 right), 0x2c DrawText(text, x, y, w, ...), 0x30 CreateTextBox(text, w, h, ...)
// -> _RADBitmap*, 0x54/0x60 spacing.
int font_slot(int slot, void* self, const int* a) {
    static bool all = menv("DEBUG_FONT") != nullptr;
    static std::map<int, bool> seen;
    if (all || !seen[slot]) {
        seen[slot] = true;
        LOG("font %p slot 0x%x args %d %d %d %d %d %d %d %d", self, slot * 4, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
    FontState& st = state_of(self);
    GlyphFont* f = st.font;
    switch (slot * 4) {
    case 0x08: st.r = a[1]; st.g = a[2]; st.b = a[3]; return 0;
    case 0x18: {                                   // Load(font name, glyphs, flag)
        if (GlyphFont* nf = font_named(reinterpret_cast<const char*>(a[0]))) st.font = nf;
        return 1;
    }
    case 0x24: st.align = a[0]; return 0;
    case 0x54: case 0x60: st.spacing = a[0] > 4 ? 0 : a[0]; return 0;
    case 0x2c: {                                   // DrawText(text, x, y, width, ...)
        if (!a[0] || !f) return 0;
        std::string clean = strip_markup(tr(reinterpret_cast<const char*>(a[0])));
        const unsigned char* t = reinterpret_cast<const unsigned char*>(clean.c_str());
        int x = a[1], y = a[2], w = a[3], tw = text_width(f, t, st.spacing);
        if (w > 0) x += st.align == 1 ? (w - tw) / 2 : st.align == 2 ? w - tw : 0;
        else x -= st.align == 1 ? tw / 2 : st.align == 2 ? tw : 0;
        glyphs_draw(legacy::target_bitmap(), f, t, strlen(reinterpret_cast<const char*>(t)), x, y, st.spacing, st.r, st.g, st.b);
        legacy::touched_target();
        return tw;
    }
    case 0x30: {                                   // CreateTextBox(text, w, h, ...) -> _RADBitmap*
        const char* t = reinterpret_cast<const char*>(a[0]);
        if (!f) return 0;
        Bitmap tmp(0, 0, 0, 16);
        smack_text(&tmp, t, self, 0, a[1], a[2], st.r, st.g, st.b);
        _RADBitmap* r = legacy::rad_new(tmp.w, tmp.h, kKey);
        BITMAP* wrap32 = legacy::al_wrap32(r->px, r->w, r->h);
        blit(tmp.al, wrap32, 0, 0, 0, 0, tmp.w, tmp.h);
        destroy_bitmap(wrap32);
        return (int)reinterpret_cast<intptr_t>(r);
    }
    }
    return 0;
}
}  // namespace

BmpFont::BmpFont(short w, short h, unsigned short) {
    legacy::video_init();
    font_attach(this);
    GlyphFont* f = font_for(w, h);
    if (!f) f = default_font();
    g_fonts[this].font = f;
}

void Bitmap::CreateSmackTextBox(char const* text, FontBase* font, signed char just, unsigned short max_w,
                                unsigned short max_h, bool) {
    smack_text(this, text, font, just, max_w, max_h, state_of(font).r, state_of(font).g, state_of(font).b);
}
void Bitmap::CreateColoredSmackTextBox(char const* text, FontBase* font, signed char just, unsigned short max_w,
                                       unsigned short max_h, short r, short g, short b, unsigned char, bool) {
    smack_text(this, text, font, just, max_w, max_h, r, g, b);
}

// TrueType text into the existing bitmap, inside the box; colours are offsets from white.
// align: 1 left, 2 centre, 4 right (2 in most calls).
// shrink: reduce the size until the text fits the box on one line (the "S" variant); otherwise
// word-wrap to the box width, lines centred vertically.
static void ttf_box(Bitmap* bm, char const* text, int x, int y, int w, int h, int r, int g, int b, int size,
                    int align, bool bold, char* font, int spacing, bool shrink) {
    if (!text) return;
    text = tr(text);
    if (bm->w <= 0 || bm->h <= 0) bm->resize(x + w, y + h);
    int a = align == 2 ? 2 : align == 4 ? 1 : 0;
    int sp = spacing > 0 ? spacing : 0;
    static bool dbg = menv("DEBUG_TEXT") != nullptr;
    if (dbg) LOG("TTF%s '%s' box %d,%d %dx%d rgb %d,%d,%d size %d align %d %s", shrink ? "S" : "", text, x, y, w, h, r, g, b, size, align, font ? font : "-");
    int cr = std::clamp(255 + r, 0, 255), cg = std::clamp(255 + g, 0, 255), cb = std::clamp(255 + b, 0, 255);
    std::vector<std::string> lines;
    if (shrink || w <= 0) {
        while (shrink && w > 0 && size > 6 && legacy::ttf_measure(text, size, bold, font, sp) > w) size--;
        while (shrink && h > 0 && size > 6 && size > h) size--;
        lines.push_back(text);
    } else {
        std::string cur, word;
        auto flush = [&]() {
            if (word.empty()) return;
            std::string t = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && legacy::ttf_measure(t.c_str(), size, bold, font, sp) > w) { lines.push_back(cur); cur = word; }
            else cur = t;
            word.clear();
        };
        for (const char* p = text; *p; p++) {
            if (*p == '\n') { flush(); lines.push_back(cur); cur.clear(); }
            else if (*p == ' ') flush();
            else word += *p;
        }
        flush();
        if (!cur.empty() || lines.empty()) lines.push_back(cur);
        // still too tall for the box: shrink until it fits
        while (h > 0 && size > 8 && (int)lines.size() * (size + size / 6) > h * 5 / 4) { size--; }
    }
    int lh = size + size / 6;
    int total = (int)lines.size() * lh;
    int y0 = h > 0 ? y + (h - total) / 2 : y;
    for (size_t i = 0; i < lines.size(); i++)
        legacy::ttf_draw(bm->al, lines[i].c_str(), x, y0 + (int)i * lh, w, lh, cr, cg, cb, size, a, bold, font, sp, 0);
}
void Bitmap::CreateColoredTextBoxTTF(char const* text, int x, int y, int w, int h, int r, int g, int b, int size,
                                     int align, bool bold, char* font, int spacing) {
    ttf_box(this, text, x, y, w, h, r, g, b, size, align, bold, font, spacing, false);
}
void Bitmap::CreateColoredTextBoxTTFS(char const* text, int x, int y, int w, int h, int r, int g, int b, int size,
                                      int align, bool bold, char* font, int spacing) {
    ttf_box(this, text, x, y, w, h, r, g, b, size, align, bold, font, spacing, true);
}
void Bitmap::CreateHelpBmpFromInfoFile(int, int, int, int, int, int, int, int, char*, bool, char*) {}

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
            g_fonts[f].font = default_font();
            memcpy(g + off, &f, sizeof f);
        }
    }
    return reinterpret_cast<MegacGraphics*>(g);
}

// ------------------------------------------------------------------------------ String
String::String(char const* fnt, int sz, char const* t, int j, signed char sp, int r_, int g_, int b_,
               unsigned char, unsigned char, unsigned short st) {
    memset(this, 0, sizeof *this);
    snprintf(font, sizeof font, "%s", fnt ? fnt : "bureau");
    size = sz; just = j; spacing = sp; r = r_; g = g_; b = b_; style = st;
    set_text(t);
}
String::String(BmpFont* f, char const* t, int j, signed char sp, int r_, int g_, int b_, unsigned char, unsigned char) {
    memset(this, 0, sizeof *this);
    bmpfont = f; just = j; spacing = sp; r = r_; g = g_; b = b_;
    set_text(t);
}
void String::set_text(const char* t) { free(text); text = strdup(t ? tr(t) : ""); }
void String::ChangeColor(int r_, int g_, int b_, unsigned char) { r = r_; g = g_; b = b_; }
String* String::Copy() {
    auto* s = static_cast<String*>(operator new(sizeof(String)));
    memcpy(s, this, sizeof *s);
    s->text = strdup(text ? text : "");
    return s;
}

namespace legacy {
// A String laid out in a w x h box (0 = fit): TrueType lines, word-wrapped to w.
BITMAP* render_string(const String* s, int w, int h) {
    if (!s || !s->text) return nullptr;
    bool bold = s->style & 0x08;
    int px = s->size > 0 ? s->size : 20;
    // String +0x50 is the interline spacing (libmerit2d SetInterLineSpacing), not letter spacing
    int line_h = std::max(4, px + px / 6 + s->spacing);
    std::vector<std::string> lines;
    {
        std::string cur, word;
        auto fits = [&](const std::string& t) { return w <= 0 || ttf_measure(t.c_str(), px, bold, s->font, 0) <= w; };
        auto flush = [&]() {
            if (word.empty()) return;
            std::string trial = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && !fits(trial) && !(s->style & 0x100)) { lines.push_back(cur); cur = word; }
            else cur = trial;
            word.clear();
        };
        for (const char* p = s->text; *p; p++) {
            if (*p == '\n') { flush(); lines.push_back(cur); cur.clear(); }
            else if (*p == ' ') flush();
            else word += *p;
        }
        flush();
        lines.push_back(cur);
    }
    int tw = 0;
    for (auto& l : lines) tw = std::max(tw, ttf_measure(l.c_str(), px, bold, s->font, 0));
    // a word wider than the box: widen the bitmap rather than cut it (it is centred on the box)
    int W = std::max(w > 0 ? w : 1, tw), H = h > 0 ? h : std::max(1, (int)lines.size() * line_h);
    BITMAP* b = create_bitmap_ex(16, W, H);
    for (int y = 0; y < H; y++) std::fill(row16(b, y), row16(b, y) + W, (uint16_t)kKey16);
    int total = (int)lines.size() * line_h;
    int y0 = (s->just & 0x80) ? 0 : (H - total) / 2;
    int align = (s->just & 0x7f) == 1 ? 0 : (s->just & 0x7f) == 2 ? 1 : 2;
    int r = std::clamp(255 + s->r, 0, 255), g = std::clamp(255 + s->g, 0, 255), bl = std::clamp(255 + s->b, 0, 255);
    for (size_t i = 0; i < lines.size(); i++)
        ttf_draw(b, lines[i].c_str(), 0, y0 + (int)i * line_h, W, line_h, r, g, bl, px, align, bold, s->font, 0, 0);
    return b;
}

// LoadBmp: an animation file's frames as a Bitmap chain. .dlt frames are deltas (composited);
// .spr frames are whole pictures.
Bitmap* load_bitmap_chain(const char* name, bool lang, int max_frames) {
    if (!name) return nullptr;
    std::string path;
    bool delta = false;
    if (find_asset(name, lang, ".flc", path) || find_asset(name, lang, ".fli", path)) {
        Anim* a = legacy::anim_load(name, lang);
        if (!a) return nullptr;
        Bitmap *first = nullptr, *prev = nullptr;
        for (size_t i = 0; i < a->frames.size() && (max_frames <= 0 || (int)i < max_frames); i++) {
            auto* b = new Bitmap(0, 0, 1, 16);
            b->from_frame(a->frames[i]);
            b->frame_index = (int)i;
            b->prev = prev;
            if (prev) prev->next = b; else first = b;
            prev = b;
        }
        delete a;
        return first;
    }
    if (find_asset(name, lang, ".spr", path)) delta = false;
    else if (find_asset(name, lang, ".dlt", path)) delta = true;
    else if (find_asset(name, lang, "", path)) delta = path.find(".dlt") != std::string::npos;
    else {
        // a plain picture (switcheroo LoadBmp("infobox") -> infobox.pcx) as a one-frame chain
        for (const char* ext : {".pcx", ".jpg", ".png", ".tga", ".img"})
            if (find_asset(name, lang, ext, path)) {
                auto* b = new Bitmap(0, 0, 1, 16);
                char buf[512]; snprintf(buf, sizeof buf, "%s", path.c_str());
                bool ok = !strcmp(ext, ".pcx") ? b->LoadPCX(buf, -1, 8, 0) : !strcmp(ext, ".tga") ? b->LoadTGA(buf, 0)
                        : !strcmp(ext, ".img") ? b->LoadData(buf, 0, -1) : b->LoadJPEG(buf, -1, 0, 0);
                if (ok) return b;
                delete b;
            }
        LOG("LoadBmp: missing %s", name);
        return nullptr;
    }
    std::vector<uint8_t> d;
    size_t off, count;
    if (!merit_read_gz(path.c_str(), d) || !merit_container(d, off, count)) {
        // a plain picture (pcx/img/tga/jpg) as a one-frame chain
        auto* b = new Bitmap(0, 0, 1, 16);
        char buf[512]; snprintf(buf, sizeof buf, "%s", name);
        if (b->LoadPCX(buf, -1, 8, 0) || b->LoadJPEG(buf, -1, 0, 0) || b->LoadData(buf, 0, -1)) return b;
        delete b;
        return nullptr;
    }
    std::vector<MeritFrame> frames;
    merit_read_frames(d, off, frames, max_frames > 0 ? (size_t)max_frames : (size_t)-1);
    frames_to_px(frames);
    if (frames.empty()) return nullptr;
    Bitmap *first = nullptr, *prev = nullptr;
    std::vector<uint32_t> canvas;
    int cw = frames[0].w, ch = frames[0].h;
    for (size_t i = 0; i < frames.size(); i++) {
        auto* b = new Bitmap(0, 0, 1, 16);
        if (delta) {
            if (canvas.empty()) canvas.assign((size_t)cw * ch, kKey);
            const MeritFrame& f = frames[i];
            for (int y = 0; y < f.h && y < ch; y++)
                for (int x = 0; x < f.w && x < cw; x++) {
                    uint32_t p = f.argb[(size_t)y * f.w + x];
                    if (p != kKey) canvas[(size_t)y * cw + x] = p;
                }
            MeritFrame c; c.w = cw; c.h = ch; c.argb = canvas;
            b->from_frame(c);
        } else {
            b->from_frame(frames[i]);
        }
        b->frame_index = (int)i;
        b->prev = prev;
        if (prev) prev->next = b; else first = b;
        prev = b;
    }
    return first;
}
void free_bitmap_chain(Bitmap* b) {
    while (b && b->prev) b = b->prev;
    while (b) { Bitmap* n = b->next; delete b; b = n; }
}
}  // namespace legacy

// VideoClass::ShowPCX(name, x, y, flag): a picture straight onto the open buffer (run21, tritowers)
class VideoClass { public: static void ShowPCX(char const*, short, short, unsigned char); };
void VideoClass::ShowPCX(char const* name, short x, short y, unsigned char) {
    if (!name) return;
    Bitmap b(0, 0, 0, 16);
    char buf[512];
    snprintf(buf, sizeof buf, "%s", name);
    if (b.LoadPCX(buf, -1, 8, 0)) b.Display(x, y, 0);
}
namespace TextUtils { bool ContainsArabic(char const*); }
bool TextUtils::ContainsArabic(char const*) { return false; }
