// SPDX-License-Identifier: GPL-3.0-or-later
// Wall-clock accounting of host work by category, to find what stalls the
// real-time loop (audio underruns). Cheap enough to leave on.
#pragma once

#include <chrono>
#include <cstdint>

namespace hk::prof {

enum Cat { DISK, DRAW, PRESENT, SPU68K, GLUE, NCAT };
inline const char *const NAMES[NCAT] = {"disk", "draw", "present", "68000", "glue"};

inline uint64_t g_ns[NCAT];   // accumulated since the last reset

inline uint64_t now_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Scope {
    Cat c;
    uint64_t t0;
    explicit Scope(Cat cat) : c(cat), t0(now_ns()) {}
    ~Scope() { g_ns[c] += now_ns() - t0; }
};

inline void reset() {
    for (auto &v : g_ns) v = 0;
}

}  // namespace hk::prof
