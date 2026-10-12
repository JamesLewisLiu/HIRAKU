// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include <cstdlib>
#include <thread>

#include "../shell/core.h"
#include "macro.h"
#include "pacing.h"
#include "picture.h"
#include "input.h"
#include "../core/prof.h"

namespace hk::host {

namespace {
uint64_t g_settle_until = 0;   // window/renderer setup right after opening: not a stall
SDL_Window *g_win = nullptr;
SDL_Renderer *g_ren = nullptr;
SDL_Texture *g_tex = nullptr;
SDL_GLContext g_gl = nullptr;     // OpenGL (the picture modes, runtime/host/picture.h), else the SDL renderer
std::string g_shaders_dir, g_title, g_macro;   // g_macro: the code macro being typed, for the title
bool g_title_dirty = false;
std::vector<std::string> g_picture_config;
picture::Mode g_mode = picture::Mode::off, g_shown = picture::Mode::off;
SDL_AudioStream *g_audio = nullptr;
int g_tw = 0, g_th = 0;
std::chrono::steady_clock::time_point g_t0;
bool g_started = false;

int g_rate = 44100;               // the core's audio rate (Core::audio_rate)
int g_target_frames = 0;          // audio queued ahead, when audio paces
bool g_fast = false;              // Tab held: run as fast as possible (audio is dropped)
int g_device_frames = 0;
bool g_quit = false;
bool g_display_sync = true;       // display pacing (pacing.h)
double g_ratio = 1.0;             // the audio stream's frequency ratio: the game's speed, steered
double g_last_vsec = -1;
void (*g_glFlush)() = nullptr;
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
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && (e.key.mod & SDL_KMOD_CTRL) &&
            e.key.key >= SDLK_1 && e.key.key <= SDLK_9) {   // Ctrl+1..9: the game's code macros
            macro::trigger((size_t)(e.key.key - SDLK_1));
            continue;   // not a game input
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F10 && g_gl)   // F10: off, light, full
            g_mode = g_mode == picture::Mode::off ? picture::Mode::light
                   : g_mode == picture::Mode::light ? picture::Mode::full : picture::Mode::off;
        if (e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED || e.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED)
            pacing::display_changed();
        if (e.type == SDL_EVENT_JOYSTICK_ADDED) input_joystick_added(e.jdevice.which);
        input_event(e);
    }
    auto now = std::chrono::steady_clock::now();
    input_update(std::chrono::duration<double, std::milli>(now - g_last_poll).count());
    g_last_poll = now;
}

// An OpenGL 3.3 window; false (and nothing left open) if it can't run here.
bool open_gl(const char *title, int scale) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    g_win = SDL_CreateWindow(title, 640 * scale, 480 * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (g_win) g_gl = SDL_GL_CreateContext(g_win);
    if (g_gl) SDL_GL_SetSwapInterval(0);   // as with the renderer: presenting never waits for the display
    if (g_gl && picture::init(g_shaders_dir, g_picture_config)) return true;
    fprintf(stderr, "picture: no OpenGL 3.3 (%s): the SDL renderer, sharp pixels only\n", g_gl ? "see above" : SDL_GetError());
    picture::shutdown();
    if (g_gl) SDL_GL_DestroyContext(g_gl);
    if (g_win) SDL_DestroyWindow(g_win);
    g_gl = nullptr;
    g_win = nullptr;
    return false;
}
}  // namespace

void window_set_picture(const std::string &mode, const std::string &shaders_dir, const std::vector<std::string> &config_dirs) {
    if (!picture::parse_mode(mode, g_mode)) fprintf(stderr, "picture: %s is not off, light or full\n", mode.c_str());
    g_shaders_dir = shaders_dir;
    g_picture_config = config_dirs;
}

void window_set_display_sync(bool on) { g_display_sync = on; }

bool window_open(const char *title, int scale, const std::string &bindings, int target_ms, bool fullscreen, int rate) {
    g_rate = rate;
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "128");   // ask for a small device buffer
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return false;
    input_init(bindings);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, g_rate};
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (g_audio) {
        SDL_AudioSpec dev{};
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_audio), &dev, &g_device_frames);
        g_target_frames = g_rate * target_ms / 1000;
        // start with the target lead of silence: the first frames' window setup
        // would otherwise find an empty queue (and grow the target for nothing)
        std::vector<float> silence((size_t)g_target_frames * 2, 0.0f);
        SDL_PutAudioStreamData(g_audio, silence.data(), (int)(silence.size() * sizeof(float)));
        SDL_ResumeAudioStreamDevice(g_audio);
        g_settle_until = prof::now_ns() + 250'000'000;
    }
    g_title = title;
    macro::on_change([](const std::string &name) {
        g_macro = name;
        g_title_dirty = true;
    });
    if (open_gl(title, scale)) {
        picture::prepare(g_mode);
        if (fullscreen) SDL_SetWindowFullscreen(g_win, true);
        g_glFlush = (void (*)())SDL_GL_GetProcAddress("glFlush");
        pacing::init(g_win, g_display_sync);
        return true;
    }
    if (!SDL_CreateWindowAndRenderer(title, 640 * scale, 480 * scale, SDL_WINDOW_RESIZABLE, &g_win, &g_ren)) return false;
    SDL_SetRenderVSync(g_ren, 0);   // presenting must never block on the display: we choose when (pacing.h)
    SDL_SetRenderLogicalPresentation(g_ren, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (fullscreen) SDL_SetWindowFullscreen(g_win, true);
    pacing::init(g_win, g_display_sync);
    return true;
}

// Present pacing: real time between presents, against the machine's own frame period. With the
// display not waiting (vsync off), this is what a variable-refresh display shows; a fixed one
// shows each frame for a whole number of its refreshes (docs/PLAY.md, "Frame pacing").
struct PresentStats {
    uint64_t n = 0, last_ns = 0;
    double last_vsec = 0, sum = 0, sum2 = 0, worst = 0;   // deviation from the period, ms
    uint64_t hist[6] = {};   // |deviation| <0.5, <1, <2, <4, <8, >=8 ms
    void add(double vsec) {
        uint64_t t = prof::now_ns();
        if (last_ns && !g_fast && t >= g_settle_until) {
            double d = (t - last_ns) / 1e6 - (vsec - last_vsec) * 1e3, a = std::fabs(d);
            n++, sum += d, sum2 += d * d, worst = std::max(worst, a);
            hist[a < 0.5 ? 0 : a < 1 ? 1 : a < 2 ? 2 : a < 4 ? 3 : a < 8 ? 4 : 5]++;
        }
        last_ns = t, last_vsec = vsec;
    }
} g_present;

// Display pacing: steer the sound's playback rate to the game's speed, keeping the queue at its
// target where it is lowest (as the burst starts).
void steer_audio() {
    if (!g_audio) return;
    double q_ms = SDL_GetAudioStreamQueued(g_audio) / 8 * 1000.0 / (g_rate * g_ratio);
    double target_ms = g_target_frames * 1000.0 / g_rate;
    if (q_ms < target_ms / 2) {   // starting, or after a stall: fill up at once, not over seconds
        std::vector<float> silence((size_t)((target_ms - q_ms) * g_rate * g_ratio / 1000) * 2, 0.0f);
        SDL_PutAudioStreamData(g_audio, silence.data(), (int)(silence.size() * sizeof(float)));
        q_ms = target_ms;
    }
    double e = std::clamp((q_ms - target_ms) / target_ms, -1.0, 1.0);
    double r = pacing::speed() * (1.0 + 0.005 * e);
    if (std::fabs(r - g_ratio) > 1e-6) SDL_SetAudioStreamFrequencyRatio(g_audio, (float)r), g_ratio = r;
}

bool window_frame(const uint32_t *rgb, int w, int h, double vsec) {
    const double period = g_last_vsec < 0 || g_fast ? 0.0 : vsec - g_last_vsec;
    g_last_vsec = vsec;
    const bool sync = pacing::frame(period) && !g_fast;
    if (g_gl) {
        prof::Scope ps(prof::PRESENT);
        int ww = 0, wh = 0;
        SDL_GetWindowSizeInPixels(g_win, &ww, &wh);
        // letterboxed at the picture's aspect, as SDL_LOGICAL_PRESENTATION_LETTERBOX does
        float s = std::min((float)ww / w, (float)wh / h);
        int vw = std::max(1, (int)std::lround(w * s)), vh = std::max(1, (int)std::lround(h * s));
        picture::Mode shown = picture::draw(rgb, w, h, (ww - vw) / 2, (wh - vh) / 2, vw, vh, ww, wh, g_mode);
        if (sync) {
            if (g_glFlush) g_glFlush();   // the GPU draws while we wait for the commit time
            pacing::wait_commit();
            pacing::commit();
        }
        SDL_GL_SwapWindow(g_win);
        if (sync) pacing::committed();
        if (shown != g_shown) {
            g_shown = shown;
            g_title_dirty = true;
        }
    } else if (!g_tex || w != g_tw || h != g_th) {
        if (g_tex) SDL_DestroyTexture(g_tex);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(g_tex, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderLogicalPresentation(g_ren, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        g_tw = w;
        g_th = h;
    }
    if (!g_gl) {
        prof::Scope ps(prof::PRESENT);
        SDL_UpdateTexture(g_tex, nullptr, rgb, w * 4);
        SDL_RenderClear(g_ren);
        SDL_RenderTexture(g_ren, g_tex, nullptr, nullptr);
        if (sync) {
            SDL_FlushRenderer(g_ren);
            pacing::wait_commit();
            pacing::commit();
        }
        SDL_RenderPresent(g_ren);
        if (sync) pacing::committed();
    }
    if (!sync) g_present.add(vsec);

    if (g_title_dirty && g_win) {
        g_title_dirty = false;
        std::string t = g_title;
        if (g_shown != picture::Mode::off) t += std::string(" - ") + picture::mode_label(g_shown);
        if (!g_macro.empty()) t += " - typing: " + g_macro;
        SDL_SetWindowTitle(g_win, t.c_str());
    }
    poll();
    if (g_quit) return false;
    if (g_fast) return true;    // fast-forward: no pacing at all
    if (sync) {                 // the display's grid paces time; the sound follows
        pacing::wait_next();
        poll();
        pacing::pump();
        steer_audio();
        return !g_quit;
    }
    if (g_audio && g_ratio != 1.0) SDL_SetAudioStreamFrequencyRatio(g_audio, 1.0f), g_ratio = 1.0;
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
            if (g_target_frames < g_rate * 30 / 1000) g_target_frames += g_rate / 1000;   // +1 ms, up to 30 ms
        }
        if (queued < g_rate / 1000) {   // under 1 ms left: about to crackle
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
    // (with display pacing, the display is the clock: window_frame waits instead)
    const uint64_t w0 = prof::ticks();
    while (!pacing::active() && SDL_GetAudioStreamQueued(g_audio) > g_target_frames * 8 && !g_quit) {
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
    char b[2048];
    int n = snprintf(b, sizeof b,
                     "audio: device buffer %d frames (%.1f ms), queue target %d frames (%.1f ms, after self-tuning)\n"
                     "audio: %llu chunks, %llu ran low (<1 ms queued), %llu found the queue empty; worst gap %.1f ms\n"
                     "audio: host work per chunk  <1:%llu <2:%llu <4:%llu <8:%llu <16:%llu <32:%llu <64:%llu >=64:%llu ms\n"
                     "audio: during low chunks:",
                     g_device_frames, g_device_frames * 1000.0 / g_rate, g_target_frames, g_target_frames * 1000.0 / g_rate,
                     (unsigned long long)g_ast.chunks, (unsigned long long)g_ast.low, (unsigned long long)g_ast.empty,
                     g_ast.worst_gap_ms, (unsigned long long)g_ast.gap_hist[0], (unsigned long long)g_ast.gap_hist[1],
                     (unsigned long long)g_ast.gap_hist[2], (unsigned long long)g_ast.gap_hist[3],
                     (unsigned long long)g_ast.gap_hist[4], (unsigned long long)g_ast.gap_hist[5],
                     (unsigned long long)g_ast.gap_hist[6], (unsigned long long)g_ast.gap_hist[7]);
    for (int c = 0; c < prof::NCAT; c++)
        n += snprintf(b + n, sizeof b - n, " %s %.0f ms", prof::NAMES[c], g_ast.low_ns[c] / 1e6);
    if (g_present.n) {
        const PresentStats &p = g_present;
        double mean = p.sum / p.n, sd = std::sqrt(std::max(0.0, p.sum2 / p.n - mean * mean));
        snprintf(b + n, sizeof b - n,
                 "\nvideo: %llu presents, time between them minus the frame period: mean %+.3f ms, sd %.2f ms, worst %.2f ms; "
                 "|off| <0.5:%llu <1:%llu <2:%llu <4:%llu <8:%llu >=8:%llu ms",
                 (unsigned long long)p.n, mean, sd, p.worst, (unsigned long long)p.hist[0], (unsigned long long)p.hist[1],
                 (unsigned long long)p.hist[2], (unsigned long long)p.hist[3], (unsigned long long)p.hist[4],
                 (unsigned long long)p.hist[5]);
    }
    return std::string(b) + "\n" + pacing::describe();
}

void window_close() {
    pacing::shutdown();
    if (g_audio) SDL_DestroyAudioStream(g_audio);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_gl) {
        picture::shutdown();
        SDL_GL_DestroyContext(g_gl);
    }
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

}  // namespace hk::host
