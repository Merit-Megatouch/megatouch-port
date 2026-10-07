// AllegroGL's extension entry points: global function pointers (__aglBindBuffer...) that
// allegro_gl filled after creating the context. Merit3D games (luxor2) call through them.
// Generated from the GL games' imports; filled by legacy::agl_load() once the context exists.
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <cstdlib>
#include <string>

extern void* (*__aglXGetProcAddressARB)(const unsigned char*);   // allegro.cpp

extern "C" {
void* __aglBeginQuery;
void* __aglBindBuffer;
void* __aglBlendFuncSeparate;
void* __aglBufferData;
void* __aglBufferSubData;
void* __aglDeleteBuffers;
void* __aglDeleteQueries;
void* __aglEndQuery;
void* __aglFogCoordPointer;
void* __aglFogCoordd;
void* __aglFogCoorddv;
void* __aglFogCoordf;
void* __aglFogCoordfv;
void* __aglGenBuffers;
void* __aglGenQueries;
void* __aglGetBufferParameteriv;
void* __aglGetBufferPointerv;
void* __aglGetBufferSubData;
void* __aglGetQueryObjectiv;
void* __aglGetQueryObjectuiv;
void* __aglGetQueryiv;
void* __aglIsBuffer;
void* __aglIsQuery;
void* __aglMapBuffer;
void* __aglMultiDrawArrays;
void* __aglMultiDrawElements;
void* __aglPointParameterf;
void* __aglPointParameterfv;
void* __aglSecondaryColor3b;
void* __aglSecondaryColor3bv;
void* __aglSecondaryColor3d;
void* __aglSecondaryColor3dv;
void* __aglSecondaryColor3f;
void* __aglSecondaryColor3fv;
void* __aglSecondaryColor3i;
void* __aglSecondaryColor3iv;
void* __aglSecondaryColor3s;
void* __aglSecondaryColor3sv;
void* __aglSecondaryColor3ub;
void* __aglSecondaryColor3ubv;
void* __aglSecondaryColor3ui;
void* __aglSecondaryColor3uiv;
void* __aglSecondaryColor3us;
void* __aglSecondaryColor3usv;
void* __aglSecondaryColorPointer;
void* __aglUnmapBuffer;
void* __aglWindowPos2d;
void* __aglWindowPos2dv;
void* __aglWindowPos2f;
void* __aglWindowPos2fv;
void* __aglWindowPos2i;
void* __aglWindowPos2iv;
void* __aglWindowPos2s;
void* __aglWindowPos2sv;
void* __aglWindowPos3d;
void* __aglWindowPos3dv;
void* __aglWindowPos3f;
void* __aglWindowPos3fv;
void* __aglWindowPos3i;
void* __aglWindowPos3iv;
void* __aglWindowPos3s;
void* __aglWindowPos3sv;
}

namespace {
struct Entry { void** slot; const char* name; };
const Entry kEntries[] = {
    {&__aglBeginQuery, "BeginQuery"},
    {&__aglBindBuffer, "BindBuffer"},
    {&__aglBlendFuncSeparate, "BlendFuncSeparate"},
    {&__aglBufferData, "BufferData"},
    {&__aglBufferSubData, "BufferSubData"},
    {&__aglDeleteBuffers, "DeleteBuffers"},
    {&__aglDeleteQueries, "DeleteQueries"},
    {&__aglEndQuery, "EndQuery"},
    {&__aglFogCoordPointer, "FogCoordPointer"},
    {&__aglFogCoordd, "FogCoordd"},
    {&__aglFogCoorddv, "FogCoorddv"},
    {&__aglFogCoordf, "FogCoordf"},
    {&__aglFogCoordfv, "FogCoordfv"},
    {&__aglGenBuffers, "GenBuffers"},
    {&__aglGenQueries, "GenQueries"},
    {&__aglGetBufferParameteriv, "GetBufferParameteriv"},
    {&__aglGetBufferPointerv, "GetBufferPointerv"},
    {&__aglGetBufferSubData, "GetBufferSubData"},
    {&__aglGetQueryObjectiv, "GetQueryObjectiv"},
    {&__aglGetQueryObjectuiv, "GetQueryObjectuiv"},
    {&__aglGetQueryiv, "GetQueryiv"},
    {&__aglIsBuffer, "IsBuffer"},
    {&__aglIsQuery, "IsQuery"},
    {&__aglMapBuffer, "MapBuffer"},
    {&__aglMultiDrawArrays, "MultiDrawArrays"},
    {&__aglMultiDrawElements, "MultiDrawElements"},
    {&__aglPointParameterf, "PointParameterf"},
    {&__aglPointParameterfv, "PointParameterfv"},
    {&__aglSecondaryColor3b, "SecondaryColor3b"},
    {&__aglSecondaryColor3bv, "SecondaryColor3bv"},
    {&__aglSecondaryColor3d, "SecondaryColor3d"},
    {&__aglSecondaryColor3dv, "SecondaryColor3dv"},
    {&__aglSecondaryColor3f, "SecondaryColor3f"},
    {&__aglSecondaryColor3fv, "SecondaryColor3fv"},
    {&__aglSecondaryColor3i, "SecondaryColor3i"},
    {&__aglSecondaryColor3iv, "SecondaryColor3iv"},
    {&__aglSecondaryColor3s, "SecondaryColor3s"},
    {&__aglSecondaryColor3sv, "SecondaryColor3sv"},
    {&__aglSecondaryColor3ub, "SecondaryColor3ub"},
    {&__aglSecondaryColor3ubv, "SecondaryColor3ubv"},
    {&__aglSecondaryColor3ui, "SecondaryColor3ui"},
    {&__aglSecondaryColor3uiv, "SecondaryColor3uiv"},
    {&__aglSecondaryColor3us, "SecondaryColor3us"},
    {&__aglSecondaryColor3usv, "SecondaryColor3usv"},
    {&__aglSecondaryColorPointer, "SecondaryColorPointer"},
    {&__aglUnmapBuffer, "UnmapBuffer"},
    {&__aglWindowPos2d, "WindowPos2d"},
    {&__aglWindowPos2dv, "WindowPos2dv"},
    {&__aglWindowPos2f, "WindowPos2f"},
    {&__aglWindowPos2fv, "WindowPos2fv"},
    {&__aglWindowPos2i, "WindowPos2i"},
    {&__aglWindowPos2iv, "WindowPos2iv"},
    {&__aglWindowPos2s, "WindowPos2s"},
    {&__aglWindowPos2sv, "WindowPos2sv"},
    {&__aglWindowPos3d, "WindowPos3d"},
    {&__aglWindowPos3dv, "WindowPos3dv"},
    {&__aglWindowPos3f, "WindowPos3f"},
    {&__aglWindowPos3fv, "WindowPos3fv"},
    {&__aglWindowPos3i, "WindowPos3i"},
    {&__aglWindowPos3iv, "WindowPos3iv"},
    {&__aglWindowPos3s, "WindowPos3s"},
    {&__aglWindowPos3sv, "WindowPos3sv"},
};
}

namespace legacy {
// glvnd's glXGetProcAddressARB returns dispatch entries without a current context, so this also
// works for games that open their own GLX window (luxor2) and runs when the library loads.
void agl_load() {
    using GetProc = void* (*)(const unsigned char*);
    static GetProc gp = reinterpret_cast<GetProc>(dlsym(RTLD_DEFAULT, "glXGetProcAddressARB"));
    if (gp && !__aglXGetProcAddressARB) __aglXGetProcAddressARB = gp;
    auto get = [&](const std::string& n) -> void* {
        void* p = SDL_GL_GetCurrentContext() ? SDL_GL_GetProcAddress(n.c_str()) : nullptr;
        if (!p && gp) p = gp(reinterpret_cast<const unsigned char*>(n.c_str()));
        return p;
    };
    int n = 0;
    for (const Entry& e : kEntries) {
        std::string base = std::string("gl") + e.name;
        void* p = get(base);
        if (!p) p = get(base + "ARB");
        if (!p) p = get(base + "EXT");
        *e.slot = p;
        n += p != nullptr;
    }
    if (getenv("MEGA_DEBUG_GL")) SDL_Log("[legacy] AllegroGL extensions: %d of %d resolved", n, (int)(sizeof kEntries / sizeof kEntries[0]));
}
}
__attribute__((constructor)) static void agl_at_load() {
    if (dlsym(RTLD_DEFAULT, "glXGetProcAddressARB")) legacy::agl_load();   // libGL preloaded (GL games)
}
