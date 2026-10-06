// Dump the cabinet's game-ID table (xml_gameinfo::GameIds) using the engine's own libenums.so.
//   gameids            -> "<id>\t<NAME>" for every id
//   gameids G_TRIX     -> numeric id for one name (-1 if unknown)
#include <cstdio>
#include <cstring>
namespace xml_gameinfo {
enum GameIds : int {};
const char* enum_GameIds_tag(GameIds, bool);
GameIds enum_GameIds_value(const char*);
}
int main(int argc, char** argv) {
    using namespace xml_gameinfo;
    if (argc > 1) { printf("%d\n", (int)enum_GameIds_value(argv[1])); return 0; }
    for (int i = 0; i < 4096; i++) {
        const char* t = enum_GameIds_tag((GameIds)i, false);
        if (t && *t && strcmp(t, "UNKNOWN") != 0) printf("%d\t%s\n", i, t);
    }
}
