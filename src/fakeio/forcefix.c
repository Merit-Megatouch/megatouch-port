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
 *   - NM (name): "Merit SPR", the name the 2005 software expects of a Force's controller;
 *   - CX (calibrate: the controller reports each of two targets touched): OK, then both targets
 *     at once, so the first-boot calibration goes straight to its "MOVE CURSOR" check.
 * MEGA_FORCEFIX_TRACE=1 logs the port opens and every command.
 * The coordinates themselves don't come from here: touches reach the loader as X pointer
 * events, as from the driver's X input.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
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

static int trace = -1;
static void tracef(const char *fmt, ...) {
    if (trace < 0) trace = getenv("MEGA_FORCEFIX_TRACE") && *getenv("MEGA_FORCEFIX_TRACE") == '1';
    if (!trace) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[forcefix] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void command(struct tw *t, const char *c) {
    tracef("touch controller command \"%s\"", c);
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
    } else if (!strcmp(c, "NM")) {
        /* name: the 2005 software (Maxx Crown) on a Force requires Merit's own controller here
         * (Is_H2H_Microtouch: the answer must start "Merit SPR"), or it stops with "Touch Screen
         * controller not responding" */
        reply(t, "Merit SPR");
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
    tracef("touch controller port opened (flags %#x)", flags);
    if (!real_write) real_write = (ssize_t (*)(int, const void *, size_t))dlsym(RTLD_NEXT, "write");
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) return -1;
    if (sv[0] >= MAXFD) { close(sv[0]); close(sv[1]); errno = EMFILE; return -1; }
    if (flags & O_NONBLOCK) fcntl(sv[0], F_SETFL, O_NONBLOCK);
    memset(&tw[sv[0]], 0, sizeof tw[0]);
    tw[sv[0]].peer = sv[1];
    return sv[0];
}

ssize_t read(int fd, void *buf, size_t n) {
    static ssize_t (*real)(int, void *, size_t);
    if (!real) real = (ssize_t (*)(int, void *, size_t))dlsym(RTLD_NEXT, "read");
    ssize_t r = real(fd, buf, n);
    if (fd >= 0 && fd < MAXFD && tw[fd].peer)
        tracef("touch controller read %zd byte(s)%s%.*s", r, r > 1 ? ": " : "", r > 1 ? (int)r - 1 : 0, (char *)buf + 1);
    return r;
}

ssize_t write(int fd, const void *buf, size_t n) {
    if (!real_write) real_write = (ssize_t (*)(int, const void *, size_t))dlsym(RTLD_NEXT, "write");
    if (fd >= 0 && fd < MAXFD && tw[fd].peer) {
        controller_input(&tw[fd], buf, n);
        return n;
    }
    return real_write(fd, buf, n);
}

/* /dev/rtc: once the network is up the Force syncs its clock (SetHWClock): it sets the system
 * time (refused in the sandbox, which keeps the PC's time) and then writes the hardware clock
 * through /dev/rtc without checking the open, and its debug close() aborts on -1. The open gives
 * /dev/null here and the clock ioctls on it succeed. */
static char is_rtc[MAXFD];

/* /proc/pci: Linux 2.4's text PCI listing, which the 2005 software (Maxx Crown) reads for its
 * platform: an Intel 810 with a CMI8738 is a Force. Modern kernels have no /proc/pci, and a file
 * can't be mounted into procfs, so the path leads to our listing instead. */
#define PROC_PCI "/opt/fakeio/proc-pci.force810"
static const char *pci_path(const char *path) {
    return path && !strcmp(path, "/proc/pci") ? PROC_PCI : path;
}

FILE *fopen(const char *path, const char *mode) {
    static FILE *(*real)(const char *, const char *);
    if (!real) real = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen");
    return real(pci_path(path), mode);
}
FILE *fopen64(const char *path, const char *mode) {
    static FILE *(*real)(const char *, const char *);
    if (!real) real = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen64");
    return real(pci_path(path), mode);
}
/* the cabinet's own exists() (libamlx) is asked first */
int exists(const char *path) {
    static int (*real)(const char *);
    if (!real) real = (int (*)(const char *))dlsym(RTLD_NEXT, "exists");
    if (path && !strcmp(path, "/proc/pci")) return 1;
    return real ? real(path) : access(path, F_OK) == 0;
}

#define WRAP_OPEN(name)                                                          \
    int name(const char *path, int flags, ...) {                                 \
        static int (*real)(const char *, int, ...);                              \
        if (!real) real = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, #name); \
        path = pci_path(path);                                                   \
        if (!strncmp(path, "/dev/twscreen", 13)) return tw_open(flags);          \
        if (!strcmp(path, "/dev/rtc") || !strcmp(path, "/dev/rtc0")) {           \
            int fd = real("/dev/null", O_RDWR | O_CLOEXEC);                      \
            if (fd >= 0 && fd < MAXFD) is_rtc[fd] = 1;                           \
            return fd;                                                           \
        }                                                                        \
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
        tracef("touch controller port closed");
        real(tw[fd].peer);
        tw[fd].peer = 0;
    }
    if (fd >= 0 && fd < MAXFD) is_rtc[fd] = 0;
    return real(fd);
}

/* Routes. The Force's DHCP client (dhcpcd 1.3.22, inheriting this preload) adds the default route
 * with SIOCADDRT on its raw packet socket. Today's 64-bit kernels only translate a 32-bit
 * program's route ioctls on IPv4 sockets ("Inappropriate ioctl for device" on a packet socket),
 * so the cabinet got an address but no gateway, and no Internet. Retried on an IPv4 socket. */
#define SIOCADDRT_ 0x890B
#define SIOCDELRT_ 0x890C

int ioctl(int fd, unsigned long request, ...) {
    static int (*real)(int, unsigned long, ...);
    if (!real) real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (fd >= 0 && fd < MAXFD && tw[fd].peer) return 0;     /* TouchWare driver ioctls */
    if (fd >= 0 && fd < MAXFD && is_rtc[fd]) {               /* RTC_SET_TIME and friends */
        if (request == 0x80247009 && arg) {                  /* RTC_RD_TIME: the PC's clock (UTC) */
            time_t now = time(0);
            struct tm t;
            gmtime_r(&now, &t);
            memcpy(arg, &t, 9 * sizeof(int));                /* struct rtc_time = tm's first 9 */
        }
        return 0;
    }
    int r = real(fd, request, arg);
    if (r < 0 && errno == ENOTTY && (request == SIOCADDRT_ || request == SIOCDELRT_)) {
        int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (s >= 0) {
            r = real(s, request, arg);
            int e = errno;
            close(s);
            errno = e;
        }
    }
    return r;
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

/* Failed connects. The Force's MegaNet messenger uses Loudmouth 1.0 (XMPP), whose connect callback
 * accepts exactly "error" or exactly "writable" and otherwise aborts the loader ("lm-connection.c:
 * should not be reached"). Linux reports a refused or failed non-blocking connect as writable and
 * error (and hang-up) together, so for sockets with an error only the error is reported: Loudmouth then takes
 * its "connection failed" path. Programs read the error with SO_ERROR either way. */
int poll(struct pollfd *fds, nfds_t n, int timeout) {
    static int (*real)(struct pollfd *, nfds_t, int);
    if (!real) real = (int (*)(struct pollfd *, nfds_t, int))dlsym(RTLD_NEXT, "poll");
    int r = real(fds, n, timeout);
    for (nfds_t i = 0; r > 0 && i < n; i++) {
        struct stat st;
        if ((fds[i].revents & POLLERR) && (fds[i].revents & (POLLOUT | POLLHUP)) &&
            fstat(fds[i].fd, &st) == 0 && S_ISSOCK(st.st_mode))
            fds[i].revents &= ~(POLLOUT | POLLHUP);
    }
    return r;
}

/* The CPU. The Force had an Intel Celeron (family 6); the 2005 software checks Allegro's
 * cpu_family for it (ForceChk1, which also decides which cabinet code the security key must
 * carry), so on an AMD or a newer Intel PC it would take the cabinet for something else.
 * Allegro's check_cpu (CPUID) runs as usual and the result is then made the Force's. */
void check_cpu(void) {
    static void (*real)(void);
    if (!real) real = (void (*)(void))dlsym(RTLD_NEXT, "check_cpu");
    if (real) real();
    int *family = (int *)dlsym(RTLD_DEFAULT, "cpu_family");
    int *model = (int *)dlsym(RTLD_DEFAULT, "cpu_model");
    char *vendor = (char *)dlsym(RTLD_DEFAULT, "cpu_vendor");
    if (family) *family = 6;
    if (model) *model = 8;                       /* Celeron (Coppermine) */
    if (vendor) strcpy(vendor, "GenuineIntel");
}

