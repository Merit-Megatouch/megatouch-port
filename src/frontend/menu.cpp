// megatouch-menu: our own Megatouch front end (phase 2). It is written from scratch: the cabinet's
// protected loader is never run. It shows the classic 640x480 menu with the cabinet's own
// artwork (menugraphics), lets the player pick a category, a game and the number of players,
// and runs the game with games/<name>/run, coming back when it exits. Also: attract mode, high
// scores, and an operator screen (free play / credits, language, volume).
//
//   shared/bin/megatouch-menu [--repo DIR] [--window WxH]     (normally via ./menu)
#include "../common/merit_rle.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <map>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
const int W = 640, H = 480;                      // the menu art's resolution
std::string g_repo;
SDL_Window* g_win;
SDL_Renderer* g_ren;
int g_winW = 1024, g_winH = 768;

bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}
std::vector<std::string> list_dir(const std::string& d) {
    std::vector<std::string> v;
    if (DIR* dp = opendir(d.c_str())) {
        while (dirent* e = readdir(dp)) if (e->d_name[0] != '.') v.push_back(e->d_name);
        closedir(dp);
    }
    std::sort(v.begin(), v.end());
    return v;
}
std::string read_file(const std::string& p) {
    std::string s;
    if (FILE* f = fopen(p.c_str(), "rb")) {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        fclose(f);
    }
    return s;
}

// ---------------------------------------------------------------------------- data locations
// The cabinet's data: shared/data-common (made by `make setup`), else the cabinet snapshot.
std::vector<std::string> data_roots() {
    return {g_repo + "/shared/data-common/usr/local/gamedata", g_repo + "/cabinet/root/usr/local/gamedata",
            g_repo + "/cabinet/ion"};
}
std::string find_data(const std::string& rel) {
    for (const auto& r : data_roots())
        for (const char* ext : {"", ".gz"})
            if (exists(r + "/" + rel + ext)) return r + "/" + rel + ext;
    return "";
}

// ---------------------------------------------------------------------------- graphics
struct Tex { SDL_Texture* t = nullptr; int w = 0, h = 0; };
std::map<std::string, Tex> g_tex;

Tex tex_from_argb(const std::vector<uint32_t>& px, int w, int h) {
    Tex t;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return t;
    for (int y = 0; y < h; y++) memcpy(static_cast<uint8_t*>(s->pixels) + y * s->pitch, &px[(size_t)y * w], (size_t)w * 4);
    t.t = SDL_CreateTextureFromSurface(g_ren, s);
    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    t.w = w; t.h = h;
    SDL_FreeSurface(s);
    return t;
}
// .spr/.dlt (first frame) through the Merit decoder; .jpg/.pcx/.png/.tga through SDL_image.
// Magenta and fully transparent pixels are see-through.
const Tex& image(const std::string& rel) {
    auto it = g_tex.find(rel);
    if (it != g_tex.end()) return it->second;
    Tex t;
    std::string p = find_data(rel);
    std::vector<uint8_t> d;
    size_t off, count;
    if (!p.empty() && merit_read_gz(p.c_str(), d) && merit_container(d, off, count)) {
        std::vector<MeritFrame> fr;
        merit_read_frames(d, off, fr, 1);
        if (!fr.empty()) {
            for (auto& c : fr[0].argb) if ((c & 0xffffff) == 0xff00ff) c = 0;
            t = tex_from_argb(fr[0].argb, fr[0].w, fr[0].h);
        }
    } else if (!p.empty()) {
        if (SDL_Surface* s = IMG_Load(p.c_str())) {
            SDL_SetColorKey(s, SDL_TRUE, SDL_MapRGB(s->format, 255, 0, 255));
            t.t = SDL_CreateTextureFromSurface(g_ren, s);
            t.w = s->w; t.h = s->h;
            SDL_FreeSurface(s);
        }
    }
    return g_tex[rel] = t;
}
void draw(const Tex& t, int x, int y, int alpha = 255) {
    if (!t.t) return;
    SDL_Rect r{x, y, t.w, t.h};
    SDL_SetTextureAlphaMod(t.t, (Uint8)alpha);
    SDL_RenderCopy(g_ren, t.t, nullptr, &r);
}
void fill(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) {
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ren, r, g, b, a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(g_ren, &rc);
}

// ---------------------------------------------------------------------------- text
struct Font {
    std::vector<uint8_t> data;
    stbtt_fontinfo info;
    bool ok = false;
};
Font g_font;
void font_init() {
    for (const char* f : {"ttf/bureau.ttf", "ttf/Bureau.ttf", "ttf/ArundinaSans-Bold.ttf", "ttf/DejaVuLGCSans.ttf"}) {
        std::string p = find_data(f);
        if (p.empty()) continue;
        std::string s = read_file(p);
        g_font.data.assign(s.begin(), s.end());
        if (stbtt_InitFont(&g_font.info, g_font.data.data(), 0)) { g_font.ok = true; return; }
    }
}
int text_width(const std::string& s, int px) {
    if (!g_font.ok) return (int)s.size() * px / 2;
    float sc = stbtt_ScaleForPixelHeight(&g_font.info, (float)px), w = 0;
    for (unsigned char c : s) { int adv, lsb; stbtt_GetCodepointHMetrics(&g_font.info, c, &adv, &lsb); w += adv * sc; }
    return (int)w;
}
// align: 0 left, 1 centre, 2 right (about x)
void text(const std::string& s, int x, int y, int px, SDL_Color col, int align = 0) {
    if (!g_font.ok || s.empty()) return;
    int tw = text_width(s, px);
    if (align == 1) x -= tw / 2; else if (align == 2) x -= tw;
    float sc = stbtt_ScaleForPixelHeight(&g_font.info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_font.info, &asc, &desc, &gap);
    int bw = tw + 4, bh = px + 4;
    std::vector<uint32_t> buf((size_t)bw * bh, 0);
    float pen = 1;
    for (unsigned char c : s) {
        int gw, gh, ox, oy;
        unsigned char* bm = stbtt_GetCodepointBitmap(&g_font.info, sc, sc, c, &gw, &gh, &ox, &oy);
        for (int j = 0; j < gh; j++)
            for (int i = 0; i < gw; i++) {
                int X = (int)pen + ox + i, Y = (int)(asc * sc) + oy + j + 1;
                if (X < 0 || Y < 0 || X >= bw || Y >= bh || !bm[j * gw + i]) continue;
                uint32_t a = std::max<uint32_t>(buf[(size_t)Y * bw + X] >> 24, bm[j * gw + i]);
                buf[(size_t)Y * bw + X] = a << 24 | (uint32_t)col.r << 16 | (uint32_t)col.g << 8 | col.b;
            }
        stbtt_FreeBitmap(bm, nullptr);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_font.info, c, &adv, &lsb);
        pen += adv * sc;
    }
    Tex t = tex_from_argb(buf, bw, bh);
    // shadow, then the text
    SDL_SetTextureColorMod(t.t, 0, 0, 0);
    SDL_Rect sh{x + 1, y + 1, bw, bh};
    SDL_RenderCopy(g_ren, t.t, nullptr, &sh);
    SDL_SetTextureColorMod(t.t, 255, 255, 255);
    SDL_Rect r{x, y, bw, bh};
    SDL_RenderCopy(g_ren, t.t, nullptr, &r);
    SDL_DestroyTexture(t.t);
}
const SDL_Color WHITE{255, 255, 255, 255}, YELLOW{255, 230, 80, 255}, GREY{190, 190, 190, 255};

// ---------------------------------------------------------------------------- catalogue
struct Game {
    std::string dir;                             // games/<dir>
    std::string lib, title, logo, category;
    int id = 0, max_players = 1;
    long best = 0;
    std::string best_name;
};
std::vector<Game> g_games;

std::map<std::string, std::string> parse_conf(const std::string& path) {
    std::map<std::string, std::string> m;
    std::string s = read_file(path);
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find('\n', i);
        std::string line = s.substr(i, e == std::string::npos ? std::string::npos : e - i);
        i = e == std::string::npos ? s.size() : e + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos) m[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return m;
}
// gamedata.xml: <game><DLLName>..</DLLName><Description>..</Description><MaxPlayers>..</MaxPlayers>...
struct Info { std::string desc, dir; int maxp = 0; };
std::map<std::string, Info> load_gamedata() {
    std::map<std::string, Info> m;
    std::string x = read_file(find_data("config/gamedata.xml"));
    auto tag = [](const std::string& blk, const char* t) {
        std::string o = std::string("<") + t + ">", c = std::string("</") + t + ">";
        size_t a = blk.find(o), b = a == std::string::npos ? a : blk.find(c, a);
        return a == std::string::npos || b == std::string::npos ? std::string() : trim(blk.substr(a + o.size(), b - a - o.size()));
    };
    size_t i = 0;
    while ((i = x.find("<game>", i)) != std::string::npos) {
        size_t e = x.find("</game>", i);
        if (e == std::string::npos) break;
        std::string blk = x.substr(i, e - i);
        Info inf{tag(blk, "Description"), tag(blk, "Directory"), atoi(tag(blk, "MaxPlayers").c_str())};
        std::string dll = lower(tag(blk, "DLLName"));
        if (!dll.empty()) m[dll] = inf;
        i = e;
    }
    return m;
}
// the classic menu's categories (cat_icons): cards, puzzles, quizword, megasports, strategy, erotic
std::string category_of(const std::string& name, const std::string& title) {
    std::string n = lower(name + " " + title);
    auto has = [&](std::initializer_list<const char*> ks) { for (auto k : ks) if (n.find(k) != std::string::npos) return true; return false; };
    if (has({"penthouse", "hunks", "strip", "pe", "chip", "erotic", "panty", "gender", "photopop", "ps", "babes"}) &&
        has({"penthouse", "hunks", "strip", "chip", "erotic", "panty", "gender", "photopop", "pepix", "peboxxi", "pelookout", "pephunt", "psmystery", "babes"}))
        return "erotic";
    if (has({"triv", "quiz", "word", "phraze", "spell", "wordy", "zap", "dojo", "lingo", "brain", "dino"})) return "quizword";
    if (has({"poker", "21", "solitaire", "rummy", "hearts", "spades", "euchre", "bandits", "wild8", "take2", "card", "holdem",
             "black_jack", "cribbage", "gin", "elevenup", "eleven", "tritowers", "tri_", "castles", "mulligan", "clocker", "clock"}))
        return "cards";
    if (has({"bowling", "pool", "nineball", "golf", "baseball", "field_goal", "hoop", "boxing", "racing", "beer_pong", "bball",
             "qbzone", "qshot", "deflection", "football", "hockey", "pit_crew", "darts", "bike", "dodge"}))
        return "megasports";
    if (has({"chess", "checker", "bgammon", "backgammon", "dominoes", "mahjong", "battle", "four", "reversi", "go_", "strategy", "tic"}))
        return "strategy";
    return "puzzles";
}
void load_scores(Game& g) {
    // megatouch-host keeps one best score per game: data/var/merit/highscores/<GAME_ID>.txt
    std::string s = read_file(g_repo + "/games/" + g.dir + "/data/var/merit/highscores/" + std::to_string(g.id) + ".txt");
    if (s.empty()) return;
    char name[64] = "";
    long score = 0;
    if (sscanf(s.c_str(), "%ld %63[^\n]", &score, name) >= 1) { g.best = score; g.best_name = name; }
}
void load_catalogue() {
    auto info = load_gamedata();
    for (const auto& d : list_dir(g_repo + "/games")) {
        std::string conf = g_repo + "/games/" + d + "/game.conf";
        if (!exists(conf) || !exists(g_repo + "/games/" + d + "/run")) continue;
        auto c = parse_conf(conf);
        Game g;
        g.dir = d;
        g.lib = c.count("LIB") ? c["LIB"] : d + ".so";
        std::string dll = lower(g.lib.substr(0, g.lib.rfind('.')));
        g.title = c.count("TITLE") ? c["TITLE"] : d;
        if (g.title.rfind("Megatouch ", 0) == 0) g.title = g.title.substr(10);
        g.id = c.count("GAME_ID") ? atoi(c["GAME_ID"].c_str()) : 0;
        auto it = info.find(dll);
        if (it != info.end()) {
            if (!it->second.desc.empty()) g.title = it->second.desc;
            g.max_players = std::max(1, it->second.maxp);
        }
        if (c.count("MAX_PLAYERS")) g.max_players = atoi(c["MAX_PLAYERS"].c_str());
        if (c.count("FRONTEND_HIDE")) continue;  // services (jukebox, operator setup) are not games
        g.max_players = std::min(4, g.max_players);
        for (const std::string& n : {dll, d, lower(d)})
            for (const char* lang : {"english"})
                if (g.logo.empty() && !find_data(std::string("menugraphics/game/logos/") + lang + "/" + n + ".spr").empty())
                    g.logo = std::string("menugraphics/game/logos/") + lang + "/" + n + ".spr";
        g.category = c.count("CATEGORY_MENU") ? c["CATEGORY_MENU"] : category_of(d, g.title);
        load_scores(g);
        g_games.push_back(g);
    }
    std::sort(g_games.begin(), g_games.end(), [](const Game& a, const Game& b) { return a.title < b.title; });
}

// ---------------------------------------------------------------------------- settings
struct Settings { bool free_play = true; int credits = 0; std::string language = "english"; int volume = 80; } g_set;
std::string settings_path() { return g_repo + "/build/frontend.conf"; }
void load_settings() {
    auto c = parse_conf(settings_path());
    if (c.count("FREE_PLAY")) g_set.free_play = c["FREE_PLAY"] == "1";
    if (c.count("CREDITS")) g_set.credits = atoi(c["CREDITS"].c_str());
    if (c.count("LANGUAGE")) g_set.language = c["LANGUAGE"];
    if (c.count("VOLUME")) g_set.volume = atoi(c["VOLUME"].c_str());
}
void save_settings() {
    mkdir((g_repo + "/build").c_str(), 0755);
    if (FILE* f = fopen(settings_path().c_str(), "w")) {
        fprintf(f, "# megatouch-menu operator settings\nFREE_PLAY=%d\nCREDITS=%d\nLANGUAGE=%s\nVOLUME=%d\n", g_set.free_play ? 1 : 0,
                g_set.credits, g_set.language.c_str(), g_set.volume);
        fclose(f);
    }
}

// ---------------------------------------------------------------------------- input
struct Touch { bool down = false; int x = 0, y = 0; };
bool g_quit;
std::function<void(SDL_Keycode)> g_onkey;
// one frame of events: returns the tap (finger/mouse up) if there was one
bool poll(Touch& tap) {
    SDL_Event e;
    bool got = false;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_quit = true;
        else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) { tap.x = e.button.x; tap.y = e.button.y; got = true; }
        else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_c) { g_set.credits++; save_settings(); }          // coin
            if (g_onkey) g_onkey(e.key.keysym.sym);
        }
    }
    return got;
}
bool inside(const Touch& t, int x, int y, int w, int h) { return t.x >= x && t.x < x + w && t.y >= y && t.y < y + h; }

// ---------------------------------------------------------------------------- launching
// Runs games/<dir>/run with the player count and the operator's language; the menu window is
// hidden meanwhile. Returns after the game exits.
void launch(Game& g, int players) {
    if (!g_set.free_play) {
        if (g_set.credits < players) return;
        g_set.credits -= players;
        save_settings();
    }
    SDL_HideWindow(g_win);
    SDL_PumpEvents();
    pid_t pid = fork();
    if (pid == 0) {
        std::string dir = g_repo + "/games/" + g.dir;
        if (chdir(dir.c_str()) != 0) _exit(127);
        setenv("MEGA_PLAYERS", std::to_string(players).c_str(), 1);
        setenv("MEGA_LANGUAGE", g_set.language.c_str(), 1);
        execl((dir + "/run").c_str(), "run", (char*)nullptr);
        _exit(127);
    }
    if (pid > 0) { int st; waitpid(pid, &st, 0); }
    load_scores(g);
    SDL_ShowWindow(g_win);
    SDL_RaiseWindow(g_win);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}

// ---------------------------------------------------------------------------- screens
enum Screen { ATTRACT, MAIN, CATEGORY, DETAIL, SCORES, OPERATOR };
struct Cat { const char* id; const char* label; const char* icon; };
const Cat kCats[] = {{"cards", "CARDS", "cards"},       {"puzzles", "PUZZLES", "puzzles"}, {"quizword", "QUIZ & WORD", "quizword"},
                     {"megasports", "SPORTS", "megasports"}, {"strategy", "STRATEGY", "strategy"}, {"erotic", "ADULT", "erotic"}};

void present() { SDL_RenderPresent(g_ren); SDL_Delay(16); }
void background() {
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    draw(image("menugraphics/main/2k3/ionbkg.spr"), 0, 0);
}
void credits_line() {
    std::string s = g_set.free_play ? "FREE PLAY" : "CREDITS " + std::to_string(g_set.credits);
    text(s, W - 10, H - 24, 16, YELLOW, 2);
}
// a button made from button01A/B with a label
bool button(const Touch* tap, const std::string& label, int x, int y, int w = 94, int h = 36) {
    const Tex& b = image("menugraphics/main/2k3/buttons/button01A.spr");
    if (b.t) { SDL_Rect r{x, y, w, h}; SDL_SetTextureAlphaMod(b.t, 255); SDL_RenderCopy(g_ren, b.t, nullptr, &r); }
    else fill(x, y, w, h, 40, 60, 160);
    text(label, x + w / 2, y + h / 2 - 9, 15, WHITE, 1);
    return tap && inside(*tap, x, y, w, h);
}

std::vector<std::string> attract_images() {
    std::vector<std::string> v;
    for (const auto& r : data_roots()) {
        for (const std::string& sub : {std::string("/menugraphics/idle"), std::string("/menugraphics/idle/english")})
            for (const auto& f : list_dir(r + sub)) {
                std::string lf = lower(f);
                if ((lf.size() > 4 && lf.compare(lf.size() - 4, 4, ".jpg") == 0) && lf.find("germany") == std::string::npos)
                    v.push_back(sub.substr(1) + "/" + f);
            }
        if (!v.empty()) break;
    }
    return v;
}
}  // namespace

int main(int argc, char** argv) {
    // repo root: --repo, else two levels above shared/bin/megatouch-menu
    char exe[4096] = "";
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) { exe[n] = 0; g_repo = exe; for (int i = 0; i < 3; i++) g_repo = g_repo.substr(0, g_repo.rfind('/')); }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--repo") && i + 1 < argc) g_repo = argv[++i];
        else if (!strcmp(argv[i], "--window") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &g_winW, &g_winH);
    }
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    g_win = SDL_CreateWindow("Megatouch", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_winW, g_winH, SDL_WINDOW_RESIZABLE);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
    SDL_RenderSetLogicalSize(g_ren, W, H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    font_init();
    load_settings();
    load_catalogue();
    fprintf(stderr, "[menu] %zu games\n", g_games.size());

    Screen scr = ATTRACT;
    std::string cat;
    int page = 0, sel = -1, players = 1, opsel = 0;
    Uint32 last_input = SDL_GetTicks(), attract_t = 0;
    size_t attract_i = 0;
    auto attract = attract_images();
    const char* langs[] = {"english", "french", "spanish", "german", "italian"};
    bool shot = getenv("MEGA_MENU_SHOT") != nullptr;  // headless check: one screenshot per screen
    g_onkey = [&](SDL_Keycode k) {
        if (k == SDLK_ESCAPE) { if (scr == MAIN || scr == ATTRACT) g_quit = true; else scr = MAIN; }
        if (k == SDLK_F2) scr = OPERATOR;          // the cabinet's operator button
    };
    while (!g_quit) {
        Touch tap;
        bool tapped = poll(tap);
        Touch* t = tapped ? &tap : nullptr;
        if (tapped) last_input = SDL_GetTicks();
        if (scr != ATTRACT && scr != OPERATOR && SDL_GetTicks() - last_input > 60000) scr = ATTRACT;   // idle timeout (settings: 1 min)

        background();
        switch (scr) {
        case ATTRACT: {
            if (!attract.empty()) {
                if (SDL_GetTicks() - attract_t > 6000) { attract_t = SDL_GetTicks(); attract_i = (attract_i + 1) % attract.size(); }
                const Tex& a = image(attract[attract_i]);
                if (a.t) { SDL_Rect r{0, 0, W, H}; SDL_SetTextureAlphaMod(a.t, 255); SDL_RenderCopy(g_ren, a.t, nullptr, &r); }
            } else draw(image("menugraphics/main/2k3/ionlogo.spr"), (W - 340) / 2, 180);
            if ((SDL_GetTicks() / 600) % 2) text("TOUCH THE SCREEN TO PLAY", W / 2, H - 50, 22, YELLOW, 1);
            if (t) scr = MAIN;
            break;
        }
        case MAIN: {
            draw(image("menugraphics/main/2k3/ionlogo.spr"), (W - 340) / 2, 14);
            for (int i = 0; i < 6; i++) {
                int x = 40 + (i % 3) * 195, y = 120 + (i / 3) * 120;
                draw(image("menugraphics/main/2k3/buttons/catbuttonA.spr"), x, y);
                draw(image(std::string("menugraphics/main/2k3/buttons/cat_icons/") + kCats[i].icon + ".spr"), x + 8, y + 8);
                int count = (int)std::count_if(g_games.begin(), g_games.end(), [&](const Game& g) { return g.category == kCats[i].id; });
                text(kCats[i].label, x + 78, y + 92, 17, WHITE, 1);
                text(std::to_string(count) + " games", x + 78, y + 110, 13, GREY, 1);
                if (t && inside(*t, x, y, 156, 87)) { cat = kCats[i].id; page = 0; scr = CATEGORY; }
            }
            if (button(t, "HIGH SCORES", 40, 400, 150)) scr = SCORES;
            if (button(t, "ALL GAMES", 245, 400, 150)) { cat = ""; page = 0; scr = CATEGORY; }
            credits_line();
            break;
        }
        case CATEGORY: {
            std::vector<int> idx;
            for (int i = 0; i < (int)g_games.size(); i++) if (cat.empty() || g_games[i].category == cat) idx.push_back(i);
            const int per = 12, pages = std::max(1, ((int)idx.size() + per - 1) / per);
            page = std::min(page, pages - 1);
            draw(image("menugraphics/main/2k3/optionwindow.spr"), 2, 10);
            std::string head = cat.empty() ? "ALL GAMES" : "GAMES";
            for (const auto& c : kCats) if (cat == c.id) head = c.label;
            text(head + "  (" + std::to_string(page + 1) + "/" + std::to_string(pages) + ")", W / 2, 20, 20, YELLOW, 1);
            for (int k = 0; k < per && page * per + k < (int)idx.size(); k++) {
                Game& g = g_games[idx[page * per + k]];
                int x = 40 + (k % 4) * 142, y = 60 + (k / 4) * 105;
                const Tex& lg = g.logo.empty() ? image("") : image(g.logo);
                if (lg.t) draw(lg, x, y);
                else { fill(x, y, 122, 62, 20, 40, 110, 220); text(g.title.substr(0, 16), x + 61, y + 22, 13, WHITE, 1); }
                text(g.title.substr(0, 18), x + 61, y + 66, 12, WHITE, 1);
                if (t && inside(*t, x, y, 122, 80)) { sel = idx[page * per + k]; players = 1; scr = DETAIL; }
            }
            if (page > 0 && button(t, "< PREV", 20, 430)) page--;
            if (page + 1 < pages && button(t, "NEXT >", 526, 430)) page++;
            if (button(t, "BACK", 273, 430)) scr = MAIN;
            break;
        }
        case DETAIL: {
            Game& g = g_games[sel];
            draw(image("menugraphics/game/gameinfo.spr"), 0, 26);
            if (!g.logo.empty()) { const Tex& lg = image(g.logo); SDL_Rect r{W / 2 - 122, 60, 244, 124}; if (lg.t) SDL_RenderCopy(g_ren, lg.t, nullptr, &r); }
            text(g.title, W / 2, 196, 26, YELLOW, 1);
            if (g.best > 0) text("HIGH SCORE  " + std::to_string(g.best) + "  " + g.best_name, W / 2, 236, 16, WHITE, 1);
            if (g.max_players > 1) {
                text("PLAYERS", W / 2, 268, 16, GREY, 1);
                for (int p = 1; p <= g.max_players; p++) {
                    int x = W / 2 - g.max_players * 33 + (p - 1) * 66;
                    std::string b = std::string("menugraphics/game/buttons/p") + std::to_string(p) + (p == players ? "butta.spr" : "butt.spr");
                    draw(image(b), x, 292);
                    if (t && inside(*t, x, 292, 56, 58)) players = p;
                }
            }
            bool can = g_set.free_play || g_set.credits >= players;
            draw(image("menugraphics/game/buttons/playbutt.spr"), W / 2 - 28, 362, can ? 255 : 90);
            text(can ? "PLAY" : "INSERT CREDIT", W / 2, 420, 15, can ? WHITE : YELLOW, 1);
            if (t && can && inside(*t, W / 2 - 28, 362, 56, 56)) { launch(g, players); last_input = SDL_GetTicks(); }
            if (button(t, "BACK", 20, 430)) scr = CATEGORY;
            credits_line();
            break;
        }
        case SCORES: {
            text("HIGH SCORES", W / 2, 16, 24, YELLOW, 1);
            std::vector<const Game*> v;
            for (const auto& g : g_games) if (g.best > 0) v.push_back(&g);
            std::sort(v.begin(), v.end(), [](const Game* a, const Game* b) { return a->title < b->title; });
            int y = 56;
            if (v.empty()) text("NO SCORES YET", W / 2, 200, 18, WHITE, 1);
            for (size_t i = 0; i < v.size() && i < 18; i++, y += 20) {
                text(v[i]->title, 60, y, 15, WHITE);
                text(std::to_string(v[i]->best), 440, y, 15, YELLOW, 2);
                text(v[i]->best_name, 460, y, 15, GREY);
            }
            if (button(t, "BACK", 273, 430)) scr = MAIN;
            break;
        }
        case OPERATOR: {
            fill(30, 30, W - 60, H - 60, 0, 0, 0, 200);
            text("OPERATOR SETUP", W / 2, 44, 24, YELLOW, 1);
            std::string rows[] = {std::string("FREE PLAY: ") + (g_set.free_play ? "ON" : "OFF"), "CREDITS: " + std::to_string(g_set.credits),
                                  "LANGUAGE: " + g_set.language, "VOLUME: " + std::to_string(g_set.volume) + "%"};
            for (int i = 0; i < 4; i++) {
                int y = 110 + i * 60;
                text(rows[i], 80, y, 20, i == opsel ? YELLOW : WHITE);
                if (button(t, "-", 420, y - 6, 50)) {
                    opsel = i;
                    if (i == 0) g_set.free_play = !g_set.free_play;
                    if (i == 1) g_set.credits = std::max(0, g_set.credits - 1);
                    if (i == 2) { int k = 0; for (int j = 0; j < 5; j++) if (g_set.language == langs[j]) k = j; g_set.language = langs[(k + 4) % 5]; }
                    if (i == 3) g_set.volume = std::max(0, g_set.volume - 10);
                    save_settings();
                }
                if (button(t, "+", 490, y - 6, 50)) {
                    opsel = i;
                    if (i == 0) g_set.free_play = !g_set.free_play;
                    if (i == 1) g_set.credits++;
                    if (i == 2) { int k = 0; for (int j = 0; j < 5; j++) if (g_set.language == langs[j]) k = j; g_set.language = langs[(k + 1) % 5]; }
                    if (i == 3) g_set.volume = std::min(100, g_set.volume + 10);
                    save_settings();
                }
            }
            text(std::to_string(g_games.size()) + " games installed", W / 2, 360, 15, GREY, 1);
            if (button(t, "EXIT SETUP", 245, 410, 150)) scr = MAIN;
            break;
        }
        }
        present();
        if (shot) {
            // MEGA_MENU_SHOT=dir: walk through the screens once, saving each (headless check)
            static int step = 0;
            static const Screen order[] = {ATTRACT, MAIN, CATEGORY, DETAIL, SCORES, OPERATOR};
            if (++step % 20 == 0) {
                int k = step / 20 - 1;
                if (k >= 6) break;
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, g_winW, g_winH, 32, SDL_PIXELFORMAT_ARGB8888);
                SDL_RenderReadPixels(g_ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
                std::string p = std::string(getenv("MEGA_MENU_SHOT")) + "/menu" + std::to_string(k) + ".bmp";
                SDL_SaveBMP(s, p.c_str());
                SDL_FreeSurface(s);
                if (k + 1 < 6) { scr = order[k + 1]; if (scr == CATEGORY) cat = "cards"; if (scr == DETAIL) sel = 0; }
            }
        }
    }
    SDL_Quit();
    return 0;
}
