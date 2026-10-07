/*
 * Fake libTwDrvFifo.so — the client library of 3M's MicroTouch "TouchWare" driver daemon.
 *
 * The loader refuses to start unless libtouchscreen can open a MicroTouch controller through
 * this library (CheckTouchScreen → MicroTouch::open). Touch positions do not come through here:
 * on the cabinet the TouchWare X input driver turned touches into pointer events, which the
 * loader reads through Allegro's mouse — in the sandbox the X mouse does the same job.
 * So this fake only answers the control channel of one USB controller:
 *   ioctl 2  list devices        → device 1
 *   ioctl 9  hardware info       → "USB"
 *   write: USB setup packets wrapped by libtouchscreen (microtouch_usb.cpp)
 *     40 07 ..  reset              c0 06 .. 14 00  status → 20 bytes, [0]=6, [3]=status
 *     40 04 ..  calibrate          c0 02 11 .. 21  read block 1 → 33 bytes, [0]=4, data at +3
 *     40 03 11 .. 1e 00 + 30 bytes write block 1 (calibration data)
 * Status 4 = command complete. Calibration reports each target as touched immediately.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char reply[64];
static int reply_len;
static unsigned char block1[30];
static int status = 4, cal_phase = -1;

static void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[twfake] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

int TwDrvFifo_open(int dev, int flags) {
    (void)flags;
    return 100 + dev;
}

int TwDrvFifo_close(int fd) { (void)fd; return 0; }
int TwDrvFifo_SetAsync(int fd, int on) { (void)fd; (void)on; return 0; }

int TwDrvFifo_ioctl(int fd, int cmd, int a, int b, void *buf, unsigned len) {
    (void)fd; (void)a; (void)b;
    uint16_t *w = buf;
    switch (cmd) {
    case 2:                                     /* device list: [size][bytes of ids][ids...] */
        if (len < 6) return -1;
        w[0] = 6; w[1] = 2; w[2] = 1;
        return 6;
    case 9:                                     /* hardware info: string at byte offset w[0] */
        if (len < 8) return -1;
        memset(buf, 0, len);
        w[0] = 4;
        memcpy((char *)buf + 4, "USB", 4);
        return 8;
    default:
        logf_("ioctl %d ignored", cmd);
        return 0;
    }
}

static void queue_status(void) {
    memset(reply, 0, 20);
    reply[0] = 0x06;
    if (cal_phase >= 0) {                       /* calibration: targets 1..2 touched, then done */
        reply[3] = (unsigned char)(cal_phase < 3 ? ++cal_phase : 4);
        if (reply[3] == 4) cal_phase = -1;
    } else {
        reply[3] = (unsigned char)status;
    }
    reply_len = 20;
}

int TwDrvFifo_write(int fd, const void *buf, unsigned len) {
    (void)fd;
    const unsigned char *p = buf;
    if (len < 8) return (int)len;
    if (p[0] == 0x40 && p[1] == 0x07) { status = 4; cal_phase = -1; logf_("reset"); }
    else if (p[0] == 0x40 && p[1] == 0x04) { cal_phase = 0; logf_("calibrate"); }
    else if (p[0] == 0xC0 && p[1] == 0x06) queue_status();
    else if (p[0] == 0xC0 && p[1] == 0x02) {
        memset(reply, 0, 33);
        reply[0] = 0x04; reply[1] = 0x11; reply[2] = 0x1E;
        memcpy(reply + 3, block1, 30);
        reply_len = 33;
    } else if (p[0] == 0x40 && p[1] == 0x03 && len >= 38) {
        memcpy(block1, p + 8, 30);
        status = 4;
    } else logf_("unknown packet %02x %02x", p[0], p[1]);
    return (int)len;
}

int TwDrvFifo_read(int fd, void *buf, unsigned len) {
    (void)fd;
    if (!reply_len) return 0;
    int n = reply_len < (int)len ? reply_len : (int)len;
    memcpy(buf, reply, n);
    reply_len = 0;
    return n;
}
