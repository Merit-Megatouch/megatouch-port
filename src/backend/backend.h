// Shared declarations for the SDL2 game-device backend (libgame_device_sprite.so).
//
// The backend never re-declares the engine's C++ classes. Each object is built the way the
// compiler would: call the base constructor exported by the engine, then point the object at
// a copy of the base vtable with only the slots we implement patched in (clone_vtable).
// Methods are plain functions taking `self` first — GCC's i386 calling convention for members.
// Field offsets and slot numbers come from the original sprite backend; see docs/05.
#pragma once
#include "../common/env.h"
#include "merit_abi.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define LOG(...) do { fprintf(stderr, "[mega] " __VA_ARGS__); fputc('\n', stderr); } while (0)

// ---- engine plumbing (engine.cpp) -------------------------------------------------------

// Field at byte offset `off` inside an engine object.
template <typename T> inline T& at(void* obj, int off) { return *reinterpret_cast<T*>(static_cast<char*>(obj) + off); }

void* sym(const char* mangledName);                              // engine symbol or abort
void** clone_vtable(const char* vtableSym, int extraSlots = 0);  // copy of a base vtable, ready to patch
typedef void (*ctor0_t)(void*);
typedef void (*dtor_t)(void*);

// ---- shared state (engine.cpp) ----------------------------------------------------------

extern SDL_Window* g_window;
extern SDL_Renderer* g_renderer;
extern int g_logicalW, g_logicalH;        // MEGA_WIDTH / MEGA_HEIGHT
extern bool g_quit;
double now_ms();

// Per-frame cost counters, printed when a frame hitches (MEGA_HITCH_MS).
struct FrameStats { int uploads, creates, imgLoads, sndLoads; double uploadMs, imgMs, sndMs, renderMs, updateMs; long uploadBytes; };
extern FrameStats g_fs;

// ---- textures (textures.cpp) ------------------------------------------------------------

// CPU pixels per animation frame (in the engine's format) + lazily uploaded SDL textures.
struct TexData {
    int w, h, bpp;                               // bpp of the CPU buffer: 16, 24 or 32
    std::vector<std::vector<uint8_t>> frames;
    std::vector<SDL_Texture*> gpu;
    std::vector<bool> dirty;
};
// Our TexData* lives right after Graphics::BaseTexture (size 0x38).
inline TexData*& texdata(void* tex) { return at<TexData*>(tex, 0x38); }
SDL_Texture* tex_upload(TexData* d, int frame);
TexData* spr_make_texture(int w, int h, const std::vector<std::vector<uint32_t>>& frames);
TexData* load_spr(const char* path);                              // spr.cpp
void* res_create();                                               // Graphics resource manager singleton

// ---- the other engine objects ---------------------------------------------------------

void* fact_new(Graphics::Scene* scene, TextSystem::ITextSystem* text);   // sprites.cpp
void* in_create();                                                        // input.cpp
extern bool g_mouseDown;
extern float g_mouseX, g_mouseY;
void* snd_new();                                                          // sound.cpp
void* net_new();                                                          // net.cpp
void* netlink_new();                                                      // net.cpp
