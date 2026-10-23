// SPDX-License-Identifier: GPL-3.0-or-later
// Wall-clock accounting of host work by category, to find what stalls the
// real-time loop (audio underruns). Cheap enough to leave on: scopes read the
// CPU's cycle counter on x86-64 (a few ns; a clock_gettime per guest exception
// cost ~2 % of a run), converted to nanoseconds only when reported.
#pragma once

#include <chrono>
#include <cstdint>
#if defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace hk::prof {

// CPU: the board's main CPU as MAME's scheduler runs it (System 573, Twinkle: the R3000); 68000:
// a sound board's CPU (Firebeat's SPU, Twinkle's); GPU: the PS1 GPU's commands and scanout on the
// emulation thread (its drawing thread's own work not, its waits for it yes); DEVICES: MAME's
// timers (the devices' work between the CPUs' turns)
enum Cat { DISK, DRAW, PRESENT, SPU68K, GLUE, CPU, GPU, DEVICES, NCAT };
inline const char *const NAMES[NCAT] = {"disk", "draw", "present", "68000", "glue", "cpu", "gpu", "devices"};

inline uint64_t g_ticks[NCAT];   // accumulated since the last reset, in ticks()
inline uint64_t g_total[NCAT];   // accumulated since start (never reset)

// HK_PROFILE's sampler (runtime/core/sampler.cpp), told each frame's number so that with
// HK_PROFILE_FRAMES=A-B,C-D it samples only those frames' work; null without it
inline void (*g_sample_frame)(uint64_t frame) = nullptr;

inline uint64_t now_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

#if defined(__x86_64__)
inline uint64_t ticks() { return __rdtsc(); }
// Nanoseconds per tick, measured against the steady clock since first use
// (the TSC runs at a constant rate on every x86-64 CPU HIRAKU targets).
inline double ns_per_tick() {
    static const uint64_t t0 = ticks(), n0 = now_ns();
    const uint64_t dt = ticks() - t0, dn = now_ns() - n0;
    return dt > 1'000'000 ? (double)dn / (double)dt : 0.3;   // ~3 GHz until measured
}
#elif defined(__aarch64__)
// The generic timer's virtual count, readable from user space on every AArch64 OS
inline uint64_t ticks() {
    uint64_t t;
    asm volatile("mrs %0, cntvct_el0" : "=r"(t));
    return t;
}
inline double ns_per_tick() {
    static const double r = [] {
        uint64_t f;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(f));
        return f ? 1e9 / (double)f : 1.0;
    }();
    return r;
}
#else
inline uint64_t ticks() { return now_ns(); }
inline double ns_per_tick() { return 1.0; }
#endif

inline double ns(Cat c) { return (double)g_ticks[c] * ns_per_tick(); }

// Time the host spent waiting on purpose (audio pacing), in ticks(): the
// frame-time report subtracts it, leaving the work per frame.
inline uint64_t g_wait_ticks;

// A scope's time counts once: one inside another (a GPU write in the CPU's turn) pauses the
// outer one's count until it ends. Per thread.
inline thread_local int g_open = -1;          // the innermost scope's category, -1: none
inline thread_local uint64_t g_open_since;    // ... counted from

struct Scope {
    int outer;
    explicit Scope(Cat c) : outer(g_open) {
        const uint64_t t = ticks();
        if (outer >= 0) g_ticks[outer] += t - g_open_since, g_total[outer] += t - g_open_since;
        g_open = c, g_open_since = t;
    }
    ~Scope() {
        const uint64_t t = ticks(), d = t - g_open_since;
        g_ticks[g_open] += d;
        g_total[g_open] += d;
        g_open = outer, g_open_since = t;
    }
};

inline void reset() {
    for (auto &v : g_ticks) v = 0;
}

}  // namespace hk::prof
