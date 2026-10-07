// libmega_unity.so — preloaded into the cabinet's Unity 3.2 LinuxPlayer.
//
// The cabinet player always asks FMOD Ex for its OSS output (setOutput(10); launcher.sh also
// passes -FMOD_OUTPUTTYPE_OSS), which nothing provides on a modern desktop. This replaces the
// requested output with PulseAudio (13 in this FMOD Ex build), which WSLg and desktops provide.
// MEGA_FMOD_OUTPUT=<n> picks another output (0 autodetect, 10 OSS, 11 ALSA, 12 ESD, 13 PulseAudio).
#include "../common/env.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>

namespace FMOD { class System; }

enum { kOutputPulseAudio = 13 };

// The same filesystem shim megatouch-host uses (src/host/fs_shim.cpp, linked in): cabinet paths
// (/var/merit, /usr/local/ion_only, ...) map into the game's data/ folder (MEGA_DATA).
extern "C" void trix_set_data_root(const char* root);
__attribute__((constructor)) static void mega_unity_init() {
    if (const char* d = menv("DATA")) trix_set_data_root(d);
}

extern "C" int _ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE(FMOD::System* sys, int type) {
    using SetOutput = int (*)(FMOD::System*, int);
    static auto real = reinterpret_cast<SetOutput>(dlsym(RTLD_NEXT, "_ZN4FMOD6System9setOutputE15FMOD_OUTPUTTYPE"));
    int want = menv("FMOD_OUTPUT") ? atoi(menv("FMOD_OUTPUT")) : kOutputPulseAudio;
    int r = real(sys, want);
    fprintf(stderr, "[mega] FMOD output %d requested, using %d (result %d)\n", type, want, r);
    return r;
}
