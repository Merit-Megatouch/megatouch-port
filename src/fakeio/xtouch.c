/*
 * xtouch — synthetic touches and keys on an X display (XTest), for testing the loader.
 *   xtouch click X Y [hold_ms]     press and release the left button at X,Y (screen coordinates)
 *   xtouch drag X1 Y1 X2 Y2
 *   xtouch key NAME[+NAME...] [ms]  press keys together (held ms, default 80), e.g. F9 2000
 * libX11/libXtst are loaded with dlopen (no headers needed).
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *(*pXOpenDisplay)(const char *);
static int (*pXFlush)(void *);
static int (*pXSync)(void *, int);
static unsigned long (*pXStringToKeysym)(const char *);
static unsigned char (*pXKeysymToKeycode)(void *, unsigned long);
static int (*pMotion)(void *, int, int, int, unsigned long);
static int (*pButton)(void *, unsigned, int, unsigned long);
static int (*pKey)(void *, unsigned, int, unsigned long);

int main(int argc, char **argv) {
    void *x = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL), *t = dlopen("libXtst.so.6", RTLD_NOW);
    if (!x || !t) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    pXOpenDisplay = dlsym(x, "XOpenDisplay");
    pXFlush = dlsym(x, "XFlush");
    pXSync = dlsym(x, "XSync");
    pXStringToKeysym = dlsym(x, "XStringToKeysym");
    pXKeysymToKeycode = dlsym(x, "XKeysymToKeycode");
    pMotion = dlsym(t, "XTestFakeMotionEvent");
    pButton = dlsym(t, "XTestFakeButtonEvent");
    pKey = dlsym(t, "XTestFakeKeyEvent");
    void *d = pXOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "cannot open display\n"); return 1; }
    if (argc >= 4 && !strcmp(argv[1], "click")) {
        int hold = argc > 4 ? atoi(argv[4]) : 80;
        pMotion(d, -1, atoi(argv[2]), atoi(argv[3]), 0);
        pXSync(d, 0); usleep(50000);
        pButton(d, 1, 1, 0); pXSync(d, 0); usleep(hold * 1000);
        pButton(d, 1, 0, 0);
    } else if (argc >= 6 && !strcmp(argv[1], "drag")) {
        int x1 = atoi(argv[2]), y1 = atoi(argv[3]), x2 = atoi(argv[4]), y2 = atoi(argv[5]);
        pMotion(d, -1, x1, y1, 0); pButton(d, 1, 1, 0); pXSync(d, 0);
        for (int i = 1; i <= 20; i++) {
            pMotion(d, -1, x1 + (x2 - x1) * i / 20, y1 + (y2 - y1) * i / 20, 0);
            pXSync(d, 0); usleep(15000);
        }
        pButton(d, 1, 0, 0);
    } else if (argc >= 3 && !strcmp(argv[1], "key")) {
        unsigned codes[8];
        int n = 0;
        for (char *tok = strtok(argv[2], "+"); tok && n < 8; tok = strtok(NULL, "+")) {
            unsigned long ks = pXStringToKeysym(tok);
            codes[n] = ks ? pXKeysymToKeycode(d, ks) : 0;
            if (!codes[n]) { fprintf(stderr, "unknown key %s\n", tok); return 1; }
            n++;
        }
        for (int i = 0; i < n; i++) { pKey(d, codes[i], 1, 0); pXSync(d, 0); usleep(40000); }
        usleep((argc > 3 ? atoi(argv[3]) : 80) * 1000);
        for (int i = n - 1; i >= 0; i--) { pKey(d, codes[i], 0, 0); pXSync(d, 0); usleep(20000); }
    } else {
        fprintf(stderr, "usage: xtouch click X Y [ms] | drag X1 Y1 X2 Y2 | key NAME[+NAME]\n");
        return 2;
    }
    pXFlush(d);
    pXSync(d, 0);
    return 0;
}
