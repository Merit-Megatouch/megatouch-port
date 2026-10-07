/*
 * soundfix — keeps the cabinet's sound manager out of a use-after-free (known bug 15).
 *
 * Sound::BaseSoundManager::StopSound (libmerit_sound.so) walks its std::map of playing sounds,
 * and when the backend's ImplementationStopSound reports success it erases the entry and goes on
 * iterating from the freed node. The 2008 allocator left freed memory intact, so it worked on the
 * cabinet; on the modern runtime the freed node is reused and the walk never ends: Trix froze after
 * the first card with its last sound stuttering.
 *
 * Each backend's ImplementationStopSound is wrapped here: the original still stops the voice, but
 * the wrapper reports "not stopped", so StopSound leaves the entry alone; BaseSoundManager::Update
 * drops it once IsPlaying says the voice is gone. (Same fix as the SDL2 backend, src/backend/sound.cpp.)
 * The backends' vtables reference these functions by symbol, so an LD_PRELOAD definition wins.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>

typedef int (*stop_fn)(void *self, void *info);

static stop_fn original(const char *lib, const char *sym) {
    void *h = dlopen(lib, RTLD_NOW | RTLD_NOLOAD);   /* the backend is already loaded */
    stop_fn f = h ? (stop_fn)dlsym(h, sym) : NULL;
    if (!f) fprintf(stderr, "[soundfix] %s: %s not found\n", lib, sym);
    return f;
}

#define WRAP(lib, sym)                                         \
    int sym(void *self, void *info) {                          \
        static stop_fn f;                                      \
        if (!f) f = original(lib, #sym);                       \
        if (f) f(self, info);                                  \
        return 0;                                              \
    }

WRAP("libsound_sprite.so", _ZN5Sound6Sprite19CSpriteSoundManager23ImplementationStopSoundERNS_16BaseSoundManager9SoundInfoE)
WRAP("libsound_sdl.so", _ZN5Sound3SDL16CSDLSoundManager23ImplementationStopSoundERNS_16BaseSoundManager9SoundInfoE)
WRAP("libsound_irrklang.so", _ZN5Sound8IrrKlang15CIKSoundManager23ImplementationStopSoundERNS_16BaseSoundManager9SoundInfoE)
WRAP("libmerit_sound.so", _ZN5Sound17CNULLSoundManager23ImplementationStopSoundERNS_16BaseSoundManager9SoundInfoE)
