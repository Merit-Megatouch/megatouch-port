/*
 * lantap — the cabinet's network port on a megalan (virtual cabinet network). Runs inside the
 * cabinet's sandbox (its own network namespace): creates the tap device eth0 and passes every
 * Ethernet frame between it and the megalan switch's unix socket (4-byte big-endian length +
 * frame). The cabinet's own network manager and DHCP client then configure eth0 as usual.
 *
 *   lantap SOCKET IFNAME [MAC]      (started by xinit.sh when MEGA_LAN is set)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <net/if_arp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MAXFRAME 9216

static int write_all(int fd, const unsigned char *b, size_t n) {
    while (n) {
        ssize_t w = write(fd, b, n);
        if (w < 0) { if (errno == EINTR || errno == EAGAIN) continue; return -1; }
        b += w; n -= w;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: lantap SOCKET IFNAME [MAC]\n"); return 2; }
    int tap = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (tap < 0) { perror("[lantap] /dev/net/tun"); return 1; }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", argv[2]);
    if (ioctl(tap, TUNSETIFF, &ifr) < 0) { perror("[lantap] TUNSETIFF"); return 1; }
    if (argc > 3) {                                   /* a stable MAC: the same DHCP address each time */
        unsigned m[6];
        if (sscanf(argv[3], "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
            int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            struct ifreq hw;
            memset(&hw, 0, sizeof hw);
            snprintf(hw.ifr_name, IFNAMSIZ, "%s", argv[2]);
            hw.ifr_hwaddr.sa_family = ARPHRD_ETHER;
            for (int i = 0; i < 6; i++) hw.ifr_hwaddr.sa_data[i] = (char)m[i];
            if (ioctl(s, SIOCSIFHWADDR, &hw) < 0) perror("[lantap] SIOCSIFHWADDR");
            close(s);
        }
    }
    int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", argv[1]);
    if (connect(sock, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("[lantap] connect"); return 1; }
    fprintf(stderr, "[lantap] %s connected to %s\n", argv[2], argv[1]);

    static unsigned char in[4 + MAXFRAME], out[4 + MAXFRAME];
    size_t have = 0;
    struct pollfd p[2] = { { tap, POLLIN, 0 }, { sock, POLLIN, 0 } };
    for (;;) {
        if (poll(p, 2, -1) < 0) { if (errno == EINTR) continue; perror("[lantap] poll"); break; }
        if (p[0].revents & POLLIN) {                   /* cabinet → switch */
            ssize_t n = read(tap, out + 4, MAXFRAME);
            if (n > 0) {
                out[0] = n >> 24; out[1] = n >> 16; out[2] = n >> 8; out[3] = n;
                if (write_all(sock, out, 4 + n) < 0) { perror("[lantap] write to switch"); break; }
            }
        }
        if (p[1].revents & (POLLIN | POLLHUP | POLLERR)) {   /* switch → cabinet */
            ssize_t n = read(sock, in + have, sizeof in - have);
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (n < 0) { perror("[lantap] read from switch"); break; }
            if (n == 0) break;
            have += n;
            while (have >= 4) {
                size_t len = (size_t)in[0] << 24 | in[1] << 16 | in[2] << 8 | in[3];
                if (len > MAXFRAME) { fprintf(stderr, "[lantap] bad frame\n"); return 1; }
                if (have < 4 + len) break;
                if (write(tap, in + 4, len) < 0 && errno != EIO) perror("[lantap] tap write");
                memmove(in, in + 4 + len, have - 4 - len);
                have -= 4 + len;
            }
        }
    }
    fprintf(stderr, "[lantap] switch gone\n");
    return 1;
}
