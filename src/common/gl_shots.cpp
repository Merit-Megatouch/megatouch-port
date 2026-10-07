// glXSwapBuffers hook shared by the Unity and loader preload libraries.
#include "env.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
// ---------------------------------------------------------------------------------------------
// Screenshots for headless checks: MEGA_SHOT_DIR=<abs dir> [MEGA_SHOT_EVERY=<swaps>, default 120]
// saves frameNNNNN.bmp from the back buffer before each Nth glXSwapBuffers.
#include <cstring>
#include <vector>
#include <string>
typedef void (*SwapFn)(void*, unsigned long);
typedef void (*ReadPixelsFn)(int, int, int, int, unsigned, unsigned, void*);
typedef void (*GetIntegervFn)(unsigned, int*);

static void save_bmp(const std::string& path, int w, int h, const unsigned char* rgba) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    int row = w * 3, pad = (4 - row % 4) % 4, size = 54 + (row + pad) * h;
    unsigned char hdr[54] = {'B', 'M'};
    auto le32 = [&](int o, int v) { for (int i = 0; i < 4; i++) hdr[o + i] = (v >> (8 * i)) & 255; };
    le32(2, size); le32(10, 54); le32(14, 40); le32(18, w); le32(22, h);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> line(row + pad, 0);
    for (int y = 0; y < h; y++) {                  // GL rows are bottom-up, like BMP
        const unsigned char* p = rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) { line[x * 3] = p[x * 4 + 2]; line[x * 3 + 1] = p[x * 4 + 1]; line[x * 3 + 2] = p[x * 4]; }
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
}

extern "C" void glXSwapBuffers(void* dpy, unsigned long drawable) {
    static auto real = reinterpret_cast<SwapFn>(dlsym(RTLD_NEXT, "glXSwapBuffers"));
    static const char* dir = menv("SHOT_DIR");
    static int every = menv("SHOT_EVERY") ? atoi(menv("SHOT_EVERY")) : 120, n;
    if (dir && ++n % every == 0) {
        static auto rp = reinterpret_cast<ReadPixelsFn>(dlsym(RTLD_DEFAULT, "glReadPixels"));
        static auto gi = reinterpret_cast<GetIntegervFn>(dlsym(RTLD_DEFAULT, "glGetIntegerv"));
        int vp[4] = {0, 0, 0, 0};
        if (gi) gi(0x0BA2 /* GL_VIEWPORT */, vp);
        if (rp && vp[2] > 0 && vp[3] > 0) {
            std::vector<unsigned char> px((size_t)vp[2] * vp[3] * 4);
            rp(vp[0], vp[1], vp[2], vp[3], 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px.data());
            char name[64];
            snprintf(name, sizeof name, "/frame%05d.bmp", n);
            save_bmp(std::string(dir) + name, vp[2], vp[3], px.data());
        }
    }
    real(dpy, drawable);
}
