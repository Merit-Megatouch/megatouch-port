// The game device (GameDevice::BaseGameDevice): window, renderer, event pump, frame loop
// hooks, debug aids (screenshots, autoclick, frame stats). Exports CreateNewGameDevice().

#include "backend.h"
#include <SDL2/SDL_image.h>
#include <cstring>

// ---------------------------------------------------------------------------
// Game device. BaseGameDevice: +8 ResourceLocator*, +0x10 object factory,
// +0x14 std::vector<Scene*>, +0x20 input, +0x24 sound, +0x28 net utils,
// +0x2c text system.

static void** g_devVtbl;

static bool dev_impl_init(void* self, GameDevice::GameConfig* cfg) {
    void* snd = snd_new();
    at<void*>(self, 0x24) = snd;
    (*reinterpret_cast<void (***)(void*, void*)>(snd))[5](snd, at<void*>(self, 8));  // SetResourceLocator
    (*reinterpret_cast<int (***)(void*)>(snd))[2](snd);                               // Initialize

    void* net = net_new();
    at<void*>(self, 0x28) = net;

    g_logicalW = menv_int("WIDTH", 1280);
    g_logicalH = menv_int("HEIGHT", 800);
    cfg->SetGameID((xml_gameinfo::GameIds)menv_int("GAME_ID", 245));
    cfg->SetNumPlayers(1);
    cfg->SetRAMAmountMB(1024);
    cfg->SetContentRatingLevel(0);
    cfg->SetIsQuestionableContentAllowed(false);
    cfg->SetCardFanning(menv_int("CARD_FANNING", 0) != 0, 4.0f);   // operator option; Trix does not ship its art
    cfg->SetWindowWidth(g_logicalW);
    cfg->SetWindowHeight(g_logicalH);
    cfg->SetScreenWidth(g_logicalW);
    cfg->SetScreenHeight(g_logicalH);
    cfg->SetLanguage(menv("LANGUAGE") ? menv("LANGUAGE") : "english");

    Graphics::Scene* scene = at<std::vector<Graphics::Scene*>>(self, 0x14)[0];
    at<void*>(self, 0x10) = fact_new(scene, at<TextSystem::ITextSystem*>(self, 0x2c));
    at<void*>(self, 0x20) = in_create();

    if (!g_window) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        g_window = SDL_CreateWindow(menv("TITLE") ? menv("TITLE") : "Megatouch", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    g_logicalW, g_logicalH, SDL_WINDOW_RESIZABLE);
        if (!g_window) { LOG("SDL_CreateWindow: %s", SDL_GetError()); return false; }
        // TRIX_RENDERER=software|opengl|opengles2 picks the SDL renderer; TRIX_VSYNC=0 disables vsync.
        const char* want = menv("RENDERER");
        bool vsync = !menv("VSYNC") || atoi(menv("VSYNC")) != 0;
        int idx = -1;
        if (want) for (int i = 0; i < SDL_GetNumRenderDrivers(); i++) {
            SDL_RendererInfo ri;
            if (SDL_GetRenderDriverInfo(i, &ri) == 0 && strcmp(ri.name, want) == 0) idx = i;
        }
        Uint32 flags = (want && strcmp(want, "software") == 0) ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
        if (vsync) flags |= SDL_RENDERER_PRESENTVSYNC;
        g_renderer = SDL_CreateRenderer(g_window, idx, flags);
        if (!g_renderer) g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
        SDL_RendererInfo ri;
        SDL_GetRendererInfo(g_renderer, &ri);
        LOG("renderer: %s%s", ri.name, (ri.flags & SDL_RENDERER_PRESENTVSYNC) ? " (vsync)" : "");
        SDL_RenderSetLogicalSize(g_renderer, g_logicalW, g_logicalH);
    }

    // The cabinet ran 30 logic updates and 30 frames per second. TRIX_FPS=60 doubles both;
    // the engine passes the fixed step (1/rate) to the game as dt.
    if (const char* fps = menv("FPS")) {
        int r = atoi(fps);
        if (r >= 15 && r <= 240) { at<int>(self, 0x30) = r; at<int>(self, 0x68) = r; }
    }

    void* rm = res_create();
    (*reinterpret_cast<void (***)(void*, void*)>(rm))[1 + 2](rm, at<void*>(self, 8));  // SetResourceLocator
    return true;
}

// Debugging aid: TRIX_AUTOCLICK="frame:x,y;frame:x,y;..." taps the screen at given update ticks.
static void autoclick() {
    static const char* spec = menv("AUTOCLICK");
    static int tick;
    if (!spec) return;
    tick++;
    for (const char* p = spec; p && *p;) {
        int f, x, y;
        if (sscanf(p, "%d:%d,%d", &f, &x, &y) == 3) {
            if (tick == f) { g_mouseX = x; g_mouseY = y; g_mouseDown = true; }
            if (tick == f + 3) { g_mouseX = x; g_mouseY = y; g_mouseDown = false; }
        }
        p = strchr(p, ';');
        if (p) p++;
    }
}

static bool dev_impl_update(void*, float) {
    double tu = now_ms();
    struct Done { double t0; ~Done() { g_fs.updateMs += now_ms() - t0; } } done{tu};
    autoclick();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: g_quit = true; break;
        case SDL_MOUSEMOTION: g_mouseX = e.motion.x; g_mouseY = e.motion.y; break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) { g_mouseDown = true; g_mouseX = e.button.x; g_mouseY = e.button.y; }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) { g_mouseDown = false; g_mouseX = e.button.x; g_mouseY = e.button.y; }
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_F11) {
                bool fs = SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(g_window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            break;
        }
    }
    return !g_quit;
}

// TRIX_FRAME_STATS=1: one line per second with frame pacing numbers.
static void frame_stats(double gap, double renderMs, double presentMs) {
    static bool on = menv("FRAME_STATS");
    static double windowStart, maxGap, sumRender, sumPresent;
    static int frames, over34, over50;
    if (!on) return;
    double t = now_ms();
    if (!windowStart) windowStart = t;
    frames++; sumRender += renderMs; sumPresent += presentMs;
    if (gap > maxGap) maxGap = gap;
    if (gap > 34) over34++;
    if (gap > 50) over50++;
    if (t - windowStart >= 1000) {
        fprintf(stderr, "[frames] fps=%.1f maxgap=%.1fms >34ms=%d >50ms=%d render=%.1fms present=%.1fms\n",
                frames * 1000.0 / (t - windowStart), maxGap, over34, over50, sumRender / frames, sumPresent / frames);
        windowStart = t; maxGap = sumRender = sumPresent = 0; frames = over34 = over50 = 0;
    }
}

static void hitch_report(double renderMs) {
    static double last = 0, limit = menv("HITCH_MS") ? atof(menv("HITCH_MS")) : 50;
    static bool on = menv("PROFILE") || menv("HITCH_MS");
    double t = now_ms();
    double gap = last ? t - last : 0;
    last = t;
    frame_stats(gap, renderMs, g_fs.renderMs);
    if (on && gap > limit)
        fprintf(stderr, "[hitch] t=%.0fms gap=%.1fms render=%.1fms present+gl=%.1fms uploads=%d (%d new, %.1fMB, %.1fms) images=%d (%.1fms) sounds=%d (%.1fms)\n",
                t, gap, renderMs, g_fs.renderMs, g_fs.uploads, g_fs.creates, g_fs.uploadBytes / 1048576.0, g_fs.uploadMs,
                g_fs.imgLoads, g_fs.imgMs, g_fs.sndLoads, g_fs.sndMs);
    g_fs = FrameStats{};
}

static void dev_impl_render(void* self, float t) {
    double tr = now_ms();
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    auto& scenes = at<std::vector<void*>>(self, 0x14);
    for (void* s : scenes) (*reinterpret_cast<void (***)(void*, float)>(s))[3](s, t);  // Scene::Render

    // Debugging aid: TRIX_SHOT_DIR=/some/dir [TRIX_SHOT_EVERY=n] saves a PNG every n frames.
    static const char* shotDir = menv("SHOT_DIR");
    static int shotEvery = menv("SHOT_EVERY") ? atoi(menv("SHOT_EVERY")) : 60;
    static int frameNo;
    if (shotDir && shotEvery > 0 && ++frameNo % shotEvery == 0) {
        int w, h;
        SDL_GetRendererOutputSize(g_renderer, &w, &h);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (s && SDL_RenderReadPixels(g_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
            char path[512];
            snprintf(path, sizeof path, "%s/frame%05d.png", shotDir, frameNo);
            IMG_SavePNG(s, path);
        }
        if (s) SDL_FreeSurface(s);
    }
    double tp = now_ms();
    SDL_RenderPresent(g_renderer);
    g_fs.renderMs = now_ms() - tp;
    hitch_report(tp - tr);
}

static void dev_destroy(void* self) {
    reinterpret_cast<void (*)(void*)>(sym("_ZN10GameDevice14BaseGameDevice7DestroyEv"))(self);
}
static void dev_show_cursor(void*, bool show) { SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE); }
static void* dev_netlink(void*) { return netlink_new(); }
static void dev_save_screen(void*, const std::string*) {}
static void dev_dtor(void* self) {
    dev_destroy(self);
    reinterpret_cast<dtor_t>(sym("_ZN10GameDevice14BaseGameDeviceD2Ev"))(self);
}
static void dev_dtor_delete(void* self) { dev_dtor(self); operator delete(self); }

namespace GameDevice {
// The one symbol g_trix.so imports from this library.
void* CreateNewGameDevice() {
    if (!g_devVtbl) {
        g_devVtbl = clone_vtable("_ZTVN10GameDevice14BaseGameDeviceE");
        g_devVtbl[0] = (void*)dev_dtor;
        g_devVtbl[1] = (void*)dev_dtor_delete;
        g_devVtbl[4] = (void*)dev_destroy;
        g_devVtbl[7] = (void*)dev_show_cursor;
        g_devVtbl[11] = (void*)dev_netlink;
        g_devVtbl[12] = (void*)dev_save_screen;
        g_devVtbl[28] = (void*)dev_impl_init;
        g_devVtbl[29] = (void*)dev_impl_update;
        g_devVtbl[30] = (void*)dev_impl_render;
    }
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) LOG("SDL_Init: %s", SDL_GetError());
        IMG_Init(IMG_INIT_PNG);
    }
    void* d = operator new(0x118);
    reinterpret_cast<ctor0_t>(sym("_ZN10GameDevice14BaseGameDeviceC2Ev"))(d);
    at<int>(d, 0x114) = -1;
    at<void**>(d, 0) = g_devVtbl;
    return d;
}
}
