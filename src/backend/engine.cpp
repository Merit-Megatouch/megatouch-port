// Engine plumbing: symbol lookup, vtable cloning, and state shared by the backend files.

#include "backend.h"
#include <dlfcn.h>
#include <link.h>
#include <cstdlib>
#include <cstring>

void* sym(const char* name) {
    void* p = dlsym(RTLD_DEFAULT, name);
    if (!p) { LOG("missing engine symbol %s", name); abort(); }
    return p;
}

// Copy a base-class vtable (including the offset-to-top and typeinfo header words) and
// leave room for `extraSlots` additional virtuals. Returns the address the vptr must hold.
void** clone_vtable(const char* vtSym, int extraSlots) {
    Dl_info info; ElfW(Sym)* es = nullptr;
    void* vt = sym(vtSym);
    if (!dladdr1(vt, &info, (void**)&es, RTLD_DL_SYMENT) || !es) { LOG("no size for %s", vtSym); abort(); }
    size_t words = es->st_size / sizeof(void*);
    void** copy = static_cast<void**>(calloc(words + extraSlots, sizeof(void*)));
    memcpy(copy, vt, es->st_size);
    return copy + 2;
}

FrameStats g_fs;
double now_ms() { return SDL_GetPerformanceCounter() * 1000.0 / SDL_GetPerformanceFrequency(); }

SDL_Window* g_window;
SDL_Renderer* g_renderer;
int g_logicalW = 1280, g_logicalH = 800;
bool g_quit;
