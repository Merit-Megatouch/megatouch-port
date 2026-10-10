/*
 * gameevents — adds "a game started / ended" to the fake board's event log (events.jsonl).
 *
 * The loader starts every game through ExecuteGameID (start): it calls Logger::SetGameID(id)
 * before the game runs and Logger::SetGameID(previous id) after it returns. Logger lives in
 * liblogging.so, so a definition here wins over it; this one notes the change and calls the
 * original. The same path runs the cabinet's own screens (Setup, Calibrate, ...: IDs from
 * 10000 up), which are reported as "screen" instead of "game".
 * Event lines: {"t":..., "type":"game", "state":"start"|"end", "id":N, "tag":"..."}
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define SETGAMEID "_ZN6Logger9SetGameIDEN12xml_gameinfo7GameIdsE"
#define GAMETAG   "_ZN12xml_gameinfo16enum_GameIds_tagENS_7GameIdsEb"

static int stack[8], depth;          /* IDs of the running games (games can start games) */

static void event(const char *state, int id) {
    static const char *(*tag)(int, int);
    if (!tag) tag = (const char *(*)(int, int))dlsym(RTLD_DEFAULT, GAMETAG);
    const char *t = tag ? tag(id, 0) : NULL;
    char dir[512], line[512];
    const char *d = getenv("MEGAIO_DIR");
    snprintf(dir, sizeof dir, "%s/events.jsonl", d ? d : "/var/merit/fakeio");
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int n = snprintf(line, sizeof line, "{\"t\":%lld.%03d,\"type\":\"%s\",\"state\":\"%s\",\"id\":%d,\"tag\":\"%s\"}\n",
                     (long long)tv.tv_sec, (int)(tv.tv_usec / 1000), id >= 10000 ? "screen" : "game", state, id,
                     t && !strpbrk(t, "\"\\") ? t : "");
    int fd = open(dir, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd < 0) return;
    if (write(fd, line, n) < 0) { /* nothing to do */ }
    close(fd);
}

/* ExecuteGameID calls SetGameID twice: at +0x4f (the new ID; returns to +0x54) and at +0x684
 * (the previous ID, after the game; returns to +0x689). The return address says which one. */
#define EXECUTEGAMEID "_Z13ExecuteGameIDN12xml_gameinfo7GameIdsEPKcS2_"

void _ZN6Logger9SetGameIDEN12xml_gameinfo7GameIdsE(int id) {
    static void (*real)(int);
    static char *exec;
    if (!real) {
        real = (void (*)(int))dlsym(RTLD_NEXT, SETGAMEID);
        exec = (char *)dlsym(RTLD_DEFAULT, EXECUTEGAMEID);
    }
    char *ret = (char *)__builtin_return_address(0);
    if (exec && ret > exec && ret < exec + 0x1000) {
        if (ret < exec + 0x100) {                      /* a game or screen starts */
            if (depth < 8) stack[depth] = id;
            depth++;
            event("start", id);
        } else if (depth > 0) {                        /* it returned */
            depth--;
            event("end", depth < 8 ? stack[depth] : -1);
        }
    }
    if (real) real(id);
}
