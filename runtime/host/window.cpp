// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <thread>

#include "../firebeat/board.h"
#include "input.h"

namespace hk::host {

namespace {
SDL_Window *g_win = nullptr;
SDL_Renderer *g_ren = nullptr;
SDL_Texture *g_tex = nullptr;
SDL_AudioStream *g_audio = nullptr;
int g_tw = 0, g_th = 0;
std::chrono::steady_clock::time_point g_t0;
bool g_started = false;

int g_target_frames = 0;          // audio queued ahead, when audio paces
bool g_fast = false;              // Tab held: run as fast as possible (audio is dropped)
int g_device_frames = 0;
bool g_quit = false;
auto g_last_poll = std::chrono::steady_clock::now();

// Poll controls: called every few milliseconds of emulated time.
void poll() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) g_quit = true;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
            (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
            bool fs = SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN;
            SDL_SetWindowFullscreen(g_win, !fs);
        }
        if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && e.key.key == SDLK_TAB) {
            g_fast = e.type == SDL_EVENT_KEY_DOWN;
            if (g_fast && g_audio) SDL_ClearAudioStream(g_audio);
        }
        if (e.type == SDL_EVENT_JOYSTICK_ADDED) input_joystick_added(e.jdevice.which);
        input_event(e);
    }
    auto now = std::chrono::steady_clock::now();
    input_update(std::chrono::duration<double, std::milli>(now - g_last_poll).count());
    g_last_poll = now;
}
}  // namespace

bool window_open(const char *title, int scale, const std::string &bindings, int target_ms) {
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "128");   // ask for a small device buffer
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return false;
    input_init(bindings);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (g_audio) {
        SDL_AudioSpec dev{};
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_audio), &dev, &g_device_frames);
        g_target_frames = 44100 * target_ms / 1000;
        SDL_ResumeAudioStreamDevice(g_audio);
    }
    if (!SDL_CreateWindowAndRenderer(title, 640 * scale, 480 * scale, SDL_WINDOW_RESIZABLE, &g_win, &g_ren)) return false;
    SDL_SetRenderVSync(g_ren, 0);   // audio paces the machine; presenting must never block on the display
    SDL_SetRenderLogicalPresentation(g_ren, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return true;
}

bool window_frame(const uint32_t *rgb, int w, int h, double vsec) {
    if (!g_tex || w != g_tw || h != g_th) {
        if (g_tex) SDL_DestroyTexture(g_tex);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(g_tex, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderLogicalPresentation(g_ren, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        g_tw = w;
        g_th = h;
    }
    SDL_UpdateTexture(g_tex, nullptr, rgb, w * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderTexture(g_ren, g_tex, nullptr, nullptr);
    SDL_RenderPresent(g_ren);

    poll();
    if (g_quit) return false;
    if (g_fast) return true;    // fast-forward: no pacing at all
    if (g_audio) return true;   // audio paces time (audio_push)

    // no audio device: pace virtual time to real time on video frames
    auto now = std::chrono::steady_clock::now();
    if (!g_started) { g_t0 = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(vsec)); g_started = true; }
    auto due = g_t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(vsec));
    if (due > now) std::this_thread::sleep_until(due);
    else if (now - due > std::chrono::milliseconds(250)) g_t0 += now - due;   // fell behind: don't try to catch up
    return true;
}

void audio_push(const float *lr, int frames) {
    if (!g_audio) return;
    if (g_fast) { poll(); return; }
    SDL_PutAudioStreamData(g_audio, lr, frames * 8);
    // audio is the clock: wait while more than the target is queued, polling controls
    while (SDL_GetAudioStreamQueued(g_audio) > g_target_frames * 8 && !g_quit) {
        poll();
        SDL_DelayPrecise(250'000);   // 0.25 ms
    }
    poll();
}

std::string audio_describe() {
    if (!g_audio) return "no audio device";
    char b[160];
    snprintf(b, sizeof b, "audio: device buffer %d frames (%.1f ms), queue target %d frames (%.1f ms)", g_device_frames,
             g_device_frames * 1000.0 / 44100, g_target_frames, g_target_frames * 1000.0 / 44100);
    return b;
}

void window_close() {
    if (g_audio) SDL_DestroyAudioStream(g_audio);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

}  // namespace hk::host
