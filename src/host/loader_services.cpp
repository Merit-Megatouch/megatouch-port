// Stand-ins for the cabinet "loader" services that games and engine libraries import
// directly. tools/analyze.sh lists any a new game needs that are not here yet.
// Before adding one, disassemble a caller: static vs member functions mangle the same but
// are called differently (Translator::Translate is static).
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cctype>
#include <sys/stat.h>
// ---------------------------------------------------------------------------
// loader services

// Player scores, indexed by player. Read by g_trix and the meritbasegame helpers.
extern "C" { int PlrScore[8]; char ef_fp_filename[256]; }

namespace xml_gameinfo { enum GameIds : int {}; }

// Translations: /usr/local/gamedata/translations/<name>.utf8 is a '|'-separated table
// KEYSTRINGID|KEYSTRING|ENGLISH|GERMAN|... Keys (and numeric IDs) map to the English column.
class Translator {
public:
    static Translator* GetInstance();
    static bool LoadTranslations(char const*, bool);
    static char const* Translate(char const*);   // static: callers push only the string
};
#include <map>
static std::map<std::string, std::string>& tr_table() { static std::map<std::string, std::string> t; return t; }

static void tr_load_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    char* line = nullptr; size_t cap = 0; ssize_t n; bool header = true;
    while ((n = getline(&line, &cap, f)) > 0) {
        std::string l(line, n);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (header) { header = false; if (l.compare(0, 3, "\xef\xbb\xbf") == 0) l.erase(0, 3); if (l.find("KEYSTRING") != std::string::npos) continue; }
        size_t a = l.find('|'); if (a == std::string::npos) continue;
        size_t b = l.find('|', a + 1); if (b == std::string::npos) continue;
        size_t c = l.find('|', b + 1);
        std::string id = l.substr(0, a), key = l.substr(a + 1, b - a - 1);
        std::string en = l.substr(b + 1, c == std::string::npos ? std::string::npos : c - b - 1);
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

Translator* Translator::GetInstance() { static char inst[64]; return reinterpret_cast<Translator*>(inst); }
bool Translator::LoadTranslations(char const* name, bool) {
    static bool common;
    if (!common) { common = true; tr_load_file("/usr/local/gamedata/translations/SupportFiles.utf8"); }
    if (name) tr_load_file(std::string("/usr/local/gamedata/translations/") + name + ".utf8");
    return true;
}
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
};
ContinueControl::ContinueControl() {}
ContinueControl::~ContinueControl() {}
bool ContinueControl::display(unsigned int, unsigned int) { return true; }

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
char const* HighScoresManager::HighestName(xml_gameinfo::GameIds, int) { return best().name.c_str(); }

// The cabinet's language setting. Locale::Languages: 0 = English, 3 = French, 4 = Spanish, ...
// (same order as LanguagesSupported in gamedata.xml). Games pick dictionaries/help by it.
namespace Locale {
class LanguageManager {
public:
    static LanguageManager* GetInstance();
    int Active() const;
};
LanguageManager* LanguageManager::GetInstance() { static char inst[64]; return reinterpret_cast<LanguageManager*>(inst); }
int LanguageManager::Active() const { return 0; }
}

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

class Profiler { public: static Profiler* GetInstance(); void DumpResults(); };
Profiler* Profiler::GetInstance() { static char inst[64]; return reinterpret_cast<Profiler*>(inst); }
void Profiler::DumpResults() {}

// libmerit_threads: the loader's main-thread handle
extern "C" { unsigned long MainThread; }

