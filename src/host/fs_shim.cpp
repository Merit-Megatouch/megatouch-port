// Filesystem shim: libc wrappers exported by megatouch-host. They redirect the cabinet's
// absolute paths (/usr/local/games, /usr/local/gamedata, /usr/local/ion_only, /var/merit,
// /dev/merit_ipc) into the game's data/ folder, and reimplement the old glibc stat/readdir
// ABI the 2008 libraries use (32-bit inode fields overflow on modern filesystems).
#include "../common/env.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <dirent.h>
#include <glob.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstddef>
#include <string>
// ---------------------------------------------------------------------------
// path redirection

static std::string g_root;  // e.g. /path/to/Trix/data

static const char* const kPrefixes[] = {"/usr/local/games", "/usr/local/gamedata", "/usr/local/ion_only",
                                        "/var/merit", "/dev/merit_ipc", nullptr};

// Returns the redirected path in `buf`, or the original pointer if not redirected.
static const char* redirect(const char* path, char* buf, size_t len) {
    static bool trace = menv("TRACE_FILES") != nullptr;
    if (trace && path) fprintf(stderr, "[file] %s\n", path);
    if (!path || path[0] != '/' || g_root.empty()) return path;
    // The resource locator's wildcard search builds "//usr/local/..." (base "/" + "/usr...");
    // the kernel treats repeated slashes as one, so must we.
    const char* q = path;
    while (q[1] == '/') q++;
    for (const char* const* p = kPrefixes; *p; p++) {
        size_t n = strlen(*p);
        if (strncmp(q, *p, n) == 0 && (q[n] == '/' || q[n] == 0)) {
            snprintf(buf, len, "%s%s", g_root.c_str(), q);
            return buf;
        }
    }
    return path;
}

#define REAL(name) static auto real_##name = reinterpret_cast<decltype(&name)>(dlsym(RTLD_NEXT, #name))
#define RPATH(p) char rbuf_##p[4096]; p = redirect(p, rbuf_##p, sizeof rbuf_##p)

extern "C" {

int open(const char* path, int flags, ...) {
    REAL(open);
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    RPATH(path);
    return real_open(path, flags, mode);
}
int open64(const char* path, int flags, ...) {
    REAL(open64);
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    RPATH(path);
    return real_open64(path, flags, mode);
}
FILE* fopen(const char* path, const char* mode) { REAL(fopen); RPATH(path); return real_fopen(path, mode); }
FILE* fopen64(const char* path, const char* mode) { REAL(fopen64); RPATH(path); return real_fopen64(path, mode); }
int access(const char* path, int m) { REAL(access); RPATH(path); return real_access(path, m); }
DIR* opendir(const char* path) { REAL(opendir); RPATH(path); return real_opendir(path); }
int mkdir(const char* path, mode_t m) { REAL(mkdir); RPATH(path); return real_mkdir(path, m); }
int rmdir(const char* path) { REAL(rmdir); RPATH(path); return real_rmdir(path); }
int unlink(const char* path) { REAL(unlink); RPATH(path); return real_unlink(path); }
int remove(const char* path) { REAL(remove); RPATH(path); return real_remove(path); }
int rename(const char* a, const char* b) { REAL(rename); RPATH(a); RPATH(b); return real_rename(a, b); }
int symlink(const char* a, const char* b) { REAL(symlink); RPATH(b); return real_symlink(a, b); }
char* realpath(const char* path, char* out) { REAL(realpath); RPATH(path); return real_realpath(path, out); }
int stat64(const char* path, struct stat64* st) { REAL(stat64); RPATH(path); return real_stat64(path, st); }
// Inside data/ the links are ours (sharing files with shared/), not the cabinet's: a redirected
// lstat follows them, so the game sees a folder where the cabinet had a folder.
int lstat64(const char* path, struct stat64* st) {
    REAL(lstat64); REAL(stat64);
    const char* orig = path;
    RPATH(path);
    return path != orig ? real_stat64(path, st) : real_lstat64(path, st);
}

// The 2008-era libraries call the old glibc stat/readdir entry points directly, with the
// original 32-bit struct layouts. Those fail with EOVERFLOW on filesystems with 64-bit
// inode numbers (e.g. WSL's /mnt/c, /mnt/e), so they are reimplemented on top of the
// 64-bit calls and converted.
struct OldStat {             // glibc i386 struct stat, _STAT_VER_LINUX (3)
    unsigned long long st_dev;
    unsigned short pad1;
    unsigned long st_ino;
    unsigned int st_mode, st_nlink, st_uid, st_gid;
    unsigned long long st_rdev;
    unsigned short pad2;
    long st_size, st_blksize, st_blocks;
    long st_atime_, st_atime_nsec, st_mtime_, st_mtime_nsec, st_ctime_, st_ctime_nsec;
    unsigned long unused4, unused5;
};
static_assert(sizeof(OldStat) == 88, "old struct stat layout");

static int to_old(int r, const struct stat64& n, void* out) {
    if (r != 0) return r;
    OldStat* o = static_cast<OldStat*>(out);
    memset(o, 0, sizeof *o);
    o->st_dev = n.st_dev;
    o->st_ino = (unsigned long)n.st_ino;
    o->st_mode = n.st_mode;
    o->st_nlink = n.st_nlink;
    o->st_uid = n.st_uid;
    o->st_gid = n.st_gid;
    o->st_rdev = n.st_rdev;
    o->st_size = n.st_size > 0x7fffffffLL ? 0x7fffffff : (long)n.st_size;
    o->st_blksize = n.st_blksize;
    o->st_blocks = (long)n.st_blocks;
    o->st_atime_ = n.st_atim.tv_sec; o->st_atime_nsec = n.st_atim.tv_nsec;
    o->st_mtime_ = n.st_mtim.tv_sec; o->st_mtime_nsec = n.st_mtim.tv_nsec;
    o->st_ctime_ = n.st_ctim.tv_sec; o->st_ctime_nsec = n.st_ctim.tv_nsec;
    return 0;
}

int __xstat(int, const char* path, void* st) {
    RPATH(path);
    struct stat64 n;
    return to_old(stat64(path, &n), n, st);
}
int __lxstat(int, const char* path, void* st) {
    REAL(lstat64); REAL(stat64);
    const char* orig = path;
    RPATH(path);
    struct stat64 n;
    return to_old(path != orig ? real_stat64(path, &n) : real_lstat64(path, &n), n, st);
}
int __fxstat(int, int fd, void* st) {
    struct stat64 n;
    return to_old(fstat64(fd, &n), n, st);
}
int __xstat64(int ver, const char* path, struct stat64* st) {
    static auto f = reinterpret_cast<int (*)(int, const char*, struct stat64*)>(dlvsym(RTLD_NEXT, "__xstat64", "GLIBC_2.2"));
    RPATH(path);
    return f(ver, path, st);
}

struct OldDirent { long d_ino; long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[256]; };
void* readdir_old(DIR* d) __asm__("readdir");
void* readdir_old(DIR* d) {
    static thread_local OldDirent out;
    struct dirent64* e = readdir64(d);
    if (!e) return nullptr;
    out.d_ino = (long)e->d_ino;
    out.d_off = (long)e->d_off;
    out.d_reclen = sizeof out;
    out.d_type = e->d_type;
    snprintf(out.d_name, sizeof out.d_name, "%s", e->d_name);
    return &out;
}

int glob(const char* pattern, int flags, int (*errfunc)(const char*, int), glob_t* g) {
    REAL(glob);
    char buf[4096];
    const char* p = redirect(pattern, buf, sizeof buf);
    int r = real_glob(p, flags, errfunc, g);
    if (menv("TRACE_FILES")) fprintf(stderr, "[glob] %s flags=%x -> %d (%zu)\n", p, flags, r, r == 0 ? g->gl_pathc : 0);
    if (r == 0 && p != pattern) {
        // hand back paths in the cabinet's namespace so callers can keep composing them
        for (size_t i = 0; i < g->gl_pathc; i++) {
            char*& s = g->gl_pathv[i + g->gl_offs];
            if (strncmp(s, g_root.c_str(), g_root.size()) == 0) memmove(s, s + g_root.size(), strlen(s + g_root.size()) + 1);
        }
    }
    return r;
}

// Cabinet IPC: games and engine libraries talk to the loader over unix sockets under
// /dev/merit_ipc/ (libipc_new). Nothing listens in a port, so every request fails; a game
// that treats that as a hardware/key failure may misbehave later. Each distinct endpoint
// is logged once (every attempt with MEGA_TRACE_IPC=1) so such games are easy to spot.
int connect(int fd, const struct sockaddr* addr, socklen_t len) {
    REAL(connect);
    if (addr && addr->sa_family == AF_UNIX && len > (socklen_t)offsetof(sockaddr_un, sun_path)) {
        const char* path = reinterpret_cast<const sockaddr_un*>(addr)->sun_path;
        if (*path && strncmp(path, "/dev/merit_ipc", 14) == 0) {
            int r = real_connect(fd, addr, len);
            static std::string seen;
            static bool all = menv("TRACE_IPC") != nullptr;
            std::string key = std::string("|") + path + "|";
            if (all || seen.find(key) == std::string::npos) {
                seen += key;
                fprintf(stderr, "[ipc] connect %s -> %s (no cabinet loader)\n", path, r == 0 ? "ok" : strerror(errno));
            }
            return r;
        }
    }
    return real_connect(fd, addr, len);
}

void trix_set_data_root(const char* root) { g_root = root; }

}  // extern "C"
