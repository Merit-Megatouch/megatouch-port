// Sound: a small mixer on one SDL audio device behind Sound::BaseSoundManager.
// WAV via SDL, OGG via stb_vorbis (third_party/).

#include "backend.h"
#include <cstring>
#include <map>
#include <mutex>
#include <algorithm>

// ---------------------------------------------------------------------------
// Sound. SoundInfo: +0 handle, +8 const char* file, +0xc bool loop,
// +0x10 float volume, +0x18 bool pause, +0x30 std::string* (preload path).

struct Pcm { std::vector<int16_t> s; };  // interleaved stereo 44.1k
struct Voice { Pcm* pcm; size_t pos; bool loop, paused; float vol; int id; };
static SDL_AudioDeviceID g_audio;
static std::mutex g_audioMx;
static std::map<std::string, Pcm*> g_pcmCache;
static std::vector<Voice> g_voices;
static int g_nextVoice = 1;

extern "C" int stb_vorbis_decode_filename(const char* filename, int* channels, int* sample_rate, short** output);

// SDL's own file opening rejects files on WSL drvfs mounts; read into memory instead.
static std::vector<std::vector<uint8_t>> g_fileBufs;
static SDL_RWops* rw_from_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    std::vector<uint8_t> b;
    uint8_t chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) b.insert(b.end(), chunk, chunk + n);
    fclose(f);
    g_fileBufs.push_back(std::move(b));
    return SDL_RWFromConstMem(g_fileBufs.back().data(), (int)g_fileBufs.back().size());
}

static Pcm* pcm_load(const std::string& path) {
    auto it = g_pcmCache.find(path);
    if (it != g_pcmCache.end()) return it->second;
    double t0 = now_ms();
    struct Done { double t0; ~Done() { g_fs.sndLoads++; g_fs.sndMs += now_ms() - t0; } } done{t0};
    Pcm* p = nullptr;
    int16_t* data = nullptr; int ch = 0, rate = 0, frames = 0;
    Uint8* wbuf = nullptr; Uint32 wlen = 0; SDL_AudioSpec ws;
    if (path.size() > 4 && strcasecmp(path.c_str() + path.size() - 4, ".ogg") == 0) {
        short* out = nullptr;
        frames = stb_vorbis_decode_filename(path.c_str(), &ch, &rate, &out);
        if (frames > 0) data = out;
    } else if (SDL_LoadWAV_RW(rw_from_file(path.c_str()), 1, &ws, &wbuf, &wlen)) {
        SDL_AudioCVT cvt;
        SDL_BuildAudioCVT(&cvt, ws.format, ws.channels, ws.freq, AUDIO_S16SYS, 2, 44100);
        cvt.len = wlen;
        cvt.buf = (Uint8*)SDL_malloc(wlen * (cvt.len_mult > 0 ? cvt.len_mult : 1));
        memcpy(cvt.buf, wbuf, wlen);
        SDL_FreeWAV(wbuf);
        if (cvt.needed) SDL_ConvertAudio(&cvt);
        else cvt.len_cvt = cvt.len;   // no conversion: SDL leaves len_cvt unset
        p = new Pcm;
        p->s.assign((int16_t*)cvt.buf, (int16_t*)(cvt.buf + cvt.len_cvt));
        SDL_free(cvt.buf);
    }
    if (data) {
        SDL_AudioCVT cvt;
        SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, ch, rate, AUDIO_S16SYS, 2, 44100);
        size_t len = (size_t)frames * ch * 2;
        cvt.len = len;
        cvt.buf = (Uint8*)SDL_malloc(len * (cvt.len_mult > 0 ? cvt.len_mult : 1));
        memcpy(cvt.buf, data, len);
        free(data);
        if (cvt.needed) SDL_ConvertAudio(&cvt);
        else cvt.len_cvt = cvt.len;   // no conversion: SDL leaves len_cvt unset
        p = new Pcm;
        p->s.assign((int16_t*)cvt.buf, (int16_t*)(cvt.buf + cvt.len_cvt));
        SDL_free(cvt.buf);
    }
    if (!p) LOG("could not load sound %s", path.c_str());
    g_pcmCache[path] = p;
    return p;
}

static void audio_cb(void*, Uint8* stream, int len) {
    int16_t* out = (int16_t*)stream;
    int n = len / 2;
    std::vector<int> mix(n, 0);
    std::lock_guard<std::mutex> lk(g_audioMx);
    for (auto& v : g_voices) {
        if (v.paused || !v.pcm) continue;
        const auto& s = v.pcm->s;
        for (int i = 0; i < n; i++) {
            if (v.pos >= s.size()) { if (v.loop && !s.empty()) v.pos = 0; else break; }
            mix[i] += (int)(s[v.pos++] * v.vol);
        }
    }
    for (int i = 0; i < n; i++) out[i] = (int16_t)std::max(-32768, std::min(32767, mix[i]));
    for (size_t i = 0; i < g_voices.size();)
        if (!g_voices[i].loop && g_voices[i].pcm && g_voices[i].pos >= g_voices[i].pcm->s.size()) g_voices.erase(g_voices.begin() + i);
        else i++;
}

static Voice* voice_find(int id) {
    for (auto& v : g_voices) if (v.id == id) return &v;
    return nullptr;
}

static void** g_sndVtbl;
static int snd_init(void*) {
    if (!g_audio) {
        SDL_AudioSpec want{}, have;
        want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
        want.callback = audio_cb;
        g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
        else LOG("audio unavailable: %s", SDL_GetError());
    }
    return 1;
}
static void snd_update(void* self, float dt) {
    reinterpret_cast<void (*)(void*, float)>(sym("_ZN5Sound16BaseSoundManager6UpdateEf"))(self, dt);
}
static void snd_destroy(void* self) {
    reinterpret_cast<void (*)(void*)>(sym("_ZN5Sound16BaseSoundManager7DestroyEv"))(self);
}
static int snd_preload(void*, void* info) {
    const char* f = at<const char*>(info, 8);   // +4 short name, +8 resolved path (std::string)
    if (f && *f) pcm_load(f);
    return 1;
}
static bool snd_debug() { static bool d = menv("DEBUG_SOUND"); return d; }
static int snd_play(void*, void* info) {
    const char* f = at<const char*>(info, 8);
    if (snd_debug()) LOG("t=%.0f play %s loop=%d vol=%g", now_ms(), f ? f : "(null)", at<bool>(info, 0xc), at<float>(info, 0x10));
    Pcm* p = f ? pcm_load(f) : nullptr;
    std::lock_guard<std::mutex> lk(g_audioMx);
    Voice v{p, 0, at<bool>(info, 0xc), false, at<float>(info, 0x10), g_nextVoice++};
    g_voices.push_back(v);
    at<int>(info, 0) = v.id;
    if (snd_debug()) LOG("  -> id=%d pcm=%p samples=%zu", v.id, (void*)p, p ? p->s.size() : 0);
    return 1;
}
// Returns 0 even on success. BaseSoundManager::StopSound erases the map node and then
// keeps iterating from the freed node (a use-after-free that happened to survive the
// cabinet's old malloc); reporting "not stopped" keeps it off that path. The voice is
// silenced here, and BaseSoundManager::Update drops the entry once IsPlaying is false.
static int snd_stop(void*, void* info) {
    std::lock_guard<std::mutex> lk(g_audioMx);
    int id = at<int>(info, 0);
    if (snd_debug()) LOG("stop id=%d", id);
    for (size_t i = 0; i < g_voices.size(); i++) if (g_voices[i].id == id) { g_voices.erase(g_voices.begin() + i); break; }
    return 0;
}
static int snd_volume(void*, void* info) {
    std::lock_guard<std::mutex> lk(g_audioMx);
    if (snd_debug()) LOG("volume id=%d vol=%g", at<int>(info, 0), at<float>(info, 0x10));
    if (Voice* v = voice_find(at<int>(info, 0))) v->vol = at<float>(info, 0x10);
    return 1;
}
static bool snd_playing(void*, void* info) {
    std::lock_guard<std::mutex> lk(g_audioMx);
    bool r = voice_find(at<int>(info, 0)) != nullptr;
    if (snd_debug()) LOG("playing? id=%d -> %d", at<int>(info, 0), r);
    return r;
}
static int snd_pause(void*, void* info) {
    std::lock_guard<std::mutex> lk(g_audioMx);
    if (snd_debug()) LOG("pause id=%d p=%d", at<int>(info, 0), at<bool>(info, 0x18));
    if (Voice* v = voice_find(at<int>(info, 0))) v->paused = at<bool>(info, 0x18);
    return 1;
}

// Pure virtual in the base as well.
static void snd_dtor(void* self) {
    snd_destroy(self);
    reinterpret_cast<dtor_t>(sym("_ZN5Sound16BaseSoundManagerD2Ev"))(self);
}
static void snd_dtor_delete(void* self) { snd_dtor(self); operator delete(self); }

void* snd_new() {
    if (!g_sndVtbl) {
        g_sndVtbl = clone_vtable("_ZTVN5Sound16BaseSoundManagerE");
        g_sndVtbl[0] = (void*)snd_dtor;
        g_sndVtbl[1] = (void*)snd_dtor_delete;
        g_sndVtbl[2] = (void*)snd_init;
        g_sndVtbl[3] = (void*)snd_update;
        g_sndVtbl[4] = (void*)snd_destroy;
        g_sndVtbl[17] = (void*)snd_preload;
        g_sndVtbl[18] = (void*)snd_play;
        g_sndVtbl[19] = (void*)snd_stop;
        g_sndVtbl[20] = (void*)snd_volume;
        g_sndVtbl[21] = (void*)snd_playing;
        g_sndVtbl[22] = (void*)snd_pause;
    }
    void* s = operator new(0x34);
    reinterpret_cast<ctor0_t>(sym("_ZN5Sound16BaseSoundManagerC2Ev"))(s);
    at<int>(s, 0x30) = 0;
    at<void**>(s, 0) = g_sndVtbl;
    return s;
}
