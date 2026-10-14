// SPDX-License-Identifier: GPL-3.0-or-later
// Virtual time. One tick = one PPC403 clock (66 MHz). Every instruction is
// charged what MAME charges it (src/ppc/cycles.h: most 1, folded branches 0,
// multiply 4, divide 33): the interpreter per instruction, recompiled code per
// straight-line run (HK_TICK).
// Devices schedule events on this clock, so a run is deterministic: the same
// inputs at the same ticks give the same machine.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace hk {

constexpr uint64_t CPU_HZ = 66'000'000;

inline uint64_t ticks_from_hz(double hz) { return (uint64_t)((double)CPU_HZ / hz + 0.5); }
inline uint64_t ticks_from_us(double us) { return (uint64_t)((double)CPU_HZ * us / 1e6 + 0.5); }

using EventFn = std::function<void()>;

struct Event {
    uint64_t when = UINT64_MAX;
    EventFn fn;
    std::string name;
};

// A timer handle owned by a device. Re-arming replaces the pending expiry.
class Timer {
public:
    Timer(std::string name, EventFn fn);
    ~Timer();
    void at(uint64_t when);          // absolute tick
    void in(uint64_t ticks);         // relative to now
    void cancel();
    bool armed() const;
    uint64_t when() const;

private:
    int slot_;
};

uint64_t now();                      // current virtual tick (includes ticks consumed since the last sync)
void charge(uint64_t ticks);         // account work outside the poll mechanism (e.g. DMA)
void sched_sync();                   // fold consumed budget into now() and run due events
void sched_reset();
// Let `ticks` of time pass with the main CPU idle: every device event in that
// span runs, in order. Used for the BIOS time HIRAKU skips (other boards,
// like the SPU, boot during it).
void sched_idle(uint64_t ticks);

// Called when something may have made an interrupt deliverable: forces the
// next poll / interpreter step to look.
void sched_kick();

}  // namespace hk
