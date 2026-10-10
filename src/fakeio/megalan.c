/*
 * megalan — a virtual cabinet network: an Ethernet switch for several cabinets, with a router
 * to the internet built in. Cabinets on one megalan see each other as if plugged into the same
 * switch (broadcasts included: MegaLink finds its partners with UDP broadcasts on port 4700 and
 * then plays over TCP), and reach the internet through the router (libslirp: DHCP, DNS, NAT;
 * the same code slirp4netns uses for a single cabinet).
 *
 *   megalan SOCKET          listen on the unix socket SOCKET (scripts/loader.sh starts it for
 *                           MEGA_LOADER_NET=lan[:NAME], one per NAME)
 *
 * Each cabinet's sandbox connects through lantap (its eth0). Frames on the socket are a 4-byte
 * big-endian length followed by the Ethernet frame (QEMU's "stream" netdev framing).
 *
 * The network: 10.0.2.0/24, router and gateway 10.0.2.2, DNS 10.0.2.3, DHCP from 10.0.2.15,
 * the same as a single cabinet's. Each cabinet gets its own address, and the last number of the
 * address is its MegaLink ID. megalan exits 60 s after the last cabinet disconnects.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <execinfo.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <slirp/libslirp.h>

#define MAXPORTS 32
#define MAXFRAME 9216
#define ROUTER (-1)

typedef struct {
    int fd;
    unsigned char buf[4 + MAXFRAME];
    size_t have;
} Port;

static Port ports[MAXPORTS];
static int nports_ever;
static Slirp *slirp;

/* MAC address table: learned source addresses */
#define MACS 256
static struct { unsigned char mac[6]; int port; time_t seen; } macs[MACS];

static void logm(const char *fmt, ...) {
    va_list ap;
    char t[32];
    time_t now = time(NULL);
    strftime(t, sizeof t, "%F %T", localtime(&now));
    fprintf(stderr, "%s [megalan] ", t);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void learn(const unsigned char *mac, int port) {
    if (mac[0] & 1) return;
    int free_slot = -1, oldest = 0;
    for (int i = 0; i < MACS; i++) {
        if (macs[i].seen && !memcmp(macs[i].mac, mac, 6)) {
            if (macs[i].port != port)
                logm("%02x:%02x:%02x:%02x:%02x:%02x on %s%d", mac[0], mac[1], mac[2], mac[3], mac[4],
                     mac[5], port == ROUTER ? "router" : "port ", port == ROUTER ? 0 : port);
            macs[i].port = port; macs[i].seen = time(NULL);
            return;
        }
        if (!macs[i].seen && free_slot < 0) free_slot = i;
        if (macs[i].seen < macs[oldest].seen) oldest = i;
    }
    int i = free_slot >= 0 ? free_slot : oldest;
    memcpy(macs[i].mac, mac, 6); macs[i].port = port; macs[i].seen = time(NULL);
}

static int lookup(const unsigned char *mac) {
    if (mac[0] & 1) return -2;                         /* broadcast / multicast */
    for (int i = 0; i < MACS; i++)
        if (macs[i].seen && !memcmp(macs[i].mac, mac, 6)) return macs[i].port;
    return -2;                                         /* unknown: flood */
}

static void port_close(int p) {
    logm("cabinet on port %d disconnected", p);
    close(ports[p].fd);
    ports[p].fd = -1;
    for (int i = 0; i < MACS; i++) if (macs[i].seen && macs[i].port == p) macs[i].seen = 0;
}

static void port_send(int p, const unsigned char *frame, size_t len) {
    unsigned char hdr[4] = { len >> 24, len >> 16, len >> 8, len };
    struct iovec iov[2] = { { hdr, 4 }, { (void *)frame, len } };
    struct msghdr m = { .msg_iov = iov, .msg_iovlen = 2 };
    if (sendmsg(ports[p].fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 && errno != EAGAIN) port_close(p);
}

/* forward a frame that came in on `from` (a port number or ROUTER) */
static void switch_frame(int from, const unsigned char *f, size_t len) {
    if (len < 14) return;
    learn(f + 6, from);
    int to = lookup(f);
    if (to == from) return;
    if (to == ROUTER) { slirp_input(slirp, f, len); return; }
    if (to >= 0) { if (ports[to].fd >= 0) port_send(to, f, len); return; }
    for (int p = 0; p < MAXPORTS; p++) if (p != from && ports[p].fd >= 0) port_send(p, f, len);
    if (from != ROUTER) slirp_input(slirp, f, len);
}

/* ---- libslirp glue ---- */
static ssize_t cb_send(const void *buf, size_t len, void *opaque) {
    (void)opaque;
    switch_frame(ROUTER, buf, len);
    return len;
}
static void cb_error(const char *msg, void *opaque) { (void)opaque; logm("router: %s", msg); }
static int64_t cb_clock(void *opaque) { (void)opaque; return now_ns(); }

typedef struct Timer { SlirpTimerId id; void *cb_opaque; int64_t expire_ms; struct Timer *next; } Timer;
static Timer *timers;
static void *cb_timer_new_opaque(SlirpTimerId id, void *cb_opaque, void *opaque) {
    (void)opaque;
    Timer *t = calloc(1, sizeof *t);
    t->id = id; t->cb_opaque = cb_opaque; t->expire_ms = -1; t->next = timers; timers = t;
    return t;
}
static void cb_timer_free(void *timer, void *opaque) {
    (void)opaque;
    for (Timer **pp = &timers; *pp; pp = &(*pp)->next) if (*pp == timer) { *pp = (*pp)->next; break; }
    free(timer);
}
static void cb_timer_mod(void *timer, int64_t expire_ms, void *opaque) { (void)opaque; ((Timer *)timer)->expire_ms = expire_ms; }
static void cb_register(slirp_os_socket s, void *opaque) { (void)s; (void)opaque; }
static void cb_notify(void *opaque) { (void)opaque; }

/* poll set shared with libslirp */
static struct pollfd pfd[512];
static int npfd;
static int cb_add_poll(slirp_os_socket fd, int events, void *opaque) {
    (void)opaque;
    short e = 0;
    if (events & SLIRP_POLL_IN) e |= POLLIN;
    if (events & SLIRP_POLL_OUT) e |= POLLOUT;
    if (events & SLIRP_POLL_PRI) e |= POLLPRI;
    pfd[npfd].fd = fd; pfd[npfd].events = e; pfd[npfd].revents = 0;
    return npfd++;
}
static int cb_get_revents(int idx, void *opaque) {
    (void)opaque;
    short r = pfd[idx].revents;
    int e = 0;
    if (r & POLLIN) e |= SLIRP_POLL_IN;
    if (r & POLLOUT) e |= SLIRP_POLL_OUT;
    if (r & POLLPRI) e |= SLIRP_POLL_PRI;
    if (r & POLLERR) e |= SLIRP_POLL_ERR;
    if (r & POLLHUP) e |= SLIRP_POLL_HUP;
    return e;
}

static void port_read(int p) {
    Port *pt = &ports[p];
    ssize_t n = read(pt->fd, pt->buf + pt->have, sizeof pt->buf - pt->have);
    if (n <= 0) { if (n == 0 || (errno != EAGAIN && errno != EINTR)) port_close(p); return; }
    pt->have += n;
    for (;;) {
        if (pt->have < 4) break;
        size_t len = (size_t)pt->buf[0] << 24 | pt->buf[1] << 16 | pt->buf[2] << 8 | pt->buf[3];
        if (len > MAXFRAME) { logm("port %d: bad frame length %zu", p, len); port_close(p); return; }
        if (pt->have < 4 + len) break;
        switch_frame(p, pt->buf + 4, len);
        if (pt->fd < 0) return;
        memmove(pt->buf, pt->buf + 4 + len, pt->have - 4 - len);
        pt->have -= 4 + len;
    }
}

static void on_crash(int sig) {
    void *bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "[megalan] crashed (signal %d):\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: megalan SOCKET\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGSEGV, on_crash);
    signal(SIGABRT, on_crash);
    for (int p = 0; p < MAXPORTS; p++) ports[p].fd = -1;

    int ls = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", argv[1]);
    unlink(argv[1]);
    if (bind(ls, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(ls, 8) < 0) { perror(argv[1]); return 1; }

    SlirpConfig cfg = { .version = 6,     /* 6: register_poll_socket, not the old register_poll_fd */ .in_enabled = true, .if_mtu = 1500, .if_mru = 1500,
                        .disable_host_loopback = true };
    inet_pton(AF_INET, "10.0.2.0", &cfg.vnetwork);
    inet_pton(AF_INET, "255.255.255.0", &cfg.vnetmask);
    inet_pton(AF_INET, "10.0.2.2", &cfg.vhost);
    inet_pton(AF_INET, "10.0.2.15", &cfg.vdhcp_start);
    inet_pton(AF_INET, "10.0.2.3", &cfg.vnameserver);
    static const SlirpCb cb = { .send_packet = cb_send, .guest_error = cb_error, .clock_get_ns = cb_clock,
                                .timer_free = cb_timer_free, .timer_mod = cb_timer_mod, .notify = cb_notify,
                                .timer_new_opaque = cb_timer_new_opaque,
                                .register_poll_socket = cb_register, .unregister_poll_socket = cb_register };
    slirp = slirp_new(&cfg, &cb, NULL);
    if (!slirp) { logm("router failed to start"); return 1; }
    logm("listening on %s (10.0.2.0/24, router 10.0.2.2)", argv[1]);

    time_t empty_since = time(NULL);
    for (;;) {
        npfd = 0;
        uint32_t timeout = 1000;
        for (Timer *t = timers; t; t = t->next)
            if (t->expire_ms >= 0) {
                int64_t left = t->expire_ms - now_ns() / 1000000;
                if (left < 0) left = 0;
                if ((uint32_t)left < timeout) timeout = left;
            }
        slirp_pollfds_fill_socket(slirp, &timeout, cb_add_poll, NULL);
        int first = npfd;
        pfd[npfd].fd = ls; pfd[npfd].events = POLLIN; pfd[npfd].revents = 0; npfd++;
        int portidx[MAXPORTS];
        for (int p = 0; p < MAXPORTS; p++) {
            portidx[p] = -1;
            if (ports[p].fd < 0) continue;
            portidx[p] = npfd;
            pfd[npfd].fd = ports[p].fd; pfd[npfd].events = POLLIN; pfd[npfd].revents = 0; npfd++;
        }
        int r = poll(pfd, npfd, (int)timeout);
        slirp_pollfds_poll(slirp, r < 0, cb_get_revents, NULL);
        /* expired timers; a handler may free or re-arm timers, so start over after each one */
        for (int again = 1; again;) {
            again = 0;
            int64_t nowms = now_ns() / 1000000;
            for (Timer *t = timers; t; t = t->next)
                if (t->expire_ms >= 0 && t->expire_ms <= nowms) {
                    t->expire_ms = -1;
                    slirp_handle_timer(slirp, t->id, t->cb_opaque);
                    again = 1;
                    break;
                }
        }
        if (r <= 0) goto idle;
        if (pfd[first].revents & POLLIN) {
            int c = accept4(ls, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            int p = 0;
            while (p < MAXPORTS && ports[p].fd >= 0) p++;
            if (c >= 0 && p < MAXPORTS) {
                ports[p].fd = c; ports[p].have = 0; nports_ever++;
                logm("cabinet connected on port %d", p);
            } else if (c >= 0) close(c);
        }
        for (int p = 0; p < MAXPORTS; p++)
            if (portidx[p] >= 0 && ports[p].fd >= 0 && (pfd[portidx[p]].revents & (POLLIN | POLLHUP | POLLERR)))
                port_read(p);
    idle:;
        int any = 0;
        for (int p = 0; p < MAXPORTS; p++) if (ports[p].fd >= 0) any = 1;
        if (any) empty_since = time(NULL);
        else if (time(NULL) - empty_since > 60) { logm("no cabinets for 60 s: exiting"); break; }
    }
    unlink(argv[1]);
    return 0;
}
