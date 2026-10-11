// SPDX-License-Identifier: GPL-3.0-or-later
// PPC403GCX on-chip peripherals: external interrupt controller, timers
// (PIT/FIT/watchdog, time base), the serial port and DMA. Semantics follow
// MAME's ppccom.cpp (BSD-3-Clause, Aaron Giles et al.).
#pragma once

#include <cstdint>
#include <functional>

#include "hiraku/ppc.h"

namespace hk::cpu403 {

// EXISR / EXIER bits (IBM numbering: bit 0 = MSB)
constexpr uint32_t IRQ_CRITICAL = 0x80000000u >> 0;
constexpr uint32_t IRQ_SPUR = 0x80000000u >> 4;
constexpr uint32_t IRQ_SPUT = 0x80000000u >> 5;
constexpr uint32_t IRQ_DMA0 = 0x80000000u >> 8;
constexpr uint32_t IRQ_EXT0 = 0x80000000u >> 27;   // + n for EXT0..EXT4
inline constexpr uint32_t irq_ext(int n) { return IRQ_EXT0 >> n; }

// TCR / TSR
constexpr uint32_t TCR_WIE = 0x08000000, TCR_PIE = 0x04000000, TCR_FP = 0x03000000, TCR_FIE = 0x00800000, TCR_ARE = 0x00400000;
constexpr uint32_t TSR_ENW = 0x80000000, TSR_WIS = 0x40000000, TSR_PIS = 0x08000000, TSR_FIS = 0x04000000;

void reset();

// Drive an interrupt input (external pins EXT0..4, or internal sources).
void set_irq_line(uint32_t bit, bool asserted);

// True when an interrupt could be taken, ignoring MSR.EE.
bool irq_pending();
// The vector the 4xx would take for the pending interrupt (0x500/0x1000/0x1010), or 0.
uint32_t pending_vector();

// SPR/DCR side effects (called from hk_mtspr/hk_mtdcr/hk_mfspr)
void write_tcr(uint32_t v);
void write_tsr(uint32_t v);
void write_pit(uint32_t v);
uint32_t read_pit();
void dcr_written(uint32_t dcr, uint32_t v);

// On-chip serial port, mapped at 0x40000000-0x4000000f.
uint8_t spu_read(uint32_t offset);
void spu_write(uint32_t offset, uint8_t data);
void spu_set_tx_handler(std::function<void(uint8_t)> fn);
void spu_receive(uint8_t byte);     // a byte arriving from the outside (e.g. the dongle)

}  // namespace hk::cpu403
