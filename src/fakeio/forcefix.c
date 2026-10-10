/*
 * forcefix — small fixes the Force loader (2004–2011 software) needs on our nested X server.
 *
 * The Force loader switches resolutions through the XFree86-VidMode extension, which a real
 * Force's XFree86 had and Xephyr doesn't. `SwitchVideoModeInternal` calls
 * XF86VidModeGetAllModeLines without checking the result, so with the extension missing it walks
 * an uninitialised mode list and crashes at boot. These stand-ins (preloaded, so they win over
 * libXxf86vm) report "no other modes" and accept mode switches; Allegro's own use is unaffected
 * because it asks XF86VidModeQueryExtension first, which still says the extension is missing.
 * Resolution changes reach the nested X server through Allegro's window instead, as on ION.
 *
 * Touchscreen: the Force's MicroTouch serial controller was run by the TouchWare kernel driver,
 * whose control port /dev/twscreen1..3 passes the controller's serial protocol through; the
 * loader shows "Touch Screen Controller Error" and stops when none opens. Here an open of one
 * gives one end of a socket pair, and a small fake controller answers on the other end:
 *   - commands are SOH <text> CR; the answer is SOH "0" CR (OK);
 *   - GP1 (get parameter block 1, where the loader keeps its calibration): SOH "A" CR,
 *     SOH <52 bytes> CR, SOH "0" CR;
 *   - SP1 (set it): SOH "A" CR, then the loader writes 52 raw bytes, then SOH "0" CR. The block
 *     is kept in /var/merit/fakeio/microtouch-block1.bin;
 *   - CX (calibrate: the controller reports each of two targets touched): OK, then both targets
 *     at once, so the first-boot calibration goes straight to its "MOVE CURSOR" check.
 * The coordinates themselves don't come from here: touches reach the loader as X pointer
 * events, as from the driver's X input.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAXFD 1024
#define BLOCK1 52
#define BLOCK1_FILE "/var/merit/fakeio/microtouch-block1.bin"

static struct tw {
    int peer;                   /* the controller's end of the socket pair, or 0 */
    int len;                    /* bytes in cmd */
    int want_block;             /* after SP1: raw block bytes still to come */
    unsigned char cmd[128];
} tw[MAXFD];

static ssize_t (*real_write)(int, const void *, size_t);

static void reply(struct tw *t, const char *s) {
    unsigned char b[64];
    int n = snprintf((char *)b, sizeof b, "\001%s\r", s);
    real_write(t->peer, b, n);
}

static void load_block1(unsigned char *b) {
    memset(b, '0', BLOCK1);     /* never CR: GP1's answer ends at the first CR */
    FILE *f = fopen(BLOCK1_FILE, "rb");
    if (f) {
        if (fread(b, 1, BLOCK1, f) != BLOCK1) memset(b, '0', BLOCK1);
        fclose(f);
    }
}

static void command(struct tw *t, const char *c) {
    if (!strcmp(c, "GP1")) {
        unsigned char b[BLOCK1 + 2];
        reply(t, "A");
        b[0] = 1;
        load_block1(b + 1);
        b[BLOCK1 + 1] = '\r';
        real_write(t->peer, b, sizeof b);
        reply(t, "0");
    } else if (!strcmp(c, "SP1")) {
        reply(t, "A");
        t->want_block = BLOCK1;
        t->len = 0;
    } else if (!strcmp(c, "CX")) {
        reply(t, "0");
        reply(t, "1");          /* first target touched */
        reply(t, "1");          /* second target touched */
    } else {
        reply(t, "0");
    }
}

/* bytes the loader wrote to the controller */
static void controller_input(struct tw *t, const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (t->want_block) {
            t->cmd[t->len++] = p[i];
            if (--t->want_block == 0) {
                FILE *f = fopen(BLOCK1_FILE, "wb");
                if (f) { fwrite(t->cmd, 1, BLOCK1, f); fclose(f); }
                t->len = 0;
                reply(t, "0");
            }
        } else if (p[i] == 1) {
            t->len = 0;
        } else if (p[i] == '\r') {
            t->cmd[t->len] = 0;
            command(t, (const char *)t->cmd);
            t->len = 0;
        } else if (t->len < (int)sizeof t->cmd - 1) {
            t->cmd[t->len++] = p[i];
        }
    }
}

static int tw_open(int flags) {
    int sv[2];
    if (!real_write) real_write = (ssize_t (*)(int, const void *, size_t))dlsym(RTLD_NEXT, "write");
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) return -1;
    if (sv[0] >= MAXFD) { close(sv[0]); close(sv[1]); errno = EMFILE; return -1; }
    if (flags & O_NONBLOCK) fcntl(sv[0], F_SETFL, O_NONBLOCK);
    memset(&tw[sv[0]], 0, sizeof tw[0]);
    tw[sv[0]].peer = sv[1];
    return sv[0];
}

ssize_t write(int fd, const void *buf, size_t n) {
    if (!real_write) real_write = (ssize_t (*)(int, const void *, size_t))dlsym(RTLD_NEXT, "write");
    if (fd >= 0 && fd < MAXFD && tw[fd].peer) {
        controller_input(&tw[fd], buf, n);
        return n;
    }
    return real_write(fd, buf, n);
}

#define WRAP_OPEN(name)                                                          \
    int name(const char *path, int flags, ...) {                                 \
        static int (*real)(const char *, int, ...);                              \
        if (!real) real = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, #name); \
        if (!strncmp(path, "/dev/twscreen", 13)) return tw_open(flags);          \
        mode_t mode = 0;                                                         \
        if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); } \
        return real(path, flags, mode);                                          \
    }
WRAP_OPEN(open)
WRAP_OPEN(open64)

int close(int fd) {
    static int (*real)(int);
    if (!real) real = (int (*)(int))dlsym(RTLD_NEXT, "close");
    if (fd >= 0 && fd < MAXFD && tw[fd].peer) {
        real(tw[fd].peer);
        tw[fd].peer = 0;
    }
    return real(fd);
}

int ioctl(int fd, unsigned long request, ...) {
    static int (*real)(int, unsigned long, ...);
    if (!real) real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (fd >= 0 && fd < MAXFD && tw[fd].peer) return 0;     /* TouchWare driver ioctls */
    return real(fd, request, arg);
}

/* Screen size. The Force runs Allegro's windowed driver here (xinit-force.sh: --videomode W), and
 * Allegro resizes its window for each mode (640x480, 800x600 for calibration and Hi-Res games).
 * The nested X server's root follows with RandR, as the ION loader does itself, so the window
 * always fills the screen. Sizes not in Xephyr's RandR list are left alone. */
typedef struct { int width, height, mwidth, mheight; } RRSize;     /* XRRScreenSize */

static void follow_size(void *dpy, int w, int h) {
    static void *(*get_info)(void *, unsigned long);
    static RRSize *(*sizes)(void *, int *);
    static int (*set_config)(void *, void *, unsigned long, int, unsigned short, unsigned long);
    static void (*free_info)(void *);
    static unsigned long (*root_of)(void *, int);
    static int (*default_screen)(void *);
    static int tried;
    if (!tried) {
        tried = 1;
        void *x = dlopen("libXrandr.so.2", RTLD_NOW | RTLD_GLOBAL);
        if (x) {
            get_info = (void *(*)(void *, unsigned long))dlsym(x, "XRRGetScreenInfo");
            sizes = (RRSize *(*)(void *, int *))dlsym(x, "XRRConfigSizes");
            set_config = (int (*)(void *, void *, unsigned long, int, unsigned short, unsigned long))
                dlsym(x, "XRRSetScreenConfig");
            free_info = (void (*)(void *))dlsym(x, "XRRFreeScreenConfigInfo");
        }
        root_of = (unsigned long (*)(void *, int))dlsym(RTLD_DEFAULT, "XRootWindow");
        default_screen = (int (*)(void *))dlsym(RTLD_DEFAULT, "XDefaultScreen");
    }
    if (!get_info || !sizes || !set_config || !free_info || !root_of || !default_screen) return;
    unsigned long root = root_of(dpy, default_screen(dpy));
    void *conf = get_info(dpy, root);
    if (!conf) return;
    int n = 0;
    RRSize *s = sizes(conf, &n);
    for (int i = 0; i < n; i++)
        if (s[i].width == w && s[i].height == h) {
            set_config(dpy, conf, root, i, 1 /* RR_Rotate_0 */, 0 /* CurrentTime */);
            break;
        }
    free_info(conf);
}

int XResizeWindow(void *dpy, unsigned long win, unsigned int w, unsigned int h) {
    static int (*real)(void *, unsigned long, unsigned int, unsigned int);
    if (!real) real = (int (*)(void *, unsigned long, unsigned int, unsigned int))dlsym(RTLD_NEXT, "XResizeWindow");
    int r = real(dpy, win, w, h);
    if (w >= 640 && h >= 480) follow_size(dpy, (int)w, (int)h);
    return r;
}

typedef struct {                /* XF86VidModeModeLine (xf86vmode.h), 32-bit */
    unsigned short hdisplay, hsyncstart, hsyncend, htotal, hskew;
    unsigned short vdisplay, vsyncstart, vsyncend, vtotal;
    unsigned int flags;
    int privsize;
    void *c_private;
} ModeLine;

int XF86VidModeGetModeLine(void *dpy, int screen, int *dotclock, ModeLine *line) {
    (void)dpy; (void)screen;
    if (dotclock) *dotclock = 0;
    if (line) memset(line, 0, sizeof *line);
    return 1;
}

int XF86VidModeGetAllModeLines(void *dpy, int screen, int *count, void ***modes) {
    (void)dpy; (void)screen;
    if (count) *count = 0;
    if (modes) *modes = 0;
    return 1;
}

int XF86VidModeSwitchToMode(void *dpy, int screen, void *mode) {
    (void)dpy; (void)screen; (void)mode;
    return 1;
}

int XF86VidModeSetViewPort(void *dpy, int screen, int x, int y) {
    (void)dpy; (void)screen; (void)x; (void)y;
    return 1;
}
