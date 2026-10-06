// megatouch-host: runs one Megatouch GameDevice game outside the cabinet.
//
// Reads <home>/game.conf (home = $MEGA_HOME, else the executable's directory):
//   LIB=g_trix.so                                   game library in <home>/lib
//   ASSET_DIR=usr/local/ion_only/games/g_trix       cabinet path of its assets (under <home>/data)
//   GAME_ID=245  WIDTH=1280  HEIGHT=800  LANGUAGE=english  TITLE=Megatouch Trix
// Every key is exported as MEGA_<KEY> for the backend unless already set in the environment,
// so any of them can be overridden per run (e.g. MEGA_WIDTH=1024 ./run).
#include <dlfcn.h>
#include <libgen.h>
#include <unistd.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <csignal>
#include <execinfo.h>
#include "../common/env.h"

extern "C" void trix_profile_start();
extern "C" void trix_profile_dump();
extern "C" void trix_profile_recover();
extern "C" void trix_set_data_root(const char* root);

// Print a symbolized backtrace (library + offset) on crashes; the engine has no debug info.
static void crash(int sig, siginfo_t* si, void* uc) {
    trix_profile_recover();   // returns only if the fault was not inside the sampler
    void* frames[64];
    int n = backtrace(frames, 64);
    fprintf(stderr, "\n*** signal %d at address %p\n", sig, si->si_addr);
    for (int i = 0; i < n; i++) {
        Dl_info d;
        if (dladdr(frames[i], &d) && d.dli_fname) {
            fprintf(stderr, "  #%d %s+0x%lx  (%s)\n", i, d.dli_fname,
                    (unsigned long)((char*)frames[i] - (char*)d.dli_fbase), d.dli_sname ? d.dli_sname : "?");
        } else {
            fprintf(stderr, "  #%d %p\n", i, frames[i]);
        }
    }
    trix_profile_dump();
    _exit(128 + sig);
}

// KEY=value lines; '#' starts a comment. Values are exported as MEGA_KEY (environment wins).
static void load_config(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "warning: no %s\n", path.c_str()); return; }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || !*s) continue;
        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        std::string key(s), val(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) val.pop_back();
        size_t lead = val.find_first_not_of(" \t");
        val = lead == std::string::npos ? "" : val.substr(lead);
        setenv(("MEGA_" + key).c_str(), val.c_str(), 0);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    struct sigaction sa = {};
    sa.sa_sigaction = crash;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);

    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) { perror("readlink"); return 1; }
    exe[n] = 0;
    std::string base = menv("HOME") ? menv("HOME") : dirname(exe);
    load_config(base + "/game.conf");

    std::string root = menv("DATA") ? menv("DATA") : base + "/data";
    trix_set_data_root(root.c_str());

    const char* lib = menv("LIB");
    const char* assets = menv("ASSET_DIR");
    if (!lib || !assets) { fprintf(stderr, "game.conf must set LIB and ASSET_DIR\n"); return 1; }

    // The cabinet's loader starts each game from inside its own directory; layouts and
    // effects are opened with paths relative to it.
    std::string gameDir = root + "/" + assets;
    if (chdir(gameDir.c_str()) != 0) { perror(gameDir.c_str()); return 1; }

    std::string game = base + "/lib/" + lib;
    void* h = dlopen(game.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "failed to load %s: %s\n", game.c_str(), dlerror()); return 1; }
    trix_profile_start();
    auto gameMain = reinterpret_cast<int (*)(int, char**)>(dlsym(h, "main"));
    if (!gameMain) { fprintf(stderr, "%s has no main\n", lib); return 1; }
    return gameMain(argc, argv) ? 0 : 1;
}
