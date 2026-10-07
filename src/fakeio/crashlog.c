/*
 * crashlog — LD_PRELOAD helper for the loader sandbox: on SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT
 * print the faulting instruction, registers and a frame-pointer backtrace, each address resolved
 * to "object+offset" from /proc/self/maps, then re-raise the signal.
 *
 * It also emulates x86 port I/O. The loader still carries code for the pre-ION ISA board
 * (ports 0x228-0x22A) and asks for iopl/ioperm, which WSL kernels do not have. Those calls
 * succeed (startfix.c), and the resulting in/out instructions fault here: reads return 0xFF,
 * writes are dropped, each port is logged once.
 *
 * And it makes generated code run: Allegro 4 builds blitters in malloc'd memory and calls them,
 * which needs readable-implies-executable memory (gone from current x86-64 kernels). An
 * instruction fetch from a non-executable page is answered by making that page RWX.
 *
 * The program installs its own SIGSEGV handler (sig_backtrace.cpp), so sigaction() and
 * signal() are wrapped: for the signals handled here, the program's handler is recorded and
 * called whenever this one does not resolve the fault.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static char maps[1 << 20];

static void where(unsigned long a, char *out, size_t n) {
    snprintf(out, n, "?");
    char *line = maps;
    while (line && *line) {
        unsigned long lo, hi, off;
        char perm[8], path[256] = "";
        if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %255[^\n]", &lo, &hi, perm, &off, path) >= 4 &&
            a >= lo && a < hi) {
            const char *b = strrchr(path, '/');
            snprintf(out, n, "%s+%#lx", b ? b + 1 : (*path ? path : "anon"), a - lo + off);
            return;
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
}

static unsigned char seen_port[0x10000];

static void note_port(unsigned port, int out) {
    if (seen_port[port & 0xFFFF]) return;
    seen_port[port & 0xFFFF] = 1;
    fprintf(stderr, "[portio] %s port %#x (emulated)\n", out ? "write" : "read", port);
}

/* Emulate in/out/ins/outs at EIP. Returns 1 if the instruction was handled. */
static int emulate_portio(greg_t *g) {
    unsigned char *ip = (unsigned char *)g[REG_EIP];
    int len = 0, size = 4, rep = 0;
    for (;; len++) {
        if (ip[len] == 0x66) size = 2;
        else if (ip[len] == 0xF3 || ip[len] == 0xF2) rep = 1;
        else break;
    }
    unsigned char op = ip[len++];
    unsigned dx = g[REG_EDX] & 0xFFFF;
    unsigned long mask = size == 2 ? 0xFFFF : 0xFFFFFFFF;
    switch (op) {
    case 0xE4: note_port(ip[len], 0); g[REG_EAX] |= 0xFF; len++; break;
    case 0xE5: note_port(ip[len], 0); g[REG_EAX] |= mask; len++; break;
    case 0xEC: note_port(dx, 0); g[REG_EAX] |= 0xFF; break;
    case 0xED: note_port(dx, 0); g[REG_EAX] |= mask; break;
    case 0xE6: case 0xE7: note_port(ip[len], 1); len++; break;
    case 0xEE: case 0xEF: note_port(dx, 1); break;
    case 0x6C: case 0x6D: {                      /* ins: store 0xFF.. at ES:EDI */
        int sz = op == 0x6C ? 1 : size;
        unsigned long n = rep ? g[REG_ECX] : 1;
        note_port(dx, 0);
        memset((void *)g[REG_EDI], 0xFF, n * sz);
        g[REG_EDI] += n * sz;
        if (rep) g[REG_ECX] = 0;
        break;
    }
    case 0x6E: case 0x6F: {                      /* outs: skip the data */
        int sz = op == 0x6E ? 1 : size;
        unsigned long n = rep ? g[REG_ECX] : 1;
        note_port(dx, 1);
        g[REG_ESI] += n * sz;
        if (rep) g[REG_ECX] = 0;
        break;
    }
    default: return 0;
    }
    g[REG_EIP] += len;
    return 1;
}

#define NSIG_OURS 32
static struct sigaction app_action[NSIG_OURS];
static int ours[NSIG_OURS];
static int (*real_sigaction)(int, const struct sigaction *, struct sigaction *);

static int make_exec(void *addr) {
    static int warned;
    long pg = sysconf(_SC_PAGESIZE);
    void *page = (void *)((unsigned long)addr & ~(pg - 1));
    if (mprotect(page, pg * 2, PROT_READ | PROT_WRITE | PROT_EXEC) != 0 &&
        mprotect(page, pg, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return 0;
    if (!warned++) fprintf(stderr, "[nx] made generated code at %p executable\n", addr);
    return 1;
}

static void chain(int sig, siginfo_t *si, void *uc) {
    struct sigaction *a = &app_action[sig];
    if (a->sa_flags & SA_SIGINFO) {
        if (a->sa_sigaction) { a->sa_sigaction(sig, si, uc); return; }
    } else if (a->sa_handler != SIG_DFL && a->sa_handler != SIG_IGN && a->sa_handler) {
        a->sa_handler(sig);
        return;
    } else if (a->sa_handler == SIG_IGN) return;
}

static void report(int sig, siginfo_t *si, void *uc_);

static void handler(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    greg_t *g = uc->uc_mcontext.gregs;
    if (sig == SIGSEGV && si->si_addr == NULL && emulate_portio(g)) return;
    if (sig == SIGSEGV && si->si_code == SEGV_ACCERR &&
        (unsigned long)si->si_addr - (unsigned long)g[REG_EIP] < 16 &&   /* fetch, may straddle pages */
        make_exec(si->si_addr)) return;
    report(sig, si, uc_);
    struct sigaction *a = &app_action[sig];
    if ((a->sa_flags & SA_SIGINFO) ? a->sa_sigaction != NULL
                                   : (a->sa_handler != SIG_DFL && a->sa_handler != NULL)) {
        chain(sig, si, uc_);
        return;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    if (!real_sigaction) real_sigaction = dlsym(RTLD_NEXT, "sigaction");
    if (sig > 0 && sig < NSIG_OURS && ours[sig]) {
        if (old) *old = app_action[sig];
        if (act) app_action[sig] = *act;
        return 0;
    }
    return real_sigaction(sig, act, old);
}

sighandler_t signal(int sig, sighandler_t h) {
    if (sig > 0 && sig < NSIG_OURS && ours[sig]) {
        sighandler_t prev = app_action[sig].sa_handler;
        memset(&app_action[sig], 0, sizeof app_action[sig]);
        app_action[sig].sa_handler = h;
        return prev;
    }
    if (!real_sigaction) real_sigaction = dlsym(RTLD_NEXT, "sigaction");
    struct sigaction a, o;
    memset(&a, 0, sizeof a);
    a.sa_handler = h;
    a.sa_flags = SA_RESTART;
    if (real_sigaction(sig, &a, &o) != 0) return SIG_ERR;
    return o.sa_handler;
}

static void report(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    int fd = open("/proc/self/maps", 0);
    size_t n = 0;
    ssize_t r;
    while (fd >= 0 && n < sizeof maps - 1 && (r = read(fd, maps + n, sizeof maps - 1 - n)) > 0) n += r;
    maps[n] = 0;
    if (fd >= 0) close(fd);
    char w[300];
    greg_t *g = uc->uc_mcontext.gregs;
    where(g[REG_EIP], w, sizeof w);
    fprintf(stderr, "\n[crash] %s (pid %d): signal %d at %#lx (%s), fault address %p\n",
            program_invocation_short_name, (int)getpid(), sig,
            (unsigned long)g[REG_EIP], w, si->si_addr);
    fprintf(stderr, "[crash] eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx\n",
            (unsigned long)g[REG_EAX], (unsigned long)g[REG_EBX], (unsigned long)g[REG_ECX],
            (unsigned long)g[REG_EDX], (unsigned long)g[REG_ESI], (unsigned long)g[REG_EDI],
            (unsigned long)g[REG_EBP], (unsigned long)g[REG_ESP]);
    unsigned long *sp = (unsigned long *)g[REG_ESP];
    where(sp[0], w, sizeof w);
    fprintf(stderr, "[crash]   [esp] %#lx %s\n", sp[0], w);
    /* stack scan: words that point into executable code outside libc (likely return addresses) */
    for (int i = 0, shown = 0; i < 512 && shown < 12; i++) {
        unsigned long v = sp[i];
        if (v < 0x1000) continue;
        where(v, w, sizeof w);
        if (w[0] == '?' || !strncmp(w, "libc.so", 7) || !strncmp(w, "[stack]", 7) || !strncmp(w, "anon", 4) ||
            !strncmp(w, "[heap]", 6) || strstr(w, "crashlog")) continue;
        fprintf(stderr, "[crash]   stack+%#x: %#lx %s\n", i * 4, v, w);
        shown++;
    }
    /* format strings nearby on the stack (crashes inside printf-style logging) */
    for (int i = 0, shown = 0; i < 512 && shown < 6; i++) {
        unsigned long v = sp[i];
        where(v, w, sizeof w);
        if (w[0] == '?' || !strncmp(w, "[stack]", 7)) continue;
        const char *str = (const char *)v;
        int len = 0, pct = 0;
        while (len < 200 && str[len] >= 0x20 && str[len] < 0x7f) { if (str[len] == '%') pct = 1; len++; }
        if (pct && len > 3 && str[len] == 0) { fprintf(stderr, "[crash]   fmt stack+%#x: \"%.120s\"\n", i * 4, str); shown++; }
    }
    unsigned long *fp = (unsigned long *)g[REG_EBP];
    for (int i = 0; i < 24 && fp && ((unsigned long)fp & 3) == 0; i++) {
        unsigned long ret = fp[1];
        if (ret < 0x1000) break;
        where(ret, w, sizeof w);
        fprintf(stderr, "[crash]   #%d %#lx %s\n", i, ret, w);
        unsigned long *next = (unsigned long *)fp[0];
        if (next <= fp) break;
        fp = next;
    }
}

__attribute__((constructor)) static void init(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    static char altstack[65536];
    stack_t ss = {.ss_sp = altstack, .ss_size = sizeof altstack};
    sigaltstack(&ss, NULL);
    real_sigaction = dlsym(RTLD_NEXT, "sigaction");
    int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
    for (unsigned i = 0; i < sizeof sigs / sizeof *sigs; i++) {
        real_sigaction(sigs[i], &sa, NULL);
        ours[sigs[i]] = 1;
    }
}
