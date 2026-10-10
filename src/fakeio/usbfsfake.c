/*
 * usbfsfake — the fake I/O board for loaders that use Linux usbfs directly (Force, 2004–2011).
 *
 * The ION loader reaches the board through libusb, which our fake board (fakeio.c, built as
 * libusb-1.0.so.0) replaces. The Force loader has its own small libusb-0.1-style layer instead
 * (usb_open, usb_control_msg …): it reads /proc/bus/usb/devices to find the board, opens
 * /proc/bus/usb/BBB/DDD, reads the 18-byte device descriptor and sends every board command
 * as ioctl(USBDEVFS_CONTROL). This preload answers those: opening the two paths gives in-memory
 * files (the listing names one board, 04b4:6473 FX1, on bus 1 as device 2), and control
 * requests on the device go to the fake board's libusb_control_transfer, so both loaders share
 * one board (coins, keys, EEPROM, events). Other usbfs ioctls (set configuration, claim
 * interface …) succeed without doing anything.
 *
 * MEGA_USBFS_TRACE=1 logs every request.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#define USBFS "/proc/bus/usb"
#define USBDEVFS_CONTROL 0xc0105500u
#define DEVICE_PATH USBFS "/001/002"
#define MAXFD 1024

static const unsigned char descriptor[18] = {
    0x12, 0x01, 0x10, 0x01, 0xff, 0xff, 0xff, 0x40,   /* USB 1.1, vendor class, 64-byte ep0 */
    0xb4, 0x04, 0x73, 0x64, 0x01, 0x00,               /* 04b4:6473, rev 0.01 */
    0x00, 0x00, 0x00, 0x01,                           /* no strings, one configuration */
};
static const char devices[] =
    "T:  Bus=01 Lev=00 Prnt=00 Port=00 Cnt=00 Dev#=  1 Spd=12  MxCh= 2\n"
    "P:  Vendor=0000 ProdID=0000 Rev= 2.04\n"
    "\n"
    "T:  Bus=01 Lev=01 Prnt=01 Port=00 Cnt=01 Dev#=  2 Spd=12  MxCh= 0\n"
    "D:  Ver= 1.10 Cls=ff(vend.) Sub=ff Prot=ff MxPS=64 #Cfgs=  1\n"
    "P:  Vendor=04b4 ProdID=6473 Rev= 0.01\n";

static char is_board[MAXFD];
static int trace = -1;

static void tracef(const char *fmt, ...) {
    if (trace < 0) trace = getenv("MEGA_USBFS_TRACE") && *getenv("MEGA_USBFS_TRACE") == '1';
    if (!trace) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[usbfs] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* an in-memory file holding data */
static int memfile(const char *name, const void *data, size_t len) {
    int fd = (int)syscall(SYS_memfd_create, name, 0);
    if (fd < 0) return -1;
    if (write(fd, data, len) != (ssize_t)len) { close(fd); return -1; }
    lseek(fd, 0, SEEK_SET);
    return fd;
}

static int fake_open(const char *path) {
    if (!strcmp(path, USBFS "/devices")) return memfile("usb-devices", devices, sizeof devices - 1);
    if (!strcmp(path, DEVICE_PATH)) {
        int fd = memfile("usb-board", descriptor, sizeof descriptor);
        if (fd >= 0 && fd < MAXFD) is_board[fd] = 1;
        tracef("board opened (fd %d)", fd);
        return fd;
    }
    errno = ENOENT;
    return -1;
}

#define WRAP_OPEN(name)                                                          \
    int name(const char *path, int flags, ...) {                                 \
        static int (*real)(const char *, int, ...);                              \
        if (!real) real = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, #name); \
        if (!strncmp(path, USBFS, sizeof USBFS - 1)) return fake_open(path);         \
        mode_t mode = 0;                                                         \
        if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); } \
        return real(path, flags, mode);                                          \
    }
WRAP_OPEN(open)
WRAP_OPEN(open64)

int close(int fd) {
    static int (*real)(int);
    if (!real) real = (int (*)(int))dlsym(RTLD_NEXT, "close");
    if (fd >= 0 && fd < MAXFD) is_board[fd] = 0;
    return real(fd);
}

/* the fake board (libusb-1.0.so.0 from fakeio.c) */
static int (*board_control)(void *, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char *, uint16_t, unsigned int);

static int board_ready(void) {
    if (board_control) return 1;
    void *h = dlopen("/opt/fakeio/libusb-1.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "[usbfs] no fake board: %s\n", dlerror()); return 0; }
    int (*init)(void *) = (int (*)(void *))dlsym(h, "libusb_init");
    board_control = (int (*)(void *, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char *, uint16_t, unsigned int))
        dlsym(h, "libusb_control_transfer");
    if (init) init(NULL);
    return board_control != NULL;
}

struct usbdevfs_ctrl {          /* struct usbdevfs_ctrltransfer, 32-bit */
    uint8_t type, request;
    uint16_t value, index, length;
    uint32_t timeout;
    void *data;
};

int ioctl(int fd, unsigned long request, ...) {
    static int (*real)(int, unsigned long, ...);
    if (!real) real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (fd < 0 || fd >= MAXFD || !is_board[fd]) return real(fd, request, arg);
    if ((uint32_t)request == USBDEVFS_CONTROL) {
        struct usbdevfs_ctrl *c = arg;
        if (!board_ready()) { errno = ENODEV; return -1; }
        int r = board_control(NULL, c->type, c->request, c->value, c->index, c->data, c->length, c->timeout);
        tracef("control %02x %02x value %04x index %04x len %u -> %d", c->type, c->request, c->value,
               c->index, c->length, r);
        if (r < 0) { errno = EPIPE; return -1; }
        return r;
    }
    tracef("ioctl %#lx (ignored)", request);
    return 0;
}
