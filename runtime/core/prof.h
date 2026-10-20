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

enum Cat { DISK, DRAW, PRESENT, SPU68K, GLUE, NCAT };
inline const char *const NAMES[NCAT] = {"disk", "draw", "present", "68000", "glue"};

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

struct Scope {
    Cat c;
    uint64_t t0;
    explicit Scope(Cat cat) : c(cat), t0(ticks()) {}
    ~Scope() {
        const uint64_t d = ticks() - t0;
        g_ticks[c] += d;
        g_total[c] += d;
    }
};

inline void reset() {
    for (auto &v : g_ticks) v = 0;
}

}  // namespace hk::prof
