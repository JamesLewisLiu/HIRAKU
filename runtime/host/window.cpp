// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <thread>

#include "../firebeat/board.h"

namespace hk::host {

namespace {
SDL_Window *g_win = nullptr;
SDL_Renderer *g_ren = nullptr;
SDL_Texture *g_tex = nullptr;
SDL_AudioStream *g_audio = nullptr;
int g_tw = 0, g_th = 0;
std::chrono::steady_clock::time_point g_t0;
bool g_started = false;

// beatmania III keyboard layout (MAME's defaults where they exist)
void key(SDL_Keycode k, bool down) {
    auto &in = fb::inputs();
    auto bit_low = [&](uint16_t &port, uint16_t bit) { port = down ? (port & ~bit) : (port | bit); };
    auto bit8_low = [&](uint8_t &port, uint8_t bit) { port = down ? (port & ~bit) : (port | bit); };
    switch (k) {
    case SDLK_F2: bit8_low(in.in[0], 0x01); break;        // Test
    case SDLK_9: bit8_low(in.in[0], 0x02); break;         // Service
    case SDLK_5: bit_low(in.io[1], 0x0001); break;        // Coin
    case SDLK_1: bit_low(in.io[0], 0x0020); break;        // P1 Start
    case SDLK_Z: bit_low(in.io[0], 0x0001); break;        // P1 keys 1..5
    case SDLK_S: bit_low(in.io[0], 0x0002); break;
    case SDLK_X: bit_low(in.io[0], 0x0004); break;
    case SDLK_D: bit_low(in.io[0], 0x0008); break;
    case SDLK_C: bit_low(in.io[0], 0x0010); break;
    case SDLK_LSHIFT: in.in[0] = down ? (in.in[0] | 0x10) : (in.in[0] & ~0x10); break;   // P1 foot pedal
    default: break;
    }
}
}  // namespace

bool window_open(const char *title, int scale) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return false;
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (g_audio) SDL_ResumeAudioStreamDevice(g_audio);
    if (!SDL_CreateWindowAndRenderer(title, 640 * scale, 480 * scale, SDL_WINDOW_RESIZABLE, &g_win, &g_ren)) return false;
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

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) return false;
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            if (e.key.key == SDLK_ESCAPE) return false;
            if (!e.key.repeat) key(e.key.key, e.type == SDL_EVENT_KEY_DOWN);
        }
    }

    // pace virtual time to real time
    auto now = std::chrono::steady_clock::now();
    if (!g_started) { g_t0 = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(vsec)); g_started = true; }
    auto due = g_t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(vsec));
    if (due > now) std::this_thread::sleep_until(due);
    else if (now - due > std::chrono::milliseconds(250)) g_t0 += now - due;   // fell behind: don't try to catch up
    return true;
}

void audio_push(const float *lr, int frames) {
    if (!g_audio) return;
    if (SDL_GetAudioStreamQueued(g_audio) > (int)(44100 * 8 * 0.15)) return;
    SDL_PutAudioStreamData(g_audio, lr, frames * 8);
}

void window_close() {
    if (g_audio) SDL_DestroyAudioStream(g_audio);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

}  // namespace hk::host
