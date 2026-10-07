// TrueType text for the legacy engine (BitmapTextTTF, Bitmap::CreateColoredTextBoxTTF...).
//
// The cabinet rendered these through Xft with the EuroFONT families in /usr/local/gamedata/ttf
// (font.config maps "bureau" -> "EFN BureauC", fonts.dir maps styles to files). This renders the
// same .ttf files with stb_truetype into Allegro bitmaps.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb_truetype.h"
#include "allegro.h"
#include "legacy_ttf.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
struct Font { std::vector<unsigned char> data; stbtt_fontinfo info; bool ok = false; };
std::map<std::string, Font*> g_fonts;

// family (lower case, as games pass it) -> regular / bold file in gamedata/ttf
struct Family { const char* name; const char* regular; const char* bold; };
const Family kFamilies[] = {
    {"bureau", "bureau1", "bureau3"},   {"default", "bureau1", "bureau3"}, {"menu", "bureau1", "bureau3"},
    {"ailanthus", "ailant1", "ailant2"}, {"betula", "betula1", "betula1"}, {"castanea", "cast1", "cast1"},
    {"erica", "erica", "erica"},         {"script", "erica", "erica"},     {"eukalyptus", "eukalyp1", "eukalyp1"},
    {"fagus", "fagus1", "fagus1"},       {"flores", "flor", "flor"},       {"ginkgo", "ginkgo", "ginkgo"},
    {"juglans", "juglan", "juglan"},     {"liquid ambar", "liquida", "liquida"}, {"magnolia", "magnol1", "magnol1"},
    {"notes", "notes1", "notes2"},       {"olea", "olea1", "olea1"},       {"olea alba", "olea3", "olea3"},
    {"parrotia", "parrotia", "parrotia"}, {"pinus nigra", "pinusn", "pinusn"}, {"populus", "popul1", "popul1"},
    {"quercus", "querc1", "querc1"},     {"tamar alba", "tamar1", "tamar1"}, {"tamar nigra", "tamar2", "tamar2"},
    {"thotem brush", "thotem", "thotem"}, {"torreya", "torrey1", "torrey3"}, {"tsuga", "tsuga", "tsuga"},
};

Font* load(const std::string& file) {
    auto it = g_fonts.find(file);
    if (it != g_fonts.end()) return it->second;
    auto* f = new Font;
    std::string path = "/usr/local/gamedata/ttf/" + file + ".ttf";   // mapped into data/ by the fs shim
    if (FILE* fp = fopen(path.c_str(), "rb")) {
        fseek(fp, 0, SEEK_END);
        f->data.resize(ftell(fp));
        fseek(fp, 0, SEEK_SET);
        f->ok = fread(f->data.data(), 1, f->data.size(), fp) == f->data.size() &&
                stbtt_InitFont(&f->info, f->data.data(), stbtt_GetFontOffsetForIndex(f->data.data(), 0));
        fclose(fp);
    }
    if (!f->ok) fprintf(stderr, "[legacy] ttf: cannot load %s\n", path.c_str());
    g_fonts[file] = f;
    return f;
}

Font* font_for(const char* family, bool bold) {
    std::string fam = family ? family : "bureau";
    for (auto& c : fam) c = (char)tolower((unsigned char)c);
    for (auto& f : kFamilies)
        if (fam == f.name) return load(bold ? f.bold : f.regular);
    Font* f = load(fam);                                     // a file name given directly
    return f->ok ? f : load(bold ? "bureau3" : "bureau1");
}

// UTF-8 decode (games pass translated, UTF-8 text)
int next_cp(const unsigned char*& p) {
    int c = *p++;
    if (c < 0x80) return c;
    int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    c &= 0x3f >> n;
    while (n-- && (*p & 0xc0) == 0x80) c = c << 6 | (*p++ & 0x3f);
    return c;
}
}  // namespace

namespace legacy {

int ttf_measure(const char* text, int px, bool bold, const char* family, int spacing) {
    Font* f = font_for(family, bold);
    if (!f->ok || !text) return 0;
    float sc = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
    float x = 0;
    int prev = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p;) {
        int cp = next_cp(p);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        if (prev) x += sc * stbtt_GetCodepointKernAdvance(&f->info, prev, cp);
        x += sc * adv + spacing;
        prev = cp;
    }
    return (int)(x - (prev ? spacing : 0) + 0.5f);
}

// Draws `text` in the box (x, y, w, h) of `dst`. align: 0 left, 1 right, 2 centre (vertically
// centred in the box either way). Anti-aliased edges are blended over what is in the bitmap.
void ttf_draw(BITMAP* dst, const char* text, int bx, int by, int bw, int bh, int r, int g, int b, int px,
              int align, bool bold, const char* family, int spacing, int outline) {
    Font* f = font_for(family, bold);
    if (!dst || !f->ok || !text || px <= 0) return;
    float sc = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    int tw = ttf_measure(text, px, bold, family, spacing);
    int th = (int)((asc - desc) * sc);
    float x = align == 2 ? bx + (bw - tw) / 2.0f : align == 1 ? bx + bw - tw : bx;
    int base = by + (bh > 0 ? (bh - th) / 2 : 0) + (int)(asc * sc);
    int depth = dst->vtable->color_depth;
    int prev = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p;) {
        int cp = next_cp(p);
        if (prev) x += sc * stbtt_GetCodepointKernAdvance(&f->info, prev, cp);
        int gw, gh, ox, oy;
        unsigned char* bm = stbtt_GetCodepointBitmapSubpixel(&f->info, sc, sc, x - (int)x, 0, cp, &gw, &gh, &ox, &oy);
        for (int pass = outline > 0 ? 0 : 1; pass < 2 && bm; pass++) {
            int rad = pass == 0 ? outline : 0;
            for (int j = -rad; j < gh + rad; j++)
                for (int i = -rad; i < gw + rad; i++) {
                    int a = 0;
                    if (pass == 1) a = (i >= 0 && j >= 0 && i < gw && j < gh) ? bm[j * gw + i] : 0;
                    else for (int dy = -rad; dy <= rad; dy++) for (int dx = -rad; dx <= rad; dx++) {
                        int u = i + dx, v = j + dy;
                        if (u >= 0 && v >= 0 && u < gw && v < gh) a = std::max(a, (int)bm[v * gw + u]);
                    }
                    if (!a) continue;
                    int X = (int)x + ox + i, Y = base + oy + j;
                    if (X < dst->cl || X >= dst->cr || Y < dst->ct || Y >= dst->cb) continue;
                    int cr = pass ? r : 0, cg = pass ? g : 0, cb = pass ? b : 0;
                    int old = dst->vtable->getpixel(dst, X, Y);
                    bool masked = old == dst->vtable->mask_color;
                    int orr = masked ? cr : getr_depth(depth, old), og = masked ? cg : getg_depth(depth, old),
                        ob = masked ? cb : getb_depth(depth, old);
                    if (masked && a < 128) continue;              // keep transparency around glyphs
                    int nr = orr + (cr - orr) * a / 255, ng = og + (cg - og) * a / 255, nb = ob + (cb - ob) * a / 255;
                    int c = makecol_depth(depth, nr, ng, nb);
                    if (c == dst->vtable->mask_color) c ^= 1 << 8;  // never write the mask colour
                    dst->vtable->putpixel(dst, X, Y, c);
                }
        }
        stbtt_FreeBitmap(bm, nullptr);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        x += sc * adv + spacing;
        prev = cp;
    }
}

}  // namespace legacy
