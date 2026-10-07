/*
 * ossfake — OSS (/dev/dsp, /dev/mixer) for the cabinet's programs, played through PulseAudio.
 *
 * The loader (Allegro's OSS driver) and the Unity games (FMOD, -FMOD_OUTPUTTYPE_OSS) write
 * audio to /dev/dsp; the cabinet ran OSS v4. Here an open of /dev/dsp returns one end of a
 * UNIX socket pair: writes, select/poll and blocking behave like a sound device whose buffer
 * is the socket buffer, and a thread forwards the other end to PulseAudio (libpulse-simple,
 * loaded on first use). /dev/mixer is a stub that remembers volumes. Forked children may keep
 * writing to an inherited /dev/dsp descriptor; the thread in the parent plays it.
 * Unity games use FMOD, which is switched to PulseAudio directly (see the end of this file).
 * Settings: OSSFAKE_TRACE=1 logs ioctls.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* OSS ioctls (sys/soundcard.h) */
#define SNDCTL_DSP_RESET        0x00005000
#define SNDCTL_DSP_SYNC         0x00005001
#define SNDCTL_DSP_SPEED        0xc0045002
#define SNDCTL_DSP_STEREO       0xc0045003
#define SNDCTL_DSP_GETBLKSIZE   0xc0045004
#define SNDCTL_DSP_SETFMT       0xc0045005
#define SNDCTL_DSP_CHANNELS     0xc0045006
#define SNDCTL_DSP_POST         0x00005008
#define SNDCTL_DSP_SETFRAGMENT  0xc004500a
#define SNDCTL_DSP_GETFMTS      0x8004500b
#define SNDCTL_DSP_GETOSPACE    0x8010500c
#define SNDCTL_DSP_GETISPACE    0x8010500d
#define SNDCTL_DSP_NONBLOCK     0x0000500e
#define SNDCTL_DSP_GETCAPS      0x8004500f
#define SNDCTL_DSP_GETTRIGGER   0x80045010
#define SNDCTL_DSP_SETTRIGGER   0x40045010
#define SNDCTL_DSP_GETOPTR      0x800c5012
#define SNDCTL_DSP_GETODELAY    0x80045017
#define OSS_GETVERSION          0x80044d76
#define SOUND_MIXER_INFO        0x805c4d65
#define AFMT_U8   0x08
#define AFMT_S16_LE 0x10
#define DSP_CAP_TRIGGER 0x1000
#define DSP_CAP_REALTIME 0x0200

typedef struct { int format; uint32_t rate; uint8_t channels; } pa_sample_spec;
typedef struct { uint32_t maxlength, tlength, prebuf, minreq, fragsize; } pa_buffer_attr;
static void *(*p_new)(const char *, const char *, int, const char *, const char *,
                      const pa_sample_spec *, const void *, const pa_buffer_attr *, int *);
static int (*p_write)(void *, const void *, size_t, int *);
static int (*p_drain)(void *, int *);
static void (*p_free)(void *);

struct dsp {
    int used, fd, peer;         /* fd: what the program holds; peer: our end */
    int rate, channels, fmt, fragsize, nfrags;
    uint64_t written;           /* bytes taken from the socket (for GETOPTR) */
    int nonblock;
};
#define MAXDSP 8
static struct dsp dsps[MAXDSP];
static int mixer_fds[16], nmixer;
static int mixer_vol[32];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int trace;

static int (*real_open)(const char *, int, ...);
static int (*real_open64)(const char *, int, ...);
static int (*real_ioctl)(int, unsigned long, ...);
static int (*real_close)(int);

static void init_real(void) {
    if (real_open) return;
    real_open = dlsym(RTLD_NEXT, "open");
    real_open64 = dlsym(RTLD_NEXT, "open64");
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    real_close = dlsym(RTLD_NEXT, "close");
    trace = getenv("OSSFAKE_TRACE") && *getenv("OSSFAKE_TRACE") == '1';
}

static int load_pulse(void) {
    if (p_new) return 1;
    void *h = dlopen("libpulse-simple.so.0", RTLD_NOW);
    if (!h) { fprintf(stderr, "[ossfake] no libpulse-simple: %s\n", dlerror()); return 0; }
    p_new = dlsym(h, "pa_simple_new");
    p_write = dlsym(h, "pa_simple_write");
    p_drain = dlsym(h, "pa_simple_drain");
    p_free = dlsym(h, "pa_simple_free");
    return p_new && p_write;
}

static struct dsp *find_dsp(int fd) {
    for (int i = 0; i < MAXDSP; i++) if (dsps[i].used && dsps[i].fd == fd) return &dsps[i];
    return NULL;
}

static int is_mixer(int fd) {
    for (int i = 0; i < nmixer; i++) if (mixer_fds[i] == fd) return 1;
    return 0;
}

/* Forwarding thread: one PulseAudio stream per open, reopened when the format changes. */
static void *pump(void *arg) {
    struct dsp *d = arg;
    char buf[8192];
    void *pa = NULL;
    int cur_rate = 0, cur_ch = 0, cur_fmt = 0, err;
    for (;;) {
        ssize_t n = read(d->peer, buf, sizeof buf);
        if (n <= 0) break;
        d->written += n;
        if (!pa || cur_rate != d->rate || cur_ch != d->channels || cur_fmt != d->fmt) {
            if (pa) { p_drain(pa, &err); p_free(pa); pa = NULL; }
            if (!load_pulse()) continue;
            pa_sample_spec ss = { d->fmt == AFMT_U8 ? 0 : 3, (uint32_t)d->rate, (uint8_t)d->channels };
            uint32_t bps = d->rate * d->channels * (d->fmt == AFMT_U8 ? 1 : 2);
            pa_buffer_attr ba = { (uint32_t)-1, bps / 10, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1 };
            char name[64];
            snprintf(name, sizeof name, "OSS %s", program_invocation_short_name);
            pa = p_new(NULL, name, 1 /* PLAYBACK */, NULL, "Megatouch", &ss, NULL, &ba, &err);
            if (!pa) fprintf(stderr, "[ossfake] pa_simple_new failed (%d)\n", err);
            cur_rate = d->rate; cur_ch = d->channels; cur_fmt = d->fmt;
        }
        if (pa) p_write(pa, buf, n, &err);
    }
    if (pa) { p_drain(pa, &err); p_free(pa); }
    real_close(d->peer);
    pthread_mutex_lock(&lock);
    d->used = 0;
    pthread_mutex_unlock(&lock);
    return NULL;
}

static int open_dsp(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) return -1;
    int small = 16384;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small);
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &small, sizeof small);
    fcntl(sv[0], F_SETFD, 0);                  /* the program's end survives exec, like a device */
    pthread_mutex_lock(&lock);
    struct dsp *d = NULL;
    for (int i = 0; i < MAXDSP; i++) if (!dsps[i].used) { d = &dsps[i]; break; }
    if (!d) { pthread_mutex_unlock(&lock); real_close(sv[0]); real_close(sv[1]); errno = EBUSY; return -1; }
    memset(d, 0, sizeof *d);
    d->used = 1; d->fd = sv[0]; d->peer = sv[1];
    d->rate = 8000; d->channels = 1; d->fmt = AFMT_U8; d->fragsize = 4096; d->nfrags = 4;
    pthread_mutex_unlock(&lock);
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &a, pump, d);
    if (trace) fprintf(stderr, "[ossfake] %s opened /dev/dsp as fd %d\n", program_invocation_short_name, sv[0]);
    return sv[0];
}

static int dev_kind(const char *path) {
    if (!path) return 0;
    if (!strcmp(path, "/dev/dsp") || !strcmp(path, "/dev/dsp0") || !strcmp(path, "/dev/audio") ||
        !strncmp(path, "/dev/oss/", 9)) return 1;
    if (!strcmp(path, "/dev/mixer") || !strcmp(path, "/dev/mixer0")) return 2;
    return 0;
}

static int open_mixer(void) {
    int fd = real_open("/dev/null", O_RDWR);
    if (fd >= 0 && nmixer < 16) mixer_fds[nmixer++] = fd;
    return fd;
}

static int fake_open(const char *path, int flags) {
    int k = dev_kind(path);
    if (k == 1) {
        int fd = open_dsp();
        if (fd >= 0 && (flags & O_NONBLOCK)) { find_dsp(fd)->nonblock = 1; fcntl(fd, F_SETFL, O_NONBLOCK); }
        return fd;
    }
    return open_mixer();
}

int open(const char *path, int flags, ...) {
    init_real();
    if (dev_kind(path)) return fake_open(path, flags);
    va_list ap; va_start(ap, flags); int mode = va_arg(ap, int); va_end(ap);
    return real_open(path, flags, mode);
}

int open64(const char *path, int flags, ...) {
    init_real();
    if (dev_kind(path)) return fake_open(path, flags);
    va_list ap; va_start(ap, flags); int mode = va_arg(ap, int); va_end(ap);
    return real_open64(path, flags, mode);
}

int __open_2(const char *path, int flags) { return open(path, flags); }
int __open64_2(const char *path, int flags) { return open64(path, flags); }

int close(int fd) {
    init_real();
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nmixer; i++) if (mixer_fds[i] == fd) mixer_fds[i] = mixer_fds[--nmixer];
    pthread_mutex_unlock(&lock);
    return real_close(fd);                     /* closing the dsp end ends its pump thread */
}

static int queued(int fd) {
    int q = 0;
    if (real_ioctl(fd, 0x5411 /* TIOCOUTQ = SIOCOUTQ */, &q) != 0) q = 0;
    return q;
}

static int dsp_ioctl(struct dsp *d, unsigned long req, void *arg) {
    int *ip = arg;
    int bufsize = d->fragsize * d->nfrags;
    switch (req) {
    case SNDCTL_DSP_RESET: case SNDCTL_DSP_POST: return 0;
    case SNDCTL_DSP_SYNC: while (queued(d->fd) > 0) usleep(2000); return 0;
    case SNDCTL_DSP_SPEED: if (*ip > 0) d->rate = *ip; *ip = d->rate; return 0;
    case SNDCTL_DSP_STEREO: d->channels = *ip ? 2 : 1; return 0;
    case SNDCTL_DSP_CHANNELS: if (*ip == 1 || *ip == 2) d->channels = *ip; *ip = d->channels; return 0;
    case SNDCTL_DSP_SETFMT:
        if (*ip == AFMT_U8 || *ip == AFMT_S16_LE) d->fmt = *ip;
        *ip = d->fmt; return 0;
    case SNDCTL_DSP_GETFMTS: *ip = AFMT_U8 | AFMT_S16_LE; return 0;
    case SNDCTL_DSP_SETFRAGMENT: {
        int sel = *ip & 0xFFFF, n = (*ip >> 16) & 0x7FFF;
        if (sel < 7) sel = 7;
        if (sel > 15) sel = 15;
        d->fragsize = 1 << sel;
        d->nfrags = n < 2 ? 2 : n > 16 ? 16 : n;
        int sz = d->fragsize * d->nfrags;
        setsockopt(d->fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
        return 0;
    }
    case SNDCTL_DSP_GETBLKSIZE: *ip = d->fragsize; return 0;
    case SNDCTL_DSP_GETOSPACE: {
        int q = queued(d->fd), free_ = bufsize - q;
        if (free_ < 0) free_ = 0;
        ip[0] = free_ / d->fragsize; ip[1] = d->nfrags; ip[2] = d->fragsize; ip[3] = free_;
        return 0;
    }
    case SNDCTL_DSP_GETISPACE: memset(arg, 0, 16); return 0;
    case SNDCTL_DSP_NONBLOCK: d->nonblock = 1; fcntl(d->fd, F_SETFL, O_NONBLOCK); return 0;
    case SNDCTL_DSP_GETCAPS: *ip = DSP_CAP_TRIGGER | DSP_CAP_REALTIME | 1; return 0;
    case SNDCTL_DSP_GETTRIGGER: *ip = 2; return 0;
    case SNDCTL_DSP_SETTRIGGER: return 0;
    case SNDCTL_DSP_GETOPTR: {
        ip[0] = (int)d->written; ip[1] = 0; ip[2] = (int)(d->written % (bufsize ? bufsize : 1));
        return 0;
    }
    case SNDCTL_DSP_GETODELAY: *ip = queued(d->fd); return 0;
    case OSS_GETVERSION: *ip = 0x040100; return 0;
    default:
        if (trace) fprintf(stderr, "[ossfake] dsp ioctl %#lx ignored\n", req);
        return 0;
    }
}

static int mixer_ioctl(unsigned long req, void *arg) {
    int *ip = arg;
    unsigned nr = req & 0xFF;
    if (req == OSS_GETVERSION) { *ip = 0x040100; return 0; }
    if (req == SOUND_MIXER_INFO) { memset(arg, 0, 92); strcpy(arg, "ossfake"); strcpy((char *)arg + 16, "Megatouch"); return 0; }
    if ((req & 0xFF00) == 0x4D00 && ip) {      /* SOUND_MIXER_READ_x / WRITE_x */
        if (nr == 0xFF || nr == 0xFE || nr == 0xFD || nr == 0xFB || nr == 0xFC) { *ip = 0x3FFF; return 0; }  /* masks */
        if (nr < 32) {
            if ((req >> 30) & 1) mixer_vol[nr] = *ip;    /* write direction bit */
            else *ip = mixer_vol[nr] ? mixer_vol[nr] : 0x4B4B;
        }
        return 0;
    }
    if (trace) fprintf(stderr, "[ossfake] mixer ioctl %#lx ignored\n", req);
    return 0;
}

int ioctl(int fd, unsigned long req, ...) {
    init_real();
    va_list ap; va_start(ap, req); void *arg = va_arg(ap, void *); va_end(ap);
    struct dsp *d = find_dsp(fd);
    if (d) {
        if (trace) fprintf(stderr, "[ossfake] dsp ioctl %#lx\n", req);
        return dsp_ioctl(d, req, arg);
    }
    if (is_mixer(fd)) return mixer_ioctl(req, arg);
    return real_ioctl(fd, req, arg);
}

/* Unity games: FMOD Ex's OSS output fails on emulated OSS ("failed to get driver capabilities"),
 * so the player's setOutput(OSS) is turned into PulseAudio (13 in this FMOD Ex build), as
 * src/unity/fmod_output.cpp does for the ported games. OSSFAKE_FMOD_OUTPUT=<n> overrides. */
int _ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE(void *sys, int type) {
    typedef int (*set_output_fn)(void *, int);
    static set_output_fn real;
    if (!real) real = (set_output_fn)dlsym(RTLD_NEXT, "_ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE");
    const char *e = getenv("OSSFAKE_FMOD_OUTPUT");
    int want = e ? atoi(e) : 13;
    int r = real ? real(sys, want) : -1;
    fprintf(stderr, "[ossfake] FMOD output %d requested, using %d (result %d)\n", type, want, r);
    return r;
}
