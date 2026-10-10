/*
 * enumtag — print the cabinet's names for game options and game IDs, using its own libenums.so
 * (runs inside the loader sandbox: scripts/loader.sh run /opt/fakeio/enumtag ...).
 *
 *   enumtag option 0 1 2 ...    → "0 SOL_FREE_GAME_INX" ...
 *   enumtag game 30 232 ...     → "30 G_SRUN21" ...
 *   enumtag language 0 1 ...    → "0 ENGLISH" ...   (liblocale.so: key language-mask bits)
 *   enumtag country 0 1 ...     → "0 USA" ...
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    static const char *const kinds[][3] = {
        {"option", "/usr/local/lib/libenums.so", "_ZN15xml_gameoptions24enum_GameOptionIndex_tagENS_15GameOptionIndexEb"},
        {"game", "/usr/local/lib/libenums.so", "_ZN12xml_gameinfo16enum_GameIds_tagENS_7GameIdsEb"},
        {"language", "/usr/local/lib/liblocale.so", "_ZN6Locale18enum_Languages_tagENS_9LanguagesEb"},
        {"country", "/usr/local/lib/liblocale.so", "_ZN6Locale18enum_Countries_tagENS_9CountriesEb"},
    };
    int k = -1;
    for (int i = 0; argc >= 3 && i < 4; i++) if (!strcmp(argv[1], kinds[i][0])) k = i;
    if (k < 0) {
        fprintf(stderr, "usage: enumtag option|game|language|country N...\n");
        return 2;
    }
    void *h = dlopen(kinds[k][1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    const char *sym = kinds[k][2];
    const char *(*tag)(int, int) = (const char *(*)(int, int))dlsym(h, sym);
    if (!tag) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    for (int i = 2; i < argc; i++) {
        int n = atoi(argv[i]);
        const char *s = tag(n, 0);
        printf("%d %s\n", n, s ? s : "?");
    }
    return 0;
}
