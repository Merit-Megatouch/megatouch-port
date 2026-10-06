// Stand-ins for the cabinet "loader" services that games and engine libraries import
// directly. tools/analyze.sh lists any a new game needs that are not here yet.
// Before adding one, disassemble a caller: static vs member functions mangle the same but
// are called differently (Translator::Translate is static).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
};
HighScoresManager* HighScoresManager::Instance() { static char inst[64]; return reinterpret_cast<HighScoresManager*>(inst); }
bool HighScoresManager::HighEnough(int, int) { return false; }
int HighScoresManager::Winner() const { return 0; }

class Logger {
public:
    static void SetApplicationID(xml_gameinfo::GameIds);
};
void Logger::SetApplicationID(xml_gameinfo::GameIds) {}

