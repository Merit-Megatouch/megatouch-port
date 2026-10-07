/*
 * fakeio — a stand-in for the cabinet's USB I/O board, built as libusb-1.0.so.0.
 *
 * The loader drives an EZ-USB FX1 board through libusb (docs/reference/io-board.md). This
 * library answers in its place: one board (04b4:6473), 64 KB of board RAM, the 0xA0 RAM
 * requests and the 0xA1 command set. Persistent state lives in files under MEGAIO_DIR
 * (default /var/merit/fakeio):
 *   eeprom.bin   8 KB board EEPROM (serial number, operator PIN …); created with the cabinet's
 *                serial number (MEGAIO_SERIAL overrides) the first time
 *   key.bin      optional security-key image: 8 x 1 KB blocks as stored on the iButton
 *   login/<id>   240-byte records of front-reader keys (fobs, player keys)
 *   ctl          shared control block written by `megaio` (coins, switches, fob touches)
 * Log: every unusual request goes to stderr with a "[fakeio]" prefix (MEGAIO_TRACE=1: all).
 *
 * Cabinet hardware on the keyboard (read with XQueryKeymap at every I/O poll, so it works
 * whichever window has focus on the loader's display):
 *   F1 SETUP button   F2 CALIBRATE button   F5-F8 coin into channel 1-4
 *   F9 hold the operator key on the reader  F10 hold a player key on the reader
 * Key ROM IDs: MEGAIO_OPERATOR_KEY / MEGAIO_PLAYER_KEY (16 hex digits, family 02); defaults below.
 * MEGAIO_JOYSTICK=1 adds the joystick accessory (USBIO::JoystickFound, games such as Luxor or
 * Lookout): arrow keys move it, Space is the left button and Enter the right one. Off by default, because a
 * present joystick changes attract-mode and game behaviour.
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "megaio.h"

#define VID 0x04b4
#define PID 0x6473
#define MBOX 0xE000u

/* XDATA addresses the host uses (io-board.md, "Commands") */
enum {
    X_KEYID = 0x1C31, X_KEYDATA = 0x1C39, X_DECRYPT = 0x1D25, X_DECRYPT_DONE = 0x1D29,
    X_OUTPUTS = 0x1D2E, X_STATUS1 = 0x1D35, X_MDSTAT = 0x1D36, X_CONFIRM = 0x1D37,
    X_STATUS = 0x1D3B, X_EEBUF = 0x1D6B, X_BLKSEL = 0x1F25, X_BLOCK = 0x1F2D,
    X_ESKEY_SN = 0x1F0B, X_KEYPRESENT = 0x2786, X_ESKEY_DATA = 0x2798, X_CABID = 0x2756,
    X_CABSTR = 0x2758, X_SOLENOID = 0x272D, X_DOCKSN = 0x2730, X_JOYSTICK = 0x1D3A,
};

static uint8_t xd[0x10000];
static uint8_t eeprom[0x2000];
static uint8_t keyimg[8 * 1024];
static int have_key;
static char dir[512];
static int trace;
static struct megaio_ctl *ctl;
static uint8_t login_rec[240];
static uint8_t last_coin_seq[8];

static void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[fakeio] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
#define TRACE(...) do { if (trace) logf_(__VA_ARGS__); } while (0)

static void path(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", dir, name); }

static int load(const char *name, void *buf, size_t len) {
    char p[600];
    path(p, sizeof p, name);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    size_t got = fread(buf, 1, len, f);
    fclose(f);
    return got > 0;
}

static void save(const char *name, const void *buf, size_t len) {
    char p[600], t[620];
    path(p, sizeof p, name);
    snprintf(t, sizeof t, "%s.new", p);
    FILE *f = fopen(t, "wb");
    if (!f) { logf_("cannot write %s: %s", t, strerror(errno)); return; }
    fwrite(buf, 1, len, f);
    fclose(f);
    rename(t, p);
}

static void open_ctl(void) {
    char p[600];
    path(p, sizeof p, "ctl");
    int fd = open(p, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { logf_("no control file %s: %s", p, strerror(errno)); return; }
    if (ftruncate(fd, sizeof(struct megaio_ctl)) == 0) {
        void *m = mmap(NULL, sizeof(struct megaio_ctl), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (m != MAP_FAILED) ctl = m;
    }
    close(fd);
    if (ctl && ctl->magic != MEGAIO_MAGIC) {
        memset(ctl, 0, sizeof *ctl);
        ctl->magic = MEGAIO_MAGIC;
        ctl->st[8] = 0xFF;             /* DIP bank DS1 is active low: all off */
    }
    if (ctl) memcpy(last_coin_seq, ctl->coins, 8);
}

static void init_state(void) {
    static int done;
    if (done) return;
    done = 1;
    trace = getenv("MEGAIO_TRACE") && *getenv("MEGAIO_TRACE") == '1';
    snprintf(dir, sizeof dir, "%s", getenv("MEGAIO_DIR") ? getenv("MEGAIO_DIR") : "/var/merit/fakeio");
    mkdir(dir, 0777);
    char p[600];
    path(p, sizeof p, "login");
    mkdir(p, 0777);
    memset(eeprom, 0xFF, sizeof eeprom);
    if (!load("eeprom.bin", eeprom, sizeof eeprom)) {
        /* a fresh board carries the cabinet's serial number: marker "100293JKKJ" at 0x1FD6 and
         * the number at 0x1FE0 (USBIO::ReadSNFromEEPROM). This image's cabinet: 043010MRE60023. */
        const char *sn = getenv("MEGAIO_SERIAL") ? getenv("MEGAIO_SERIAL") : "043010MRE60023";
        memcpy(eeprom + 0x1FD6, "100293JKKJ", 10);
        memset(eeprom + 0x1FE0, 0, 16);
        memcpy(eeprom + 0x1FE0, sn, strlen(sn) < 15 ? strlen(sn) : 15);
        save("eeprom.bin", eeprom, sizeof eeprom);
    }
    have_key = load("key.bin", keyimg, sizeof keyimg);
    open_ctl();
    logf_("board %04x:%04x ready (state in %s, security key image: %s)", VID, PID, dir,
          have_key ? "yes" : "none");
}

/* Values the loader reads straight out of board RAM after the firmware starts. */
static void board_reset(void) {
    memset(xd, 0, MBOX);
    memset(xd + X_ESKEY_SN, 0xFF, 8);     /* no ES key: HasESKey() is false for all-0xFF */
    memset(xd + X_ESKEY_DATA, 0x00, 8);
    xd[X_STATUS + 8] = 0xFF;
    xd[X_JOYSTICK] = getenv("MEGAIO_JOYSTICK") && *getenv("MEGAIO_JOYSTICK") == '1';
}

static void key_rom_id(uint8_t id[8]) {
    /* ROM ID of the cabinet's last security key (/var/merit/.kf), unless one is configured */
    static const uint8_t def[8] = {0x8c, 0x14, 0xfc, 0x02, 0x00, 0x40, 0x00, 0x0e};
    memcpy(id, def, 8);
    if (ctl && ctl->key_id_set) memcpy(id, ctl->key_id, 8);
}

static void login_path(char *out, size_t n, const uint8_t id[8]) {
    char name[64];
    snprintf(name, sizeof name, "login/%02x%02x%02x%02x%02x%02x%02x%02x",
             id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
    path(out, n, name);
}

static int fob_present(void) { return ctl && ctl->fob_present; }

/* Front-reader key records (240 bytes, USBIO::TryReadingLoginKey): bytes 0-7 the key's own ROM
 * ID, 8-15 the type tag, u16 at 0xAE = sum of all other bytes. Tags: "m8b4uc3d" player key
 * (My Merit / TournaMAXX login), "y2ger1b0" operator key (PlayerKeyClass::EnterStateStart opens
 * OperatorKeyEntry, which asks for the PIN stored with the key's ROM ID in Setup Operator Keys).
 * Merit shipped keys already programmed; a player key whose record is missing or invalid is
 * served as a freshly programmed, empty one, and an operator key always as an operator key. */
static uint16_t login_checksum(const uint8_t *r) {
    unsigned sum = 0;
    for (int i = 0; i < 240; i++) if (i != 0xAE && i != 0xAF) sum += r[i];
    return (uint16_t)sum;
}

static int login_record_valid(const uint8_t *r, const uint8_t id[8]) {
    return memcmp(r, id, 8) == 0 && login_checksum(r) == (uint16_t)(r[0xAE] | r[0xAF] << 8);
}

static void provision_login_record(uint8_t *r, const uint8_t id[8], int operator_key) {
    memset(r, 0, 240);
    memcpy(r, id, 8);
    memcpy(r + 8, operator_key ? "y2ger1b0" : "m8b4uc3d", 8);
    uint16_t c = login_checksum(r);
    r[0xAE] = c & 0xFF;
    r[0xAF] = c >> 8;
}

/* ---- keyboard hotkeys (libX11 loaded on demand; no X headers needed) ---- */
static void *kb_dpy;
static int (*pXQueryKeymap)(void *, char[32]);
static unsigned char (*pXKeysymToKeycode)(void *, unsigned long);
static unsigned char kb_code[14];      /* F1 F2 F5 F6 F7 F8 F9 F10, Left Up Right Down Space Return */
static int kb_prev[14], kb_tried;
/* joystick: raw values matching the loader's default calibration (USBIO::Init) */
enum { JX_MIN = -1757, JX_MID = 314, JX_MAX = 1891, JY_MIN = -1732, JY_MID = 446, JY_MAX = 2039 };
static uint8_t kb_fob_id[2][8];
static int kb_fob_active = -1;

static void parse_id(const char *env, const char *def, uint8_t out[8]) {
    const char *h = getenv(env);
    if (!h || strlen(h) != 16) h = def;
    for (int i = 0; i < 8; i++) { unsigned v = 0; sscanf(h + 2 * i, "%2x", &v); out[i] = v; }
}

static void kb_init(void) {
    kb_tried = 1;
    if (getenv("MEGAIO_NO_KEYS")) return;
    void *x = dlopen("libX11.so.6", RTLD_NOW);
    if (!x) return;
    void *(*open_display)(const char *) = dlsym(x, "XOpenDisplay");
    pXQueryKeymap = dlsym(x, "XQueryKeymap");
    pXKeysymToKeycode = dlsym(x, "XKeysymToKeycode");
    if (!open_display || !pXQueryKeymap || !pXKeysymToKeycode) return;
    kb_dpy = open_display(NULL);
    if (!kb_dpy) return;
    static const unsigned long sym[14] = {0xffbe, 0xffbf, 0xffc2, 0xffc3, 0xffc4, 0xffc5, 0xffc6, 0xffc7,
                                          0xff51, 0xff52, 0xff53, 0xff54, 0x20, 0xff0d};
    for (int i = 0; i < 14; i++) kb_code[i] = pXKeysymToKeycode(kb_dpy, sym[i]);
    parse_id("MEGAIO_OPERATOR_KEY", "024d454741100130", kb_fob_id[0]);
    parse_id("MEGAIO_PLAYER_KEY", "02504c4159455205", kb_fob_id[1]);
    logf_("hotkeys: F1 setup, F2 calibrate, F5-F8 coins 1-4, F9 operator key, F10 player key%s",
          getenv("MEGAIO_JOYSTICK") && *getenv("MEGAIO_JOYSTICK") == '1' ? ", joystick on arrows/Space/Enter" : "");
}

static void kb_poll(void) {
    if (!kb_tried) kb_init();
    if (!kb_dpy || !ctl) return;
    char map[32];
    pXQueryKeymap(kb_dpy, map);
    int down[14];
    for (int i = 0; i < 14; i++) down[i] = kb_code[i] && (map[kb_code[i] >> 3] >> (kb_code[i] & 7) & 1);
    if (xd[X_JOYSTICK]) {
        int16_t x = down[8] ? JX_MAX : down[10] ? JX_MIN : JX_MID;    /* the stick's X runs right-to-left */
        int16_t y = down[9] ? JY_MIN : down[11] ? JY_MAX : JY_MID;
        ctl->st[12] = x & 0xFF; ctl->st[13] = (uint16_t)x >> 8;
        ctl->st[14] = y & 0xFF; ctl->st[15] = (uint16_t)y >> 8;
        /* bit 4 = left button, bit 7 = right button (Calibrate Joystick screen) */
        ctl->st[9] = (uint8_t)((ctl->st[9] & ~0x90) | (down[12] ? 0x10 : 0) | (down[13] ? 0x80 : 0));
    }
    /* buttons follow the key; coins count presses */
    ctl->st[9] = (uint8_t)((ctl->st[9] & ~3) | (down[0] ? 1 : 0) | (down[1] ? 2 : 0));
    for (int i = 0; i < 4; i++) if (down[2 + i] && !kb_prev[2 + i]) ctl->coins[i]++;
    for (int k = 0; k < 2; k++) {
        if (down[6 + k] && !kb_prev[6 + k]) {
            memcpy(ctl->fob_id, kb_fob_id[k], 8);
            ctl->fob_kind = k == 0;
            ctl->fob_present = 1;
            kb_fob_active = k;
            logf_("%s key touched", k == 0 ? "operator" : "player");
        } else if (!down[6 + k] && kb_prev[6 + k] && kb_fob_active == k) {
            ctl->fob_present = 0;
            kb_fob_active = -1;
        }
    }
    memcpy(kb_prev, down, sizeof down);
}

static void do_poll(uint8_t flags, uint16_t heartbeat) {
    uint8_t *st = xd + X_STATUS;
    kb_poll();
    for (int i = 0; i < 8; i++) {
        uint8_t n = 0;
        if (ctl) { n = (uint8_t)(ctl->coins[i] - last_coin_seq[i]); last_coin_seq[i] = ctl->coins[i]; }
        st[i] = n & 0x7F;
        if (n) logf_("coin channel %d: %u pulse(s)", i, n);
    }
    if (ctl) memcpy(st + 8, ctl->st + 8, 16);
    else st[8] = 0xFF;
    if (ctl) {
        ctl->outputs_flags = flags;
        ctl->heartbeat = heartbeat;
        memcpy(ctl->outputs, xd + X_OUTPUTS, 6);
        for (int m = 0; m < 2; m++) if (xd[X_OUTPUTS + 1 + m]) {
            ctl->meters[m] += xd[X_OUTPUTS + 1 + m];
            logf_("%s meter +%u", m ? "TournaMAXX" : "coin", xd[X_OUTPUTS + 1 + m]);
        }
        ctl->polls++;
    }
}

static void command(uint8_t cmd, uint8_t arg, uint16_t idx) {
    switch (cmd) {
    case 0x03: memcpy(xd + MBOX, xd + idx, arg); return;              /* RAM → mailbox */
    case 0x0E: memcpy(xd + idx, xd + MBOX, arg); return;              /* mailbox → RAM */
    case 0x06: do_poll(arg, idx); return;
    case 0x13: TRACE("init %#x", idx); return;
    case 0x14: logf_("prepare for reload"); return;
    case 0x16: TRACE("watchdog timeout %u", idx); return;
    case 0x24: logf_("power-cycle requested (ignored)"); return;
    case 0x0A: memcpy(xd + X_EEBUF, eeprom + (idx & 0x1FFF), arg); return;
    case 0x09:
        memcpy(eeprom + (idx & 0x1FFF), xd + X_EEBUF, arg);
        save("eeprom.bin", eeprom, sizeof eeprom);
        return;
    case 0x04:                                                          /* session value */
        for (int i = 0; i < 4; i++) xd[X_DECRYPT + i] = (uint8_t)rand();
        xd[X_DECRYPT_DONE] = 1;
        return;
    case 0x01: {                                                        /* security key ROM ID */
        uint8_t id[8];
        key_rom_id(id);
        for (int i = 0; i < 8; i++) xd[X_KEYID + i] = id[i] ^ 0x08;
        return;
    }
    case 0x11:                                                          /* security key block */
        if (have_key && idx < 8) {
            memcpy(xd + X_BLOCK, keyimg + idx * 1024, 1024);
            xd[X_KEYPRESENT] = 1;
        } else {
            xd[X_KEYPRESENT] = 0;
        }
        TRACE("key block %u (%s)", idx, have_key ? "served" : "no key");
        return;
    case 0x12: xd[X_CONFIRM] = 1; return;                               /* same key still in */
    case 0x02: xd[X_STATUS1] = 0; xd[X_KEYPRESENT] = 0; return;         /* DS1991 subkeys: none */
    case 0x17: memset(xd + X_KEYDATA, 0, 56); return;
    case 0x0B:                                                          /* front reader probe */
        if (fob_present()) {
            xd[X_STATUS1] = 1;
            memcpy(xd + X_KEYID, ctl->fob_id, 8);
        } else {
            xd[X_STATUS1] = 2;
        }
        return;
    case 0x07: {                                                        /* read login key record */
        if (!fob_present()) { xd[X_STATUS1] = 2; return; }
        char p[600];
        login_path(p, sizeof p, ctl->fob_id);
        memset(login_rec, 0, sizeof login_rec);
        FILE *f = fopen(p, "rb");
        if (f) { fread(login_rec, 1, sizeof login_rec, f); fclose(f); }
        if (ctl->fob_kind == 1 || !login_record_valid(login_rec, ctl->fob_id))
            provision_login_record(login_rec, ctl->fob_id, ctl->fob_kind == 1);
        memcpy(xd + X_KEYID, login_rec, sizeof login_rec);
        xd[X_STATUS1] = 1;
        return;
    }
    case 0x08: {                                                        /* write login key record */
        if (!fob_present()) { xd[X_STATUS1] = 2; return; }
        if (ctl->fob_kind == 1) { xd[X_STATUS1] = 1; return; }          /* operator keys stay as they are */
        char p[600];
        login_path(p, sizeof p, ctl->fob_id);
        FILE *f = fopen(p, "wb");
        if (f) { fwrite(xd + X_KEYID, 1, 240, f); fclose(f); }
        xd[X_STATUS1] = 1;
        logf_("login key record written");
        return;
    }
    case 0x05: xd[X_MDSTAT] = 1; return;                                /* ticket printer line */
    case 0x20: return;                                                  /* dock serial: in RAM */
    case 0x21: return;
    case 0x23: memset(xd + X_SOLENOID, 0, 3); return;
    case 0x25: xd[X_SOLENOID] = 1; return;
    default:
        logf_("unhandled command %#04x arg %#04x index %#06x", cmd, arg, idx);
    }
}

/* ---- libusb-1.0 surface used by the loader ---- */

typedef struct libusb_context libusb_context;
typedef struct libusb_device { int dummy; } libusb_device;
typedef struct libusb_device_handle { int open; } libusb_device_handle;
struct libusb_device_descriptor {
    uint8_t bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
};

static libusb_device the_dev;
static libusb_device_handle the_handle;

int libusb_init(libusb_context **ctx) { init_state(); if (ctx) *ctx = NULL; return 0; }
void libusb_exit(libusb_context *ctx) { (void)ctx; }

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list) {
    (void)ctx;
    init_state();
    libusb_device **l = calloc(2, sizeof *l);
    l[0] = &the_dev;
    *list = l;
    return 1;
}

void libusb_free_device_list(libusb_device **list, int unref) { (void)unref; free(list); }

int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *d) {
    (void)dev;
    memset(d, 0, sizeof *d);
    d->bLength = 18; d->bDescriptorType = 1; d->bcdUSB = 0x0200; d->bMaxPacketSize0 = 64;
    d->idVendor = VID; d->idProduct = PID; d->bNumConfigurations = 1;
    return 0;
}

libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context *ctx, uint16_t v, uint16_t p) {
    (void)ctx;
    init_state();
    if (v != VID || p != PID) return NULL;
    if (!the_handle.open) board_reset();
    the_handle.open = 1;
    return &the_handle;
}

void libusb_close(libusb_device_handle *h) { if (h) h->open = 0; }
int libusb_claim_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int libusb_release_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int libusb_kernel_driver_active(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int libusb_detach_kernel_driver(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }

int libusb_control_transfer(libusb_device_handle *h, uint8_t type, uint8_t req, uint16_t value,
                            uint16_t index, unsigned char *data, uint16_t len, unsigned int timeout) {
    (void)h; (void)timeout;
    if (req == 0xA0) {                       /* chip RAM access (firmware load, mailbox) */
        if (value + (unsigned)len > sizeof xd) return -1;
        if (type & 0x80) memcpy(data, xd + value, len);
        else {
            memcpy(xd + value, data, len);
            if (value == 0xE600 && len == 1 && data[0] == 0) {
                logf_("firmware started");
                board_reset();
            }
        }
        return len;
    }
    if (req == 0xA1) {
        uint8_t cmd = value & 0xFF, arg = value >> 8;
        if (cmd != 0x03 && cmd != 0x0E && cmd != 0x06) TRACE("cmd %#04x arg %#04x idx %#06x", cmd, arg, index);
        command(cmd, arg, index);
        return 0;
    }
    logf_("unexpected control request type %#x req %#x", type, req);
    return -9;                               /* LIBUSB_ERROR_PIPE */
}
