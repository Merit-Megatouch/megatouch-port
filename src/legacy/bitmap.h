// The loader's C++ Bitmap (0x94 bytes, games allocate it and read fields inline) and text
// classes. Layout: docs/reference/sprite-engine.md §3 "Bitmap".
#pragma once
#include "engine.h"
#include <cstddef>

struct _IO_FILE;
class FontBase;
class BmpFont;

class Bitmap {
public:
    Bitmap(int w, int h, unsigned char a3, unsigned char bpp);
    virtual ~Bitmap();                           // vtable {D1, D0}: games delete via slot 1

    bool LoadPCX(char*, int, int, unsigned char);
    bool LoadData(char*, unsigned char, int);
    bool LoadCompressedData(char*, _IO_FILE*);
    bool LoadTGA_32(char*);
    bool LoadTGA(char*, int);
    bool LoadJPEG(char*, int, int, unsigned char);
    bool LoadPNG(char*, int);
    bool LoadRawPNG(char*);
    void Display(int x, int y, int f);
    void DisplayRegion(int sx, int sy, int w, int h, int dx, int dy, int f);
    void DisplayZ(int x, int y, int z, unsigned char);
    int DrawtoVBZ(int x, int y, int z);          // persistent z-ordered item on the shown buffer
    void CopyFromCurrent(int x, int y);
    void CopyFromScreen(int x, int y, bool);
    void CopyFromVB(int x, int y, int vb);
    void CopyToBitmap(Bitmap* dst, int sx, int sy, int w, int h, int dx, int dy);
    void SetDim(int w, int h);
    void Clear();
    void GreyBmp();
    void Colorize(int r, int g, int b);
    void ReplaceTrans(bool);
    void Scale(int w, int h, unsigned char);
    void ScaleInto(Bitmap* dst, int w, int h, unsigned char);
    void Crop(Bitmap* dst, unsigned char);
    void Compress(bool, int, unsigned short*);
    void DeCompress();
    void DeCompress32();
    void FreeMemory();
    unsigned short* CData(int);
    void setCData(unsigned short*);
    void freeCData();
    void TTFtoCData(unsigned char*, unsigned char*, int, int);
    Bitmap* NextAnim();
    Bitmap* PrevAnim();
    void NextAnim(Bitmap*);
    void PrevAnim(Bitmap*);
    // animations attached to the bitmap (.dlt "smacks")
    bool SmackAnimationLoad(char*, unsigned char);
    void SmackAnimationJumpTo(int);
    void SmackAnimationRewind();
    void SmackAnimationDisplay(int, int, unsigned char);
    void SmackAnimationAdvance(unsigned char);
    void SmackAnimationUnload();
    void SmackAnimationChangeFRate(int);
    void DisplaySmack(unsigned short, unsigned short, unsigned char);
    bool LoadSmack(char*, unsigned char, unsigned char);
    void ConvertSmack(unsigned char);
    bool DeltaOpen(char*);
    bool DeltaAdvance(bool, int*);
    // drawing in absolute RGB
    void DrawPoint(int x, int y, int r, int g, int b);
    void DrawLine(int x1, int y1, int x2, int y2, int r, int g, int b);
    void DrawRectangle(int x, int y, int w, int h, int r, int g, int b);
    void DrawCircle(int x, int y, int rad, int r, int g, int b, bool fill);
    void DrawEllipse(int x, int y, int rx, int ry, int r, int g, int b);
    void DrawArc(int x, int y, float a1, float a2, int rad, int r, int g, int b);
    void DrawTriangle(int, int, int, int, int, int, int, int, int);
    void DrawSpline(int, int, int, int, int, int, int, int, int, int, int);
    // text
    void CreateSmackTextBox(char const*, FontBase*, signed char, unsigned short, unsigned short, bool);
    void CreateColoredSmackTextBox(char const*, FontBase*, signed char, unsigned short, unsigned short,
                                   short, short, short, unsigned char, bool);
    void CreateColoredTextBoxTTF(char const*, int, int, int, int, int, int, int, int, int, bool, char*, int);
    void CreateColoredTextBoxTTFS(char const*, int, int, int, int, int, int, int, int, int, bool, char*, int);
    void CreateHelpBmpFromInfoFile(int, int, int, int, int, int, int, int, char*, bool, char*);

    // ---- layout (offsets games use inline)
    unsigned char pad04[6];
    short fx_r, fx_g, fx_b;                      // +0x0a colour effect (offsets from white)
    unsigned char pad10[0x10];
    unsigned short depth;                        // +0x20
    unsigned char flags;                         // +0x22: 0x02 opaque, 0x80 streamed delta
    unsigned char pad23[9];
    BITMAP* al;                                  // +0x2c the Allegro bitmap (16 bpp)
    short scale_x, scale_y;                      // +0x30
    unsigned char scale_on;                      // +0x34
    unsigned char pad35[11];
    unsigned short frames;                       // +0x40 smack frame count
    unsigned char pad42[2];
    _RADBitmap* rad;                             // +0x44 RAD image handed to ConvertSmack
    int csize;                                   // +0x48
    int w, h;                                    // +0x4c, +0x50
    unsigned char effect;                        // +0x54 Display effect mode
    unsigned char pad55[3];
    // engine-private from here (games never touch +0x58..)
    Bitmap* next;                                // +0x58 frame chain
    Bitmap* prev;                                // +0x5c
    Anim* anim;                                  // +0x60 attached animation
    int frame_index;                             // +0x64 index in the chain
    unsigned short* cdata;                       // +0x68 compressed (RLE) pixels set by setCData
    unsigned char* smack;                        // +0x6c handle put at +0x44 for a loaded animation
                                                 // (wild8 clears flag bytes at +0xc..+0xe in it)
    unsigned char pad70[0x94 - 0x70];

    // helpers for the engine
    void resize(int w, int h);                   // reallocate, filled transparent
    void from_frame(const MeritFrame& f);        // 32-bit frame -> this bitmap
    void from_argb(int w, int h, const uint32_t* argb);   // 32-bit with alpha in GL mode, else 16-bit
    void draw_to(BITMAP* dst, int sx, int sy, int w, int h, int dx, int dy, bool black_trans);
    bool ready() { if (!al) DeCompress(); return al != nullptr; }
};
static_assert(sizeof(Bitmap) == 0x94, "Bitmap layout");
static_assert(offsetof(Bitmap, al) == 0x2c, "Bitmap +0x2c");
static_assert(offsetof(Bitmap, w) == 0x4c, "Bitmap +0x4c");
static_assert(offsetof(Bitmap, effect) == 0x54, "Bitmap +0x54");

// String (0x68): text with a TrueType font, attached to a sprite.
class String {
public:
    String(char const* font, int size, char const* text, int just, signed char spacing, int r, int g, int b,
           unsigned char, unsigned char, unsigned short style);
    String(BmpFont*, char const*, int, signed char, int, int, int, unsigned char, unsigned char);
    void ChangeColor(int r, int g, int b, unsigned char);
    String* Copy();
    void set_text(const char* t);

    unsigned char pad00[0x0c];
    char* text;                                  // +0x0c (read by libmerit2d)
    char font[32];                               // +0x10
    int size;                                    // +0x30
    int just;                                    // +0x34: 1 left, 2 right, 4 centre, +0x80 top
    int r, g, b;                                 // +0x38 offsets from white
    unsigned short style;                        // +0x44: 0x08 bold
    unsigned char pad46[2];
    BmpFont* bmpfont;                            // +0x48
    unsigned char pad4c[4];
    signed char spacing;                         // +0x50 (read by libmerit2d)
    unsigned char pad51[0x68 - 0x51];
};
static_assert(sizeof(String) == 0x68, "String layout");
static_assert(offsetof(String, text) == 0x0c, "String +0x0c");
static_assert(offsetof(String, spacing) == 0x50, "String +0x50");

namespace legacy {
// a LoadBmp result: every frame of an animation file as a Bitmap chain (NULL if missing)
Bitmap* load_bitmap_chain(const char* name, bool lang, int max_frames);
void free_bitmap_chain(Bitmap* first);
// renders a String into a new 16-bit bitmap of the given box size (0 = fit the text)
BITMAP* render_string(const String* s, int w, int h);
BITMAP* bmpfont_render(const void* font, const char* text, int just, int w, int h, int r, int g, int b);
void bmpfont_box(Bitmap* b, const void* font, const char* text, int just, int w, int h, int r, int g, int bl);
}
