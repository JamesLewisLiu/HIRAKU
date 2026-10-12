// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <vector>
#include <cstdlib>
#include <thread>

#include "../shell/core.h"
#include "input.h"
#include "../core/prof.h"

namespace hk::host {

namespace {
uint64_t g_settle_until = 0;   // window/renderer setup right after opening: not a stall
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

bool window_open(const char *title, int scale, const std::string &bindings, int target_ms, bool fullscreen) {
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "128");   // ask for a small device buffer
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return false;
    input_init(bindings);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (g_audio) {
        SDL_AudioSpec dev{};
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_audio), &dev, &g_device_frames);
        g_target_frames = 44100 * target_ms / 1000;
        // start with the target lead of silence: the first frames' window setup
        // would otherwise find an empty queue (and grow the target for nothing)
        std::vector<float> silence((size_t)g_target_frames * 2, 0.0f);
        SDL_PutAudioStreamData(g_audio, silence.data(), (int)(silence.size() * sizeof(float)));
        SDL_ResumeAudioStreamDevice(g_audio);
        g_settle_until = prof::now_ns() + 250'000'000;
    }
    if (!SDL_CreateWindowAndRenderer(title, 640 * scale, 480 * scale, SDL_WINDOW_RESIZABLE, &g_win, &g_ren)) return false;
    SDL_SetRenderVSync(g_ren, 0);   // audio paces the machine; presenting must never block on the display
    SDL_SetRenderLogicalPresentation(g_ren, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (fullscreen) SDL_SetWindowFullscreen(g_win, true);
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
    {
        prof::Scope ps(prof::PRESENT);
        SDL_UpdateTexture(g_tex, nullptr, rgb, w * 4);
        SDL_RenderClear(g_ren);
        SDL_RenderTexture(g_ren, g_tex, nullptr, nullptr);
        SDL_RenderPresent(g_ren);
    }

    poll();
    if (g_quit) return false;
    if (g_fast) return true;    // fast-forward: no pacing at all
    if (g_audio) return true;   // audio paces time (audio_push)

    // no audio device: pace virtual time to real time on video frames
    auto now = std::chrono::steady_clock::now();
    if (!g_started) { g_t0 = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(vsec)); g_started = true; }
    auto due = g_t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(vsec));
    if (due > now) {
        const uint64_t w0 = prof::ticks();
        std::this_thread::sleep_until(due);
        prof::g_wait_ticks += prof::ticks() - w0;
    }
    else if (now - due > std::chrono::milliseconds(250)) g_t0 += now - due;   // fell behind: don't try to catch up
    return true;
}

// Underrun accounting: the queue level when each chunk arrives, and what the
// host did since the previous chunk when it ran low.
struct AudioStats {
    uint64_t chunks = 0, low = 0, empty = 0;
    double worst_gap_ms = 0;
    uint64_t gap_hist[8] = {};   // work time per chunk: <1,<2,<4,<8,<16,<32,<64,>=64 ms
    uint64_t low_ns[prof::NCAT] = {};
} g_ast;
uint64_t g_last_push_end = 0;
bool g_log_stalls = getenv("HK_LOG_STALLS") != nullptr;

// One chunk to the device, with pacing, underrun accounting and input polling.
void push_chunk(const float *lr, int frames) {
    if (g_fast) {               // fast-forward: no pacing, and the time isn't a stall
        poll();
        g_last_push_end = 0;
        return;
    }
    uint64_t t = prof::now_ns();
    if (g_last_push_end && t >= g_settle_until) {
        double gap = (t - g_last_push_end) / 1e6;
        int b = gap < 1 ? 0 : gap < 2 ? 1 : gap < 4 ? 2 : gap < 8 ? 3 : gap < 16 ? 4 : gap < 32 ? 5 : gap < 64 ? 6 : 7;
        g_ast.gap_hist[b]++;
        g_ast.worst_gap_ms = std::max(g_ast.worst_gap_ms, gap);
        int queued = SDL_GetAudioStreamQueued(g_audio) / 8;
        g_ast.chunks++;
        if (queued == 0) {
            g_ast.empty++;
            // self-tuning: an underrun means the machine needs more lead on this host
            if (g_target_frames < 44100 * 30 / 1000) g_target_frames += 44;   // +1 ms, up to 30 ms
        }
        if (queued < 44) {   // under 1 ms left: about to crackle
            g_ast.low++;
            for (int c = 0; c < prof::NCAT; c++) g_ast.low_ns[c] += (uint64_t)prof::ns((prof::Cat)c);
            if (g_log_stalls) {
                fprintf(stderr, "stall: frame %llu, %.2f ms of work, queue %d frames:", (unsigned long long)core().frame_count(), gap, queued);
                for (int c = 0; c < prof::NCAT; c++) fprintf(stderr, " %s %.2f", prof::NAMES[c], prof::ns((prof::Cat)c) / 1e6);
                fprintf(stderr, "\n");
            }
        }
    }
    SDL_PutAudioStreamData(g_audio, lr, frames * 8);
    // audio is the clock: wait while more than the target is queued, polling controls
    const uint64_t w0 = prof::ticks();
    while (SDL_GetAudioStreamQueued(g_audio) > g_target_frames * 8 && !g_quit) {
        poll();
        SDL_DelayPrecise(250'000);   // 0.25 ms
    }
    prof::g_wait_ticks += prof::ticks() - w0;
    poll();
    prof::reset();
    g_last_push_end = prof::now_ns();
}

// The emulated sound hardware may deliver a few samples at a time (the SPU catching
// up on every DMA word while a song loads); batch them so pacing and input polling
// run once per >= 64 frames (1.5 ms), not per sample.
std::vector<float> g_pending;

void audio_push(const float *lr, int frames) {
    if (!g_audio) return;
    if (g_pending.empty() && frames >= 64) { push_chunk(lr, frames); return; }
    g_pending.insert(g_pending.end(), lr, lr + 2 * frames);
    if (g_pending.size() >= 2 * 64) {
        push_chunk(g_pending.data(), (int)(g_pending.size() / 2));
        g_pending.clear();
    }
}

std::string audio_describe() {
    if (!g_audio) return "no audio device";
    char b[1024];
    int n = snprintf(b, sizeof b,
                     "audio: device buffer %d frames (%.1f ms), queue target %d frames (%.1f ms, after self-tuning)\n"
                     "audio: %llu chunks, %llu ran low (<1 ms queued), %llu found the queue empty; worst gap %.1f ms\n"
                     "audio: host work per chunk  <1:%llu <2:%llu <4:%llu <8:%llu <16:%llu <32:%llu <64:%llu >=64:%llu ms\n"
                     "audio: during low chunks:",
                     g_device_frames, g_device_frames * 1000.0 / 44100, g_target_frames, g_target_frames * 1000.0 / 44100,
                     (unsigned long long)g_ast.chunks, (unsigned long long)g_ast.low, (unsigned long long)g_ast.empty,
                     g_ast.worst_gap_ms, (unsigned long long)g_ast.gap_hist[0], (unsigned long long)g_ast.gap_hist[1],
                     (unsigned long long)g_ast.gap_hist[2], (unsigned long long)g_ast.gap_hist[3],
                     (unsigned long long)g_ast.gap_hist[4], (unsigned long long)g_ast.gap_hist[5],
                     (unsigned long long)g_ast.gap_hist[6], (unsigned long long)g_ast.gap_hist[7]);
    for (int c = 0; c < prof::NCAT; c++)
        n += snprintf(b + n, sizeof b - n, " %s %.0f ms", prof::NAMES[c], g_ast.low_ns[c] / 1e6);
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
