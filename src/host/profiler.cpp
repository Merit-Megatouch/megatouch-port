// Wall-clock sampling profiler for the main (game) thread.
//
//   TRIX_PROFILE=/path/out.prof   enable; samples every TRIX_PROFILE_US microseconds (default 2000)
//
// A POSIX timer delivers SIGPROF to the main thread only. The handler records EIP plus
// return addresses found by walking the frame-pointer chain (the 2013 engine is built
// with frame pointers; modern libraries without them just cut the chain short).
// Addresses are symbolized with dladdr at exit and written as text for tools/profreport.py:
//   S <id> <library> <offset> <symbol>
//   T <ms> <id> <id> ...      (innermost first)
#include "../common/env.h"
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <execinfo.h>
#include <setjmp.h>
#include <map>
#include <string>

namespace {
const int kDepth = 40;
struct Sample { float ms; uint8_t n; uint32_t pc[kDepth]; };
Sample* g_samples;
size_t g_cap, g_count;
uintptr_t g_stackLo, g_stackHi;
const char* g_out;
timer_t g_timer;
// Unwinding can fault on frames without unwind info (e.g. llvmpipe's JIT code). The crash
// handler calls trix_profile_recover(), which jumps back here and keeps just the PC.
volatile sig_atomic_t g_inSample;
sigjmp_buf g_unwindJmp;

double mono_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void on_sample(int, siginfo_t*, void* ctx) {
    if (g_count >= g_cap) return;
    auto* uc = static_cast<ucontext_t*>(ctx);
    Sample& s = g_samples[g_count];
    s.ms = (float)mono_ms();
    // DWARF unwinding (via the sigreturn trampoline) gets through libc, Mesa, etc.,
    // which have no frame pointers. Frames 0-1 are this handler and the trampoline.
    static void* frames[kDepth + 2];
    uintptr_t pc = uc->uc_mcontext.gregs[REG_EIP];
    s.n = 0;
    s.pc[s.n++] = pc;
    int n = 0;
    g_inSample = 1;
    if (sigsetjmp(g_unwindJmp, 1) == 0) n = backtrace(frames, kDepth + 2);
    g_inSample = 0;
    int start = 0;
    for (int i = 0; i < n; i++) if (reinterpret_cast<uintptr_t>(frames[i]) == pc) { start = i + 1; break; }
    if (!start) start = n > 2 ? 2 : n;
    for (int i = start; i < n && s.n < kDepth; i++) s.pc[s.n++] = reinterpret_cast<uintptr_t>(frames[i]);
    g_count++;
}

std::string describe(uintptr_t a, std::string* lib, uintptr_t* off) {
    Dl_info d;
    if (!dladdr(reinterpret_cast<void*>(a), &d) || !d.dli_fname) { *lib = "?"; *off = a; return "?"; }
    const char* base = strrchr(d.dli_fname, '/');
    *lib = base ? base + 1 : d.dli_fname;
    *off = a - reinterpret_cast<uintptr_t>(d.dli_fbase);
    if (!d.dli_sname) return "?";
    int st = 0;
    char* dem = abi::__cxa_demangle(d.dli_sname, nullptr, nullptr, &st);
    std::string r = st == 0 && dem ? dem : d.dli_sname;
    free(dem);
    return r;
}

void dump() {
    if (!g_out || !g_samples) return;
    timer_delete(g_timer);
    FILE* f = fopen(g_out, "w");
    if (!f) return;
    std::map<uint32_t, int> ids;
    for (size_t i = 0; i < g_count; i++)
        for (int j = 0; j < g_samples[i].n; j++) ids.emplace(g_samples[i].pc[j], 0);
    int next = 0;
    for (auto& kv : ids) {
        kv.second = next++;
        std::string lib; uintptr_t off;
        std::string name = describe(kv.first, &lib, &off);
        for (char& c : name) if (c == '\n') c = ' ';
        fprintf(f, "S %d %s %lx %s\n", kv.second, lib.c_str(), (unsigned long)off, name.c_str());
    }
    for (size_t i = 0; i < g_count; i++) {
        fprintf(f, "T %.1f", g_samples[i].ms);
        for (int j = 0; j < g_samples[i].n; j++) fprintf(f, " %d", ids[g_samples[i].pc[j]]);
        fputc('\n', f);
    }
    fclose(f);
    fprintf(stderr, "[profile] %zu samples written to %s\n", g_count, g_out);
    g_out = nullptr;
}
}  // namespace

extern "C" void trix_profile_dump() { dump(); }
extern "C" void trix_profile_recover() {
    if (g_inSample) { g_inSample = 0; siglongjmp(g_unwindJmp, 1); }
}

extern "C" void trix_profile_start() {
    g_out = menv("PROFILE");
    if (!g_out) return;
    long us = menv("PROFILE_US") ? atol(menv("PROFILE_US")) : 2000;
    g_cap = (size_t)(30 * 60 * 1e6 / us);  // up to 30 minutes
    g_samples = static_cast<Sample*>(calloc(g_cap, sizeof(Sample)));

    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    void* lo; size_t sz;
    pthread_attr_getstack(&attr, &lo, &sz);
    g_stackLo = reinterpret_cast<uintptr_t>(lo);
    g_stackHi = g_stackLo + sz;

    void* warm[4];
    backtrace(warm, 4);   // load libgcc_s now, not inside the signal handler
    struct sigaction sa = {};
    sa.sa_sigaction = on_sample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, nullptr);

    sigevent sev = {};
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGPROF;
    sev._sigev_un._tid = (pid_t)syscall(SYS_gettid);
    timer_create(CLOCK_MONOTONIC, &sev, &g_timer);
    itimerspec its = {};
    its.it_interval.tv_nsec = us * 1000;
    its.it_value.tv_nsec = us * 1000;
    timer_settime(g_timer, 0, &its, nullptr);
    atexit(dump);
    fprintf(stderr, "[profile] sampling main thread every %ld us -> %s\n", us, g_out);
}
