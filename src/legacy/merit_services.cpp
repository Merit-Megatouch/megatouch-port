// Loader services the newer legacy games (Merit2d / MeritBaseGame framework) use: platform info,
// the input and sound managers, and MegacGlobals' world stack and screen save/restore.
#include "sprite.h"
#include "../common/env.h"
#include <SDL2/SDL.h>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// ------------------------------------------------------------------------------ system_info
// We run as an ION cabinet: not a Force/GameTime/MusicBox model, no continuous play.
namespace system_info {
class SystemInfo {
public:
    static SystemInfo* GetInstance();
    bool IsPlatformForce();
    bool IsPlatformGameTime();
    bool IsPlatformION();
    bool IsMusicBox(bool);
    bool IsContinuousPlayActive();
    int GetPlatformType(bool);
};
SystemInfo* SystemInfo::GetInstance() { static char inst[64]; return reinterpret_cast<SystemInfo*>(inst); }
bool SystemInfo::IsPlatformForce() { return false; }
bool SystemInfo::IsPlatformGameTime() { return false; }
bool SystemInfo::IsPlatformION() { return true; }
bool SystemInfo::IsMusicBox(bool) { return false; }
bool SystemInfo::IsContinuousPlayActive() { return false; }
int SystemInfo::GetPlatformType(bool) { return 0; }
}

// ------------------------------------------------------------------------------ MeritInput
namespace MeritInput {
class InputManager {
public:
    static InputManager* GetInstance();
    static void DeleteInstance();
    void Update(bool);
    bool KeyboardGetKey(int) const;
    int MouseGetX() const;
    int MouseGetY() const;
    bool MouseGetButton(int) const;
    bool MouseGetButtonReleased(int) const;
    void MouseEnable(bool);
    void MouseSetButton(int, int);
    void* GetMouse();
    bool JoystickGetButton(int) const;
    int JoystickGetX() const;
    int JoystickGetY() const;
    void JoystickSetRange(int, int);
};
namespace { bool g_prevDown, g_released; }
InputManager* InputManager::GetInstance() { static char inst[64]; return reinterpret_cast<InputManager*>(inst); }
void InputManager::DeleteInstance() {}
void InputManager::Update(bool) {
    legacy::pump();
    bool down = legacy::mouse_down();
    g_released = g_prevDown && !down;
    g_prevDown = down;
}
// Allegro key codes (KEY_ESC = 59 in Allegro 4.0) and ASCII escape
bool InputManager::KeyboardGetKey(int k) const {
    if (k == 59 || k == 27) return legacy::key_down(SDL_SCANCODE_ESCAPE);
    return false;
}
int InputManager::MouseGetX() const { return legacy::mouse_x(); }
int InputManager::MouseGetY() const { return legacy::mouse_y(); }
bool InputManager::MouseGetButton(int b) const { return b == 0 && legacy::mouse_down(); }
bool InputManager::MouseGetButtonReleased(int b) const { return b == 0 && g_released; }
void InputManager::MouseEnable(bool) {}
void InputManager::MouseSetButton(int, int) {}
void* InputManager::GetMouse() { static char mouse[64]; return mouse; }
bool InputManager::JoystickGetButton(int) const { return false; }
int InputManager::JoystickGetX() const { return 0; }
int InputManager::JoystickGetY() const { return 0; }
void InputManager::JoystickSetRange(int, int) {}
}

// ------------------------------------------------------------------------------ MeritSound
namespace MeritSound {
enum ChannelId : int {};
class SoundManager {
public:
    static SoundManager* GetInstance();
    static void DelInstance();
    void Terminate();
    void Update(unsigned int);
    int LoadSound(std::string const&);
    int PlaySound(std::string const&, bool loop, int vol, unsigned int, unsigned int);
    int PlaySound(int id, bool loop, int vol, unsigned int, unsigned int);
    void StopSound(int, unsigned int);
    bool SoundIsPlaying(int) const;
    void SetSoundVolume(int, int);
    void RampSoundVolume(int, int, int);
};
class VolumeManager {
public:
    int GetVolume(ChannelId) const;
    void SetVolume(ChannelId, int);
};
namespace {
std::vector<std::string> g_sounds;               // LoadSound ids
int vol255(int v) { return v < 0 ? 255 : v > 100 ? 255 : v * 255 / 100; }   // volumes are percent
}
SoundManager* SoundManager::GetInstance() { static char inst[64]; return reinterpret_cast<SoundManager*>(inst); }
void SoundManager::DelInstance() {}
void SoundManager::Terminate() {}
void SoundManager::Update(unsigned int) {}
int SoundManager::LoadSound(std::string const& name) { g_sounds.push_back(name); return (int)g_sounds.size() - 1; }
int SoundManager::PlaySound(std::string const& name, bool loop, int vol, unsigned int, unsigned int) {
    return legacy::play_wave(name.c_str(), vol255(vol), loop);
}
int SoundManager::PlaySound(int id, bool loop, int vol, unsigned int a, unsigned int b) {
    if (id < 0 || id >= (int)g_sounds.size()) return -1;
    return PlaySound(g_sounds[id], loop, vol, a, b);
}
void SoundManager::StopSound(int voice, unsigned int) { legacy::stop_voice(voice); }
bool SoundManager::SoundIsPlaying(int voice) const { return legacy::voice_playing(voice); }
void SoundManager::SetSoundVolume(int voice, int vol) { legacy::set_voice_volume(voice, vol255(vol)); }
void SoundManager::RampSoundVolume(int voice, int vol, int) { legacy::set_voice_volume(voice, vol255(vol)); }
int VolumeManager::GetVolume(ChannelId) const { return 100; }
void VolumeManager::SetVolume(ChannelId, int) {}
}

// ------------------------------------------------------------------------------ MegacGlobals
// PushWorld/PopWorld: a fresh world for a sub-screen (Merit2d games), and back.
class MegacGlobals {
public:
    static MegacGlobals* GetInstance();
    void PushWorld(char const*, char const*, bool);
    void PopWorld();
    void SaveScreen();
    void RestoreScreen();
};
namespace {
std::vector<WorldClass*> g_worlds;
std::vector<std::vector<uint32_t>> g_screens;
WorldClass*& slot() { return *reinterpret_cast<WorldClass**>(reinterpret_cast<unsigned char*>(MegacGlobals::GetInstance()) + 0x207c); }
}
void MegacGlobals::PushWorld(char const*, char const*, bool) {
    g_worlds.push_back(slot());
    auto* w = new DOSLinuxWorld();
    w->Init(0, 1);
    slot() = w;
}
void MegacGlobals::PopWorld() {
    if (g_worlds.empty()) return;
    WorldClass* w = slot();
    slot() = g_worlds.back();
    g_worlds.pop_back();
    WorldClass* saved = slot();
    slot() = w;                                  // its sprites unregister from it while it dies
    delete w;
    slot() = saved;
}
void MegacGlobals::SaveScreen() {
    const uint32_t* p = reinterpret_cast<const uint32_t*>(screen->line[0]);
    g_screens.emplace_back(p, p + SW * SH);
}
void MegacGlobals::RestoreScreen() {
    if (g_screens.empty()) return;
    memcpy(screen->line[0], g_screens.back().data(), (size_t)SW * SH * 4);
    g_screens.pop_back();
    legacy::touched_target();
}
