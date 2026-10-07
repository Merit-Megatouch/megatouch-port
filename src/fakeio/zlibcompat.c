/*
 * zlibcompat — the cabinet's own zlib (1.2.3, 2007) for the cabinet's code.
 *
 * The sandbox has one libz.so.1: the modern runtime's, which its libraries (libpng16, libxml2,
 * libpciaccess) need. The loader's sprite reader (LoadCompAnim: gzopen/gzread/gzseek/gztell over
 * .spr.gz files) depends on how zlib 1.2.3 behaved; on the modern zlib some reads come back
 * wrong, and games such as Super Boxxi crash on garbage sizes and pointers (bad_alloc, free() of
 * junk). Here the gz* file functions are wrapped: calls from cabinet code (anything not under
 * /opt/rt) go to a private copy of the cabinet's zlib, renamed so it can be loaded next to the
 * modern one (build/loader/bin/libz-cabinet.so, made by `make loader`); calls from the runtime's
 * libraries keep the modern zlib. A file stays with the zlib that opened it.
 * MEGA_ZLIB=modern turns this off.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *gzFile;
static void *old;                 /* the cabinet's zlib, loaded privately */
static int disabled;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static gzFile handles[256];       /* files opened through the cabinet's zlib */

static void init(void) {
    static int done;
    if (done) return;
    done = 1;
    const char *m = getenv("MEGA_ZLIB");
    if (m && !strcmp(m, "modern")) { disabled = 1; return; }
    old = dlopen("/opt/fakeio/libz-cabinet.so", RTLD_NOW | RTLD_LOCAL);
    if (!old) { fprintf(stderr, "[zlibcompat] %s: using the modern zlib\n", dlerror()); disabled = 1; }
}

static void *fn_old(const char *name) { return dlsym(old, name); }
static void *fn_new(const char *name) { return dlsym(RTLD_NEXT, name); }

/* Is the caller cabinet code (→ old zlib) rather than a runtime library under /opt/rt? */
static int cabinet_caller(void *ret) {
    init();
    if (disabled) return 0;
    Dl_info i;
    return !(dladdr(ret, &i) && i.dli_fname && !strncmp(i.dli_fname, "/opt/rt/", 8));
}

static void track(gzFile f) {
    if (!f) return;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 256; i++) if (!handles[i]) { handles[i] = f; break; }
    pthread_mutex_unlock(&lock);
}

static int is_old(gzFile f, int forget) {
    if (!f || disabled) return 0;
    int r = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < 256; i++) if (handles[i] == f) { r = 1; if (forget) handles[i] = NULL; break; }
    pthread_mutex_unlock(&lock);
    return r;
}

#define PICK(f, name, type) ((type)(is_old(f, 0) ? fn_old(name) : fn_new(name)))

gzFile gzopen(const char *path, const char *mode) {
    typedef gzFile (*t)(const char *, const char *);
    if (cabinet_caller(__builtin_return_address(0))) { gzFile f = ((t)fn_old("gzopen"))(path, mode); track(f); return f; }
    return ((t)fn_new("gzopen"))(path, mode);
}

gzFile gzdopen(int fd, const char *mode) {
    typedef gzFile (*t)(int, const char *);
    if (cabinet_caller(__builtin_return_address(0))) { gzFile f = ((t)fn_old("gzdopen"))(fd, mode); track(f); return f; }
    return ((t)fn_new("gzdopen"))(fd, mode);
}

int gzclose(gzFile f) {
    typedef int (*t)(gzFile);
    int o = is_old(f, 1);
    return ((t)(o ? fn_old("gzclose") : fn_new("gzclose")))(f);
}

int gzread(gzFile f, void *buf, unsigned len) { return PICK(f, "gzread", int (*)(gzFile, void *, unsigned))(f, buf, len); }
int gzwrite(gzFile f, const void *buf, unsigned len) { return PICK(f, "gzwrite", int (*)(gzFile, const void *, unsigned))(f, buf, len); }
long gzseek(gzFile f, long off, int whence) { return PICK(f, "gzseek", long (*)(gzFile, long, int))(f, off, whence); }
long gztell(gzFile f) { return PICK(f, "gztell", long (*)(gzFile))(f); }
int gzrewind(gzFile f) { return PICK(f, "gzrewind", int (*)(gzFile))(f); }
int gzeof(gzFile f) { return PICK(f, "gzeof", int (*)(gzFile))(f); }
int gzdirect(gzFile f) { return PICK(f, "gzdirect", int (*)(gzFile))(f); }
char *gzgets(gzFile f, char *buf, int len) { return PICK(f, "gzgets", char *(*)(gzFile, char *, int))(f, buf, len); }
int gzputs(gzFile f, const char *s) { return PICK(f, "gzputs", int (*)(gzFile, const char *))(f, s); }
int gzgetc(gzFile f) { return PICK(f, "gzgetc", int (*)(gzFile))(f); }
int gzputc(gzFile f, int c) { return PICK(f, "gzputc", int (*)(gzFile, int))(f, c); }
int gzungetc(int c, gzFile f) { return PICK(f, "gzungetc", int (*)(int, gzFile))(c, f); }
int gzflush(gzFile f, int flush) { return PICK(f, "gzflush", int (*)(gzFile, int))(f, flush); }
int gzsetparams(gzFile f, int level, int strategy) { return PICK(f, "gzsetparams", int (*)(gzFile, int, int))(f, level, strategy); }
const char *gzerror(gzFile f, int *err) { return PICK(f, "gzerror", const char *(*)(gzFile, int *))(f, err); }
void gzclearerr(gzFile f) { PICK(f, "gzclearerr", void (*)(gzFile))(f); }

int gzprintf(gzFile f, const char *fmt, ...) {
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return n;
    if (n >= (int)sizeof buf) n = sizeof buf - 1;
    return gzwrite(f, buf, n);
}
