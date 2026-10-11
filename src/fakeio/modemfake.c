/* modemfake.so: a dial-up modem on /dev/modem for the 2005 software (Maxx Crown), whose
 * TournaMAXX connection only dials (WvDial, libwvdial.so, then pppd).
 *
 * Opening /dev/modem gives the slave of a pseudo-terminal; a thread on the master acts as a
 * Hayes modem: every AT command gets OK (with echo), ATD... gets CONNECT and then the start of a
 * PPP frame (WvDial starts pppd when it sees one), ATH and a dropped DTR (B0, TIOCMBIC) hang up.
 * TIOCMGET reports carrier detect while "connected". The link itself is not PPP: loader.sh mounts
 * pppd-fake.sh over /usr/sbin/pppd and names the sandbox's slirp interface ppp0, so the cabinet's
 * traffic goes out over the ordinary network once it believes it has dialled in.
 *
 * MEGA_MODEM_TRACE=1 logs the AT conversation. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int slave_fd = -1, master_fd = -1;
static volatile int connected;
static int trace = -1;

static void tr(const char *fmt, ...) {
    if (trace < 0) trace = getenv("MEGA_MODEM_TRACE") != NULL;
    if (!trace) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[modemfake] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void say(int m, const char *s) { if (write(m, s, strlen(s)) < 0) {} }

static void hangup(const char *why) {
    if (connected) tr("hang up (%s)", why);
    connected = 0;
}

/* The modem: command lines end with CR; "+++" while connected returns to command mode. */
static void *modem(void *arg) {
    int m = (int)(long)arg;
    char line[256], buf[256];
    size_t len = 0;
    int plus = 0, echo = 1;
    for (;;) {
        ssize_t n = read(m, buf, sizeof buf);
        if (n <= 0) break;                  /* every slave closed */
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (connected) {                /* data mode: only "+++" matters */
                plus = c == '+' ? plus + 1 : 0;
                if (plus == 3) { connected = 0; plus = 0; usleep(200000); say(m, "\r\nOK\r\n"); tr("+++: command mode"); }
                continue;
            }
            if (echo) { if (write(m, &c, 1) < 0) {} }
            if (c == '\n') continue;
            if (c != '\r') { if (len < sizeof line - 1) line[len++] = c; continue; }
            line[len] = 0;
            len = 0;
            char *at = line;
            while (*at == ' ') at++;
            if (strncasecmp(at, "AT", 2) != 0) continue;
            tr("<- %s", at);
            char up[256];
            size_t k;
            for (k = 0; at[k] && k < sizeof up - 1; k++) up[k] = (at[k] >= 'a' && at[k] <= 'z') ? at[k] - 32 : at[k];
            up[k] = 0;
            /* walk the commands: "&x", "+x", "%x", "\\x" are extended (letter and value), "Sn=v" /
             * "Sn?" registers, D dials (the rest of the line is the number), anything else a letter
             * with an optional number */
            char *d = NULL, *q = up + 2;
            int hang = 0;
            while (*q && !d) {
                if (*q == ' ') { q++; continue; }
                char cmd = *q++;
                if (cmd == '&' || cmd == '+' || cmd == '%' || cmd == '\\') {
                    if (*q) q++;
                    while (*q && (strchr("0123456789=,?", *q) || (cmd == '+' && *q >= 'A' && *q <= 'Z'))) q++;
                } else if (cmd == 'S') {
                    while (*q && strchr("0123456789=?", *q)) q++;
                } else if (cmd == 'D') {
                    d = q;
                } else {
                    char *v = q;
                    while (*q >= '0' && *q <= '9') q++;
                    if (cmd == 'E') echo = q == v || *v != '0';
                    if (cmd == 'Z') echo = 1;
                    if (cmd == 'H' && (q == v || *v == '0')) hang = 1;
                }
            }
            if (d) {
                usleep(1500000);            /* "dialling" */
                say(m, "\r\nCONNECT 33600\r\n");
                connected = 1;
                tr("-> CONNECT 33600 (%s)", d + 1);
                usleep(300000);
                /* the start of an LCP Configure-Request: WvDial starts pppd when it sees "~\xff}#" */
                say(m, "~\xff}#\xc0!}!}!} }4}\"}&} } } } }%}&\x12\x34\x56\x78}'}\"}(}\"~");
            } else {
                if (hang) hangup("ATH");
                say(m, "\r\nOK\r\n");
            }
        }
    }
    pthread_mutex_lock(&lock);
    if (master_fd == m) master_fd = -1;
    pthread_mutex_unlock(&lock);
    close(m);
    tr("modem closed");
    return NULL;
}

static int open_modem(int flags) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) || unlockpt(m)) { if (m >= 0) close(m); errno = ENODEV; return -1; }
    int (*real)(const char *, int, ...) = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
    int s = real(ptsname(m), (flags & ~O_CREAT) | O_NOCTTY);
    if (s < 0) { close(m); errno = ENODEV; return -1; }
    struct termios t;                       /* raw on both sides, like a serial line */
    if (tcgetattr(m, &t) == 0) { cfmakeraw(&t); tcsetattr(m, TCSANOW, &t); }
    pthread_mutex_lock(&lock);
    slave_fd = s; master_fd = m; connected = 0;
    pthread_mutex_unlock(&lock);
    pthread_t th;
    pthread_create(&th, NULL, modem, (void *)(long)m);
    pthread_detach(th);
    tr("open /dev/modem -> %s (fd %d)", ptsname(m), s);
    return s;
}

static int is_modem(const char *path) { return path && strcmp(path, "/dev/modem") == 0; }

#define OPEN_WRAP(name)                                                                  \
    int name(const char *path, int flags, ...) {                                        \
        static int (*real)(const char *, int, ...);                                     \
        if (!real) real = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, #name);     \
        mode_t mode = 0;                                                                 \
        if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); } \
        if (is_modem(path)) return open_modem(flags);                                    \
        return real(path, flags, mode);                                                  \
    }
OPEN_WRAP(open)
OPEN_WRAP(open64)
OPEN_WRAP(__open)
OPEN_WRAP(__open64)

/* modem status lines and hang-up by DTR / baud 0 */
int ioctl(int fd, unsigned long req, ...) {
    static int (*real)(int, unsigned long, ...);
    if (!real) real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (fd >= 0 && fd == slave_fd) {
        switch (req) {
        case TIOCMGET:
            *(int *)arg = TIOCM_DTR | TIOCM_RTS | TIOCM_CTS | TIOCM_DSR | (connected ? TIOCM_CD : 0);
            return 0;
        case TIOCMBIC:
            if (*(int *)arg & TIOCM_DTR) hangup("DTR dropped");
            return 0;
        case TIOCMSET:
            if (!(*(int *)arg & TIOCM_DTR)) hangup("DTR dropped");
            return 0;
        case TIOCMBIS:
            return 0;
        case TCSETS: case TCSETSW: case TCSETSF:
            if (cfgetospeed((struct termios *)arg) == B0) hangup("baud 0");
            break;
        }
    }
    return real(fd, req, arg);
}

int close(int fd) {
    static int (*real)(int);
    if (!real) real = (int (*)(int))dlsym(RTLD_NEXT, "close");
    if (fd >= 0 && fd == slave_fd) {
        pthread_mutex_lock(&lock);
        slave_fd = -1;
        pthread_mutex_unlock(&lock);
        hangup("closed");
    }
    return real(fd);
}

/* with MEGA_MODEM_TRACE: where the cabinet connects once "online" (server address and port) */
int connect(int fd, const struct sockaddr *sa, socklen_t len) {
    static int (*real)(int, const struct sockaddr *, socklen_t);
    if (!real) real = (int (*)(int, const struct sockaddr *, socklen_t))dlsym(RTLD_NEXT, "connect");
    int r = real(fd, sa, len), e = errno;
    if (trace < 0) trace = getenv("MEGA_MODEM_TRACE") != NULL;
    if (trace && sa && sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        char a[32];
        inet_ntop(AF_INET, &in->sin_addr, a, sizeof a);
        tr("connect %s:%d -> %d (%s)", a, ntohs(in->sin_port), r, r ? strerror(e) : "ok");
    }
    errno = e;
    return r;
}
