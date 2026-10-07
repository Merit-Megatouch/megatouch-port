// Textures (Graphics::BaseTexture), image loading (PNG/TGA via SDL_image, .spr via spr.cpp)
// and the resource manager singleton (Graphics::BaseResourceManager).

#include "backend.h"
#include <unistd.h>
#include <SDL2/SDL_image.h>
#include <cstring>

// ---------------------------------------------------------------------------
// Textures
//
// BaseTexture layout used here:
//   +0x0c TextureInfo { int bpp; int byteSize; unsigned w; unsigned h; }
//   +0x20 std::string image file
//   +0x24 Core::Rect source rect (x,y,w,h at +0x28..+0x34)
// Our data is appended at +0x38.

static void** g_texVtbl;

SDL_Texture* tex_upload(TexData* d, int frame) {
    if (frame < 0 || frame >= (int)d->frames.size()) frame = 0;
    if (d->gpu[frame] && !d->dirty[frame]) return d->gpu[frame];
    double t0 = now_ms();
    g_fs.uploads++; g_fs.uploadBytes += (long)d->w * d->h * 4;
    if (!d->gpu[frame]) g_fs.creates++;
    struct Done { double t0; ~Done() { g_fs.uploadMs += now_ms() - t0; } } done{t0};
    if (!d->gpu[frame]) {
        d->gpu[frame] = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, d->w, d->h);
        SDL_SetTextureBlendMode(d->gpu[frame], SDL_BLENDMODE_BLEND);
    }
    void* px; int pitch;
    if (SDL_LockTexture(d->gpu[frame], nullptr, &px, &pitch) == 0) {
        const uint8_t* src = d->frames[frame].data();
        for (int y = 0; y < d->h; y++) {
            uint32_t* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(px) + y * pitch);
            if (d->bpp == 32) {
                memcpy(dst, src + y * d->w * 4, d->w * 4);
            } else if (d->bpp == 24) {
                const uint8_t* s = src + y * d->w * 3;
                for (int x = 0; x < d->w; x++) dst[x] = 0xff000000u | (s[3*x+2] << 16) | (s[3*x+1] << 8) | s[3*x];
            } else {
                const uint16_t* s = reinterpret_cast<const uint16_t*>(src) + y * d->w;
                for (int x = 0; x < d->w; x++) {
                    uint16_t c = s[x];
                    if (c == 0xf81f) { dst[x] = 0; continue; }  // Allegro magic pink = transparent
                    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                    dst[x] = 0xff000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
                }
            }
        }
        SDL_UnlockTexture(d->gpu[frame]);
    }
    d->dirty[frame] = false;
    return d->gpu[frame];
}

static void tex_free(void* self) {
    TexData* d = texdata(self);
    if (!d) return;
    for (SDL_Texture* t : d->gpu) if (t) SDL_DestroyTexture(t);
    delete d;
    texdata(self) = nullptr;
}

static void tex_fill_info(void* self, void* info) {
    TexData* d = texdata(self);
    if (!d) return;
    at<int>(info, 0) = d->bpp;
    at<int>(info, 4) = d->w * d->h * (d->bpp / 8);
    at<unsigned>(info, 8) = d->w;
    at<unsigned>(info, 12) = d->h;
}

static void tex_finish_load(void* self, const std::string* file) {
    if (file) at<std::string>(self, 0x20) = *file;
    tex_fill_info(self, static_cast<char*>(self) + 0x0c);
    TexData* d = texdata(self);
    at<float>(self, 0x28) = 0;
    at<float>(self, 0x2c) = 0;
    at<float>(self, 0x30) = (float)d->w;
    at<float>(self, 0x34) = (float)d->h;
}

static int tex_type(void*) { return 2; }
static void tex_noop(void*) {}
static int tex_zero(void*) { return 0; }
static void* tex_lock(void* self) {
    TexData* d = texdata(self);
    return d && !d->frames.empty() ? d->frames[0].data() : nullptr;
}
static void tex_unlock(void* self) {
    TexData* d = texdata(self);
    if (d) for (size_t i = 0; i < d->dirty.size(); i++) d->dirty[i] = true;
}
static void tex_destroy(void* self) {
    tex_free(self);
    reinterpret_cast<void (*)(void*)>(sym("_ZN8Graphics11BaseTexture7DestroyEv"))(self);
}
static void tex_dtor(void* self) {
    tex_free(self);
    reinterpret_cast<dtor_t>(sym("_ZN8Graphics11BaseTextureD2Ev"))(self);
}
static void tex_dtor_delete(void* self) { tex_dtor(self); operator delete(self); }

static void* tex_new() {
    if (!g_texVtbl) {
        g_texVtbl = clone_vtable("_ZTVN8Graphics11BaseTextureE");
        g_texVtbl[0] = (void*)tex_dtor;
        g_texVtbl[1] = (void*)tex_dtor_delete;
        g_texVtbl[16] = (void*)tex_type;            // GetTextureType
        g_texVtbl[17] = (void*)tex_noop;            // MakeHardwareTexture
        g_texVtbl[18] = (void*)tex_noop;            // MakeSoftwareTexture
        g_texVtbl[19] = (void*)tex_zero;            // ImplementationBind
        g_texVtbl[20] = (void*)tex_zero;            // ImplementationUnbind
        g_texVtbl[21] = (void*)tex_lock;            // ImplementationLock
        g_texVtbl[22] = (void*)tex_unlock;          // ImplementationUnlock
        g_texVtbl[23] = (void*)tex_fill_info;       // FillTextureInfo
        g_texVtbl[24] = (void*)tex_destroy;         // Destroy
    }
    const int size = 0x38 + sizeof(TexData*);   // BaseTexture + our pointer
    void* t = operator new(size);
    memset(t, 0, size);
    reinterpret_cast<ctor0_t>(sym("_ZN8Graphics11BaseTextureC2Ev"))(t);
    at<void**>(t, 0) = g_texVtbl;
    texdata(t) = nullptr;
    return t;
}

static TexData* texdata_new(int w, int h, int bpp, int frames) {
    TexData* d = new TexData;
    d->w = w; d->h = h; d->bpp = bpp;
    d->frames.assign(frames, std::vector<uint8_t>((size_t)w * h * (bpp / 8)));
    d->gpu.assign(frames, nullptr);
    d->dirty.assign(frames, true);
    return d;
}

// --- image loaders ---------------------------------------------------------

static TexData* load_with_sdl_image(const char* path) {
    // Read the file ourselves: SDL's own file check rejects files on WSL's drvfs mounts.
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    std::vector<uint8_t> buf;
    uint8_t chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) buf.insert(buf.end(), chunk, chunk + n);
    fclose(f);
    // TGA has no signature to sniff, so pass the type from the extension.
    const char* dot = strrchr(path, '.');
    std::string type = dot ? dot + 1 : "";
    for (char& c : type) c = (char)toupper((unsigned char)c);
    SDL_Surface* s = IMG_LoadTyped_RW(SDL_RWFromConstMem(buf.data(), (int)buf.size()), 1, type.c_str());
    if (!s) return nullptr;
    SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s);
    if (!c) return nullptr;
    TexData* d = texdata_new(c->w, c->h, 32, 1);
    for (int y = 0; y < c->h; y++)
        memcpy(d->frames[0].data() + y * c->w * 4, static_cast<uint8_t*>(c->pixels) + y * c->pitch, c->w * 4);
    SDL_FreeSurface(c);
    return d;
}

TexData* spr_make_texture(int w, int h, const std::vector<std::vector<uint32_t>>& frames) {
    TexData* d = texdata_new(w, h, 32, (int)frames.size());
    for (size_t i = 0; i < frames.size(); i++) memcpy(d->frames[i].data(), frames[i].data(), (size_t)w * h * 4);
    return d;
}

static TexData* load_image(const std::string& file) {
    double t0 = now_ms();
    struct Done { double t0; ~Done() { g_fs.imgLoads++; g_fs.imgMs += now_ms() - t0; } } done{t0};
    TexData* d = nullptr;
    std::string path = file;
    // content-pack images are named relative to the content root (photo hunt pictures)
    if (!path.empty() && path[0] != '/' && access(path.c_str(), F_OK) != 0) {
        std::string c = "/usr/local/ion_only/content/" + path;
        if (access(c.c_str(), F_OK) == 0) path = c;
    }
    if (path.find(".spr") != std::string::npos) d = load_spr(path.c_str());
    else d = load_with_sdl_image(path.c_str());
    if (!d) LOG("failed to load image %s (%s)", file.c_str(), SDL_GetError());
    return d;
}

// ---------------------------------------------------------------------------
// Resource manager (BaseResourceManager + 0x20 = manager type, 2 for sprite)

static void** g_resVtbl;

static int res_load_file(void* self, const std::string* file, Graphics::ImageResource* res) {
    TexData* d = load_image(*file);
    if (!d) return 0;
    void* t = tex_new();
    texdata(t) = d;
    tex_finish_load(t, file);
    Graphics::Frame fr{res->GetTotalTextures(), Core::Rect(0, 0, (float)d->w, (float)d->h)};
    for (size_t i = 0; i < d->frames.size(); i++) res->AddFrame(fr);
    res->AddTexture(static_cast<Graphics::ITexture*>(t));
    return 1;
}

static void* res_load_mem(void* self, const uint8_t* data, int w, int h, int fmt) {
    int bpp = fmt == 2 ? 24 : fmt == 3 ? 32 : 16;
    void* t = tex_new();
    TexData* d = texdata_new(w, h, bpp, 1);
    if (data) memcpy(d->frames[0].data(), data, d->frames[0].size());
    texdata(t) = d;
    tex_finish_load(t, nullptr);
    return t;
}

void* res_create() {
    void*& inst = *static_cast<void**>(sym("_ZN8Graphics16IResourceManager11m_pInstanceE"));
    if (inst) (*reinterpret_cast<void (***)(void*)>(inst))[1](inst);
    if (!g_resVtbl) {
        g_resVtbl = clone_vtable("_ZTVN8Graphics19BaseResourceManagerE");
        g_resVtbl[13] = (void*)res_load_file;
        g_resVtbl[14] = (void*)res_load_mem;
    }
    void* r = operator new(0x24);
    reinterpret_cast<ctor0_t>(sym("_ZN8Graphics19BaseResourceManagerC2Ev"))(r);
    at<int>(r, 0x20) = 2;
    at<void**>(r, 0) = g_resVtbl;
    inst = r;
    return r;
}
