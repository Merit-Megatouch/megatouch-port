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

// Pango markup the games pass ("<b>0</b>", "<span foreground=...>"): tags dropped, entities decoded
static std::string plain(const char* t) {
    std::string out;
    for (const char* p = t; *p; p++) {
        if (*p == '<' && (isalpha((unsigned char)p[1]) || p[1] == '/')) {
            const char* e = strchr(p, '>');
            if (e) { p = e; continue; }
        }
        if (*p == '&') {
            static const struct { const char* ent; char c; } ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
            bool hit = false;
            for (auto& en : ents) if (!strncmp(p, en.ent, strlen(en.ent))) { out += en.c; p += strlen(en.ent) - 1; hit = true; break; }
            if (hit) continue;
        }
        out += *p;
    }
    return out;
}

int ttf_measure(const char* text_in, int px, bool bold, const char* family, int spacing) {
    Font* f = font_for(family, bold);
    if (!f->ok || !text_in) return 0;
    std::string clean = plain(text_in);
    const char* text = clean.c_str();
    float sc = stbtt_ScaleForPixelHeight(&f->info, (float)px);
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
void ttf_draw(BITMAP* dst, const char* text_in, int bx, int by, int bw, int bh, int r, int g, int b, int px,
              int align, bool bold, const char* family, int spacing, int outline) {
    Font* f = font_for(family, bold);
    if (!dst || !f->ok || !text_in || px <= 0) return;
    std::string clean = plain(text_in);
    const char* text = clean.c_str();
    float sc = stbtt_ScaleForPixelHeight(&f->info, (float)px);
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


// Glyph coverage (0..255) of `text` laid out in a w x h box: word-wrapped at w, '\n' breaks,
// halign 0 left / 1 centre / 2 right, valign 0 top / 1 centre / 2 bottom, `line_gap` extra pixels
// between lines. Pango markup is dropped; <b> selects the bold face.
void ttf_coverage(const char* markup, int w, int h, int px, int halign, int valign, bool bold, const char* family,
                  int line_gap, std::vector<unsigned char>& out) {
    out.assign((size_t)std::max(0, w) * std::max(0, h), 0);
    if (!markup || w <= 0 || h <= 0 || px <= 0) return;
    if (strstr(markup, "<b>")) bold = true;
    std::string text = plain(markup);
    Font* f = font_for(family, bold);
    if (!f->ok) return;
    float sc = stbtt_ScaleForPixelHeight(&f->info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    int lh = (int)((asc - desc) * sc) + line_gap;
    if (lh < 1) lh = 1;
    // wrap
    std::vector<std::string> lines;
    {
        std::string cur, word;
        auto fits = [&](const std::string& t) { return ttf_measure(t.c_str(), px, bold, family, 0) <= w; };
        auto flush = [&]() {
            if (word.empty()) return;
            std::string t = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && !fits(t)) { lines.push_back(cur); cur = word; } else cur = t;
            word.clear();
        };
        for (const char* p = text.c_str(); *p; p++) {
            if (*p == '\n') { flush(); lines.push_back(cur); cur.clear(); }
            else if (*p == ' ') flush();
            else word += *p;
        }
        flush();
        if (!cur.empty() || lines.empty()) lines.push_back(cur);
    }
    int total = (int)lines.size() * lh;
    int y0 = valign == 1 ? (h - total) / 2 : valign == 2 ? h - total : 0;
    for (size_t li = 0; li < lines.size(); li++) {
        const std::string& l = lines[li];
        int lw = ttf_measure(l.c_str(), px, bold, family, 0);
        float x = halign == 1 ? (w - lw) / 2.0f : halign == 2 ? (float)(w - lw) : 0.0f;
        int base = y0 + (int)li * lh + (int)(asc * sc);
        int prev = 0;
        for (const unsigned char* p = (const unsigned char*)l.c_str(); *p;) {
            int cp = next_cp(p);
            if (prev) x += sc * stbtt_GetCodepointKernAdvance(&f->info, prev, cp);
            int gw, gh, ox, oy;
            unsigned char* bm = stbtt_GetCodepointBitmapSubpixel(&f->info, sc, sc, x - (int)x, 0, cp, &gw, &gh, &ox, &oy);
            for (int j = 0; j < gh; j++)
                for (int i = 0; i < gw; i++) {
                    int X = (int)x + ox + i, Y = base + oy + j;
                    if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                    unsigned char& o = out[(size_t)Y * w + X];
                    o = std::max(o, bm[j * gw + i]);
                }
            stbtt_FreeBitmap(bm, nullptr);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
            x += sc * adv;
            prev = cp;
        }
    }
}

// size of `markup` laid out at px, wrapped at max_w
void ttf_text_size(const char* markup, int px, bool bold, const char* family, int line_gap, int max_w, int& w, int& h) {
    w = h = 0;
    if (!markup) return;
    if (strstr(markup, "<b>")) bold = true;
    std::string text = plain(markup);
    Font* f = font_for(family, bold);
    if (!f->ok) return;
    float sc = stbtt_ScaleForPixelHeight(&f->info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    int lh = (int)((asc - desc) * sc) + line_gap;
    std::vector<std::string> lines;
    std::string cur, word;
    auto fits = [&](const std::string& t) { return max_w <= 0 || ttf_measure(t.c_str(), px, bold, family, 0) <= max_w; };
    auto flush = [&]() {
        if (word.empty()) return;
        std::string t = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && !fits(t)) { lines.push_back(cur); cur = word; } else cur = t;
        word.clear();
    };
    for (const char* p = text.c_str(); *p; p++) {
        if (*p == '\n') { flush(); lines.push_back(cur); cur.clear(); }
        else if (*p == ' ') flush();
        else word += *p;
    }
    flush();
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    for (auto& l : lines) w = std::max(w, ttf_measure(l.c_str(), px, bold, family, 0));
    h = (int)lines.size() * std::max(1, lh);
}

}  // namespace legacy
