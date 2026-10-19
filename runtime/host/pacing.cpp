// SPDX-License-Identifier: GPL-3.0-or-later
#include "pacing.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unordered_set>
#include <vector>

#include "../core/prof.h"

#ifdef HK_WITH_WAYLAND
#include <poll.h>
#include <wayland-client.h>

#include "fifo-v1-client.h"
#include "presentation-time-client.h"
#include "content-type-v1-client.h"
#endif

namespace hk::host::pacing {

namespace {

double g_max_skew = 0.03;   // the most the game's speed may change (pop'n at 60 Hz: 2.6 %); HK_DISPLAY_SYNC_SKEW
constexpr int64_t MS = 1'000'000;

SDL_Window *g_win = nullptr;
bool g_enabled = false, g_active = false, g_fresh = true;
int64_t g_T = 0;               // the display's refresh period, ns (0: unknown)
bool g_T_exact = false;        // from the compositor's presentation feedback
int g_k = 1;                   // refreshes per frame
double g_speed = 1.0;
double g_game_hz = 0;

int64_t g_target = 0;          // the vblank the current frame is for (steady clock, ns)
int64_t g_lead = 0;            // commit this long before it (negative: after it)
bool g_lead_set = false;
bool g_settled = false;        // a frame came late while the lead was coming down: keep it
int g_streak = 0;              // frames on time since the lead last changed
int64_t g_budget = 4 * MS;     // the burst starts this long before its commit
std::vector<int64_t> g_samples;   // recent burst + draw times
size_t g_sample_at = 0;
int64_t g_burst_start = 0;
bool g_overrun = false;        // this frame wasn't ready at its commit time

uint64_t n_frames = 0, n_presented = 0, n_on_time = 0, n_late = 0, n_overrun = 0, n_resync = 0, n_discarded = 0,
         n_moved = 0;
uint64_t n_shown = 0, n_zero_copy = 0;   // every frame's presentation feedback, either mode: shown, and
                                         // of them scanned out straight from our buffer (not composited)
std::string g_state;           // the last announced state, to print changes once
bool g_debug = getenv("HK_PACING_DEBUG") != nullptr;

void sleep_until(int64_t t) {
    int64_t d = t - (int64_t)prof::now_ns();
    if (d <= 0) return;
    const uint64_t w0 = prof::ticks();
    SDL_DelayPrecise((Uint64)d);
    prof::g_wait_ticks += prof::ticks() - w0;
}

void refresh_from_mode() {
    if (!g_win) return;
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(g_win));
    if (!m) return;
    if (m->refresh_rate_numerator > 0 && m->refresh_rate_denominator > 0)
        g_T = (int64_t)(1e9 * m->refresh_rate_denominator / m->refresh_rate_numerator + 0.5);
    else if (m->refresh_rate > 0)
        g_T = (int64_t)(1e9 / m->refresh_rate + 0.5);
}

// Print the state when it changes (`key`: what counts as a change), not every flutter of the rates.
std::string g_key;
void announce(const std::string &key, const std::string &s) {
    g_state = s;
    if (key == g_key) return;
    g_key = key;
    fprintf(stderr, "display sync: %s\n", s.c_str());
}

// A frame reached the screen at `t` (steady clock), committed at `committed`. The compositor
// shows a frame at the first refresh its deadline allows (and with fifo-v1 never before the
// previous frame's last refresh), so the time from commit to screen falls as the commit moves
// later, until a commit misses the deadline: from then on every frame comes a refresh later
// (with fifo, the queue moved; without, until the next frame). Measured at 240 Hz on KWin: about
// 8 ms at best, and the deadline wanders by up to a refresh with KWin's render-time estimate.
// So: start half a refresh ahead; move the commit later, a 32nd of a refresh every 20 frames,
// until commit-to-screen (the median of 10) is a refresh slower than the best: a miss. Then catch
// up (the grid moves a refresh later: the machine waits one refresh, the sound queue absorbs it)
// and commit a 32nd plus 1.5 ms earlier, and stay. Each later miss adds 0.75 ms.
int g_skip = 5;                // presented frames ignored (the window settling, or after a catch-up)
bool g_sweep = getenv("HK_PACING_SWEEP") != nullptr;   // testing: keep moving the commit, to map the deadline
int64_t g_prev_t = 0;          // the previous presented frame, 0 after a gap
int64_t g_best = 0;            // the shortest commit-to-screen median (0: none yet)
std::vector<int64_t> g_window; // commit-to-screen times for the next median
constexpr int64_t MARGIN = 3 * MS / 2;
[[maybe_unused]] void presented(int64_t t, int64_t committed, int64_t target, bool overrun, int64_t refresh, int k) {
    n_presented++;
    if (refresh > 0) g_T = refresh, g_T_exact = true;
    if (!g_active || g_T <= 0) return;
    int64_t e = t - target;
    g_target += (e - std::llround((double)e / (double)g_T) * g_T) / 8;   // keep the grid on the real vblanks
    const int64_t prev = g_prev_t, d = t - committed;
    g_prev_t = t;
    if (prev) {   // the cadence, for the report: did the previous frame stay up its k refreshes?
        if (std::llround((double)(t - prev) / (double)g_T) == k) n_on_time++;
        else n_late++;
    }
    if (g_debug) fprintf(stderr, "pacing: commit to screen %.3f ms, lead %.2f ms, overrun %d\n", d / 1e6, g_lead / 1e6, (int)overrun);
    if (g_skip > 0) {
        g_skip--;
        return;
    }
    if (overrun) return;   // committed late: says nothing about the deadline
    g_window.push_back(d);
    if (g_window.size() >= 10) {
        std::nth_element(g_window.begin(), g_window.begin() + 5, g_window.end());
        const int64_t median = g_window[5];
        g_window.clear();
        if (!g_best || median < g_best) g_best = median;
        else if (median > g_best + g_T / 2 && !g_sweep) {   // a miss: catch up, then commit earlier
            g_target += g_T;
            g_lead += g_settled ? MARGIN / 2 : g_T / 32 + MARGIN;
            g_settled = true;
            g_skip = 6;
            n_moved++;
            return;
        }
    }
    if ((!g_settled || g_sweep) && ++g_streak >= 20) {
        g_lead -= g_T / 32, g_streak = 0;
        if (g_lead < -g_T * 2) g_settled = true;   // no deadline found in two refreshes: stay
    }
}

#ifdef HK_WITH_WAYLAND
wl_display *g_wd = nullptr;
wl_event_queue *g_q = nullptr;
wl_display *g_wrap = nullptr;
wl_registry *g_reg = nullptr;
wp_presentation *g_pres = nullptr;
wp_fifo_manager_v1 *g_fifo_mgr = nullptr;
wp_fifo_v1 *g_fifo = nullptr;   // holds each frame for its refreshes (fifo-v1), when the compositor has it
// The surface's content type, "game" (content-type-v1): the compositor may then treat it as one
// (KWin: direct scanout and adaptive sync are for fullscreen games)
wp_content_type_manager_v1 *g_ct_mgr = nullptr;
wp_content_type_v1 *g_ct = nullptr;
wl_surface *g_surface = nullptr;
clockid_t g_clock = CLOCK_MONOTONIC;
// Feedbacks not yet answered: destroyed before the queue, or the compositor's answer arriving
// after shutdown lands on a destroyed queue and libwayland aborts ("Tried to add event to destroyed queue").
std::unordered_set<struct wp_presentation_feedback *> g_live;

struct Pending {
    int64_t target;
    bool overrun;
    int k;
    int64_t committed;
};

int64_t clock_to_steady(int64_t t) {
    timespec ts{};
    clock_gettime(g_clock, &ts);
    return t - ((int64_t)ts.tv_sec * 1'000'000'000 + ts.tv_nsec - (int64_t)prof::now_ns());
}

const wp_presentation_feedback_listener feedback_listener = {
    [](void *, struct wp_presentation_feedback *, wl_output *) {},
    [](void *data, struct wp_presentation_feedback *fb, uint32_t sec_hi, uint32_t sec_lo, uint32_t nsec, uint32_t refresh,
       uint32_t, uint32_t, uint32_t flags) {
        auto *p = (Pending *)data;
        int64_t t = (int64_t)(((uint64_t)sec_hi << 32) | sec_lo) * 1'000'000'000 + nsec;
        n_shown++;
        if (flags & WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY) n_zero_copy++;
        if (p->k) presented(clock_to_steady(t), p->committed, p->target, p->overrun, refresh, p->k);   // k 0: observe()
        delete p;
        g_live.erase(fb);
        wp_presentation_feedback_destroy(fb);
    },
    [](void *data, struct wp_presentation_feedback *fb) {   // replaced before it was shown, or the window is hidden
        n_discarded++;
        g_prev_t = 0;
        delete (Pending *)data;
        g_live.erase(fb);
        wp_presentation_feedback_destroy(fb);
    },
};

const wp_presentation_listener presentation_listener = {
    [](void *, wp_presentation *, uint32_t clk_id) { g_clock = (clockid_t)clk_id; },
};

const wl_registry_listener registry_listener = {
    [](void *, wl_registry *reg, uint32_t name, const char *iface, uint32_t) {
        if (std::string(iface) == wp_presentation_interface.name) {
            g_pres = (wp_presentation *)wl_registry_bind(reg, name, &wp_presentation_interface, 1);
            wp_presentation_add_listener(g_pres, &presentation_listener, nullptr);
        } else if (std::string(iface) == wp_fifo_manager_v1_interface.name && !getenv("HK_PACING_NO_FIFO")) {
            g_fifo_mgr = (wp_fifo_manager_v1 *)wl_registry_bind(reg, name, &wp_fifo_manager_v1_interface, 1);
        } else if (std::string(iface) == wp_content_type_manager_v1_interface.name) {
            g_ct_mgr = (wp_content_type_manager_v1 *)wl_registry_bind(reg, name, &wp_content_type_manager_v1_interface, 1);
        }
    },
    [](void *, wl_registry *, uint32_t) {},
};

void wayland_init() {
    SDL_PropertiesID props = SDL_GetWindowProperties(g_win);
    g_wd = (wl_display *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    g_surface = (wl_surface *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    if (!g_wd || !g_surface) {
        g_wd = nullptr;
        return;
    }
    // our own queue, so SDL's event handling never sees these objects
    g_q = wl_display_create_queue(g_wd);
    g_wrap = (wl_display *)wl_proxy_create_wrapper(g_wd);
    wl_proxy_set_queue((wl_proxy *)g_wrap, g_q);
    g_reg = wl_display_get_registry(g_wrap);
    wl_registry_add_listener(g_reg, &registry_listener, nullptr);
    wl_display_roundtrip_queue(g_wd, g_q);   // the globals
    wl_display_roundtrip_queue(g_wd, g_q);   // the presentation clock
    if (g_enabled && g_fifo_mgr && g_pres) g_fifo = wp_fifo_manager_v1_get_fifo(g_fifo_mgr, g_surface);
    if (g_ct_mgr) {   // applies from the next commit (the first frame)
        g_ct = wp_content_type_manager_v1_get_surface_content_type(g_ct_mgr, g_surface);
        wp_content_type_v1_set_content_type(g_ct, WP_CONTENT_TYPE_V1_TYPE_GAME);
    }
}

void wayland_shutdown() {
    if (!g_wd) return;
    for (struct wp_presentation_feedback *fb : g_live) {
        delete (Pending *)wl_proxy_get_user_data((wl_proxy *)fb);
        wp_presentation_feedback_destroy(fb);
    }
    g_live.clear();
    if (g_ct) wp_content_type_v1_destroy(g_ct);
    if (g_ct_mgr) wp_content_type_manager_v1_destroy(g_ct_mgr);
    if (g_fifo) wp_fifo_v1_destroy(g_fifo);
    if (g_fifo_mgr) wp_fifo_manager_v1_destroy(g_fifo_mgr);
    if (g_pres) wp_presentation_destroy(g_pres);
    if (g_reg) wl_registry_destroy(g_reg);
    if (g_wrap) wl_proxy_wrapper_destroy(g_wrap);
    if (g_q) wl_event_queue_destroy(g_q);
    g_pres = nullptr, g_reg = nullptr, g_wrap = nullptr, g_q = nullptr, g_wd = nullptr, g_fifo = nullptr, g_fifo_mgr = nullptr;
    g_ct = nullptr, g_ct_mgr = nullptr;
}
#endif

}  // namespace

void init(SDL_Window *win, bool enabled) {
    g_win = win;
    g_enabled = enabled;
    g_samples.assign(512, 0);
    if (const char *s = getenv("HK_DISPLAY_SYNC_SKEW")) g_max_skew = atof(s);
    refresh_from_mode();
#ifdef HK_WITH_WAYLAND
    wayland_init();   // with display sync off too: the content type, and whether frames go out directly
#endif
    if (!enabled) announce("disabled", "off (--display-sync off): frames are shown as they come");
}

void shutdown() {
#ifdef HK_WITH_WAYLAND
    wayland_shutdown();
#endif
    g_win = nullptr;
}

void display_changed() {
    g_T_exact = false;
    refresh_from_mode();
    g_fresh = true;
    g_lead_set = false;   // find this display's deadline again
    g_settled = false;
    g_skip = 5, g_prev_t = 0, g_best = 0;
    g_window.clear();
}

void pump() {
#ifdef HK_WITH_WAYLAND
    if (!g_wd || !g_pres) return;
    while (wl_display_prepare_read_queue(g_wd, g_q) != 0) wl_display_dispatch_queue_pending(g_wd, g_q);
    wl_display_flush(g_wd);
    pollfd p{wl_display_get_fd(g_wd), POLLIN, 0};
    if (::poll(&p, 1, 0) > 0) wl_display_read_events(g_wd);
    else wl_display_cancel_read(g_wd);
    wl_display_dispatch_queue_pending(g_wd, g_q);
#endif
}

bool frame(double period) {
    if (!g_enabled) return false;
    pump();
    if (period <= 0.001 || period > 0.2) {   // the first frame, or a pause: keep the decision
        g_fresh = true;
        return g_active;
    }
    if (!g_T_exact) refresh_from_mode();
    char b[256];
    if (g_T <= 0) {
        g_active = false;
        announce("unknown", "off: the display's refresh rate is unknown");
        return false;
    }
    double hz = 1e9 / (double)g_T;
    int k = std::max(1, (int)std::lround(period * 1e9 / (double)g_T));
    double f = period * 1e9 / ((double)k * (double)g_T);
    g_game_hz = g_game_hz > 0 ? g_game_hz + (1.0 / period - g_game_hz) / 64 : 1.0 / period;
    if (std::fabs(f - 1.0) > g_max_skew) {
        if (g_active) g_active = false;
        g_fresh = true;
        g_speed = 1.0;
        snprintf(b, sizeof b,
                 "off: the %.2f Hz display doesn't fit the game's %.2f Hz, so frames are shown as they come and "
                 "scrolling judders (60, 120 or 240 Hz fit; a variable-refresh display doesn't need this)",
                 hz, g_game_hz);
        announce("off " + std::to_string(std::lround(hz * 100)), b);
        return false;
    }
    g_active = true;
    g_k = k;
    g_speed = 1e9 / (g_game_hz * (double)k * (double)g_T);   // smoothed: the sound's rate shouldn't flutter
    // said again when the game changes its video mode (pop'n boots at 59.94 Hz, then runs at 58.49)
    snprintf(b, sizeof b, "on: %.2f Hz display, %d refresh%s per frame, the game's %.2f Hz at %+.1f %% speed", hz, k,
             k == 1 ? "" : "es", 1.0 / period, (1e9 / ((double)k * (double)g_T) * period - 1.0) * 100.0);
    announce("on " + std::to_string(std::lround(hz * 100)) + " " + std::to_string(k) + " " +
                 std::to_string(std::lround(10.0 / period)), b);

    const int64_t now = (int64_t)prof::now_ns();
    if (!g_lead_set) g_lead = g_T / 2, g_lead_set = true;   // then later and later (presented())
    if (g_fresh) {
        // the first vblank this frame can make, on the grid of the last one seen
        int64_t t = g_target > 0 ? g_target : now;
        int64_t first = now + MS + g_lead;
        if (t < first) t += ((first - t + g_T - 1) / g_T) * g_T;
        g_target = t;
        g_fresh = false;
    } else {
        g_target += (int64_t)k * g_T;
        if (g_target - g_lead < now - (int64_t)k * g_T / 2) {   // far behind (a stall): rejoin the grid
            int64_t first = now + MS + g_lead;
            g_target += ((first - g_target + g_T - 1) / g_T) * g_T;
            n_resync++;
        }
    }
    n_frames++;
    return true;
}

bool active() { return g_active; }
double refresh_hz() { return g_T > 0 ? 1e9 / (double)g_T : 0.0; }
double speed() { return g_active ? g_speed : 1.0; }

void wait_commit() {
    const int64_t now = (int64_t)prof::now_ns();
    if (g_burst_start) {
        g_samples[g_sample_at++ % g_samples.size()] = now - g_burst_start;
        if (g_sample_at % 64 == 0) {   // the burst's budget: the 99th percentile of recent ones, plus 1 ms
            std::vector<int64_t> s(g_samples.begin(), g_samples.begin() + (long)std::min(g_sample_at, g_samples.size()));
            std::nth_element(s.begin(), s.begin() + (long)(s.size() * 99 / 100), s.end());
            g_budget = std::clamp(s[s.size() * 99 / 100] + MS, MS, (int64_t)g_k * g_T);
        }
    }
    const int64_t at = g_target - g_lead;
    g_overrun = now > at + MS / 5;
    if (g_overrun) n_overrun++;
    sleep_until(at);
}

void commit() {
#ifdef HK_WITH_WAYLAND
    if (g_pres) {
        struct wp_presentation_feedback *fb = wp_presentation_feedback(g_pres, g_surface);
        wp_presentation_feedback_add_listener(fb, &feedback_listener, new Pending{g_target, g_overrun, g_k, (int64_t)prof::now_ns()});
        g_live.insert(fb);
    }
    if (g_fifo) {   // shown no sooner than the previous frame's last refresh, and for one at least
        wp_fifo_v1_set_barrier(g_fifo);
        wp_fifo_v1_wait_barrier(g_fifo);
    }
#endif
}

void committed() {
#ifdef HK_WITH_WAYLAND
    if (!g_fifo) return;
    for (int i = 1; i < g_k; i++) {   // the frame's other refreshes: empty updates, one refresh each
        wp_fifo_v1_set_barrier(g_fifo);
        wp_fifo_v1_wait_barrier(g_fifo);
        wl_surface_commit(g_surface);
    }
    wl_display_flush(g_wd);
#endif
}

void wait_next() {
    const int64_t start = g_target + (int64_t)g_k * g_T - g_lead - g_budget;
    sleep_until(start);
    g_burst_start = std::max(start, (int64_t)prof::now_ns());
}

void observe() {
#ifdef HK_WITH_WAYLAND
    if (!g_pres) return;
    pump();
    struct wp_presentation_feedback *fb = wp_presentation_feedback(g_pres, g_surface);
    wp_presentation_feedback_add_listener(fb, &feedback_listener, new Pending{0, false, 0, (int64_t)prof::now_ns()});
    g_live.insert(fb);
#endif
}

std::string scanout_describe() {
    if (!n_shown) return {};
    char b[320];
    snprintf(b, sizeof b, "video: %llu frames shown, %llu of them straight from HIRAKU's picture (direct scanout, not composited)%s",
             (unsigned long long)n_shown, (unsigned long long)n_zero_copy,
             n_zero_copy * 2 < n_shown ? "; the compositor scans a window out directly when it is fullscreen (F11) with nothing over it" : "");
    return b;
}

std::string describe() {
    char b[512];
    if (!g_enabled) return "video: display sync off (--display-sync off)";
    if (!n_frames) return "video: display sync " + (g_state.empty() ? std::string("not used") : g_state);
    snprintf(b, sizeof b,
             "video: display sync %s%s, %llu frames on the %.3f Hz %s, the game at %+.2f %% speed; %llu presented: %llu held their %d refreshes, "
             "%llu irregular, %llu replaced; %llu not ready in time, %llu re-synced; commit to screen %.1f ms at best, "
             "lead %.1f ms%s (moved %llu times), burst budget %.1f ms",
             g_active ? "on" : "was on",
#ifdef HK_WITH_WAYLAND
             g_fifo ? " (fifo)" : "",
#else
             "",
#endif
             (unsigned long long)n_frames, g_T ? 1e9 / (double)g_T : 0.0,
             g_T_exact ? "refresh (from the compositor)" : "display mode (phase unknown)", (g_speed - 1.0) * 100.0, (unsigned long long)n_presented,
             (unsigned long long)n_on_time, g_k, (unsigned long long)n_late,
             (unsigned long long)n_discarded, (unsigned long long)n_overrun, (unsigned long long)n_resync,
             (double)g_best / MS, (double)g_lead / MS, g_settled ? " (settled)" : "", (unsigned long long)n_moved,
             (double)g_budget / MS);
    return b;
}

}  // namespace hk::host::pacing
