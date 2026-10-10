/*
 * megaview — the loader's window: shows the nested X server (Xephyr) scaled to any size.
 *
 * Xephyr's own window is always exactly the cabinet's current resolution (640x480 menus,
 * 768x480 widescreen, 800x600 / 1280x800 games), so it cannot be resized, maximised or made
 * fullscreen. With megaview, Xephyr runs unchanged on an invisible display (Xvfb, started by
 * scripts/loader.sh); megaview copies the nested screen (MIT-SHM) about 60 times a second into an
 * SDL2 window that can be any size, redrawing only when it changed, and sends mouse and keyboard
 * back into the nested server with XTest (so the fake board's hotkeys and touches work as before).
 *
 *   megaview DISPLAY [TITLE]      DISPLAY = the nested server (e.g. :55)
 *
 * Keys: F11 or Alt+Enter fullscreen, Ctrl+Alt+S toggle aspect (keep / stretch), Ctrl+Alt+End
 * quit (exit status 42: kiosk mode stops instead of restarting). Everything else goes to the
 * cabinet. Closing the window ends the loader (scripts/loader.sh).
 * Settings: MEGAVIEW_XTST (path of a libXtst.so.6), MEGAVIEW_STRETCH=1, MEGAVIEW_SCALE (initial
 * window scale, default 2 for 640x480), MEGAVIEW_FULLSCREEN=1, MEGAVIEW_HIDE_CURSOR=1 (touchscreens),
 * MEGAVIEW_ALIVE=<file> (touched every few seconds while the picture changes: scripts/cabinet.sh
 * uses it to spot a frozen cabinet).
 *
 * Built 64-bit against toolchain/debug's SDL2 when present (GPU scaling through WSLg's d3d12
 * Mesa), else i386 against the runtime's SDL2; libX11/libXext/libXtst are loaded with dlopen.
 */
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <utime.h>

/* ---- the few Xlib types and calls used (no headers) ---- */
typedef unsigned long XID;
typedef struct {
    int width, height, xoffset, format;
    char *data;
    int byte_order, bitmap_unit, bitmap_bit_order, bitmap_pad, depth, bytes_per_line, bits_per_pixel;
    unsigned long red_mask, green_mask, blue_mask;
    void *obdata;
    void *f[6];
} XImage;
typedef struct { unsigned long shmseg; int shmid; char *shmaddr; int readOnly; } XShmSegmentInfo;
#define ZPixmap 2
#define AllPlanes (~0UL)

static void *(*XOpenDisplay_)(const char *);
static XID (*XDefaultRootWindow_)(void *);
static int (*XFlush_)(void *), (*XSync_)(void *, int);
static int (*XGetGeometry_)(void *, XID, XID *, int *, int *, unsigned *, unsigned *, unsigned *, unsigned *);
static void *(*XDefaultVisual_)(void *, int);
static XImage *(*XGetImage_)(void *, XID, int, int, unsigned, unsigned, unsigned long, int);
static int (*XDestroyImage_)(XImage *);
static unsigned char (*XKeysymToKeycode_)(void *, unsigned long);
static void *(*XSetErrorHandler_)(int (*)(void *, void *));
static int (*XShmQueryExtension_)(void *);
static XImage *(*XShmCreateImage_)(void *, void *, unsigned, int, char *, XShmSegmentInfo *, unsigned, unsigned);
static int (*XShmAttach_)(void *, XShmSegmentInfo *), (*XShmDetach_)(void *, XShmSegmentInfo *);
static int (*XShmGetImage_)(void *, XID, XImage *, int, int, unsigned long);
static int (*XTestFakeMotionEvent_)(void *, int, int, int, unsigned long);
static int (*XTestFakeButtonEvent_)(void *, unsigned, int, unsigned long);
static int (*XTestFakeKeyEvent_)(void *, unsigned, int, unsigned long);

static int load_x(void) {
    void *x = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
    void *e = dlopen("libXext.so.6", RTLD_NOW | RTLD_GLOBAL);
    const char *tp = getenv("MEGAVIEW_XTST");
    void *t = dlopen(tp && *tp ? tp : "libXtst.so.6", RTLD_NOW);
    if (!x || !e || !t) { fprintf(stderr, "[megaview] %s\n", dlerror()); return 0; }
#define L(h, n) if (!(*(void **)&n##_ = dlsym(h, #n))) { fprintf(stderr, "[megaview] no %s\n", #n); return 0; }
    L(x, XOpenDisplay) L(x, XDefaultRootWindow) L(x, XFlush) L(x, XSync)
    L(x, XGetGeometry) L(x, XDefaultVisual) L(x, XGetImage) L(x, XDestroyImage) L(x, XKeysymToKeycode) L(x, XSetErrorHandler)
    L(e, XShmQueryExtension) L(e, XShmCreateImage) L(e, XShmAttach) L(e, XShmDetach) L(e, XShmGetImage)
    L(t, XTestFakeMotionEvent) L(t, XTestFakeButtonEvent) L(t, XTestFakeKeyEvent)
#undef L
    return 1;
}

/* ---- capture ---- */
static void *nd;              /* nested display */
static XID nroot;
static XImage *img;
static XShmSegmentInfo shm;
static int use_shm, cw, ch;   /* current capture size */

static void free_capture(void) {
    if (!img) return;
    if (use_shm) { XShmDetach_(nd, &shm); XSync_(nd, 0); shmdt(shm.shmaddr); }
    img->data = use_shm ? NULL : img->data;
    XDestroyImage_(img);
    img = NULL;
}

static int setup_capture(int w, int h) {
    free_capture();
    cw = w; ch = h;
    if (!use_shm) return 1;
    img = XShmCreateImage_(nd, XDefaultVisual_(nd, 0), 24, ZPixmap, NULL, &shm, w, h);
    if (!img) { use_shm = 0; return 1; }
    shm.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * h, IPC_CREAT | 0600);
    shm.shmaddr = img->data = shmat(shm.shmid, NULL, 0);
    shm.readOnly = 0;
    if (shm.shmid < 0 || shm.shmaddr == (void *)-1 || !XShmAttach_(nd, &shm)) {
        fprintf(stderr, "[megaview] MIT-SHM unavailable, using XGetImage\n");
        use_shm = 0; img = NULL;
        return 1;
    }
    XSync_(nd, 0);
    shmctl(shm.shmid, IPC_RMID, NULL);   /* freed once both sides detach */
    return 1;
}

/* X errors are expected: when the cabinet changes resolution, a grab of the old size fails with
 * BadMatch. Xlib's default handler would exit (and closing the viewer stops the loader), so note
 * the error and re-read the screen size on the next frame instead. */
static volatile int x_error;
static int on_x_error(void *d, void *ev) { (void)d; (void)ev; x_error = 1; return 0; }

/* copy the nested screen; returns the image (32 bpp BGRX) or NULL */
static XImage *grab(void) {
    x_error = 0;
    if (use_shm) {
        int ok = XShmGetImage_(nd, nroot, img, 0, 0, AllPlanes);
        return ok && !x_error ? img : NULL;
    }
    if (img) { XDestroyImage_(img); img = NULL; }
    img = XGetImage_(nd, nroot, 0, 0, cw, ch, AllPlanes, ZPixmap);
    return x_error ? NULL : img;
}

/* ---- input ---- */
static unsigned long keysym_for(SDL_Keycode k) {
    if (k >= 32 && k < 127) return (unsigned long)k;
    switch (k) {
    case SDLK_RETURN: return 0xff0d; case SDLK_ESCAPE: return 0xff1b; case SDLK_BACKSPACE: return 0xff08;
    case SDLK_TAB: return 0xff09; case SDLK_DELETE: return 0xffff; case SDLK_INSERT: return 0xff63;
    case SDLK_HOME: return 0xff50; case SDLK_END: return 0xff57; case SDLK_PAGEUP: return 0xff55;
    case SDLK_PAGEDOWN: return 0xff56; case SDLK_LEFT: return 0xff51; case SDLK_UP: return 0xff52;
    case SDLK_RIGHT: return 0xff53; case SDLK_DOWN: return 0xff54;
    case SDLK_LSHIFT: return 0xffe1; case SDLK_RSHIFT: return 0xffe2; case SDLK_LCTRL: return 0xffe3;
    case SDLK_RCTRL: return 0xffe4; case SDLK_LALT: return 0xffe9; case SDLK_RALT: return 0xffea;
    case SDLK_KP_ENTER: return 0xff8d;
    }
    if (k >= SDLK_F1 && k <= SDLK_F12) return 0xffbe + (k - SDLK_F1);
    return 0;
}

static void send_key(SDL_Keycode k, int down) {
    unsigned long ks = keysym_for(k);
    unsigned char kc = ks ? XKeysymToKeycode_(nd, ks) : 0;
    if (kc) XTestFakeKeyEvent_(nd, kc, down, 0);
}

static int xbutton(int b) { return b == SDL_BUTTON_LEFT ? 1 : b == SDL_BUTTON_MIDDLE ? 2 : b == SDL_BUTTON_RIGHT ? 3 : 0; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: megaview DISPLAY [TITLE]\n"); return 2; }
    const char *ndisp = argv[1];
    const char *title = argc > 2 ? argv[2] : "Megatouch ION";
    if (!load_x()) return 1;

    /* the nested server */
    for (int i = 0; i < 200 && !(nd = XOpenDisplay_(ndisp)); i++) usleep(50000);
    if (!nd) { fprintf(stderr, "[megaview] cannot open %s\n", ndisp); return 1; }
    nroot = XDefaultRootWindow_(nd);
    XSetErrorHandler_(on_x_error);
    use_shm = XShmQueryExtension_(nd) && !getenv("MEGAVIEW_NOSHM");

    /* the window */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "[megaview] %s\n", SDL_GetError()); return 1; }
    XID r; int gx, gy; unsigned w = 640, h = 480, bw, dep;
    XGetGeometry_(nd, nroot, &r, &gx, &gy, &w, &h, &bw, &dep);
    int scale = getenv("MEGAVIEW_SCALE") ? atoi(getenv("MEGAVIEW_SCALE")) : (w <= 800 ? 2 : 1);
    if (scale < 1) scale = 1;
    SDL_Window *win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       w * scale, h * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "[megaview] %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fprintf(stderr, "[megaview] %s\n", SDL_GetError()); return 1; }
    XSetErrorHandler_(on_x_error);     /* again: SDL may have installed its own */
    SDL_RendererInfo ri;
    SDL_GetRendererInfo(ren, &ri);
    fprintf(stderr, "[megaview] %s -> window (%s renderer, %s)\n", ndisp, ri.name, use_shm ? "MIT-SHM" : "XGetImage");
    SDL_ShowCursor(getenv("MEGAVIEW_HIDE_CURSOR") && *getenv("MEGAVIEW_HIDE_CURSOR") == '1' ? SDL_DISABLE : SDL_ENABLE);
    const char *alive = getenv("MEGAVIEW_ALIVE");
    Uint32 alive_at = 0;
    int stretch = getenv("MEGAVIEW_STRETCH") && *getenv("MEGAVIEW_STRETCH") == '1';
    int fullscreen = getenv("MEGAVIEW_FULLSCREEN") && *getenv("MEGAVIEW_FULLSCREEN") == '1';
    if (fullscreen) SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);

    SDL_Texture *tex = NULL;
    int tw = 0, th = 0, frame = 0, redraw = 1;
    for (;;) {
        /* the cabinet changes resolution with RandR: follow the nested root's size */
        if (frame++ % 15 == 0 || !tex || x_error) {   /* a failed grab: the size changed */
            XGetGeometry_(nd, nroot, &r, &gx, &gy, &w, &h, &bw, &dep);
            if ((int)w != tw || (int)h != th) {
                if (tex) SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
                tw = w; th = h;
                setup_capture(w, h);
                redraw = 1;
            }
        }
        if (stretch) SDL_RenderSetLogicalSize(ren, 0, 0);
        else SDL_RenderSetLogicalSize(ren, tw, th);   /* letterbox, and maps mouse coordinates */

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            int ww, wh;
            switch (ev.type) {
            case SDL_QUIT:
                return 0;
            case SDL_WINDOWEVENT:
                redraw = 1;
                break;
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                int x = ev.type == SDL_MOUSEMOTION ? ev.motion.x : ev.button.x;
                int y = ev.type == SDL_MOUSEMOTION ? ev.motion.y : ev.button.y;
                if (stretch) {          /* no logical size: scale by hand */
                    SDL_GetWindowSize(win, &ww, &wh);
                    x = ww ? x * tw / ww : x;
                    y = wh ? y * th / wh : y;
                }
                if (ev.type == SDL_MOUSEMOTION && (x < 0 || y < 0 || x >= tw || y >= th)) break;  /* black bars */
                x = x < 0 ? 0 : x >= tw ? tw - 1 : x;     /* a press/release there still counts */
                y = y < 0 ? 0 : y >= th ? th - 1 : y;
                if (getenv("MEGAVIEW_DEBUG") && ev.type != SDL_MOUSEMOTION)
                    fprintf(stderr, "[megaview] button %d %s at %d,%d\n", ev.button.button,
                            ev.type == SDL_MOUSEBUTTONDOWN ? "down" : "up", x, y);
                XTestFakeMotionEvent_(nd, -1, x, y, 0);
                if (ev.type != SDL_MOUSEMOTION && xbutton(ev.button.button))
                    XTestFakeButtonEvent_(nd, xbutton(ev.button.button), ev.type == SDL_MOUSEBUTTONDOWN, 0);
                break;
            }
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                SDL_Keycode k = ev.key.keysym.sym;
                SDL_Keymod m = ev.key.keysym.mod;
                if (ev.type == SDL_KEYDOWN && (k == SDLK_F11 || (k == SDLK_RETURN && (m & KMOD_ALT)))) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    break;
                }
                if (k == SDLK_F11) break;
                if (ev.type == SDL_KEYDOWN && k == SDLK_END && (m & KMOD_CTRL) && (m & KMOD_ALT))
                    return 42;                             /* deliberate quit (kiosk: don't restart) */
                if (ev.type == SDL_KEYDOWN && k == SDLK_s && (m & KMOD_CTRL) && (m & KMOD_ALT)) {
                    stretch = !stretch;
                    redraw = 1;
                    break;
                }
                if (!ev.key.repeat) send_key(k, ev.type == SDL_KEYDOWN);
                break;
            }
            }
        }
        XFlush_(nd);

        static int stats = -1;
        static Uint64 t_grab, t_up, t_draw, nframes;
        if (stats < 0) stats = getenv("MEGAVIEW_STATS") != NULL;
        Uint64 t0 = SDL_GetPerformanceCounter();
        XImage *im = tex ? grab() : NULL;
        Uint64 t1 = SDL_GetPerformanceCounter();
        /* redraw only when the cabinet's screen or the window changed */
        static char *prev;
        static size_t prevlen;
        int changed = redraw;
        if (im && im->bits_per_pixel == 32) {
            size_t len = (size_t)im->bytes_per_line * im->height;
            if (len != prevlen) { free(prev); prev = malloc(len); prevlen = len; changed = 1; }
            if (changed || memcmp(prev, im->data, len)) {
                memcpy(prev, im->data, len);
                SDL_UpdateTexture(tex, NULL, im->data, im->bytes_per_line);
                changed = 1;
            }
        }
        Uint64 t2 = SDL_GetPerformanceCounter();
        if (changed) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            if (tex) SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            redraw = 0;
            if (alive && *alive && SDL_GetTicks() - alive_at > 5000) {   /* the picture is moving */
                alive_at = SDL_GetTicks();
                if (utime(alive, NULL) != 0) { FILE *af = fopen(alive, "w"); if (af) fclose(af); }
            }
        }
        if (stats) {
            Uint64 t3 = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency() / 1000;
            t_grab += t1 - t0; t_up += t2 - t1; t_draw += t3 - t2;
            if (++nframes == 300) {
                fprintf(stderr, "[megaview] per frame: grab %.2f ms, upload %.2f ms, draw+present %.2f ms\n",
                        (double)t_grab / hz / 300, (double)t_up / hz / 300, (double)t_draw / hz / 300);
                t_grab = t_up = t_draw = nframes = 0;
            }
        }
        /* ~60 frames a second whether or not the renderer waits for vsync */
        static Uint32 next;
        Uint32 now = SDL_GetTicks();
        if (next > now && next - now <= 17) SDL_Delay(next - now);
        next = (next > now && next - now <= 17 ? next : now) + 16;
    }
}
