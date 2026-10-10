/*
 * megalan — a virtual cabinet network: an Ethernet switch for several cabinets, with a router
 * to the internet built in. Cabinets on one megalan see each other as if plugged into the same
 * switch (broadcasts included: MegaLink finds its partners with UDP broadcasts on port 4700 and
 * then plays over TCP), and reach the internet through the router (libslirp: DHCP, DNS, NAT;
 * the same code slirp4netns uses for a single cabinet).
 *
 *   megalan SOCKET                       this PC's cabinets only
 *   megalan SOCKET --listen [ADDR:]PORT  ... and be the hub other PCs join
 *   megalan SOCKET --connect HOST:PORT   ... and join the hub on HOST
 *   (scripts/loader.sh starts it for MEGA_LOADER_NET=lan[:NAME], one per NAME)
 *
 * Each cabinet's sandbox connects to SOCKET through lantap (its eth0). Frames are a 4-byte
 * big-endian length followed by the Ethernet frame (QEMU's "stream" netdev framing), on the unix
 * socket and on the TCP link between PCs alike.
 *
 * The network: 10.0.2.0/24, router and gateway 10.0.2.2, DNS 10.0.2.3. A cabinet's MegaLink ID
 * is the last number of its address, so addresses must be unique across all joined PCs: each PC
 * has a block of 16 (block 0 = 10.0.2.16-31, 1 = .32-47 ... 13 = .224-239). The hub is block 0
 * and hands the others theirs. Every PC keeps its own router: router traffic (DHCP answers, the
 * gateway, the internet) never crosses between PCs, everything else does.
 *
 * Joining PCs: MEGALAN_PASSWORD must be the same on all of them. The hub and each member prove
 * they know it (HMAC-SHA256 over random challenges); the password itself is never sent. The
 * traffic between PCs is not encrypted (cabinet-to-cabinet game traffic); use a VPN where that
 * matters. Members reconnect when the link drops.
 *
 * megalan exits 60 s after the last cabinet (and, for a hub, the last joined PC) has gone.
 *
 * MEGALAN_PCAP=<file>: also write every frame the switch handles (cabinets, router, other PCs)
 * to <file> in pcap format, for Wireshark or tcpdump -r (appends when the file exists).
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

#define MAXPORTS 48
#define MAXFRAME 9216
#define ROUTER (-1)
#define BLOCKS 14
#define MAGIC "MEGALAN1"

enum { LOCAL, UPLINK };
enum { UP, CONNECTING, HELLO_WAIT, AUTH_WAIT, OK_WAIT };

typedef struct {
    int fd, kind, state, block;
    unsigned char nonce[16];
    unsigned char buf[4 + MAXFRAME];
    size_t have;
    char name[64];
} Port;

static Port ports[MAXPORTS];
static Slirp *slirp;
static int my_block = -1;                 /* this PC's address block, -1 until known */
static int is_hub, is_member;
static const char *password;
static char hub_host[256], hub_port[16];
static int member_port = -1;              /* the uplink port while it exists */
static time_t next_connect, member_since;

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

/* ---- SHA-256 / HMAC (for the join handshake) ---- */
typedef struct { uint32_t h[8]; uint64_t len; unsigned char b[64]; size_t n; } Sha;
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))
static void sha_block(Sha *s, const unsigned char *p) {
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | p[4*i+1] << 16 | p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ w[i-15] >> 3;
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ w[i-2] >> 10;
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(Sha *s) {
    static const uint32_t iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(s->h, iv, sizeof iv); s->len = 0; s->n = 0;
}
static void sha_add(Sha *s, const void *data, size_t len) {
    const unsigned char *p = data;
    s->len += len;
    while (len--) { s->b[s->n++] = *p++; if (s->n == 64) { sha_block(s, s->b); s->n = 0; } }
}
static void sha_done(Sha *s, unsigned char out[32]) {
    uint64_t bits = s->len * 8;
    unsigned char pad = 0x80, z = 0;
    sha_add(s, &pad, 1);
    while (s->n != 56) sha_add(s, &z, 1);
    for (int i = 7; i >= 0; i--) { unsigned char c = bits >> (8 * i); sha_add(s, &c, 1); }
    for (int i = 0; i < 8; i++) { out[4*i] = s->h[i] >> 24; out[4*i+1] = s->h[i] >> 16; out[4*i+2] = s->h[i] >> 8; out[4*i+3] = s->h[i]; }
}
/* HMAC-SHA256(password, nonce || role) */
static void proof(const unsigned char nonce[16], const char *role, unsigned char out[32]) {
    unsigned char k[64] = {0}, ip[64], op[64], inner[32];
    size_t pl = strlen(password);
    if (pl > 64) { Sha s; sha_init(&s); sha_add(&s, password, pl); sha_done(&s, k); } else memcpy(k, password, pl);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    Sha s;
    sha_init(&s); sha_add(&s, ip, 64); sha_add(&s, nonce, 16); sha_add(&s, role, strlen(role)); sha_done(&s, inner);
    sha_init(&s); sha_add(&s, op, 64); sha_add(&s, inner, 32); sha_done(&s, out);
}
static void random_bytes(unsigned char *b, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0 || read(fd, b, n) != (ssize_t)n) { for (size_t i = 0; i < n; i++) b[i] = rand(); }
    if (fd >= 0) close(fd);
}

/* ---- MAC address table ---- */
#define MACS 512
static struct { unsigned char mac[6]; int port; time_t seen; } macs[MACS];

static void learn(const unsigned char *mac, int port) {
    if (mac[0] & 1) return;
    int free_slot = -1, oldest = 0;
    for (int i = 0; i < MACS; i++) {
        if (macs[i].seen && !memcmp(macs[i].mac, mac, 6)) { macs[i].port = port; macs[i].seen = time(NULL); return; }
        if (!macs[i].seen && free_slot < 0) free_slot = i;
        if (macs[i].seen < macs[oldest].seen) oldest = i;
    }
    int i = free_slot >= 0 ? free_slot : oldest;
    memcpy(macs[i].mac, mac, 6); macs[i].port = port; macs[i].seen = time(NULL);
}
static int lookup(const unsigned char *mac) {
    if (mac[0] & 1) return -2;
    for (int i = 0; i < MACS; i++) if (macs[i].seen && !memcmp(macs[i].mac, mac, 6)) return macs[i].port;
    return -2;
}

/* ---- ports ---- */
static int new_port(int fd, int kind, int state, const char *name) {
    for (int p = 0; p < MAXPORTS; p++)
        if (ports[p].fd < 0) {
            memset(&ports[p], 0, sizeof ports[p]);
            ports[p].fd = fd; ports[p].kind = kind; ports[p].state = state; ports[p].block = -1;
            snprintf(ports[p].name, sizeof ports[p].name, "%s", name);
            return p;
        }
    close(fd);
    return -1;
}

static void port_close(int p, const char *why) {
    logm("%s %s: %s", ports[p].kind == LOCAL ? "cabinet" : "PC", ports[p].name, why);
    close(ports[p].fd);
    ports[p].fd = -1;
    for (int i = 0; i < MACS; i++) if (macs[i].seen && macs[i].port == p) macs[i].seen = 0;
    if (p == member_port) { member_port = -1; next_connect = time(NULL) + 5; }
}

static void raw_send(int p, const void *data, size_t len) {
    if (send(ports[p].fd, data, len, MSG_NOSIGNAL) != (ssize_t)len) port_close(p, "write failed");
}

static void port_send(int p, const unsigned char *frame, size_t len) {
    unsigned char hdr[4] = { len >> 24, len >> 16, len >> 8, len };
    struct iovec iov[2] = { { hdr, 4 }, { (void *)frame, len } };
    struct msghdr m = { .msg_iov = iov, .msg_iovlen = 2 };
    if (sendmsg(ports[p].fd, &m, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 && errno != EAGAIN) port_close(p, "gone");
}

/* ---- optional capture (MEGALAN_PCAP) ---- */
static FILE *pcap;
static void pcap_open(const char *path) {
    pcap = fopen(path, "ab");
    if (!pcap) { logm("capture: %s: %s", path, strerror(errno)); return; }
    if (ftell(pcap) == 0) {
        struct { uint32_t magic; uint16_t major, minor; int32_t zone; uint32_t sigfigs, snaplen, link; }
            h = { 0xa1b2c3d4, 2, 4, 0, 0, MAXFRAME, 1 };              /* LINKTYPE_ETHERNET */
        fwrite(&h, sizeof h, 1, pcap);
    }
    logm("capture: writing frames to %s", path);
}
static void pcap_frame(const unsigned char *f, size_t len) {
    if (!pcap) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint32_t rec[4] = { (uint32_t)ts.tv_sec, (uint32_t)(ts.tv_nsec / 1000), (uint32_t)len, (uint32_t)len };
    fwrite(rec, sizeof rec, 1, pcap);
    fwrite(f, 1, len, pcap);
    fflush(pcap);
}

/* forward a frame that came in on `from` (a port or ROUTER). Router traffic stays on this PC:
 * frames from the router never go to other PCs, frames from other PCs never reach the router. */
static void switch_frame(int from, const unsigned char *f, size_t len) {
    if (len < 14) return;
    pcap_frame(f, len);
    int from_uplink = from >= 0 && ports[from].kind == UPLINK;
    learn(f + 6, from);
    int to = lookup(f);
    if (to == from) return;
    if (to == ROUTER) { if (!from_uplink && slirp) slirp_input(slirp, f, len); return; }
    if (to >= 0) {
        if (ports[to].fd >= 0 && ports[to].state == UP && !(from == ROUTER && ports[to].kind == UPLINK))
            port_send(to, f, len);
        return;
    }
    for (int p = 0; p < MAXPORTS; p++) {
        if (p == from || ports[p].fd < 0 || ports[p].state != UP) continue;
        if (from == ROUTER && ports[p].kind == UPLINK) continue;
        if (from_uplink && ports[p].kind == UPLINK && !is_hub) continue;
        port_send(p, f, len);
    }
    if (from != ROUTER && !from_uplink && slirp) slirp_input(slirp, f, len);
}

/* ---- libslirp glue ---- */
static ssize_t cb_send(const void *buf, size_t len, void *opaque) { (void)opaque; switch_frame(ROUTER, buf, len); return len; }
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

static struct pollfd pfd[1024];
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

/* this PC's router, once its address block is known */
static void start_router(int block) {
    my_block = block;
    char start[32];
    snprintf(start, sizeof start, "10.0.2.%d", 16 + 16 * block);
    SlirpConfig cfg = { .version = 6,   /* 6: register_poll_socket, not the old register_poll_fd */
                        .in_enabled = true, .if_mtu = 1500, .if_mru = 1500, .disable_host_loopback = true };
    inet_pton(AF_INET, "10.0.2.0", &cfg.vnetwork);
    inet_pton(AF_INET, "255.255.255.0", &cfg.vnetmask);
    inet_pton(AF_INET, "10.0.2.2", &cfg.vhost);
    inet_pton(AF_INET, start, &cfg.vdhcp_start);
    inet_pton(AF_INET, "10.0.2.3", &cfg.vnameserver);
    static const SlirpCb cb = { .send_packet = cb_send, .guest_error = cb_error, .clock_get_ns = cb_clock,
                                .timer_free = cb_timer_free, .timer_mod = cb_timer_mod, .notify = cb_notify,
                                .timer_new_opaque = cb_timer_new_opaque,
                                .register_poll_socket = cb_register, .unregister_poll_socket = cb_register };
    slirp = slirp_new(&cfg, &cb, NULL);
    if (!slirp) { logm("router failed to start"); exit(1); }
    logm("router up: this PC's cabinets get 10.0.2.%d-%d (block %d)", 16 + 16 * block, 31 + 16 * block, block);
}

/* ---- the link between PCs ---- */
static void member_connect(void) {
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *ai = NULL;
    next_connect = time(NULL) + 10;
    if (getaddrinfo(hub_host, hub_port, &hints, &ai) != 0 || !ai) { logm("hub %s: name not found", hub_host); return; }
    int fd = socket(ai->ai_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) < 0 && errno != EINPROGRESS) { close(fd); fd = -1; }
    freeaddrinfo(ai);
    if (fd < 0) { logm("hub %s:%s: can't connect (%s)", hub_host, hub_port, strerror(errno)); return; }
    char name[64];
    snprintf(name, sizeof name, "hub %.58s", hub_host);
    member_port = new_port(fd, UPLINK, CONNECTING, name);
}

static void hub_accept(int ls) {
    struct sockaddr_storage sa;
    socklen_t sl = sizeof sa;
    int c = accept4(ls, (struct sockaddr *)&sa, &sl, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (c < 0) return;
    char host[64] = "?";
    getnameinfo((struct sockaddr *)&sa, sl, host, sizeof host, NULL, 0, NI_NUMERICHOST);
    int one = 1;
    setsockopt(c, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    int p = new_port(c, UPLINK, AUTH_WAIT, host);
    if (p < 0) return;
    random_bytes(ports[p].nonce, 16);
    unsigned char hello[24];
    memcpy(hello, MAGIC, 8); memcpy(hello + 8, ports[p].nonce, 16);
    raw_send(p, hello, sizeof hello);
}

static int block_free(int b) {
    if (b == my_block) return 0;
    for (int p = 0; p < MAXPORTS; p++) if (ports[p].fd >= 0 && ports[p].kind == UPLINK && ports[p].block == b) return 0;
    return 1;
}

/* handshake bytes on an uplink; returns 1 once it is UP (remaining bytes are frames) */
static void uplink_handshake(int p) {
    Port *pt = &ports[p];
    if (pt->state == AUTH_WAIT && pt->have >= 8 + 32 + 1) {          /* hub: the member's proof */
        unsigned char want[32];
        proof(pt->nonce, "member", want);
        if (memcmp(pt->buf, MAGIC, 8) || memcmp(pt->buf + 8, want, 32)) { port_close(p, "wrong password"); return; }
        int b = pt->buf[40];
        if (b >= BLOCKS || !block_free(b)) { b = -1; for (int i = 1; i < BLOCKS; i++) if (block_free(i)) { b = i; break; } }
        if (b < 0) { port_close(p, "no address block left (14 PCs at most)"); return; }
        pt->block = b;
        unsigned char ok[2 + 1 + 32];
        ok[0] = 'O'; ok[1] = 'K'; ok[2] = b;
        proof(pt->nonce, "hub", ok + 3);
        memmove(pt->buf, pt->buf + 41, pt->have - 41); pt->have -= 41;
        pt->state = UP;
        raw_send(p, ok, sizeof ok);
        logm("PC %s joined (block %d: 10.0.2.%d-%d)", pt->name, b, 16 + 16 * b, 31 + 16 * b);
    } else if (pt->state == HELLO_WAIT && pt->have >= 24) {         /* member: the hub's challenge */
        if (memcmp(pt->buf, MAGIC, 8)) { port_close(p, "not a megalan hub"); return; }
        memcpy(pt->nonce, pt->buf + 8, 16);
        unsigned char reply[8 + 32 + 1];
        memcpy(reply, MAGIC, 8);
        proof(pt->nonce, "member", reply + 8);
        reply[40] = my_block >= 0 ? my_block : 0xff;
        memmove(pt->buf, pt->buf + 24, pt->have - 24); pt->have -= 24;
        pt->state = OK_WAIT;
        raw_send(p, reply, sizeof reply);
    } else if (pt->state == OK_WAIT && pt->have >= 35) {            /* member: accepted? */
        unsigned char want[32];
        proof(pt->nonce, "hub", want);
        if (pt->buf[0] != 'O' || pt->buf[1] != 'K' || memcmp(pt->buf + 3, want, 32)) { port_close(p, "hub rejected us (password?)"); return; }
        int b = pt->buf[2];
        memmove(pt->buf, pt->buf + 35, pt->have - 35); pt->have -= 35;
        pt->state = UP;
        logm("joined the hub %s", pt->name);
        if (my_block < 0) start_router(b);
        else if (b != my_block) logm("warning: the hub gave block %d, this PC already uses %d", b, my_block);
    }
}

static void port_read(int p) {
    Port *pt = &ports[p];
    ssize_t n = read(pt->fd, pt->buf + pt->have, sizeof pt->buf - pt->have);
    if (n <= 0) { if (n == 0 || (errno != EAGAIN && errno != EINTR)) port_close(p, "disconnected"); return; }
    pt->have += n;
    while (pt->fd >= 0 && pt->state != UP) {
        int before = pt->state;
        uplink_handshake(p);
        if (pt->fd < 0 || pt->state == before) return;
    }
    while (pt->fd >= 0 && pt->have >= 4) {
        size_t len = (size_t)pt->buf[0] << 24 | pt->buf[1] << 16 | pt->buf[2] << 8 | pt->buf[3];
        if (len > MAXFRAME) { port_close(p, "bad frame"); return; }
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

static int split_hostport(const char *s, char *host, size_t hl, char *port, size_t pl) {
    const char *c = strrchr(s, ':');
    if (!c) { snprintf(host, hl, "%s", ""); snprintf(port, pl, "%s", s); return 1; }
    snprintf(host, hl, "%.*s", (int)(c - s), s);
    snprintf(port, pl, "%s", c + 1);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: megalan SOCKET [--listen [ADDR:]PORT | --connect HOST:PORT]\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGSEGV, on_crash);
    signal(SIGABRT, on_crash);
    for (int p = 0; p < MAXPORTS; p++) ports[p].fd = -1;
    const char *listen_on = NULL;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--listen")) listen_on = argv[i + 1];
        else if (!strcmp(argv[i], "--connect")) { split_hostport(argv[i + 1], hub_host, sizeof hub_host, hub_port, sizeof hub_port); is_member = 1; }
    }
    is_hub = listen_on != NULL;
    if (is_hub && is_member) { fprintf(stderr, "megalan: --listen or --connect, not both\n"); return 2; }
    if (getenv("MEGALAN_PCAP") && *getenv("MEGALAN_PCAP")) pcap_open(getenv("MEGALAN_PCAP"));
    password = getenv("MEGALAN_PASSWORD");
    if ((is_hub || is_member) && (!password || strlen(password) < 8)) {
        fprintf(stderr, "megalan: set MEGALAN_PASSWORD (at least 8 characters) to join PCs\n"); return 2;
    }

    int ls = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", argv[1]);
    unlink(argv[1]);
    if (bind(ls, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(ls, 8) < 0) { perror(argv[1]); return 1; }

    int ts = -1;
    if (is_hub) {
        char host[256], port[16];
        split_hostport(listen_on, host, sizeof host, port, sizeof port);
        struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM, .ai_flags = AI_PASSIVE }, *ai = NULL;
        if (getaddrinfo(*host ? host : NULL, port, &hints, &ai) != 0 || !ai) { fprintf(stderr, "megalan: bad --listen %s\n", listen_on); return 2; }
        ts = socket(ai->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
        int one = 1;
        setsockopt(ts, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (bind(ts, ai->ai_addr, ai->ai_addrlen) < 0 || listen(ts, 8) < 0) { perror("megalan --listen"); return 1; }
        freeaddrinfo(ai);
        start_router(0);
        logm("hub: other PCs join on %s", listen_on);
    } else if (!is_member) {
        start_router(0);
    } else {
        member_since = time(NULL);
        member_connect();
    }
    logm("cabinets connect on %s", argv[1]);

    time_t empty_since = time(NULL);
    for (;;) {
        /* a member without a router yet: after 15 s without the hub, run on its own (block 13,
         * so it can't collide with the hub's own cabinets once the hub appears) */
        if (is_member && !slirp && time(NULL) - member_since > 15) {
            logm("hub not reachable yet: starting this PC's router on its own");
            start_router(BLOCKS - 1);
        }
        if (is_member && member_port < 0 && time(NULL) >= next_connect) member_connect();

        npfd = 0;
        uint32_t timeout = 1000;
        for (Timer *t = timers; t; t = t->next)
            if (t->expire_ms >= 0) {
                int64_t left = t->expire_ms - now_ns() / 1000000;
                if (left < 0) left = 0;
                if ((uint32_t)left < timeout) timeout = left;
            }
        if (slirp) slirp_pollfds_fill_socket(slirp, &timeout, cb_add_poll, NULL);
        int ils = npfd;
        pfd[npfd++] = (struct pollfd){ .fd = ls, .events = POLLIN };
        int its = -1;
        if (ts >= 0) { its = npfd; pfd[npfd++] = (struct pollfd){ .fd = ts, .events = POLLIN }; }
        int portidx[MAXPORTS];
        for (int p = 0; p < MAXPORTS; p++) {
            portidx[p] = -1;
            if (ports[p].fd < 0) continue;
            portidx[p] = npfd;
            pfd[npfd++] = (struct pollfd){ .fd = ports[p].fd, .events = ports[p].state == CONNECTING ? POLLOUT : POLLIN };
        }
        int r = poll(pfd, npfd, (int)timeout);
        if (slirp) slirp_pollfds_poll(slirp, r < 0, cb_get_revents, NULL);
        for (int again = 1; again && slirp;) {     /* timer handlers may free or re-arm timers */
            again = 0;
            int64_t nowms = now_ns() / 1000000;
            for (Timer *t = timers; t; t = t->next)
                if (t->expire_ms >= 0 && t->expire_ms <= nowms) {
                    t->expire_ms = -1; slirp_handle_timer(slirp, t->id, t->cb_opaque); again = 1; break;
                }
        }
        if (r > 0) {
            if (pfd[ils].revents & POLLIN) {
                int c = accept4(ls, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
                char name[32];
                static int ncab;
                snprintf(name, sizeof name, "#%d", ++ncab);
                if (c >= 0 && new_port(c, LOCAL, UP, name) >= 0) logm("cabinet %s connected", name);
            }
            if (its >= 0 && (pfd[its].revents & POLLIN)) hub_accept(ts);
            for (int p = 0; p < MAXPORTS; p++) {
                if (portidx[p] < 0 || ports[p].fd < 0) continue;
                short re = pfd[portidx[p]].revents;
                if (ports[p].state == CONNECTING) {
                    if (!(re & (POLLOUT | POLLERR | POLLHUP))) continue;
                    int err = 0; socklen_t el = sizeof err;
                    getsockopt(ports[p].fd, SOL_SOCKET, SO_ERROR, &err, &el);
                    if (err) { logm("hub %s:%s: %s", hub_host, hub_port, strerror(err)); port_close(p, "can't connect"); continue; }
                    int one = 1;
                    setsockopt(ports[p].fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
                    setsockopt(ports[p].fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    ports[p].state = HELLO_WAIT;
                    continue;
                }
                if (re & (POLLIN | POLLHUP | POLLERR)) port_read(p);
            }
        }
        int any = 0;
        for (int p = 0; p < MAXPORTS; p++)
            if (ports[p].fd >= 0 && (ports[p].kind == LOCAL || is_hub)) any = 1;
        if (any) empty_since = time(NULL);
        else if (time(NULL) - empty_since > 60) { logm("no cabinets for 60 s: exiting"); break; }
    }
    unlink(argv[1]);
    return 0;
}
