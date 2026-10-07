/*
 * enumtag — print the cabinet's names for game options and game IDs, using its own libenums.so
 * (runs inside the loader sandbox: scripts/loader.sh run /opt/fakeio/enumtag ...).
 *
 *   enumtag option 0 1 2 ...    → "0 SOL_FREE_GAME_INX" ...
 *   enumtag game 30 232 ...     → "30 G_SRUN21" ...
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3 || (strcmp(argv[1], "option") && strcmp(argv[1], "game"))) {
        fprintf(stderr, "usage: enumtag option|game N...\n");
        return 2;
    }
    void *h = dlopen("/usr/local/lib/libenums.so", RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    const char *sym = !strcmp(argv[1], "option")
        ? "_ZN15xml_gameoptions24enum_GameOptionIndex_tagENS_15GameOptionIndexEb"
        : "_ZN12xml_gameinfo16enum_GameIds_tagENS_7GameIdsEb";
    const char *(*tag)(int, int) = (const char *(*)(int, int))dlsym(h, sym);
    if (!tag) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    for (int i = 2; i < argc; i++) {
        int n = atoi(argv[i]);
        const char *s = tag(n, 0);
        printf("%d %s\n", n, s ? s : "?");
    }
    return 0;
}
