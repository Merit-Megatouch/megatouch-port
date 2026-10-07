// The loader's TextSystem (global `textSystem`, a TextSystem*), TextDesc and Colour: text laid
// out with a description (font, size, colour, alignment, line spacing) and rendered into
// buffers the caller uploads as textures (Merit3D) or packs into a Bitmap (TTFtoCData).
// docs/reference/merit3d.md §1-2.
#include "bitmap.h"
#include "legacy_ttf.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace xml_gameinfo { enum GameIds : int {}; }
namespace Locale { enum Languages : int {}; }
class Translator { public: static bool LoadTranslations(char const*, bool); static bool LoadTranslations(xml_gameinfo::GameIds); };

template <class T> struct CoordT { T x, y; };

struct Colour {
    short r, g, b;
    static Colour white, black;
};
Colour Colour::white = {255, 255, 255};
Colour Colour::black = {0, 0, 0};

class TextDesc {                                 // 0x54 bytes, copied by value by the games
public:
    void SetFontName(char const*);
    char font[32];                               // +0x00
    int size;                                    // +0x20
    short r, g, b;                               // +0x24 text colour
    short r2, g2, b2;                            // +0x2a second colour (outline/shadow)
    int f30, f34;
    int halign;                                  // +0x38 0 left, 1 centre, 2 right
    int valign;                                  // +0x3c 0 top, 1 centre, 2 bottom
    int mode;                                    // +0x40
    int f44;
    unsigned char f48, pad49[3];
    int f4c;
    int line_spacing;                            // +0x50
};
static_assert(sizeof(TextDesc) == 0x54, "TextDesc");
void TextDesc::SetFontName(char const* n) { snprintf(font, sizeof font, "%s", n ? n : "bureau"); }

static TextDesc default_desc() {
    TextDesc d;
    memset(&d, 0, sizeof d);
    snprintf(d.font, sizeof d.font, "bureau");
    d.size = 12;
    d.r = d.g = d.b = 255;
    d.mode = 2;
    d.line_spacing = -5;
    return d;
}

class TextSystem {
public:
    void LoadTranslations(char const*, bool);
    void LoadTranslations(xml_gameinfo::GameIds);
    void SetCurrentLanguage(Locale::Languages);
    void SetTextDescription(TextDesc const&);
    void UseDefaultTextDescription();
    void GetSpriteChannels(char const*, unsigned char**, unsigned char**, int, int, bool, bool, int, float);
    CoordT<int> GetTextSize(char const*, int, bool);
    static char* ConvertToMarkup(char const*);

    TextDesc desc = default_desc();
    std::vector<unsigned char> buf1, buf2;       // owned; valid until the next call
};
void TextSystem::LoadTranslations(xml_gameinfo::GameIds id) { Translator::LoadTranslations(id); }
void TextSystem::LoadTranslations(char const* name, bool) { Translator::LoadTranslations(name, false); }
void TextSystem::SetCurrentLanguage(Locale::Languages) {}
void TextSystem::SetTextDescription(TextDesc const& d) { desc = d; }
void TextSystem::UseDefaultTextDescription() { desc = default_desc(); }

// out1/out2 for a w x h box (row 0 at the top). bpp 32: RGBA bytes + coverage plane;
// bpp 16: RGB565 colour plane + coverage plane (for Bitmap::TTFtoCData); bpp 24: RGB.
void TextSystem::GetSpriteChannels(char const* markup, unsigned char** out1, unsigned char** out2, int w, int h,
                                   bool, bool, int bpp, float scale) {
    w = std::max(1, w); h = std::max(1, h);
    std::vector<unsigned char> cov;
    int px = legacy::ttf_em_px(desc.font, false, std::max(4.0f, desc.size * (scale > 0 ? scale : 1.0f)));
    if (getenv("MEGA_TEXT_DEBUG")) fprintf(stderr, "[text] '%s' font=%s size=%d px=%d box=%dx%d scale=%g\n", markup, desc.font, desc.size, px, w, h, scale);
    legacy::ttf_coverage(markup, w, h, px, desc.halign, desc.valign, false, desc.font, desc.line_spacing, cov);
    int r = std::clamp((int)desc.r, 0, 255), g = std::clamp((int)desc.g, 0, 255), b = std::clamp((int)desc.b, 0, 255);
    size_t n = (size_t)w * h;
    buf2 = cov;
    if (bpp == 16) {
        buf1.assign(n * 2, 0);
        uint16_t c = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        auto* o = reinterpret_cast<uint16_t*>(buf1.data());
        for (size_t i = 0; i < n; i++) o[i] = cov[i] ? c : 0;
    } else if (bpp == 24) {
        buf1.assign(n * 3, 0);
        for (size_t i = 0; i < n; i++)
            if (cov[i]) { buf1[3 * i] = (unsigned char)r; buf1[3 * i + 1] = (unsigned char)g; buf1[3 * i + 2] = (unsigned char)b; }
    } else {
        buf1.assign(n * 4, 0);
        for (size_t i = 0; i < n; i++)
            if (cov[i]) { buf1[4 * i] = (unsigned char)r; buf1[4 * i + 1] = (unsigned char)g; buf1[4 * i + 2] = (unsigned char)b; buf1[4 * i + 3] = cov[i]; }
    }
    if (out1) *out1 = buf1.data();
    if (out2) *out2 = buf2.data();
}
CoordT<int> TextSystem::GetTextSize(char const* markup, int max_w, bool) {
    CoordT<int> c{0, 0};
    legacy::ttf_text_size(markup, legacy::ttf_em_px(desc.font, false, std::max(4, desc.size)), false, desc.font, desc.line_spacing, max_w, c.x, c.y);
    return c;
}
// Callers wrap the text in markup first, so tags stay: only a bare '&' is escaped, and Latin-1
// text that is not UTF-8 is converted.
char* TextSystem::ConvertToMarkup(char const* s) {
    static std::string bufs[8];
    static int k;
    std::string& out = bufs[k++ & 7];
    out.clear();
    const unsigned char* p = (const unsigned char*)(s ? s : "");
    // valid UTF-8?
    bool utf8 = true;
    for (const unsigned char* q = p; *q && utf8; q++) {
        if (*q < 0x80) continue;
        int n = *q >= 0xf0 ? 3 : *q >= 0xe0 ? 2 : *q >= 0xc0 ? 1 : -1;
        if (n < 0) { utf8 = false; break; }
        for (int i = 1; i <= n; i++) if ((q[i] & 0xc0) != 0x80) { utf8 = false; break; }
        q += n;
    }
    for (; *p; p++) {
        if (*p == '&') {
            const char* e = strchr((const char*)p, ';');
            bool entity = e && e - (const char*)p < 8 && (isalpha(p[1]) || p[1] == '#');
            out += entity ? "&" : "&amp;";
        } else if (*p >= 0x80 && !utf8) {
            out += (char)(0xc0 | *p >> 6); out += (char)(0x80 | (*p & 0x3f));
        } else out += (char)*p;
    }
    return const_cast<char*>(out.c_str());
}

static TextSystem g_text;
TextSystem* textSystem = &g_text;                // games load the pointer and call through it

// Bitmap::TTFtoCData(colour16, alpha, w, h): a GetSpriteChannels(16 bpp) result into the bitmap
void Bitmap::TTFtoCData(unsigned char* colour, unsigned char* alpha, int tw, int th) {
    if (!colour || tw <= 0 || th <= 0) return;
    resize(tw, th);
    const uint16_t* c = reinterpret_cast<const uint16_t*>(colour);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            size_t i = (size_t)y * tw + x;
            int a = alpha ? alpha[i] : (c[i] ? 255 : 0);
            uint16_t v = c[i] == kKey16 ? (uint16_t)(kKey16 ^ 0x20) : c[i];
            reinterpret_cast<uint16_t*>(al->line[y])[x] = a >= 96 ? v : (uint16_t)kKey16;
        }
}
