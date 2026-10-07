// Shared between the legacy 2D engine (legacy.cpp) and the Merit3D layer (merit3d.cpp).
#pragma once
#include <SDL2/SDL.h>
#include <string>

namespace legacy {
void video_init();                 // window (+ GL context when MEGA_GL=1), audio
void pump();                       // events, touches, present / nothing in GL mode
bool gl_mode();
SDL_Window* window();
int screen_w();
int screen_h();
// mouse in game coordinates
int mouse_x();
int mouse_y();
bool mouse_down();
// mixer: plays 44.1 kHz stereo S16 samples; returns a voice id (-1 on failure)
int play_pcm(const std::string& key, const short* samples, size_t count, int vol255, bool loop);
void stop_voice(int voice);
void set_voice_volume(int voice, int vol255);
bool voice_playing(int voice);
}
