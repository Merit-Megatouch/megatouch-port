// The subset of Allegro 4.0 that legacy games call directly, implemented natively
// (src/legacy/allegro.cpp). Struct layouts match the cabinet's liballeg-4.0.0.so because games
// read BITMAP fields and call through the GFX_VTABLE inline.
#pragma once
#include <cstdint>

struct GFX_VTABLE;

struct BITMAP {                                   // Allegro 4.0 layout (line[] at +0x40)
    int w, h;
    int clip, cl, cr, ct, cb;
    GFX_VTABLE* vtable;
    void* write_bank;                             // asm stubs: edx=bitmap, eax=line -> eax=address
    void* read_bank;
    void* dat;
    unsigned long id;
    void* extra;
    int x_ofs, y_ofs;
    int seg;
    unsigned char* line[1];
};

// 43 slots, order taken from the relocations of __linear_vtable32 in the cabinet liballeg.
struct GFX_VTABLE {
    int color_depth;
    int mask_color;
    void* unwrite_bank;
    void (*set_clip)(BITMAP*);
    void (*acquire)(BITMAP*);
    void (*release)(BITMAP*);
    BITMAP* (*create_sub_bitmap)(BITMAP*, int, int, int, int);
    void (*created_sub_bitmap)(BITMAP*, BITMAP*);
    int (*getpixel)(BITMAP*, int, int);
    void (*putpixel)(BITMAP*, int, int, int);
    void (*vline)(BITMAP*, int, int, int, int);
    void (*hline)(BITMAP*, int, int, int, int);
    void (*hfill)(BITMAP*, int, int, int, int);
    void (*line)(BITMAP*, int, int, int, int, int);
    void (*rectfill)(BITMAP*, int, int, int, int, int);
    int (*triangle)(BITMAP*, int, int, int, int, int, int, int);
    void (*draw_sprite)(BITMAP*, BITMAP*, int, int);
    void (*draw_256_sprite)(BITMAP*, BITMAP*, int, int);
    void (*draw_sprite_v_flip)(BITMAP*, BITMAP*, int, int);
    void (*draw_sprite_h_flip)(BITMAP*, BITMAP*, int, int);
    void (*draw_sprite_vh_flip)(BITMAP*, BITMAP*, int, int);
    void (*draw_trans_sprite)(BITMAP*, BITMAP*, int, int);
    void (*draw_trans_rgba_sprite)(BITMAP*, BITMAP*, int, int);
    void (*draw_lit_sprite)(BITMAP*, BITMAP*, int, int, int);
    void (*draw_rle_sprite)(BITMAP*, const void*, int, int);
    void (*draw_trans_rle_sprite)(BITMAP*, const void*, int, int);
    void (*draw_trans_rgba_rle_sprite)(BITMAP*, const void*, int, int);
    void (*draw_lit_rle_sprite)(BITMAP*, const void*, int, int, int);
    void (*draw_character)(BITMAP*, BITMAP*, int, int, int);
    void (*draw_glyph)(BITMAP*, const void*, int, int, int);
    void (*blit_from_memory)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*blit_to_memory)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*blit_from_system)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*blit_to_system)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*blit_to_self)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*blit_to_self_forward)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*blit_to_self_backward)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*blit_between_formats)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*masked_blit)(BITMAP*, BITMAP*, int, int, int, int, int, int);
    void (*clear_to_color)(BITMAP*, int);
    void (*pivot_scaled_sprite_flip)(BITMAP*, BITMAP*, int, int, int, int, int, int, int);
    void* draw_sprite_end;
    void* blit_end;
};

extern "C" {
extern BITMAP* screen;
BITMAP* create_bitmap_ex(int depth, int w, int h);
void destroy_bitmap(BITMAP* b);
void clear_bitmap(BITMAP* b);
void blit(BITMAP* src, BITMAP* dst, int sx, int sy, int dx, int dy, int w, int h);
void masked_blit(BITMAP* src, BITMAP* dst, int sx, int sy, int dx, int dy, int w, int h);
void stretch_blit(BITMAP* s, BITMAP* d, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh);
int makecol_depth(int depth, int r, int g, int b);
int getr_depth(int depth, int c);
int getg_depth(int depth, int c);
int getb_depth(int depth, int c);
}

namespace legacy {
// Wraps an existing 32-bit pixel buffer (e.g. the screen) in a BITMAP without copying.
BITMAP* al_wrap32(uint32_t* px, int w, int h);
void al_set_mode(int w, int h);                   // gfx_driver->w/h
}
