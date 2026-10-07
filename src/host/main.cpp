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
#include <ucontext.h>
#include <cstdint>
#include <execinfo.h>
#include "../common/env.h"

extern "C" void trix_profile_start();
extern "C" void trix_profile_dump();
extern "C" void trix_profile_recover();
extern "C" void trix_set_data_root(const char* root);

// Print a symbolized backtrace (library + offset) on crashes; the engine has no debug info.
static void where(const char* label, void* pc) {
    Dl_info d;
    if (dladdr(pc, &d) && d.dli_fname)
        fprintf(stderr, "  %s %s+0x%lx  (%s)\n", label, d.dli_fname, (unsigned long)((char*)pc - (char*)d.dli_fbase),
                d.dli_sname ? d.dli_sname : "?");
    else fprintf(stderr, "  %s %p\n", label, pc);
}
// reads a word without faulting (write() reports EFAULT for unreadable memory)
static bool peek(void* const* p, void** out) {
    static int fds[2] = {-1, -1};
    if (fds[0] < 0 && pipe(fds) != 0) return false;
    if (write(fds[1], p, sizeof *p) != (ssize_t)sizeof *p) return false;
    return read(fds[0], out, sizeof *out) == (ssize_t)sizeof *out;
}
static void crash(int sig, siginfo_t* si, void* uc) {
    static volatile int depth;
    if (depth++) _exit(128 + sig);           // a fault while reporting a fault
    trix_profile_recover();   // returns only if the fault was not inside the sampler
    fflush(stdout);                                // the game's own last messages first
    fprintf(stderr, "\n*** signal %d at address %p\n", sig, si->si_addr);
#if defined(__i386__)
    // the faulting instruction and a frame-pointer walk first: backtrace() itself can fault
    // when the crashing code has no unwind information
    auto* mc = &static_cast<ucontext_t*>(uc)->uc_mcontext;
    where("pc", reinterpret_cast<void*>(mc->gregs[REG_EIP]));
    void** fp = reinterpret_cast<void**>(mc->gregs[REG_EBP]);
    for (int i = 0; i < 16 && fp && (reinterpret_cast<uintptr_t>(fp) & 3) == 0; i++) {
        void* ret; void* nx;
        if (!peek(fp + 1, &ret) || !peek(fp, &nx) || (uintptr_t)ret < 0x1000) break;
        char label[16]; snprintf(label, sizeof label, "fp%d", i);
        where(label, ret);
        void** next = static_cast<void**>(nx);
        if (next <= fp) break;
        fp = next;
    }
    // stack scan: words that point into code of a loaded library (likely return addresses)
    void** sp = reinterpret_cast<void**>(mc->gregs[REG_ESP]);
    int shown = 0;
    for (int i = 0; i < 512 && shown < 12; i++) {
        Dl_info d;
        void* v;
        if (!peek(sp + i, &v)) break;
        if ((uintptr_t)v < 0x10000 || !dladdr(v, &d) || !d.dli_fname || !d.dli_sname) continue;
        if (strstr(d.dli_fname, "/runtime/")) continue;        // libc & co.
        char label[16]; snprintf(label, sizeof label, "sp+%d", i * 4);
        where(label, v);
        shown++;
    }
#endif
    void* frames[64];
    int n = backtrace(frames, 64);
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
    // an alternate stack, so stack overflows are reported too
    static char altstack[256 * 1024];
    stack_t ss = {};
    ss.ss_sp = altstack; ss.ss_size = sizeof altstack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa = {};
    sa.sa_sigaction = crash;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
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
    // Output paths given relative to where the user started the game must survive the chdir.
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd))
        for (const char* k : {"SHOT_DIR", "PROFILE"})
            if (const char* v = menv(k); v && *v && *v != '/')
                setenv((std::string("MEGA_") + k).c_str(), (std::string(cwd) + "/" + v).c_str(), 1);
    if (chdir(gameDir.c_str()) != 0) { perror(gameDir.c_str()); return 1; }

    // PRELOAD=a.so b.so: libraries from lib/ the cabinet loader had already loaded (the legacy
    // engine stand-in, libsettings.so...), so the game's imports resolve against them.
    if (const char* pre = menv("PRELOAD")) {
        std::string list(pre), name;
        for (size_t i = 0; i <= list.size(); i++) {
            if (i < list.size() && list[i] != ' ' && list[i] != ',') { name += list[i]; continue; }
            if (name.empty()) continue;
            std::string p = base + "/lib/" + name;
            if (access(p.c_str(), F_OK) != 0) p = name;       // a library from the runtime (libGL.so.1)
            if (!dlopen(p.c_str(), RTLD_NOW | RTLD_GLOBAL)) { fprintf(stderr, "failed to preload %s: %s\n", p.c_str(), dlerror()); return 1; }
            name.clear();
        }
    }

    std::string game = base + "/lib/" + lib;
    void* h = dlopen(game.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "failed to load %s: %s\n", game.c_str(), dlerror()); return 1; }
    trix_profile_start();
    // Merit2d games (zenword) read their data from BaseGame::GetPath(), which the cabinet loader
    // set to the game's gamegraphics folder; SetPath(name) is a member that ignores `this`.
    if (auto setPath = reinterpret_cast<void (*)(void*, const char*)>(dlsym(RTLD_DEFAULT, "_ZN13MeritBaseGame8BaseGame7SetPathEPKc")))
        if (const char* ad = menv("ASSET_DIR")) {
            std::string folder(ad);
            while (!folder.empty() && folder.back() == '/') folder.pop_back();
            setPath(nullptr, folder.substr(folder.rfind('/') + 1).c_str());
        }
    if (auto gameMain = reinterpret_cast<int (*)(int, char**)>(dlsym(h, "main")))
        return gameMain(argc, argv) ? 0 : 1;
    // legacy games export only the loader's entry point
    if (auto entry = reinterpret_cast<void (*)()>(dlsym(h, "__EntryPointV12"))) { entry(); return 0; }
    fprintf(stderr, "%s has neither main nor __EntryPointV12\n", lib);
    return 1;
}
