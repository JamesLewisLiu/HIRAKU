// SPDX-License-Identifier: GPL-3.0-or-later
#include "sched.h"

#include <algorithm>
#include <vector>

#include "hiraku/recomp.h"   // hk_budget

// The poll budget: ticks left before the scheduler must look again. Generated
// code and the interpreter subtract from it; at <= 0 they call into the runtime.
int32_t hk_budget = 0;

namespace hk {

namespace {
// Indexed by Timer slot. Constructed on first use: device Timers are often
// globals in other files, created during static initialisation.
std::vector<Event> &events() {
    static std::vector<Event> v;
    return v;
}
#define g_events (events())
uint64_t g_base = 0;           // virtual time at the last sync
int32_t g_granted = 0;         // budget handed out at the last sync

uint64_t next_due() {
    uint64_t m = UINT64_MAX;
    for (auto &e : g_events) m = std::min(m, e.when);
    return m;
}

// fold the budget consumed since the last grant into the base time
void fold() {
    g_base = g_base + (uint64_t)(int64_t)(g_granted - hk_budget);
    g_granted = 0;
    hk_budget = 0;
}

void regrant() {
    uint64_t due = next_due();
    uint64_t d = due > g_base ? due - g_base : 0;
    // cap the quantum so syncs stay frequent enough for host-side work
    g_granted = (int32_t)std::min<uint64_t>(d, 1u << 20);
    hk_budget = g_granted;
}
}  // namespace

Timer::Timer(std::string name, EventFn fn) {
    slot_ = (int)g_events.size();
    g_events.push_back({UINT64_MAX, std::move(fn), std::move(name)});
}
Timer::~Timer() { g_events[slot_].when = UINT64_MAX; }

void Timer::at(uint64_t when) {
    fold();                       // never runs other events: safe inside an MMIO access
    g_events[slot_].when = std::max(when, g_base);
    regrant();
}
void Timer::in(uint64_t ticks) { at(now() + ticks); }
void Timer::again(uint64_t period) { at(g_events[slot_].fired + period); }
void Timer::cancel() { g_events[slot_].when = UINT64_MAX; }
bool Timer::armed() const { return g_events[slot_].when != UINT64_MAX; }
uint64_t Timer::when() const { return g_events[slot_].when; }

uint64_t now() { return g_base + (uint64_t)(int64_t)(g_granted - hk_budget); }

void charge(uint64_t ticks) { hk_budget -= (int32_t)std::min<uint64_t>(ticks, 1u << 30); }

void sched_sync() {
    fold();
    // run everything that is due, in time order (events may re-arm themselves)
    for (;;) {
        int best = -1;
        uint64_t bw = UINT64_MAX;
        for (size_t k = 0; k < g_events.size(); k++)
            if (g_events[k].when <= g_base && g_events[k].when < bw) { bw = g_events[k].when; best = (int)k; }
        if (best < 0) break;
        g_events[best].when = UINT64_MAX;
        g_events[best].fired = bw;
        g_events[best].fn();
    }
    regrant();
}

void sched_reset() {
    for (auto &e : g_events) e.when = UINT64_MAX;
    g_base = 0;
    g_granted = 0;
    hk_budget = 0;
}

void sched_idle(uint64_t ticks) {
    fold();
    const uint64_t end = g_base + ticks;
    for (;;) {
        uint64_t due = next_due();
        if (due > end) break;
        g_base = std::max(g_base, due);
        sched_sync();                 // runs everything due at g_base
        fold();
    }
    g_base = end;
    regrant();
}

void sched_kick() {
    fold();                       // the next poll / interpreter step syncs immediately
}

}  // namespace hk
