/*
 * cpuinfo — the PC's /proc/cpuinfo with "cpu MHz" set to the rate of the CPU's time-stamp counter.
 *   cpuinfo > file      (scripts/loader.sh binds the file over the sandbox's /proc/cpuinfo)
 *
 * The cabinet's clock (start's SystemTimer(), and copies in Luxor, Chainz 2, Six Stars,
 * opsetup...) is rdtsc divided by the first "cpu MHz" in /proc/cpuinfo, read once at start.
 * On the ION's Atom that was the counter's rate. A modern PC's counter runs at a fixed rate,
 * but "cpu MHz" is the core's current speed, often far lower when idle: the cabinet's clock
 * then runs several times too fast (animations, game timers; sound is unaffected), differently
 * on each start. Here the counter is timed against the system clock instead.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <x86intrin.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* counter ticks per microsecond over 100 ms, the counter read between two clock reads */
static double tsc_mhz(void) {
    double t0 = now();
    uint64_t c0 = __rdtsc();
    double t0b = now();
    struct timespec d = { 0, 100000000 };
    nanosleep(&d, NULL);
    double t1 = now();
    uint64_t c1 = __rdtsc();
    double t1b = now();
    double dt = (t1 + t1b) / 2 - (t0 + t0b) / 2;
    return dt > 0 ? (c1 - c0) / dt / 1e6 : 0;
}

int main(void) {
    double mhz = tsc_mhz();
    if (mhz < 100 || mhz > 20000) { fprintf(stderr, "cpuinfo: counter rate %.1f MHz?\n", mhz); return 1; }
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[4096];
    int replaced = 0;
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "cpu MHz", 7)) { printf("cpu MHz\t\t: %.3f\n", mhz); replaced++; }
            else fputs(line, stdout);
        }
        fclose(f);
    }
    /* (some virtual machines leave it out; the cabinet would divide by 0) */
    if (!replaced) printf("cpu MHz\t\t: %.3f\n", mhz);
    return 0;
}
