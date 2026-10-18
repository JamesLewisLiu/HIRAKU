// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>
#include <thread>

#include "../shell/core.h"
#include "macro.h"
#include "pacing.h"
#include "asio.h"
#include "wasapi.h"
#include "notice.h"
#include "picture.h"
#include "input.h"
#include "../core/prof.h"

namespace hk::host {

namespace {
uint64_t g_settle_until = 0;   // window/renderer setup right after opening: not a stall
std::atomic<uint64_t> g_device_pulls{0}, g_device_short{0};   // the device's pulls, and those the queue fell short of
std::atomic<uint64_t> g_device_short_late{0};                  // ... of those, after the game's boot (g_tune_from)
uint64_t g_short_seen = 0;        // g_device_short when the target last grew for it
uint64_t g_tune_from = 0;          // the queue target self-tunes from here on (window_open + 10 s)

// A new window: 480 lines a scale step, as wide as the core's monitor shape makes them (640 for
// 4:3; IIDX's 16:9 tube, 853)
static int window_width(int scale) {
    const double a = core().display_aspect();
    return (int)std::lround(480.0 * scale * (a > 0 ? a : 4.0 / 3.0));
}
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
SDL_FRect g_dst{};                // where the frame goes in the logical presentation (SDL renderer path)
std::chrono::steady_clock::time_point g_t0;
bool g_started = false;

int g_rate = 44100;               // the core's audio rate (Core::audio_rate)
std::string g_audio_device;       // window_set_audio_output: an ALSA device to have alone ("": the default);
                                  // Windows: an output device by name
bool g_audio_exclusive = false;   // Windows: WASAPI exclusive mode (wasapi.h)
std::string g_audio_asio;         // Windows: an ASIO driver (asio.h)
int g_audio_buffer = 0;           // ... and the device buffer in frames (0: 128)
std::string g_audio_output;       // what it got, for the report
int g_target_frames = 0;          // audio queued ahead, when audio paces
bool g_fast = false;              // Tab held: run as fast as possible (audio is dropped)
int g_device_frames = 0;
bool g_quit = false;
bool g_display_sync = true;       // display pacing (pacing.h)
double g_ratio = 1.0;             // the audio stream's frequency ratio: the game's speed, steered
// Display sync off, and the core hands its sound on once per frame (Core::audio_per_frame): the
// window waits for the sound after showing a frame, before the next one runs (window_frame), and
// push_chunk doesn't wait. Waiting in push_chunk, which the core calls just before the frame hook,
// put that wait between a frame's ~2 ms of emulation and its picture: a frame (16.7 ms) from the
// controls being read to the picture, where display sync takes a few milliseconds.
bool g_frame_paced = false;
uint64_t g_frame_start_ns = 0;    // when the window last let a frame run (the controls just read)
uint64_t g_last_push_end = 0;     // the host's work since then is what must fit (push_chunk)

// With g_frame_paced, each picture is held until a steady time after its frame started: the slow
// end of recent frames' emulation and drawing (as display sync's burst budget, pacing.cpp), so the
// pictures keep the game's cadence while a frame's work varies (pop'n 9: 1-8 ms with the GS).
struct FrameBudget {
    std::vector<int64_t> samples = std::vector<int64_t>(256, 0);
    size_t at = 0;
    int64_t budget = 0;           // ns; 0 until 64 frames are in
    void add(int64_t ns) {
        samples[at++ % samples.size()] = ns;
        if (at % 64) return;      // the 99th percentile of the recent ones, plus 0.5 ms
        std::vector<int64_t> s(samples.begin(), samples.begin() + (long)std::min(at, samples.size()));
        std::nth_element(s.begin(), s.begin() + (long)(s.size() * 99 / 100), s.end());
        budget = std::min(s[s.size() * 99 / 100] + 500'000, (int64_t)12'000'000);
    }
    // the picture is ready: wait until its time
    void hold(uint64_t start_ns) {
        if (!start_ns) return;
        const int64_t ready = (int64_t)(prof::now_ns() - start_ns);
        add(ready);
        if (budget > ready) {
            const uint64_t w0 = prof::ticks();
            SDL_DelayPrecise((Uint64)(budget - ready));
            prof::g_wait_ticks += prof::ticks() - w0;
        }
    }
} g_frame_budget;
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
        // no pointer over the game in fullscreen (it would also keep the compositor from
        // scanning the picture out directly, where the pointer isn't a plane of its own)
        if (e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) SDL_HideCursor();
        if (e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) SDL_ShowCursor();
        if (e.type == SDL_EVENT_JOYSTICK_ADDED) input_joystick_added(e.jdevice.which);
        if (e.type == SDL_EVENT_JOYSTICK_REMOVED) input_joystick_removed(e.jdevice.which);
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
    // opaque, 8 bits a channel: a buffer the compositor can scan out directly in fullscreen,
    // skipping its own composition (and a frame's worth of its latency)
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    g_win = SDL_CreateWindow(title, window_width(scale), 480 * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
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

void window_set_audio_output(const std::string &device, int frames, bool exclusive) {
    g_audio_device = device;
    g_audio_buffer = frames;
    g_audio_exclusive = exclusive;
}

void window_set_audio_asio(const std::string &driver) { g_audio_asio = driver; }

namespace {
// A sound card of our own (--audio-device): HIRAKU writes to it through ALSA itself, as aplay does,
// a period at a time into a ring of four kept full. SDL's own ALSA output keeps a ring of two
// periods, and a USB card's driver starves on that, playing stale sound without reporting it (a
// USB S/PDIF adapter played 4 s of sound in 6.6 s at 64 frames x 2, in 4.1 s at 64 x 4; at 64 x 3
// still 5.2). libasound is loaded at run time, as SDL loads it: no build dependency.
namespace alsa {
struct pcm;
struct hw;
using uframes = unsigned long;
struct Api {
    int (*open)(pcm **, const char *, int, int);
    int (*close)(pcm *);
    int (*hw_malloc)(hw **);
    void (*hw_free)(hw *);
    int (*hw_any)(pcm *, hw *);
    int (*set_access)(pcm *, hw *, int);
    int (*set_format)(pcm *, hw *, int);
    int (*set_channels)(pcm *, hw *, unsigned);
    int (*set_rate_near)(pcm *, hw *, unsigned *, int *);
    int (*set_period_near)(pcm *, hw *, uframes *, int *);
    int (*set_buffer_near)(pcm *, hw *, uframes *);
    int (*get_period)(const hw *, uframes *, int *);
    int (*get_buffer)(const hw *, uframes *);
    int (*hw_params)(pcm *, hw *);
    int (*wait)(pcm *, int);
    long (*avail_update)(pcm *);
    long (*writei)(pcm *, const void *, uframes);
    int (*recover)(pcm *, int, int);
    int (*drop)(pcm *);
    const char *(*strerror)(int);
} api;
pcm *g_pcm = nullptr;
int g_period = 0, g_frame_bytes = 0;
unsigned g_card_rate = 0;
std::atomic<uint64_t> g_written{0}, g_xruns{0};   // frames written after the ring's first fill, underruns
uint64_t g_t0 = 0;                                 // when the ring was first full
std::atomic<bool> g_stop{false};
std::thread *g_thread = nullptr;   // never destroyed: a game ends with exit(), the thread still writing

bool load(std::string &why) {
    static SDL_SharedObject *lib = SDL_LoadObject("libasound.so.2");   // dlopen, RTLD_NOW | RTLD_LOCAL
    if (!lib) return why = "no libasound.so.2", false;
    bool ok = true;
    auto get = [&](auto &f, const char *name) {
        f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(SDL_LoadFunction(lib, name));
        if (!f) ok = false, why = std::string("libasound has no ") + name;
    };
    get(api.open, "snd_pcm_open"), get(api.close, "snd_pcm_close"), get(api.hw_malloc, "snd_pcm_hw_params_malloc");
    get(api.hw_free, "snd_pcm_hw_params_free"), get(api.hw_any, "snd_pcm_hw_params_any");
    get(api.set_access, "snd_pcm_hw_params_set_access"), get(api.set_format, "snd_pcm_hw_params_set_format");
    get(api.set_channels, "snd_pcm_hw_params_set_channels"), get(api.set_rate_near, "snd_pcm_hw_params_set_rate_near");
    get(api.set_period_near, "snd_pcm_hw_params_set_period_size_near");
    get(api.set_buffer_near, "snd_pcm_hw_params_set_buffer_size_near");
    get(api.get_period, "snd_pcm_hw_params_get_period_size"), get(api.get_buffer, "snd_pcm_hw_params_get_buffer_size");
    get(api.hw_params, "snd_pcm_hw_params"), get(api.wait, "snd_pcm_wait"), get(api.avail_update, "snd_pcm_avail_update");
    get(api.writei, "snd_pcm_writei"), get(api.recover, "snd_pcm_recover"), get(api.drop, "snd_pcm_drop");
    get(api.strerror, "snd_strerror");
    return ok;
}

// each time a period of the ring is free: that much from the stream (silence for what it lacks:
// the stream's get callback counts that as a break), written
void run(SDL_AudioStream *stream) {
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
    std::vector<uint8_t> buf((size_t)g_period * g_frame_bytes);
    const int bytes = (int)buf.size();
    while (!g_stop.load(std::memory_order_relaxed)) {
        long avail = api.avail_update(g_pcm);
        if (avail < 0) {
            g_xruns.fetch_add(1, std::memory_order_relaxed);
            api.recover(g_pcm, (int)avail, 1);
            continue;
        }
        if (avail < g_period) {
            api.wait(g_pcm, 100);
            continue;
        }
        const int got = std::max(0, SDL_GetAudioStreamData(stream, buf.data(), bytes));
        if (got < bytes) memset(buf.data() + got, 0, (size_t)(bytes - got));
        const long n = api.writei(g_pcm, buf.data(), (uframes)g_period);
        if (n < 0) {
            g_xruns.fetch_add(1, std::memory_order_relaxed);
            api.recover(g_pcm, (int)n, 1);
        } else {
            g_written.fetch_add((uint64_t)n, std::memory_order_relaxed);
        }
    }
}

// `device` alone at `src`'s rate (or the nearest it has), periods of `period` frames: a stream
// converting `src` to it, which a thread of ours plays. "" or why not
[[maybe_unused]] std::string open(const std::string &device, const SDL_AudioSpec &src, int period, SDL_AudioStream *&stream, std::string &what) {
    std::string why;
    if (!load(why)) return why;
    int e = api.open(&g_pcm, device.c_str(), 0 /* playback */, 0);
    if (e < 0) return g_pcm = nullptr, api.strerror(e);
    hw *h = nullptr;
    api.hw_malloc(&h);
    api.hw_any(g_pcm, h);
    e = api.set_access(g_pcm, h, 3 /* RW_INTERLEAVED */);
    // float, 32 or 16 bits, whichever the card takes first
    const struct { int alsa; SDL_AudioFormat sdl; int bytes; const char *name; } formats[] = {
        {14, SDL_AUDIO_F32LE, 4, "float"}, {10, SDL_AUDIO_S32LE, 4, "32-bit"}, {2, SDL_AUDIO_S16LE, 2, "16-bit"}};
    const char *fname = nullptr;
    SDL_AudioSpec dst{SDL_AUDIO_S16LE, 2, src.freq};
    for (const auto &f : formats)
        if (e >= 0 && api.set_format(g_pcm, h, f.alsa) >= 0) {
            dst.format = f.sdl, g_frame_bytes = 2 * f.bytes, fname = f.name;
            break;
        }
    unsigned rate = (unsigned)src.freq;
    uframes p = (uframes)period, ring = 0;
    if (e >= 0 && !fname) e = -22;
    if (e >= 0) e = api.set_channels(g_pcm, h, 2);
    if (e >= 0) e = api.set_rate_near(g_pcm, h, &rate, nullptr);
    if (e >= 0) e = api.set_period_near(g_pcm, h, &p, nullptr);
    if (e >= 0) ring = 4 * p, e = api.set_buffer_near(g_pcm, h, &ring);
    if (e >= 0) e = api.hw_params(g_pcm, h);
    if (e >= 0) api.get_period(h, &p, nullptr), api.get_buffer(h, &ring);
    api.hw_free(h);
    if (e < 0) {
        api.close(g_pcm);
        g_pcm = nullptr;
        return api.strerror(e);
    }
    g_period = (int)p;
    dst.freq = (int)rate;
    stream = SDL_CreateAudioStream(&src, &dst);
    char b[160];
    snprintf(b, sizeof b, "%s, %u Hz, ring %lu frames (%.1f ms) in periods of %lu", fname, rate, ring, ring * 1000.0 / rate, p);
    what = b;
    // the ring full of silence, then the thread keeps it full
    std::vector<uint8_t> zero((size_t)ring * g_frame_bytes, 0);
    api.writei(g_pcm, zero.data(), ring);
    g_card_rate = rate;
    g_t0 = prof::now_ns();
    g_stop = false;
    g_thread = new std::thread(run, stream);
    return "";
}

void close() {
    if (!g_pcm) return;
    g_stop = true;
    if (g_thread) g_thread->join(), delete g_thread, g_thread = nullptr;
    api.drop(g_pcm);
    api.close(g_pcm);
    g_pcm = nullptr;
}
}  // namespace alsa
}  // namespace

bool window_open(const char *title, int scale, const std::string &bindings, int target_ms, bool fullscreen, int rate) {
    g_rate = rate;
    // the device's buffer: the sound server's quantum (PipeWire: node.latency)
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, std::to_string(g_audio_buffer > 0 ? g_audio_buffer : 128).c_str());
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return false;
    input_init(bindings);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, g_rate};
#ifdef _WIN32
    if (!g_audio_asio.empty()) {   // the card through its own ASIO driver: no Windows audio in between
        std::string what, why = asio::open(g_audio_asio, spec, g_audio_buffer, g_audio, what);
        if (why.empty()) {
            g_audio_output = "ASIO: " + what;
            g_device_frames = asio::period_frames();
        } else {
            fprintf(stderr, "audio: ASIO driver \"%s\": %s; Windows' audio instead\n", g_audio_asio.c_str(), why.c_str());
        }
    }
    if (!g_audio && g_audio_exclusive) {   // the device alone, in WASAPI exclusive mode: no system mixer in between
        std::string what, why = wasapi::open(g_audio_device, spec, g_audio_buffer > 0 ? g_audio_buffer : 128, g_audio, what);
        if (why.empty()) {
            g_audio_output = (g_audio_device.empty() ? std::string("the default output") : g_audio_device) +
                             " (alone, WASAPI exclusive: " + what + ")";
            g_device_frames = wasapi::period_frames();
        } else {
            fprintf(stderr, "audio: exclusive mode on %s: %s; shared mode instead\n",
                    g_audio_device.empty() ? "the default output" : g_audio_device.c_str(), why.c_str());
        }
    }
    if (!g_audio && !g_audio_device.empty()) {   // that device, shared: SDL's WASAPI output, by its name
        int n = 0;
        SDL_AudioDeviceID *ids = SDL_GetAudioPlaybackDevices(&n);
        for (int k = 0; ids && k < n && !g_audio; k++)
            if (const char *name = SDL_GetAudioDeviceName(ids[k]); name && g_audio_device == name) {
                g_audio = SDL_OpenAudioDeviceStream(ids[k], &spec, nullptr, nullptr);
                if (g_audio) g_audio_output = g_audio_device + std::string(" (shared, ") + SDL_GetCurrentAudioDriver() + ")";
            }
        SDL_free(ids);
        if (!g_audio) fprintf(stderr, "audio: no output device named \"%s\"; the system's default output instead\n", g_audio_device.c_str());
    }
#else
    if (!g_audio_device.empty()) {   // the card itself, through ALSA: no sound server in between
        std::string what, why = alsa::open(g_audio_device, spec, g_audio_buffer > 0 ? g_audio_buffer : 128, g_audio, what);
        if (why.empty()) {
            g_audio_output = g_audio_device + " (the card alone, through ALSA: " + what + ")";
            g_device_frames = alsa::g_period;
        } else {   // busy (the sound server has it open while anything plays), or no such device
            fprintf(stderr, "audio: %s: %s; the system's default output instead (the sound server lets go of a card "
                            "a few seconds after the last sound on it stops)\n", g_audio_device.c_str(), why.c_str());
        }
    }
#endif
    if (!g_audio) g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (g_audio_output.empty() && g_audio) g_audio_output = std::string("the system's default (") + SDL_GetCurrentAudioDriver() + ")";
    if (g_audio) {
        SDL_AudioSpec dev{};
        if (SDL_GetAudioStreamDevice(g_audio)) SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_audio), &dev, &g_device_frames);
        g_target_frames = g_rate * target_ms / 1000;
        // start with the target lead of silence: the first frames' window setup
        // would otherwise find an empty queue (and grow the target for nothing)
        std::vector<float> silence((size_t)g_target_frames * 2, 0.0f);
        SDL_PutAudioStreamData(g_audio, silence.data(), (int)(silence.size() * sizeof(float)));
        // the device's own view: a pull the queue can't fill breaks the sound. The queue level
        // when a chunk arrives misses most of these: with a core whose sound comes once per frame
        // the queue runs dry between chunks and is full again when the next one is counted
        SDL_SetAudioStreamGetCallback(g_audio, [](void *, SDL_AudioStream *s, int, int total) {
            g_device_pulls.fetch_add(1, std::memory_order_relaxed);
            const uint64_t t = prof::now_ns();
            if (SDL_GetAudioStreamAvailable(s) < total && !g_fast && t >= g_settle_until) {
                g_device_short.fetch_add(1, std::memory_order_relaxed);
                if (t >= g_tune_from) g_device_short_late.fetch_add(1, std::memory_order_relaxed);
            }
        }, nullptr);
        SDL_ResumeAudioStreamDevice(g_audio);
        g_settle_until = prof::now_ns() + 250'000'000;
        g_tune_from = prof::now_ns() + 10'000'000'000ull;
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
    if (!SDL_CreateWindowAndRenderer(title, window_width(scale), 480 * scale, SDL_WINDOW_RESIZABLE, &g_win, &g_ren)) return false;
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

// From the window letting a frame run (the controls just read: the games read theirs at the
// frame's start) to that frame's picture handed to the display, in 0.1 ms steps up to 50 ms.
// The display adds its own on top (pacing.h: commit to screen).
struct StartToPicture {
    uint64_t hist[501] = {}, n = 0;
    double sum = 0;
    void add(uint64_t start_ns) {
        const uint64_t t = prof::now_ns();
        if (!start_ns || g_fast || t < g_settle_until) return;
        const double ms = (t - start_ns) / 1e6;
        hist[std::min(500, (int)(ms * 10))]++, n++, sum += ms;
    }
    double percentile(double q) const {
        uint64_t need = (uint64_t)(q * (double)n), acc = 0;
        for (int k = 0; k <= 500; k++)
            if ((acc += hist[k]) > need) return (k + 1) / 10.0;
        return 50.0;
    }
} g_start_to_picture;

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

// The shape a frame is shown at: the core's monitor, or the frame's own with square pixels
static double display_aspect(int w, int h) {
    const double a = core().display_aspect();
    return a > 0 ? a : (double)w / h;
}

bool g_sync = false;   // display pacing for the frame last presented

// host/notice.h's banner: whole times its size (as large as a 360-line screen's), centred near
// the window's bottom
static void place_banner(const notice::Banner &b, int win_w, int win_h, int &x, int &y, int &dw, int &dh) {
    int k = std::max(1, win_h / 360);
    while (k > 1 && b.w * k > win_w - 16) k--;
    dw = b.w * k;
    dh = b.h * k;
    x = (win_w - dw) / 2;
    y = win_h - dh - 8 * k;
}

void window_present(const uint32_t *rgb, int w, int h, double vsec) {
    const double period = g_last_vsec < 0 || g_fast ? 0.0 : vsec - g_last_vsec;
    g_last_vsec = vsec;
    const bool sync = pacing::frame(period) && !g_fast;
    g_sync = sync;
    if (g_gl) {
        prof::Scope ps(prof::PRESENT);
        int ww = 0, wh = 0;
        SDL_GetWindowSizeInPixels(g_win, &ww, &wh);
        // letterboxed at the monitor's shape (Core::display_aspect), as SDL_LOGICAL_PRESENTATION_LETTERBOX
        // does: a 512x480 menu fills the same 4:3 as a 320x240 game
        const double aspect = display_aspect(w, h);
        int vw = ww, vh = (int)std::lround(ww / aspect);
        if (vh > wh) vh = wh, vw = (int)std::lround(wh * aspect);
        // and of that, the part the frame covers (Core::display_cover)
        double cw, ch;
        core().display_cover(w, h, cw, ch);
        vw = std::max(1, (int)std::lround(vw * cw)), vh = std::max(1, (int)std::lround(vh * ch));
        picture::Mode shown = picture::draw(rgb, w, h, (ww - vw) / 2, (wh - vh) / 2, vw, vh, ww, wh, g_mode);
        if (notice::Banner b; notice::banner(b)) {
            int bx, by, bw, bh;
            place_banner(b, ww, wh, bx, by, bw, bh);
            picture::overlay(b.pixels->data(), b.w, b.h, bx, by, bw, bh, b.alpha, ww, wh);
        }
        if (sync) {
            if (g_glFlush) g_glFlush();   // the GPU draws while we wait for the commit time
            pacing::wait_commit();
            pacing::commit();
        } else if (g_frame_paced && !g_fast) {
            if (g_glFlush) g_glFlush();
            g_frame_budget.hold(g_frame_start_ns);
        }
        if (!sync) pacing::observe();
        SDL_GL_SwapWindow(g_win);
        g_start_to_picture.add(g_frame_start_ns);
        if (sync) pacing::committed();
        if (shown != g_shown) {
            g_shown = shown;
            g_title_dirty = true;
        }
    } else if (!g_tex || w != g_tw || h != g_th) {
        if (g_tex) SDL_DestroyTexture(g_tex);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(g_tex, SDL_SCALEMODE_NEAREST);
        // the logical size at the monitor's shape (as above), the frame's own part of it in g_dst
        double cw, ch;
        core().display_cover(w, h, cw, ch);
        const float lh = (float)(h / ch), lw = (float)std::lround(lh * display_aspect(w, h));
        SDL_SetRenderLogicalPresentation(g_ren, std::max(1, (int)lw), std::max(1, (int)std::lround(lh)),
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        g_dst = {(lw - (float)(lw * cw)) / 2, (lh - (float)h) / 2, (float)(lw * cw), (float)h};
        g_tw = w;
        g_th = h;
    }
    if (!g_gl) {
        prof::Scope ps(prof::PRESENT);
        SDL_UpdateTexture(g_tex, nullptr, rgb, w * 4);
        SDL_RenderClear(g_ren);
        SDL_RenderTexture(g_ren, g_tex, nullptr, &g_dst);
        if (notice::Banner b; notice::banner(b)) {
            // in the window's own pixels, not the game's logical size
            static SDL_Texture *bt = nullptr;
            if (!bt && (bt = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, b.w, b.h))) {
                SDL_UpdateTexture(bt, nullptr, b.pixels->data(), b.w * 4);
                SDL_SetTextureBlendMode(bt, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(bt, SDL_SCALEMODE_NEAREST);
            }
            int lw, lh, ow, oh;
            SDL_RendererLogicalPresentation lm;
            if (bt && SDL_GetRenderLogicalPresentation(g_ren, &lw, &lh, &lm)) {
                SDL_SetRenderLogicalPresentation(g_ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
                SDL_GetCurrentRenderOutputSize(g_ren, &ow, &oh);
                int bx, by, bw, bh;
                place_banner(b, ow, oh, bx, by, bw, bh);
                const SDL_FRect r{(float)bx, (float)by, (float)bw, (float)bh};
                SDL_SetTextureAlphaModFloat(bt, b.alpha);
                SDL_RenderTexture(g_ren, bt, nullptr, &r);
                SDL_SetRenderLogicalPresentation(g_ren, lw, lh, lm);
            }
        }
        if (sync) {
            SDL_FlushRenderer(g_ren);
            pacing::wait_commit();
            pacing::commit();
        } else if (g_frame_paced && !g_fast) {
            SDL_FlushRenderer(g_ren);
            g_frame_budget.hold(g_frame_start_ns);
        }
        if (!sync) pacing::observe();
        SDL_RenderPresent(g_ren);
        g_start_to_picture.add(g_frame_start_ns);
        if (sync) pacing::committed();
    }
    if (!sync) g_present.add(vsec);

    // a text display the game drives (IIDX's 16-segment ticker): in the title while it shows something
    static std::string display;
    if (std::string d = core().display_text(); d != display) {
        display = d;
        g_title_dirty = true;
    }
    if (g_title_dirty && g_win) {
        g_title_dirty = false;
        std::string t = g_title;
        if (display.find_first_not_of(' ') != std::string::npos) t += " - [" + display + "]";
        if (g_shown != picture::Mode::off) t += std::string(" - ") + picture::mode_label(g_shown);
        if (!g_macro.empty()) t += " - typing: " + g_macro;
        SDL_SetWindowTitle(g_win, t.c_str());
    }
}

bool window_next(double vsec) {
    const bool sync = g_sync;
    poll();
    g_frame_start_ns = 0;
    g_frame_paced = false;
    if (g_quit) return false;
    if (g_fast) return true;    // fast-forward: no pacing at all
    if (sync) {                 // the display's grid paces time; the sound follows
        pacing::wait_next();
        poll();
        pacing::pump();
        steer_audio();
        g_frame_start_ns = prof::now_ns();
        return !g_quit;
    }
    if (g_audio && g_ratio != 1.0) SDL_SetAudioStreamFrequencyRatio(g_audio, 1.0f), g_ratio = 1.0;
    if (g_audio && core().audio_per_frame()) {
        // the sound paces time, as in audio_push, but here: until the queue is down to its
        // target, then the next frame runs (with this frame's controls read last) and is shown
        // as soon as it's drawn. The queue's lowest point is the same as waiting in push_chunk.
        g_frame_paced = true;
        const uint64_t w0 = prof::ticks();
        while (SDL_GetAudioStreamQueued(g_audio) > g_target_frames * 8 && !g_quit) {
            poll();
            SDL_DelayPrecise(250'000);   // 0.25 ms
        }
        prof::g_wait_ticks += prof::ticks() - w0;
        poll();
        g_frame_start_ns = g_last_push_end = prof::now_ns();   // the wait isn't host work
        return !g_quit;
    }
    if (g_audio) {              // audio paces time (audio_push)
        g_frame_start_ns = prof::now_ns();
        return true;
    }

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
    g_frame_start_ns = prof::now_ns();
    return true;
}

bool window_frame(const uint32_t *rgb, int w, int h, double vsec) {
    window_present(rgb, w, h, vsec);
    return window_next(vsec);
}

// Underrun accounting: the queue level when each chunk arrives, and what the
// host did since the previous chunk when it ran low.
struct AudioStats {
    uint64_t chunks = 0, low = 0, empty = 0;
    double worst_gap_ms = 0;
    uint64_t gap_hist[8] = {};   // work time per chunk: <1,<2,<4,<8,<16,<32,<64,>=64 ms
    uint64_t low_ns[prof::NCAT] = {};
} g_ast;
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
        if (queued == 0) g_ast.empty++;
        const uint64_t short_pulls = g_device_short.load(std::memory_order_relaxed);
        if (queued == 0 || short_pulls != g_short_seen) {
            // self-tuning: an underrun means the machine needs more lead on this host. Not while
            // the game boots (its loads, the display's pacing settling), and not for a stall no
            // lead would cover
            if (t >= g_tune_from && gap < 100 && g_target_frames < g_rate * 30 / 1000)
                g_target_frames += g_rate / 1000;   // +1 ms, up to 30 ms
            g_short_seen = short_pulls;
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
    // (with display pacing, the display is the clock, and with a core whose sound comes once
    // per frame the window waits after the frame: window_frame waits instead)
    const uint64_t w0 = prof::ticks();
    while (!pacing::active() && !g_frame_paced && SDL_GetAudioStreamQueued(g_audio) > g_target_frames * 8 && !g_quit) {
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
                     "audio: %s, device buffer %d frames (%.1f ms), queue target %d frames (%.1f ms, after self-tuning)\n"
                     "audio: %llu chunks, %llu ran low (<1 ms queued), %llu found the queue empty; worst gap %.1f ms\n"
                     "audio: the device pulled %llu times, %llu of them more than was queued (a break in the sound), %llu after the first 10 s\n"
                     "audio: host work per chunk  <1:%llu <2:%llu <4:%llu <8:%llu <16:%llu <32:%llu <64:%llu >=64:%llu ms\n"
                     "audio: during low chunks:",
                     g_audio_output.c_str(), g_device_frames, g_device_frames * 1000.0 / g_rate, g_target_frames, g_target_frames * 1000.0 / g_rate,
                     (unsigned long long)g_ast.chunks, (unsigned long long)g_ast.low, (unsigned long long)g_ast.empty,
                     g_ast.worst_gap_ms, (unsigned long long)g_device_pulls.load(), (unsigned long long)g_device_short.load(),
                     (unsigned long long)g_device_short_late.load(),
                     (unsigned long long)g_ast.gap_hist[0], (unsigned long long)g_ast.gap_hist[1],
                     (unsigned long long)g_ast.gap_hist[2], (unsigned long long)g_ast.gap_hist[3],
                     (unsigned long long)g_ast.gap_hist[4], (unsigned long long)g_ast.gap_hist[5],
                     (unsigned long long)g_ast.gap_hist[6], (unsigned long long)g_ast.gap_hist[7]);
    for (int c = 0; c < prof::NCAT; c++)
        n += snprintf(b + n, sizeof b - n, " %s %.0f ms", prof::NAMES[c], g_ast.low_ns[c] / 1e6);
#ifdef _WIN32
    if (wasapi::active()) n += snprintf(b + n, sizeof b - n, "\n%s", wasapi::describe().c_str());
    if (asio::active()) n += snprintf(b + n, sizeof b - n, "\n%s", asio::describe().c_str());
#endif
    if (alsa::g_pcm && alsa::g_t0) {   // the card's own pace: a starving card takes fewer frames than its rate
        const double secs = (prof::now_ns() - alsa::g_t0) / 1e9;
        n += snprintf(b + n, sizeof b - n,
                      "\naudio: the card took %.1f %% of its rate's frames over %.0f s (100: it plays what it's given), %llu underruns",
                      secs > 0 ? 100.0 * (double)alsa::g_written.load() / (secs * alsa::g_card_rate) : 0.0, secs,
                      (unsigned long long)alsa::g_xruns.load());
    }
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
    std::string out = std::string(b) + "\n" + pacing::describe();
    if (std::string sc = pacing::scanout_describe(); !sc.empty()) out += "\n" + sc;
    if (const StartToPicture &l = g_start_to_picture; l.n) {
        snprintf(b, sizeof b,
                 "\nvideo: from a frame starting (the controls read) to its picture handed to the display: mean %.1f ms, "
                 "p50 %.1f, p99 %.1f (%s)",
                 l.sum / (double)l.n, l.percentile(0.5), l.percentile(0.99),
                 pacing::active() ? "display sync"
                 : g_frame_paced  ? "the sound once per frame: waited for after the picture"
                                  : "the sound paces the machine as it comes");
        out += b;
        if (g_frame_paced && g_frame_budget.budget) {
            snprintf(b, sizeof b, ", each picture held to %.1f ms after its frame's start", g_frame_budget.budget / 1e6);
            out += b;
        }
    }
    return out;
}

void window_close() {
    pacing::shutdown();
    alsa::close();
#ifdef _WIN32
    wasapi::close();
    asio::close();
#endif
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
