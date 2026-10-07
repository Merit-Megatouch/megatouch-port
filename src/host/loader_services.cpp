// Stand-ins for the cabinet "loader" services that games and engine libraries import
// directly. tools/analyze.sh lists any a new game needs that are not here yet.
// Before adding one, disassemble a caller: static vs member functions mangle the same but
// are called differently (Translator::Translate is static).
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include <cctype>
#include <sys/stat.h>
// ---------------------------------------------------------------------------
// loader services

// Player scores, indexed by player. Read by g_trix and the meritbasegame helpers.
extern "C" { int PlrScore[8]; char ef_fp_filename[256]; }

namespace xml_gameinfo { enum GameIds : int {}; }

// Translations: /usr/local/gamedata/translations/<name>.utf8 is a '|'-separated table
// KEYSTRINGID|KEYSTRING|ENGLISH|GERMAN|... Keys (and numeric IDs) map to the English column.
namespace Locale { enum Languages : int {}; }
class Translator {
public:
    static Translator* GetInstance();
    static bool LoadTranslations(char const*, bool);
    static bool LoadTranslations(xml_gameinfo::GameIds);
    static void SetCurrentLanguage(Locale::Languages);
    static char const* Translate(char const*);   // static: callers push only the string
};
#include <map>
static std::map<std::string, std::string>& tr_table() { static std::map<std::string, std::string> t; return t; }

static void tr_load_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (getenv("MEGA_DEBUG_TR")) fprintf(stderr, "[tr] %s %s\n", path.c_str(), f ? "loaded" : "missing");
    if (!f) return;
    // KEYSTRINGID|KEYSTRING|<languages...>: the language columns are in a different order in
    // each file, so the column is found by name (MEGA_LANGUAGE, default ENGLISH)
    std::string want = getenv("MEGA_LANGUAGE") ? getenv("MEGA_LANGUAGE") : "english";
    for (auto& ch : want) ch = (char)toupper((unsigned char)ch);
    char* line = nullptr; size_t cap = 0; ssize_t n; bool header = true;
    int col = 2, en_col = 2;
    auto split = [](const std::string& l) { std::vector<std::string> v; size_t a = 0, b; while ((b = l.find('|', a)) != std::string::npos) { v.push_back(l.substr(a, b - a)); a = b + 1; } v.push_back(l.substr(a)); return v; };
    while ((n = getline(&line, &cap, f)) > 0) {
        std::string l(line, n);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (header) {
            header = false;
            if (l.compare(0, 3, "\xef\xbb\xbf") == 0) l.erase(0, 3);
            if (l.find("KEYSTRING") != std::string::npos) {
                auto h = split(l);
                for (size_t i = 0; i < h.size(); i++) { if (h[i] == want) col = (int)i; if (h[i] == "ENGLISH") en_col = (int)i; }
                continue;
            }
        }
        auto v = split(l);
        if (v.size() < 2) continue;
        const std::string& id = v[0];
        const std::string& key = v[1];
        std::string en = (int)v.size() > col ? v[col] : std::string();
        if (en.empty() && (int)v.size() > en_col) en = v[en_col];
        if (en.empty()) en = key;
        std::string out;   // the tables escape newlines as a literal backslash-n
        for (size_t i = 0; i < en.size(); i++) {
            if (en[i] == '\\' && i + 1 < en.size() && en[i + 1] == 'n') { out += '\n'; i++; }
            else out += en[i];
        }
        if (!key.empty()) tr_table()[key] = out;
        if (!id.empty()) tr_table()[id] = out;
    }
    free(line);
    fclose(f);
}

#include <dirent.h>
// strings (>= 4 printable chars) in the game's library, as `strings` would list them
static std::set<std::string> lib_strings() {
    std::set<std::string> out;
    const char* home = getenv("MEGA_HOME"); const char* lib = getenv("MEGA_LIB");
    if (!home || !lib) return out;
    FILE* f = fopen((std::string(home) + "/lib/" + lib).c_str(), "rb");
    if (!f) return out;
    std::string cur; int c;
    while ((c = fgetc(f)) != EOF) {
        if (c >= 0x20 && c < 0x7f) cur += (char)c;
        else { if (cur.size() >= 4) out.insert(cur); cur.clear(); }
    }
    fclose(f);
    return out;
}
static std::string tr_guess_table() {
    std::set<std::string> strs = lib_strings();
    if (strs.empty()) return std::string();
    std::string dir = "/usr/local/gamedata/translations/", best;
    int best_n = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) return best;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() < 6 || n.compare(n.size() - 5, 5, ".utf8") || n == "SupportFiles.utf8") continue;
        FILE* f = fopen((dir + n).c_str(), "rb");
        if (!f) continue;
        char* line = nullptr; size_t cap = 0; ssize_t len; int hits = 0;
        while ((len = getline(&line, &cap, f)) > 0) {
            char* a = strchr(line, '|'); if (!a) continue;
            char* b = strchr(a + 1, '|'); if (!b || b - a - 1 < 4) continue;
            if (strs.count(std::string(a + 1, b - a - 1))) hits++;
        }
        free(line); fclose(f);
        if (hits > best_n) { best_n = hits; best = dir + n; }
    }
    closedir(d);
    if (getenv("MEGA_DEBUG_TR")) fprintf(stderr, "[tr] best table %s (%d keys)\n", best.c_str(), best_n);
    return best_n >= 3 ? best : std::string();
}

Translator* Translator::GetInstance() { static char inst[64]; return reinterpret_cast<Translator*>(inst); }
bool Translator::LoadTranslations(char const* name, bool) {
    static bool common;
    if (!common) { common = true; tr_load_file("/usr/local/gamedata/translations/SupportFiles.utf8"); }
    if (name) tr_load_file(std::string("/usr/local/gamedata/translations/") + name + ".utf8");
    return true;
}
// By GameId (Merit2d / MeritBaseGame games): the current game's table. Its file is named after
// the library, the asset folder or the support-file name (airhockey -> airshot.utf8 is loaded
// by the game itself by name).
bool Translator::LoadTranslations(xml_gameinfo::GameIds) {
    LoadTranslations(nullptr, false);
    std::vector<std::string> names;
    if (const char* l = getenv("MEGA_LIB")) { std::string n = l; if (n.size() > 3 && n.compare(n.size() - 3, 3, ".so") == 0) n.resize(n.size() - 3); names.push_back(n); }
    if (const char* a = getenv("MEGA_ASSET_DIR")) { std::string n = a; size_t k = n.rfind('/'); if (k != std::string::npos) n = n.substr(k + 1); names.push_back(n); if (n.size() > 4 && n.compare(n.size() - 4, 4, "_new") == 0) names.push_back(n.substr(0, n.size() - 4)); }
    for (auto& n : names) {
        struct stat st;
        std::string path = "/usr/local/gamedata/translations/" + n + ".utf8";
        if (stat(path.c_str(), &st) == 0) { tr_load_file(path); return true; }
    }
    // named differently (trivia -> videowhiz, wordzap -> wordster): the table sharing the most
    // keys with the strings in the game's library
    std::string best = tr_guess_table();
    if (!best.empty()) { tr_load_file(best); return true; }
    return false;
}
void Translator::SetCurrentLanguage(Locale::Languages) {}
char const* Translator::Translate(char const* s) {
    if (!s) return "";
    auto it = tr_table().find(s);
    return it != tr_table().end() ? it->second.c_str() : s;
}

// Credit-based "continue?" prompt. Home play: always continue.
class ContinueControl {
public:
    ContinueControl();
    ~ContinueControl();
    bool display(unsigned int, unsigned int);
    bool display(unsigned int, unsigned int, bool (*)());   // with a poll callback (tennis)
    bool display(unsigned int, unsigned int, unsigned int); // pool, minigolf
    bool display();                                          // beer pong
    static bool allowed();                                   // false: games skip the prompt
};
ContinueControl::ContinueControl() {}
ContinueControl::~ContinueControl() {}
bool ContinueControl::display(unsigned int, unsigned int) { return true; }
bool ContinueControl::display(unsigned int, unsigned int, bool (*)()) { return true; }
bool ContinueControl::display(unsigned int, unsigned int, unsigned int) { return true; }
bool ContinueControl::display() { return true; }
bool ContinueControl::allowed() { return false; }

class HighScoresManager {
public:
    static HighScoresManager* Instance();
    bool HighEnough(int, int);
    int Winner() const;
    int HighestScore(int);
    char const* HighestName(int);
    // newer overloads with the game's id first (legacy games and later GameDevice games)
    bool HighEnough(xml_gameinfo::GameIds, int, int, int);
    int HighestScore(xml_gameinfo::GameIds, int);
    char const* HighestName(xml_gameinfo::GameIds, int);
    void AbortGame();
    static char const* PlayerName(int player);
};
// High scores. Games keep the running score in PlrScore[player]; at game over they call
// Winner() and HighEnough(winner, ...). On the cabinet "true" opened the loader's name-entry
// screen, which we don't have — so the best score is recorded here and false is returned,
// letting the game play its own game-over sequence. One best score per game, stored in
// /var/merit/highscores/<GAME_ID>.txt (the game's private data/var/merit) as "<score> <name>".
// The name is MEGA_PLAYER_NAME, else the user name.
namespace {
struct Best { int score = 0; std::string name; };
std::string best_path() {
    const char* id = getenv("MEGA_GAME_ID");
    return std::string("/var/merit/highscores/") + (id ? id : "0") + ".txt";
}
Best& best() {
    static Best b;
    static bool loaded;
    if (!loaded) {
        loaded = true;
        if (FILE* f = fopen(best_path().c_str(), "r")) {
            char name[64] = "";
            if (fscanf(f, "%d %63[^\n]", &b.score, name) >= 1) b.name = name;
            fclose(f);
        }
    }
    return b;
}
void save_best(int score) {
    const char* who = getenv("MEGA_PLAYER_NAME");
    if (!who || !*who) who = getenv("USER");
    std::string name = who && *who ? who : "PLAYER";
    for (char& c : name) c = (char)toupper((unsigned char)c);
    best().score = score;
    best().name = name;
    mkdir("/var/merit/highscores", 0755);
    if (FILE* f = fopen(best_path().c_str(), "w")) { fprintf(f, "%d %s\n", score, name.c_str()); fclose(f); }
}
}  // namespace

HighScoresManager* HighScoresManager::Instance() { static char inst[64]; return reinterpret_cast<HighScoresManager*>(inst); }
bool HighScoresManager::HighEnough(int player, int) {
    int score = (player >= 0 && player < 8) ? PlrScore[player] : 0;
    if (score > best().score) save_best(score);
    return false;
}
int HighScoresManager::Winner() const { return 0; }
int HighScoresManager::HighestScore(int) { return best().score; }
char const* HighScoresManager::HighestName(int) { return best().name.c_str(); }
// The id is the running game's (one best score per game), so these share the file above.
// Fourplay calls HighEnough(id, 0, 0, -1): the second argument is the player index.
bool HighScoresManager::HighEnough(xml_gameinfo::GameIds, int player, int, int) { return HighEnough(player, 0); }
int HighScoresManager::HighestScore(xml_gameinfo::GameIds, int) { return best().score; }
void HighScoresManager::AbortGame() {}
// The name a player's scores are shown under (1-based; take2 copies it per seat). No player
// logins here: "Player N".
char const* HighScoresManager::PlayerName(int player) {
    static char names[9][16];
    if (player < 1 || player > 8) player = 1;
    snprintf(names[player], sizeof names[player], "Player %d", player);
    return names[player];
}
char const* HighScoresManager::HighestName(xml_gameinfo::GameIds, int) { return best().name.c_str(); }

// The cabinet's language setting. Locale::Languages: 0 = English, 3 = French, 4 = Spanish, ...
// (same order as LanguagesSupported in gamedata.xml). Games pick dictionaries/help by it.
namespace Locale {
class LanguageManager {
public:
    static LanguageManager* GetInstance();
    int Active() const;
    int Base() const;
    int LegacyFallback(Languages);
    static int IndexOf(Languages);              // static: callers pass one argument
};
LanguageManager* LanguageManager::GetInstance() { static char inst[64]; return reinterpret_cast<LanguageManager*>(inst); }
int LanguageManager::Active() const { return 0; }
int LanguageManager::Base() const { return 0; }
int LanguageManager::LegacyFallback(Languages l) { return (int)l; }
int LanguageManager::IndexOf(Languages l) { return (int)l; }
enum LocaleMasks : int {};
}
// Language-specific file/folder suffixes: English (the only language set up) has none.
void Menu_AppendLangExt(Locale::Languages, char (&)[255]) {}
void AppendLangExt(char (&)[255], Locale::LocaleMasks) {}
bool LangDirExist(char const* path) { struct stat st; return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode); }

// Allegro 4 Unicode string helpers (the loader linked Allegro; its default text format is
// UTF-8). Word Dojo 2 uses them on dictionary words.
static int utf8_len(const unsigned char* s) {
    return *s < 0x80 ? 1 : (*s >> 5) == 6 ? 2 : (*s >> 4) == 14 ? 3 : (*s >> 3) == 30 ? 4 : 1;
}
static int utf8_decode(const unsigned char* s) {
    int n = utf8_len(s);
    if (n == 1) return *s;
    int c = *s & (0x7f >> n);
    for (int i = 1; i < n && (s[i] & 0xc0) == 0x80; i++) c = (c << 6) | (s[i] & 0x3f);
    return c;
}
extern "C" {
int ustrlen(const char* s) {
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p += utf8_len(p)) n++;
    return n;
}
// Character at index; a negative index counts from the end (Allegro semantics).
int ugetat(const char* s, int index) {
    if (index < 0) index += ustrlen(s);
    if (index < 0) return 0;
    const unsigned char* p = (const unsigned char*)s;
    for (; *p && index > 0; index--) p += utf8_len(p);
    return *p ? utf8_decode(p) : 0;
}
int ustrcmp(const char* a, const char* b) {
    const unsigned char *p = (const unsigned char*)a, *q = (const unsigned char*)b;
    while (*p && *q) {
        int c = utf8_decode(p), d = utf8_decode(q);
        if (c != d) return c - d;
        p += utf8_len(p); q += utf8_len(q);
    }
    return utf8_decode(p) - utf8_decode(q);
}
// Allegro 4 keeps the per-encoding character functions in function-pointer variables
// (games load the pointer and call through it): uwidth, ugetc, ugetx, ugetxc, usetc, ucwidth, uisok.
static int a_uwidth(const char* s) { return s && *s ? utf8_len((const unsigned char*)s) : 0; }
static int a_ugetc(const char* s) { return s && *s ? utf8_decode((const unsigned char*)s) : 0; }
static int a_ugetx(char** s) { if (!s || !*s || !**s) return 0; int c = utf8_decode((unsigned char*)*s); *s += utf8_len((unsigned char*)*s); return c; }
static int a_ugetxc(const char** s) { return a_ugetx(const_cast<char**>(s)); }
static int a_usetc(char* s, int c) {
    unsigned char* p = (unsigned char*)s;
    if (c < 0x80) { p[0] = (unsigned char)c; return 1; }
    if (c < 0x800) { p[0] = 0xc0 | c >> 6; p[1] = 0x80 | (c & 63); return 2; }
    if (c < 0x10000) { p[0] = 0xe0 | c >> 12; p[1] = 0x80 | (c >> 6 & 63); p[2] = 0x80 | (c & 63); return 3; }
    p[0] = 0xf0 | c >> 18; p[1] = 0x80 | (c >> 12 & 63); p[2] = 0x80 | (c >> 6 & 63); p[3] = 0x80 | (c & 63); return 4;
}
static int a_ucwidth(int c) { return c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4; }
static int a_uisok(int c) { return c >= 0 && c < 0x110000; }
int (*uwidth)(const char*) = a_uwidth;
int (*ugetc)(const char*) = a_ugetc;
int (*ugetx)(char**) = a_ugetx;
int (*ugetxc)(const char**) = a_ugetxc;
int (*usetc)(char*, int) = a_usetc;
int (*ucwidth)(int) = a_ucwidth;
int (*uisok)(int) = a_uisok;
int ustrsize(const char* s) { return s ? (int)strlen(s) : 0; }
int ustrsizez(const char* s) { return s ? (int)strlen(s) + 1 : 1; }
// byte offset of character `index` (negative counts from the end)
int uoffset(const char* s, int index) {
    if (index < 0) index += ustrlen(s);
    const unsigned char* p = (const unsigned char*)s;
    for (; *p && index > 0; index--) p += utf8_len(p);
    return (int)(p - (const unsigned char*)s);
}
int uisspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int utolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int utoupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
// Uppercase in place (ASCII letters; multi-byte characters are left as they are).
char* ustrupr(char* s) {
    for (unsigned char* p = (unsigned char*)s; *p; p += utf8_len(p))
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    return s;
}
}

class Logger {
public:
    enum LOG_LEVEL : int {}; enum LOG_READER : int {}; enum LOG_TYPE : int {};
    static void SetApplicationID(xml_gameinfo::GameIds);
    static void Log(char const* file, char const* func, unsigned long line, unsigned long thread, LOG_LEVEL,
                    LOG_READER, LOG_TYPE, char const* category, char const* report, bool, char const* fmt, ...);
};
void Logger::SetApplicationID(xml_gameinfo::GameIds) {}
// The cabinet's logging call (libmerit_threads, libsettings, libmerit3d...). MEGA_DEBUG_LOG=1 prints it.
void Logger::Log(char const* file, char const*, unsigned long line, unsigned long, LOG_LEVEL lvl,
                 LOG_READER, LOG_TYPE, char const*, char const*, bool, char const* fmt, ...) {
    static bool on = getenv("MEGA_DEBUG_LOG") != nullptr;
    if (!on || !fmt) return;
    const char* base = file ? strrchr(file, '/') : nullptr;
    fprintf(stderr, "[log %d] %s:%lu: ", (int)lvl, base ? base + 1 : (file ? file : "?"), line);
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}

// ---------------------------------------------------------------------------
// MegacGlobals: the loader's global state block. Games read fields inline:
//   +0x24 player count (MEGA_PLAYERS)       +0x2038 current GameId
//   +0x22dc SystemClass object               +0x22e0 MouseManager object
//   +0x5ae4 name of the last touched zone    +0x5b70 VideoClass*    +0x5b74 text buffer
// The catalogue record kept in MegacGlobals+0x0c (0x24c bytes, from the games' map node size).
// Known field: +0xb0 the game's folder name (run21 builds gamegraphics/<it>_new/...).
namespace db_gamedata {
class gamedata_record {
public:
    gamedata_record();
    gamedata_record(gamedata_record const&);
    ~gamedata_record();
    unsigned char raw[0x24c];
};
gamedata_record::gamedata_record() { memset(raw, 0, sizeof raw); }
gamedata_record::gamedata_record(gamedata_record const& o) { memcpy(raw, o.raw, sizeof raw); }
gamedata_record::~gamedata_record() {}
}
static void init_catalogue(unsigned char* at, int gid) {
    using Catalogue = std::map<int, db_gamedata::gamedata_record>;
    static_assert(sizeof(Catalogue) == 0x18, "std::map layout");
    auto* m = new (at) Catalogue;
    db_gamedata::gamedata_record r;
    std::string lib = getenv("MEGA_LIB") ? getenv("MEGA_LIB") : "";
    if (lib.size() > 3 && lib.compare(lib.size() - 3, 3, ".so") == 0) lib.resize(lib.size() - 3);
    snprintf(reinterpret_cast<char*>(r.raw + 0xb0), 0x40, "%s", lib.c_str());
    (*m)[gid] = r;
}

class MegacGlobals { public: static MegacGlobals* GetInstance(); };
MegacGlobals* MegacGlobals::GetInstance() {
    static unsigned char g[0x8000];
    static unsigned char video[256];             // the VideoClass object games call through +0x5b70
    static bool init;
    if (!init) {
        init = true;
        unsigned char* vp = video;
        memcpy(g + 0x5b70, &vp, sizeof vp);
        const char* p = getenv("MEGA_PLAYERS");
        g[0x24] = (unsigned char)(p ? atoi(p) : 1);
        const char* id = getenv("MEGA_GAME_ID");
        int gid = id ? atoi(id) : 0;
        memcpy(g + 0x2038, &gid, 4);
        // +0x0c: std::map<xml_gameinfo::GameIds, db_gamedata::gamedata_record> (game catalogue),
        // indexed by games with their GameId (run21). Same libstdc++ _Rb_tree layout as the
        // games' own template code; holds the current game's entry.
        init_catalogue(g + 0x0c, gid);
    }
    return reinterpret_cast<MegacGlobals*>(g);
}

// Cabinet marquee lights: nothing to light. LightShow() returns the manager Play() is called on.
class LightShowManager { public: enum Sequences : int {}; void Play(Sequences, bool, bool); };
LightShowManager* LightShow() { static char inst[64]; return reinterpret_cast<LightShowManager*>(inst); }
void LightShowManager::Play(Sequences, bool, bool) {}

// In-game adverts: none. (Returns std::string by value: the compiler handles the hidden pointer.)
namespace ads { class in_game_ads { public: static std::string get_ad_name(xml_gameinfo::GameIds); }; }
std::string ads::in_game_ads::get_ad_name(xml_gameinfo::GameIds) { return std::string(); }

// "Champion Edition" tournament mode: not running.
class ChampEditionI {
public:
    static void DisplayMadeIt(int, int);
    static void DisplayPrizePool(int, int);
    static int GetCurrentLeaderScore(int);
    static int GetCurrentNumberOfRounds();
};
void ChampEditionI::DisplayMadeIt(int, int) {}
void ChampEditionI::DisplayPrizePool(int, int) {}
int ChampEditionI::GetCurrentLeaderScore(int) { return 0; }
int ChampEditionI::GetCurrentNumberOfRounds() { return 0; }

class Profiler {
public:
    static Profiler* GetInstance();
    static void DelInstance();
    void DumpResults();
    void Start(std::string);
    void Stop(std::string);
};
Profiler* Profiler::GetInstance() { static char inst[64]; return reinterpret_cast<Profiler*>(inst); }
void Profiler::DelInstance() {}
void Profiler::DumpResults() {}
void Profiler::Start(std::string) {}
void Profiler::Stop(std::string) {}

// libmerit_threads: the loader's main-thread handle
extern "C" { unsigned long MainThread; }


// Bookkeeping (cabinet libbooks: credits, plays, meters). Not loaded — it depends on the
// loader's key manager and tournament code; home play has no money, so the counts are 0.
namespace enums { enum Span : int {}; }
class Books {
public:
    static Books* Instance();
    int Credits(xml_gameinfo::GameIds, enums::Span);
    void LogCategoryPlays(xml_gameinfo::GameIds, int, int);
    bool ContinueGame(xml_gameinfo::GameIds, int, int);
};
Books* Books::Instance() { static char inst[64]; return reinterpret_cast<Books*>(inst); }
int Books::Credits(xml_gameinfo::GameIds, enums::Span) { return 0; }
void Books::LogCategoryPlays(xml_gameinfo::GameIds, int, int) {}
bool Books::ContinueGame(xml_gameinfo::GameIds, int, int) { return true; }


// The operator settings table. +0x350 is a std::map<GameIds, db_config::GameInfo_record> of
// per-game options (pool/nineball read field +0x18); empty: games get default (zero) records.
namespace db_config {
class GameInfo_record {
public:
    GameInfo_record();
    GameInfo_record(GameInfo_record const&);
    ~GameInfo_record();
    unsigned char raw[0x28];                       // size from the games' map node (0x3c)
};
GameInfo_record::GameInfo_record() { memset(raw, 0, sizeof raw); }
GameInfo_record::GameInfo_record(GameInfo_record const& o) { memcpy(raw, o.raw, sizeof raw); }
GameInfo_record::~GameInfo_record() {}
}
class SettingsTable { public: static SettingsTable* Instance(); };
SettingsTable* SettingsTable::Instance() {
    alignas(16) static unsigned char t[0x1000];
    static bool init;
    if (!init) {
        init = true;
        new (t + 0x350) std::map<int, db_config::GameInfo_record>;
    }
    return reinterpret_cast<SettingsTable*>(t);
}
