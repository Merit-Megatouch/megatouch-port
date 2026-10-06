// Drawing: Graphics::BaseRenderable2D (one per sprite/text/button) and the object factory
// that hands them to the engine.

#include "backend.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Renderable2D (BaseRenderable2D: +4 std::string image name, +8 resource mgr, +0xc visible)

static void** g_rendVtbl;

static void rend_set_image(void* self, const std::string* name) { at<std::string>(self, 4) = *name; }
static void rend_set_visible(void* self, bool v) { at<bool>(self, 0xc) = v; }

static void rend_draw(void* self, const Core::Rect* src, const Core::Rect* dst, const Core::Rect* clip,
                      int frame, const Core::Color* color, float angle, Core::Vector2D* pivot,
                      Graphics::ImageResource* res) {
    if (color->a <= 0.0f || dst->w <= 0 || dst->h <= 0) return;
    Graphics::Frame* fr = res->GetFrame(frame);
    if (!fr) return;
    void* tex = res->GetTexture(fr->textureIndex);
    if (!tex) return;
    TexData* d = texdata(tex);
    if (!d) return;
    SDL_Texture* t = tex_upload(d, frame);
    if (!t) return;

    SDL_SetTextureColorMod(t, (Uint8)(fminf(color->r, 1) * 255), (Uint8)(fminf(color->g, 1) * 255), (Uint8)(fminf(color->b, 1) * 255));
    SDL_SetTextureAlphaMod(t, (Uint8)(fminf(color->a, 1) * 255));
    SDL_FRect dr{dst->x, dst->y, dst->w, dst->h};
    int flip = SDL_FLIP_NONE;
    if (src && src->w < 0) flip |= SDL_FLIP_HORIZONTAL;
    if (src && src->h < 0) flip |= SDL_FLIP_VERTICAL;
    if (clip && clip->w > 0 && clip->h > 0) {
        SDL_Rect cr{(int)clip->x, (int)clip->y, (int)clip->w, (int)clip->h};
        SDL_RenderSetClipRect(g_renderer, &cr);
    }
    if (angle != 0.0f || flip) {
        SDL_FPoint c{pivot ? pivot->x : dr.w / 2, pivot ? pivot->y : dr.h / 2};
        SDL_RenderCopyExF(g_renderer, t, nullptr, &dr, angle, &c, (SDL_RendererFlip)flip);
    } else {
        SDL_RenderCopyF(g_renderer, t, nullptr, &dr);
    }
    if (clip && clip->w > 0 && clip->h > 0) SDL_RenderSetClipRect(g_renderer, nullptr);
}

// BaseRenderable2D's destructor is pure virtual, so the derived one must be supplied.
static void rend_dtor(void* self) { reinterpret_cast<dtor_t>(sym("_ZN8Graphics16BaseRenderable2DD2Ev"))(self); }
static void rend_dtor_delete(void* self) { rend_dtor(self); operator delete(self); }

static void* rend_new() {
    if (!g_rendVtbl) {
        g_rendVtbl = clone_vtable("_ZTVN8Graphics16BaseRenderable2DE");
        g_rendVtbl[0] = (void*)rend_dtor;
        g_rendVtbl[1] = (void*)rend_dtor_delete;
        g_rendVtbl[3] = (void*)rend_set_image;
        g_rendVtbl[4] = (void*)rend_set_visible;
        g_rendVtbl[7] = (void*)rend_draw;
    }
    void* r = operator new(0x10);
    reinterpret_cast<ctor0_t>(sym("_ZN8Graphics16BaseRenderable2DC2Ev"))(r);
    at<void**>(r, 0) = g_rendVtbl;
    return r;
}

// ---------------------------------------------------------------------------
// Object factory: only needs to hand out our renderables

static void** g_factVtbl;
static int fact_xml(void*, const std::string*, const std::string*, void*) { return 0; }

void* fact_new(Graphics::Scene* scene, TextSystem::ITextSystem* text) {
    if (!g_factVtbl) {
        g_factVtbl = clone_vtable("_ZTVN8Graphics17BaseObjectFactoryE", 1);
        g_factVtbl[48] = (void*)rend_new;       // ImplementNewRenderable2D (self arg ignored)
        g_factVtbl[49] = (void*)fact_xml;       // ImplementXMLParsing
    }
    void* f = operator new(0x2c);
    reinterpret_cast<void (*)(void*, Graphics::Scene*, TextSystem::ITextSystem*)>(
        sym("_ZN8Graphics17BaseObjectFactoryC2EPNS_5SceneEPN10TextSystem11ITextSystemE"))(f, scene, text);
    at<void**>(f, 0) = g_factVtbl;
    return f;
}
