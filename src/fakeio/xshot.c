/*
 * xshot — save an X window as PNG (no Xlib headers needed; libX11 is loaded with dlopen).
 *   xshot out.png            the largest mapped top-level window (rootless Xwayland has no root image)
 *   xshot out.png 0x1234567  a specific window
 *   xshot -l                 list top-level windows
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef unsigned long XID;
typedef struct {
    int width, height, xoffset, format;
    char *data;
    int byte_order, bitmap_unit, bitmap_bit_order, bitmap_pad, depth, bytes_per_line, bits_per_pixel;
    unsigned long red_mask, green_mask, blue_mask;
} XImage;
typedef struct {
    int x, y, width, height, border_width, depth;
    void *visual;
    XID root;
    int c_class, bit_gravity, win_gravity, backing_store;
    unsigned long backing_planes, backing_pixel;
    int save_under;
    XID colormap;
    int map_installed, map_state;
    long all_event_masks, your_event_mask, do_not_propagate_mask;
    int override_redirect;
    void *screen;
} XWindowAttributes;

static void *(*pXOpenDisplay)(const char *);
static XID (*pXDefaultRootWindow)(void *);
static int (*pXQueryTree)(void *, XID, XID *, XID *, XID **, unsigned *);
static int (*pXGetWindowAttributes)(void *, XID, XWindowAttributes *);
static XImage *(*pXGetImage)(void *, XID, int, int, unsigned, unsigned, unsigned long, int);
static int (*pXFetchName)(void *, XID, char **);

static void put32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len) {
    unsigned char hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc32(0, hdr + 4, 4);
    crc = crc32(crc, data, len);
    unsigned char c[4];
    put32(c, crc);
    fwrite(c, 1, 4, f);
}

static int save_png(const char *path, XImage *im) {
    int w = im->width, h = im->height;
    size_t raw_len = (size_t)(w * 3 + 1) * h;
    unsigned char *raw = malloc(raw_len), *p = raw;
    for (int y = 0; y < h; y++) {
        *p++ = 0;
        for (int x = 0; x < w; x++) {
            uint32_t px = *(uint32_t *)(im->data + y * im->bytes_per_line + x * (im->bits_per_pixel / 8));
            *p++ = px >> 16; *p++ = px >> 8; *p++ = px;
        }
    }
    uLongf zlen = compressBound(raw_len);
    unsigned char *z = malloc(zlen);
    compress2(z, &zlen, raw, raw_len, 6);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ihdr[13];
    put32(ihdr, w); put32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, zlen);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    void *x = dlopen("libX11.so.6", RTLD_NOW);
    if (!x) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    pXOpenDisplay = dlsym(x, "XOpenDisplay");
    pXDefaultRootWindow = dlsym(x, "XDefaultRootWindow");
    pXQueryTree = dlsym(x, "XQueryTree");
    pXGetWindowAttributes = dlsym(x, "XGetWindowAttributes");
    pXGetImage = dlsym(x, "XGetImage");
    pXFetchName = dlsym(x, "XFetchName");
    void *d = pXOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "cannot open display\n"); return 1; }
    XID root = pXDefaultRootWindow(d), r, parent, *kids = NULL, best = 0;
    unsigned n = 0;
    long best_area = 0;
    pXQueryTree(d, root, &r, &parent, &kids, &n);
    int list = argc > 1 && !strcmp(argv[1], "-l");
    for (unsigned i = 0; i < n; i++) {
        XWindowAttributes a;
        if (!pXGetWindowAttributes(d, kids[i], &a)) continue;
        char *name = NULL;
        pXFetchName(d, kids[i], &name);
        if (list) printf("%#lx %dx%d+%d+%d %s %s\n", kids[i], a.width, a.height, a.x, a.y,
                         a.map_state == 2 ? "mapped" : "unmapped", name ? name : "");
        if (a.map_state == 2 && (long)a.width * a.height > best_area) { best = kids[i]; best_area = (long)a.width * a.height; }
    }
    if (list) return 0;
    if (argc < 2) { fprintf(stderr, "usage: xshot out.png [window] | -l\n"); return 2; }
    if (argc > 2) best = strtoul(argv[2], NULL, 0);
    if (!best) { fprintf(stderr, "no mapped window\n"); return 1; }
    XWindowAttributes a;
    pXGetWindowAttributes(d, best, &a);
    XImage *im = pXGetImage(d, best, 0, 0, a.width, a.height, ~0UL, 2 /* ZPixmap */);
    if (!im) { fprintf(stderr, "XGetImage failed\n"); return 1; }
    printf("window %#lx %dx%d depth %d\n", best, im->width, im->height, im->depth);
    return save_png(argv[1], im);
}
