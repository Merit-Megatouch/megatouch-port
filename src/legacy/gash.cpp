// Gash layout scripts and LinuxWorldClass (breakout, motormatch, racepoker).
//
// These games build a LinuxWorldClass (a WorldClass that publishes itself in MWorld and
// MegacGlobals+0x207c and owns a transparent root sprite at +0x60), then lay out their screens
// from a tag-stream script (layout.xml) that DoParse turns into a Gash tree of named sprites:
// windows, text, score boxes, a timer, buttons. Games find them with TopGash->LocateSprite(tag,
// name) and push their signal handler onto the root's +0x8c list. Reference: docs/reference/gash.md.
#include "sprite.h"
#include "../common/env.h"
#include <algorithm>
#include <array>
#include <functional>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class MegacGlobals { public: static MegacGlobals* GetInstance(); };          // megatouch-host
class HighScoresManager {                                                    // megatouch-host
public:
    static HighScoresManager* Instance();
    int HighestScore(int);
    char const* HighestName(int);
};
int PlayPreWave(char* name, unsigned short, bool, int vol, int, int);          // legacy.cpp
char* CommaStr(char* out, unsigned long n);                                  // legacy.cpp

class Gash;
class LinuxWorldClass;
// the elements of a List (List::each is private to sprite.cpp)
static std::vector<Group*> items_of(List* l) {
    std::vector<Group*> v;
    ListObj* h = l ? l->header() : nullptr;
    for (ListObj* n = h ? h->next : nullptr; n && n->next; n = n->next) v.push_back(static_cast<Group*>(n->data));
    return v;
}

// ------------------------------------------------------------------------------ globals (§2)
LinuxWorldClass* MWorld;
Gash* TopGash;
int HelpOnScreen;
char SSIG_NAMES[32][20];
__attribute__((constructor)) static void init_ssig_names() {
    for (int i = 0; i < 32; i++) {
        if (i > 8) snprintf(SSIG_NAMES[i], sizeof SSIG_NAMES[i], "SSIG_USER%d", i - 8);
        else snprintf(SSIG_NAMES[i], sizeof SSIG_NAMES[i], "SSIG_SYS%d", i);
    }
}
static int signal_of(const std::string& v) {
    for (int i = 0; i < 32; i++) if (!strcasecmp(SSIG_NAMES[i], v.c_str())) return i;
    return atoi(v.c_str());
}
static WorldClass*& world_slot() {
    return *reinterpret_cast<WorldClass**>(reinterpret_cast<unsigned char*>(MegacGlobals::GetInstance()) + 0x207c);
}
static int& game_state() { return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(MWorld) + 0x19c); }

// ------------------------------------------------------------------------------ LinuxWorldClass (§4)
class LinuxWorldClass : public WorldClass {
public:
    explicit LinuxWorldClass(bool);
    ~LinuxWorldClass() override;
    WorldClass* saved_world;                     // +0x208 (no game touches 0x208..0x227)
    LinuxWorldClass* saved_mworld;
    unsigned char pad210[0x228 - 0x210];
};
static_assert(sizeof(LinuxWorldClass) == 0x228, "LinuxWorldClass");

LinuxWorldClass::LinuxWorldClass(bool) {
    saved_world = world_slot();
    saved_mworld = MWorld;
    memset(pad210, 0, sizeof pad210);
    MWorld = this;
    world_slot() = this;                         // games use both (gash.md §4.2)
    auto* r = new Sprite(nullptr, 0, nullptr);
    r->children = new List(static_cast<List*>(nullptr));
    root = r;
    frozen = 0;                                  // +0x19c game state: playing
}
LinuxWorldClass::~LinuxWorldClass() {
    if (world_slot() == this) world_slot() = saved_world;
    if (MWorld == this) MWorld = saved_mworld;
}

// ------------------------------------------------------------------------------ widgets (§8)
class ScoreDisplay : public Sprite {
public:
    ScoreDisplay(Bitmap* b, unsigned long fl, Sprite* parent) : Sprite(b, fl, parent) {
        memset(padac, 0, sizeof(ScoreDisplay) - offsetof(ScoreDisplay, padac));
    }
    void Set(int v);
    void Increase(int v);
    void refresh();
    unsigned char padac[0x178 - 0xac];
    int score;                                   // +0x178, read by the games
    int pad17c;
};
static_assert(offsetof(ScoreDisplay, score) == 0x178, "ScoreDisplay +0x178");
void ScoreDisplay::refresh() {
    char buf[32];
    CommaStr(buf, (unsigned long)std::max(0, score));
    if (str) ChangeString(buf, 0);
}
void ScoreDisplay::Set(int v) { score = v; refresh(); }
void ScoreDisplay::Increase(int v) { score += v; refresh(); }

class OneBmpTimer : public Sprite {
public:
    OneBmpTimer(Bitmap* b, unsigned long fl, Sprite* parent) : Sprite(b, fl, parent) {
        memset(&now_snap, 0, sizeof(OneBmpTimer) - offsetof(OneBmpTimer, now_snap));
    }
    void Reset() override;                       // slot 10
    int DoNextFrame() override;
    int DoDraw() override;
    int TimeLeft();
    void SetTimeLeft(int ms);
    void Halt();
    bool Resume();
    void Award(ScoreDisplay* sd, int a, int b);
    uint32_t now_snap;                           // +0xac clock snapshot (the halt time while halted)
    int total;                                   // +0xb0 duration, ms
    unsigned char padb4[0xe0 - 0xb4];
    uint32_t start;                              // +0xe0 start on the world clock
    // engine
    int halted;
    int hurry_signal, hurry_sent, expired;
    int direction;
};
static_assert(offsetof(OneBmpTimer, total) == 0xb0 && offsetof(OneBmpTimer, start) == 0xe0, "OneBmpTimer fields");
static uint32_t world_clock() { return MWorld ? MWorld->clock : 0; }
void OneBmpTimer::Reset() { start = world_clock(); halted = 0; hurry_sent = 0; expired = 0; }
int OneBmpTimer::TimeLeft() {
    uint32_t t = halted ? now_snap : world_clock();
    long left = (long)total - (long)(t - start);
    return (int)std::max(0L, left);
}
void OneBmpTimer::SetTimeLeft(int ms) { start = (halted ? now_snap : world_clock()) - (uint32_t)(total - ms); }
void OneBmpTimer::Halt() { if (!halted) { now_snap = world_clock(); halted = 1; } }
bool OneBmpTimer::Resume() {
    if (!halted) return false;
    start += world_clock() - now_snap;
    halted = 0;
    return true;
}
// end-of-game time bonus: a share of `a` for the time left (gash.md §8.1, formula unknown)
void OneBmpTimer::Award(ScoreDisplay* sd, int a, int) {
    if (sd && total > 0) sd->Increase((int)((long long)a * TimeLeft() / total));
}
int OneBmpTimer::DoNextFrame() {
    if (halted || !MWorld) return 1;
    int left = TimeLeft();
    if (!hurry_sent && hurry_signal && left > 0 && left <= 10000) { hurry_sent = 1; Signal((Group::SpriteSignalType)hurry_signal); }
    if (!expired && left == 0) { expired = 1; if (game_state() == 0) game_state() = 1; }
    return 1;
}
// its one bitmap, cropped to the time left (direction 1 shrinks towards the left)
int OneBmpTimer::DoDraw() {
    if ((flags & 0x80) || !bmp || total <= 0) return 1;
    float ox, oy;
    legacy::sprite_draw_origin(ox, oy);
    float x0 = ox + x, y0 = oy + y;
    int shown = (int)((long long)bmp->w * TimeLeft() / total);
    int clip[4] = {(int)x0, (int)y0, (int)x0 + shown - 1, (int)y0 + bmp->h - 1};
    if (direction < 0) { clip[0] = (int)x0 + bmp->w - shown; clip[2] = (int)x0 + bmp->w - 1; }
    if (shown > 0) legacy::sobj_blit(bmp, x0, y0, 0x10000, 0x10000, transparency, false, clip);
    return 1;
}

// quits the game (breakout has no other way out; gash.md §8.3)
class ExitButtonSprite : public Sprite {
public:
    ExitButtonSprite(Bitmap* b, unsigned long fl, Sprite* parent) : Sprite(b, fl, parent) {}
    int SpriteClick() override {
        if (!sound.empty()) PlayPreWave(const_cast<char*>(sound.c_str()), 0, true, 255, 1000, 127);
        if (MWorld) game_state() = 3;
        return 1;
    }
    std::string sound;
};
class HelpButtonSprite : public Sprite {
public:
    HelpButtonSprite(Bitmap* b, unsigned long fl, Sprite* parent) : Sprite(b, fl, parent) {
        memset(padac, 0, sizeof padac);
    }
    int SpriteClick() override { return 1; }     // help overlay not implemented
    unsigned char padac[0xc8 - 0xac];
    char* help_text;                             // +0xc8, malloc'd (rp replaces it)
    int padcc;
};
static_assert(offsetof(HelpButtonSprite, help_text) == 0xc8, "HelpButton +0xc8");

// ------------------------------------------------------------------------------ Gash (§5)
enum GashFlags : int {};
class Gash : public Group {
public:
    Gash() : Group(nullptr, nullptr) {}
    ~Gash() override;
    Gash* LocateGash(char* tag, char* name);
    Sprite* LocateSprite(char* tag, char* name);
    void ParseSpriteAttribs(Sprite*, GashFlags);
    char* tag = nullptr;                         // +0x0c
    char* name = nullptr;                        // +0x10
    std::map<std::string, std::string>* attrs = nullptr;   // +0x14
    Sprite* sprite = nullptr;                    // +0x18
    float abs_z = 0;
    bool built = false;
};
static_assert(offsetof(Gash, sprite) == 0x18, "Gash +0x18");
Gash::~Gash() {
    // sprites belong to the world, which is deleted first (gash.md §1)
    if (children) {
        std::vector<Group*> kids = items_of(children);
        delete children; children = nullptr;
        for (Group* k : kids) delete static_cast<Gash*>(k);
    }
    free(tag); free(name);
    delete attrs;
}
static void walk(Gash* g, const std::function<void(Gash*)>& f) {
    f(g);
    if (g->children) for (Group* k : items_of(g->children)) walk(static_cast<Gash*>(k), f);
}
// depth-first; a NULL name matches the first of the tag; later duplicates win (gash.md §5.2)
Gash* Gash::LocateGash(char* t, char* n) {
    Gash* found = nullptr;
    walk(this, [&](Gash* g) {
        if (!g->tag || !t || strcasecmp(g->tag, t)) return;
        if (!n) { if (!found) found = g; return; }
        if (g->name && !strcasecmp(g->name, n)) found = g;
    });
    return found;
}
Sprite* Gash::LocateSprite(char* t, char* n) {
    Gash* g = this ? LocateGash(t, n) : nullptr;
    if (!g) LOG("Gash: no %s '%s'", t ? t : "?", n ? n : "(first)");
    return g ? g->sprite : nullptr;
}
void Gash::ParseSpriteAttribs(Sprite*, GashFlags) {}

// ------------------------------------------------------------------------------ DoParse (§3, §6)
namespace {
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}
bool is_element(const std::string& t) {
    static const char* el[] = {"window", "sprite", "text", "scorebox", "timer", "button", "exitbutton", "helpbutton",
                               "hiscorebox", "background", "hotkey"};
    for (const char* e : el) if (t == e) return true;
    return false;
}
const std::string* attr(Gash* g, const char* k) {
    auto it = g->attrs->find(k);
    return it == g->attrs->end() ? nullptr : &it->second;
}
int iattr(Gash* g, const char* k, int def = 0) { auto* v = attr(g, k); return v ? atoi(v->c_str()) : def; }
bool active(Gash* g) { auto* v = attr(g, "active"); return !v || lower(*v) == "on" || lower(*v) == "yes"; }
Bitmap* load_graphic(const std::string& file) {
    if (file.empty() || !MWorld) return nullptr;
    std::string n = file;
    size_t dot = n.rfind('.'), sl = n.rfind('/');
    if (dot != std::string::npos && (sl == std::string::npos || dot > sl)) n.resize(dot);
    return MWorld->LoadBmp(n.c_str(), 1, 1.0f, 3, 999999, true, false, nullptr);
}
void colour_of(Gash* g, int& r, int& gg, int& b) {
    r = gg = b = 0;                              // offsets from white
    if (auto* c = attr(g, "color")) {
        static const std::map<std::string, std::array<int, 3>> names = {
            {"white", {0, 0, 0}},        {"yellow", {0, 0, -255}},   {"red", {0, -255, -255}},  {"green", {-255, 0, -255}},
            {"blue", {-255, -255, 0}},   {"black", {-255, -255, -255}}, {"orange", {0, -90, -255}}, {"cyan", {-255, 0, 0}},
            {"magenta", {0, -255, 0}},   {"grey", {-128, -128, -128}}, {"gray", {-128, -128, -128}}};
        auto it = names.find(lower(trim(*c)));
        if (it != names.end()) { r = it->second[0]; gg = it->second[1]; b = it->second[2]; }
    }
    if (attr(g, "colorred")) r = std::min(0, iattr(g, "colorred"));
    if (attr(g, "colorgreen")) gg = std::min(0, iattr(g, "colorgreen"));
    if (attr(g, "colorblue")) b = std::min(0, iattr(g, "colorblue"));
}
String* string_of(Gash* g, const char* text) {
    std::string font = attr(g, "font") ? trim(*attr(g, "font")) : "bureau";
    int size = attr(g, "pointsize") ? (int)atof(attr(g, "pointsize")->c_str()) : 20;
    std::string al = attr(g, "align") ? lower(trim(*attr(g, "align"))) : "left";
    int just = al == "center" || al == "centre" ? 4 : al == "right" ? 2 : 1;
    int r, gg, b;
    colour_of(g, r, gg, b);
    bool bold = attr(g, "bold") && lower(trim(*attr(g, "bold"))) == "on";
    return new String(font.c_str(), size, text, just, (signed char)iattr(g, "spacing"), r, gg, b, 0, 0, bold ? 0x08 : 0);
}

Sprite* parent_sprite(Gash* g) {
    for (Gash* p = static_cast<Gash*>(g->owner); p; p = static_cast<Gash*>(p->owner))
        if (p->sprite) return p->sprite;
    return MWorld ? static_cast<Sprite*>(MWorld->root) : nullptr;
}
float parent_z(Gash* g) {
    for (Gash* p = static_cast<Gash*>(g->owner); p; p = static_cast<Gash*>(p->owner)) if (p->built) return p->abs_z;
    return 0;
}

// make the element's sprite (once its attributes are known: at its first child or its end)
void build(Gash* g) {
    if (g->built || !g->tag) return;
    g->built = true;
    std::string t = lower(g->tag);
    if (t == "hotkey") return;
    if (t == "background") { if (auto* f = attr(g, "graphic")) if (MWorld) MWorld->SetBack(trim(*f).c_str(), 1); return; }
    Sprite* parent = parent_sprite(g);
    unsigned long fl = active(g) ? 0 : 0x80;
    int sig = 0;
    if (auto* v = attr(g, "defaultspritesignal")) sig = signal_of(trim(*v));
    if (auto* v = attr(g, "buttonsignal")) sig = signal_of(trim(*v));
    if (sig || t == "exitbutton" || t == "helpbutton" || t == "button") fl |= 0x100;
    Bitmap* bmp = attr(g, "graphic") ? load_graphic(trim(*attr(g, "graphic"))) : nullptr;
    Sprite* s;
    if (t == "scorebox") s = new ScoreDisplay(nullptr, fl, parent);
    else if (t == "timer") s = new OneBmpTimer(bmp, fl, parent);
    else if (t == "exitbutton") {
        auto* e = new ExitButtonSprite(bmp, fl, parent);
        if (auto* v = attr(g, "exitsound")) e->sound = trim(*v);
        s = e;
    } else if (t == "helpbutton") s = new HelpButtonSprite(bmp, fl, parent);
    else s = new Sprite(t == "text" ? nullptr : bmp, fl, parent);
    g->abs_z = attr(g, "zcoord") ? parent_z(g) + (float)atof(attr(g, "zcoord")->c_str()) : parent_z(g);
    s->x = attr(g, "xcoord") ? (float)atof(attr(g, "xcoord")->c_str()) : 0;
    s->y = attr(g, "ycoord") ? (float)atof(attr(g, "ycoord")->c_str()) : 0;
    s->z = g->abs_z;
    s->signal_type = sig;
    if (t == "text" || t == "scorebox") {
        std::string text = attr(g, "string") ? trim(*attr(g, "string")) : "";
        if (t == "scorebox") text = "0";
        // HiScoreBox texts named Score / Name show the high score ("generated automatically")
        Gash* p = static_cast<Gash*>(g->owner);
        if (p && p->tag && !strcasecmp(p->tag, "hiscorebox") && g->name) {
            char buf[64];
            if (!strcasecmp(g->name, "score")) { CommaStr(buf, (unsigned long)std::max(0, HighScoresManager::Instance()->HighestScore(0))); text = buf; }
            else if (!strncasecmp(g->name, "name", 4)) { const char* n = HighScoresManager::Instance()->HighestName(0); text = n ? n : ""; }
        }
        s->str = string_of(g, text.c_str());
        if (attr(g, "xsize") || attr(g, "ysize")) s->SetSize(iattr(g, "xsize"), iattr(g, "ysize"));
        s->ProcessAssignedString();
    } else if (!bmp && (attr(g, "xsize") || attr(g, "ysize"))) {
        s->SetSize(iattr(g, "xsize"), iattr(g, "ysize"));
    }
    if (t == "timer") {
        auto* tm = static_cast<OneBmpTimer*>(s);
        double minutes = attr(g, "totaltime") ? atof(attr(g, "totaltime")->c_str()) : 1.0;
        tm->total = (int)(minutes * 60000);
        tm->direction = iattr(g, "direction", 1);
        if (auto* v = attr(g, "hurrysignal")) tm->hurry_signal = signal_of(trim(*v));
        tm->Reset();
    }
    g->sprite = s;
}
// graphic-less clickable sprites (rp SelectZone1..4) take the size of a same-position sibling
// that has a graphic (gash.md §3.3)
void size_zones(Gash* top) {
    walk(top, [&](Gash* g) {
        Sprite* s = g->sprite;
        if (!s || s->bmp || s->str || s->w > 0 || !(s->flags & 0x100) || !g->owner) return;
        Gash* p = static_cast<Gash*>(g->owner);
        if (!p->children) return;
        for (Group* x : items_of(p->children)) {
            Sprite* o = static_cast<Gash*>(x)->sprite;
            if (o && o != s && o->bmp && o->x == s->x && o->y == s->y && s->w == 0) { s->w = o->w; s->h = o->h; }
        }
    });
}
}  // namespace

void DoParse(char* path, bool) {
    if (!path) return;
    FILE* f = fopen(path, "rb");
    if (!f) {
        // motormatch is racepoker's binary and opens racepoker/layout.xml: use the game's own folder
        const char* ad = getenv("MEGA_ASSET_DIR");
        std::string p = path, base = p.substr(p.rfind('/') + 1);
        if (ad) {
            std::string alt = std::string(*ad == '/' ? "" : "/") + ad + "/" + base;
            f = fopen(alt.c_str(), "rb");
        }
        if (!f) { LOG("DoParse: cannot open %s", path); return; }
    }
    std::string d;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
    fclose(f);

    if (!TopGash) TopGash = new Gash;
    Gash* cur = TopGash;
    std::string pending_attr;                    // open leaf attribute
    std::string skip_until;                      // inside <comment>/<info>
    size_t i = 0;
    while (i < d.size()) {
        size_t lt = d.find('<', i);
        std::string text = d.substr(i, (lt == std::string::npos ? d.size() : lt) - i);
        if (lt == std::string::npos) break;
        size_t gt = d.find('>', lt);
        if (gt == std::string::npos) break;
        std::string tagtext = d.substr(lt + 1, gt - lt - 1);
        i = gt + 1;
        bool close = !tagtext.empty() && tagtext[0] == '/';
        std::string tag = lower(trim(close ? tagtext.substr(1) : tagtext));
        if (!skip_until.empty()) { if (close && tag == skip_until) skip_until.clear(); continue; }
        // the text before this tag belongs to whatever was open
        if (!pending_attr.empty()) {
            if (close && tag == pending_attr) { (*cur->attrs)[pending_attr] = trim(text); pending_attr.clear(); continue; }
            (*cur->attrs)[pending_attr] = trim(text);
            pending_attr.clear();
        } else if (cur != TopGash && !cur->name) {
            cur->name = strdup(trim(text).c_str());
        }
        if (tag == "eof") break;
        if (!close && (tag == "comment" || tag == "info")) { skip_until = tag; continue; }
        if (close) {
            if (is_element(tag) && cur != TopGash && cur->tag && lower(cur->tag) == tag) {
                if (!cur->name) cur->name = strdup("");
                build(cur);
                cur = static_cast<Gash*>(cur->owner);
            }
            continue;
        }
        if (is_element(tag)) {
            if (cur != TopGash) { if (!cur->name) cur->name = strdup(""); build(cur); }
            auto* g = new Gash;
            g->tag = strdup(tag.c_str());
            g->attrs = new std::map<std::string, std::string>;
            g->owner = cur;
            if (!cur->children) cur->children = new List(static_cast<List*>(nullptr));
            cur->children->Push(g);
            cur = g;
            continue;
        }
        if (cur != TopGash) pending_attr = tag;  // a leaf attribute: its value runs to </tag>
    }
    while (cur && cur != TopGash) { build(cur); cur = static_cast<Gash*>(cur->owner); }
    size_zones(TopGash);
}
