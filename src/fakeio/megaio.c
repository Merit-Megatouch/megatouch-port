/*
 * megaio — drive the fake I/O board while the loader runs.
 *   megaio [-d dir] coin [channel] [count]   drop coins (channel 0-7, default 0)
 *   megaio [-d dir] setup | calibrate        press the SETUP / CALIBRATE button for 0.5 s
 *   megaio [-d dir] dip <switch 1-8> on|off  DIP bank DS1 (byte 8, active low, switch N = bit 8-N)
 *   megaio [-d dir] fob <16 hex digits> [operator]|off   touch / remove a key on the front reader
 *                   (a player key, or with "operator" an operator key; IDs need family 02)
 *   megaio [-d dir] print                    plug in the books printer until the cabinet has printed
 *                   (it prints on the attract screen; the file lands in <dir>/printouts)
 *   megaio [-d dir] printer on|off           plug the books printer in / out by hand
 *   megaio [-d dir] byte N VALUE             set raw status byte N (8-23)
 *   megaio [-d dir] status                   show counters, outputs and poll count
 * dir defaults to $MEGAIO_DIR, else ../var/merit/fakeio next to this binary (build/loader/bin).
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include "megaio.h"

static int usage(void) {
    fprintf(stderr, "usage: megaio [-d dir] coin [ch] [n] | setup | calibrate | dip N on|off | byte N V | fob <hexid> [operator]|off | print | printer on|off | status\n");
    return 2;
}

int main(int argc, char **argv) {
    const char *dir = getenv("MEGAIO_DIR");
    if (argc > 2 && !strcmp(argv[1], "-d")) { dir = argv[2]; argv += 2; argc -= 2; }
    static char def[4096];
    if (!dir) {
        ssize_t n = readlink("/proc/self/exe", def, sizeof def - 64);
        if (n > 0) {
            def[n] = 0;
            char *slash = strrchr(def, '/');
            if (slash) { strcpy(slash, "/../var/merit/fakeio"); dir = def; }
        }
    }
    if (!dir) { fprintf(stderr, "megaio: set MEGAIO_DIR or pass -d\n"); return 2; }
    if (argc < 2) return usage();
    char p[4200];
    snprintf(p, sizeof p, "%s/ctl", dir);
    int fd = open(p, O_RDWR | O_CREAT, 0666);
    if (fd < 0 || ftruncate(fd, sizeof(struct megaio_ctl)) != 0) { perror(p); return 1; }
    struct megaio_ctl *c = mmap(NULL, sizeof *c, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (c == MAP_FAILED) { perror("mmap"); return 1; }
    if (c->magic != MEGAIO_MAGIC) { memset(c, 0, sizeof *c); c->magic = MEGAIO_MAGIC; c->st[8] = 0xFF; }

    const char *cmd = argv[1];
    if (!strcmp(cmd, "coin")) {
        int ch = argc > 2 ? atoi(argv[2]) : 0, n = argc > 3 ? atoi(argv[3]) : 1;
        if (ch < 0 || ch > 7 || n < 1 || n > 127) return usage();
        c->coins[ch] += n;
        printf("coin channel %d +%d\n", ch, n);
    } else if (!strcmp(cmd, "setup") || !strcmp(cmd, "calibrate")) {
        int bit = !strcmp(cmd, "setup") ? 0 : 1;
        c->st[9] |= 1u << bit;
        usleep(500000);
        c->st[9] &= ~(1u << bit);
        printf("%s pressed\n", cmd);
    } else if (!strcmp(cmd, "dip") && argc > 3) {
        int sw = atoi(argv[2]);
        if (sw < 1 || sw > 8) return usage();
        if (!strcmp(argv[3], "on")) c->st[8] &= ~(1u << (8 - sw)); else c->st[8] |= 1u << (8 - sw);
        printf("DS1 = %02x\n", c->st[8]);
    } else if (!strcmp(cmd, "fob") && argc > 2) {
        if (!strcmp(argv[2], "off")) { c->fob_present = 0; puts("fob removed"); }
        else {
            if (strlen(argv[2]) != 16) return usage();
            for (int i = 0; i < 8; i++) { unsigned v; sscanf(argv[2] + 2 * i, "%2x", &v); c->fob_id[i] = v; }
            c->fob_kind = argc > 3 && !strcmp(argv[3], "operator");
            c->fob_present = 1;
            printf("%s key %s touching\n", c->fob_kind ? "operator" : "player", argv[2]);
        }
    } else if (!strcmp(cmd, "print")) {
        uint32_t jobs = c->print_jobs;
        c->printer_auto = 1;
        c->st[9] |= 4;
        printf("printer plugged in; waiting for the attract screen to print the books...\n");
        fflush(stdout);
        for (int i = 0; i < 300 && c->print_jobs == jobs; i++) usleep(500000);
        if (c->print_jobs == jobs) {
            c->st[9] &= ~4; c->printer_auto = 0;
            printf("nothing printed in 150 s (is the cabinet on its attract screen?); printer unplugged\n");
            return 1;
        }
        printf("printed: see %s/printouts\n", dir);
    } else if (!strcmp(cmd, "printer") && argc > 2) {
        c->printer_auto = 0;
        if (!strcmp(argv[2], "on")) c->st[9] |= 4; else c->st[9] &= ~4;
        printf("printer %s\n", c->st[9] & 4 ? "plugged in" : "unplugged");
    } else if (!strcmp(cmd, "byte") && argc > 3) {
        int n = atoi(argv[2]);
        if (n < 8 || n > 23) return usage();
        c->st[n] = strtoul(argv[3], NULL, 0);
        printf("status byte %d = %02x\n", n, c->st[n]);
    } else if (!strcmp(cmd, "status")) {
        printf("polls %u  heartbeat %#x  lockout flags %#x  outputs", c->polls, c->heartbeat, c->outputs_flags);
        for (int i = 0; i < 6; i++) printf(" %02x", c->outputs[i]);
        printf("\nmeters: coin %u, TournaMAXX %u  print jobs %u%s", c->meters[0], c->meters[1], c->print_jobs,
               c->st[9] & 4 ? " (printer plugged in)" : "");
        printf("\nstatus bytes 8-15:");
        for (int i = 8; i < 16; i++) printf(" %02x", c->st[i]);
        printf("  fob %s\n", c->fob_present ? "present" : "none");
    } else return usage();
    return 0;
}
