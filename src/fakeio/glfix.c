/*
 * glfix — LD_PRELOAD helper for the ION 2014 cabinet's OpenGL games (32-bit): two places where
 * the cabinet's Intel driver did what the games expected and Mesa's software driver doesn't.
 *
 *  - glDrawPixels with the 3D state off. Games copy whole 2D pictures into the GL window this way
 *    (the "quit this game?" box over a black-and-white copy of the game, Trivia Whiz 2 and
 *    Rack 'Em; Mega Bowling's "Choose your lane" screen; coin-jam boxes) while depth testing or
 *    texturing from their 3D scene is still on. The cabinet's driver drew them anyway; Mesa
 *    applies the depth test and the texture as OpenGL says, and the picture comes out black.
 *  - drawing on, and reading from, the front buffer. AllegroGL draws the cabinet's 2D pictures
 *    straight onto the front buffer (Monkey Bash's loading bar and START banner over its "How to
 *    play" screen) and reads it back to blend with. With Mesa's software driver that drawing lands
 *    on a black copy of the screen, and the 3D frames swapped in afterwards don't show. So drawing
 *    meant for the front buffer goes to the back buffer instead, on top of the frame last shown
 *    (kept here at each swap, once a game has used the front buffer), and is shown with a swap
 *    when the program flushes, as the cabinet's driver showed it at once; reading the front reads
 *    that stand-in.
 *
 * Programs that don't use OpenGL never call these, so it is harmless in the rest of the cabinet.
 * Turn it off with MEGA_GLFIX=0 (scripts/loader.sh). From the Megatouch for Windows project
 * (its fpscount.so, which also counts and paces frames there).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>

static unsigned long swaps;
static int front_used, front_dirty;
static void save_frame(void);

void glXSwapBuffers(void *dpy, unsigned long drawable) {
    static void (*real)(void *, unsigned long);
    if (!real) real = (void (*)(void *, unsigned long))dlsym(RTLD_NEXT, "glXSwapBuffers");
    front_dirty = 0;
    save_frame();
    if (real) real(dpy, drawable);
    swaps++;
}

/* Saving each frame as it is shown (only in a game that has drawn onto the front buffer), and
 * painting it onto the front buffer before the first 2D drawing there after the swap. */
static void *(*p_malloc)(size_t);
static void (*g_push)(unsigned), (*g_pop)(void), (*g_cpush)(unsigned), (*g_cpop)(void), (*g_disable)(unsigned);
static void (*g_readb)(unsigned), (*g_drawb)(unsigned), (*g_geti)(unsigned, int *), (*g_store)(unsigned, int);
static void (*g_read)(int, int, int, int, unsigned, unsigned, void *), (*g_wpos)(int, int);
static void (*g_draw)(int, int, unsigned, unsigned, const void *);
static int g_ok = -1, saved_ok;
static int sv[4];
static unsigned char *saved;
static size_t saved_cap;
static unsigned long saved_at = (unsigned long)-1, painted = (unsigned long)-1;

static int gl_init(void) {
    if (g_ok >= 0) return g_ok;
    g_push = (void (*)(unsigned))dlsym(RTLD_NEXT, "glPushAttrib");
    g_pop = (void (*)(void))dlsym(RTLD_NEXT, "glPopAttrib");
    g_cpush = (void (*)(unsigned))dlsym(RTLD_NEXT, "glPushClientAttrib");
    g_cpop = (void (*)(void))dlsym(RTLD_NEXT, "glPopClientAttrib");
    g_disable = (void (*)(unsigned))dlsym(RTLD_NEXT, "glDisable");
    g_readb = (void (*)(unsigned))dlsym(RTLD_NEXT, "glReadBuffer");
    g_drawb = (void (*)(unsigned))dlsym(RTLD_NEXT, "glDrawBuffer");
    g_geti = (void (*)(unsigned, int *))dlsym(RTLD_NEXT, "glGetIntegerv");
    g_store = (void (*)(unsigned, int))dlsym(RTLD_NEXT, "glPixelStorei");
    g_read = (void (*)(int, int, int, int, unsigned, unsigned, void *))dlsym(RTLD_NEXT, "glReadPixels");
    g_draw = (void (*)(int, int, unsigned, unsigned, const void *))dlsym(RTLD_NEXT, "glDrawPixels");
    p_malloc = (void *(*)(size_t))dlsym(RTLD_NEXT, "malloc");
    void *(*gp)(const unsigned char *) = (void *(*)(const unsigned char *))dlsym(RTLD_NEXT, "glXGetProcAddressARB");
    g_wpos = (void (*)(int, int))dlsym(RTLD_NEXT, "glWindowPos2i");
    if (!g_wpos && gp) g_wpos = (void (*)(int, int))gp((const unsigned char *)"glWindowPos2i");
    g_ok = g_push && g_pop && g_cpush && g_cpop && g_disable && g_readb && g_drawb && g_geti && g_store &&
           g_read && g_draw && g_wpos && p_malloc;
    return g_ok;
}

static void plain_state(void) {
    g_push(0x000FFFFF);                 /* GL_ALL_ATTRIB_BITS */
    g_cpush(0x00000001);                /* GL_CLIENT_PIXEL_STORE_BIT */
    g_disable(0x0B71); g_disable(0x0DE1); g_disable(0x0BE2); g_disable(0x0BC0);
    g_disable(0x0B60); g_disable(0x0B90); g_disable(0x0C11);
    g_store(0x0D05, 4); g_store(0x0CF5, 4);           /* pack / unpack alignment */
    g_store(0x0D02, 0); g_store(0x0CF2, 0);           /* row length */
    g_store(0x0D04, 0); g_store(0x0CF4, 0); g_store(0x0D03, 0); g_store(0x0CF3, 0);   /* skips */
}

/* in glXSwapBuffers, before the real swap: keep the frame about to be shown */
static void save_frame(void) {
    if (!front_used || !gl_init()) return;
    int vp[4] = { 0, 0, 0, 0 };
    g_geti(0x0BA2, vp);                 /* GL_VIEWPORT: the game's window */
    if (vp[2] <= 0 || vp[3] <= 0 || vp[2] > 4096 || vp[3] > 4096) return;
    size_t need = (size_t)vp[2] * vp[3] * 4;
    if (need > saved_cap) { saved = (unsigned char *)p_malloc(need); saved_cap = saved ? need : 0; }
    if (!saved) return;
    plain_state();
    g_readb(0x0405);                    /* GL_BACK */
    g_read(vp[0], vp[1], vp[2], vp[3], 0x1908, 0x1401, saved);   /* RGBA, UNSIGNED_BYTE */
    g_cpop(); g_pop();
    sv[0] = vp[0]; sv[1] = vp[1]; sv[2] = vp[2]; sv[3] = vp[3];
    saved_at = swaps + 1;               /* (counted after the real swap) */
    saved_ok = 1;
}

/* front-buffer drawing, redirected to the back buffer and shown with a swap at the next flush */
static void (*r_swap)(void *, unsigned long);

/* the back buffer stands in for the front: it starts from the frame last shown */
static void front_base(void) {
    front_used = 1;
    if (!front_dirty && saved_ok && saved_at == swaps && painted != swaps && gl_init()) {
        painted = swaps;
        plain_state();
        g_drawb(0x0405);
        g_wpos(sv[0], sv[1]);
        g_draw(sv[2], sv[3], 0x1908, 0x1401, saved);
        g_cpop(); g_pop();
    }
}

void glDrawBuffer(unsigned b) {
    static void (*real)(unsigned);
    if (!real) real = (void (*)(unsigned))dlsym(RTLD_NEXT, "glDrawBuffer");
    if (!real) return;
    if (b != 0x0404 && b != 0x0400) { real(b); return; }   /* not GL_FRONT(_LEFT) */
    real(0x0405);                                   /* GL_BACK instead */
    front_base();
    front_dirty = 1;
}

/* reading the screen back (the 2D engine blends its pictures with what is on screen): the
 * stand-in front as well */
void glReadBuffer(unsigned b) {
    static void (*real)(unsigned);
    if (!real) real = (void (*)(unsigned))dlsym(RTLD_NEXT, "glReadBuffer");
    if (!real) return;
    if ((b == 0x0404 || b == 0x0400) && front_used) { front_base(); real(0x0405); return; }
    real(b);
}

static void show_front(void) {
    if (!front_dirty) return;
    front_dirty = 0;
    static void *(*cur_dpy)(void);
    static unsigned long (*cur_draw)(void);
    if (!cur_dpy) {
        cur_dpy = (void *(*)(void))dlsym(RTLD_NEXT, "glXGetCurrentDisplay");
        cur_draw = (unsigned long (*)(void))dlsym(RTLD_NEXT, "glXGetCurrentDrawable");
        r_swap = (void (*)(void *, unsigned long))dlsym(RTLD_NEXT, "glXSwapBuffers");
    }
    if (!cur_dpy || !cur_draw || !r_swap) return;
    void *dpy = cur_dpy();
    unsigned long d = cur_draw();
    if (!dpy || !d) return;
    save_frame();                       /* this becomes the frame on screen */
    r_swap(dpy, d);
    swaps++;
}

void glFlush(void) {
    static void (*real)(void);
    if (!real) real = (void (*)(void))dlsym(RTLD_NEXT, "glFlush");
    if (real) real();
    show_front();
}

void glFinish(void) {
    static void (*real)(void);
    if (!real) real = (void (*)(void))dlsym(RTLD_NEXT, "glFinish");
    if (real) real();
    show_front();
}

typedef void (*draw_t)(int, int, unsigned, unsigned, const void *);
void glDrawPixels(int w, int h, unsigned fmt, unsigned type, const void *px) {
    static draw_t real;
    static void (*push)(unsigned), (*pop)(void), (*disable)(unsigned);
    if (!real) {
        real = (draw_t)dlsym(RTLD_NEXT, "glDrawPixels");
        push = (void (*)(unsigned))dlsym(RTLD_NEXT, "glPushAttrib");
        pop = (void (*)(void))dlsym(RTLD_NEXT, "glPopAttrib");
        disable = (void (*)(unsigned))dlsym(RTLD_NEXT, "glDisable");
    }
    if (!real) return;
    if (!push || !pop || !disable) { real(w, h, fmt, type, px); return; }
    push(0x2000);                       /* GL_ENABLE_BIT */
    disable(0x0B71);                    /* GL_DEPTH_TEST */
    disable(0x0DE1);                    /* GL_TEXTURE_2D */
    disable(0x0BC0);                    /* GL_ALPHA_TEST */
    disable(0x0B60);                    /* GL_FOG */
    disable(0x0B90);                    /* GL_STENCIL_TEST */
    real(w, h, fmt, type, px);
    pop();
}
