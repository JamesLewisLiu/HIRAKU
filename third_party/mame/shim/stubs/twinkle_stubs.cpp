// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: definitions behind the Twinkle stand-ins (machine/upd765.h, machine/pc_lpt.h) and the
// 68000's MMU handler tables.
#include "emu.h"
#include "machine/upd765.h"
#include "machine/pc_lpt.h"
#include "cpu/m68000/m68000.h"

DEFINE_DEVICE_TYPE(N82077AA, n82077aa_device, "n82077aa", "N82077AA (HIRAKU stand-in)")
DEFINE_DEVICE_TYPE(PC_LPT, pc_lpt_device, "pc_lpt", "PC LPT (HIRAKU stand-in)")

u8 upd765_family_device::touched() {
    if (!m_reported) {
        fprintf(stderr, "hiraku: %s: the floppy controller was accessed; HIRAKU has a stand-in (reads 0)\n", tag());
        m_reported = true;
    }
    return 0;
}

pc_lpt_device::pc_lpt_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
    : device_t(mconfig, PC_LPT, tag, owner, clock), m_irq_handler(*this) {}

u8 pc_lpt_device::touched() {
    if (!m_reported) {
        fprintf(stderr, "hiraku: %s: the parallel port was accessed; HIRAKU has a stand-in (reads 0)\n", tag());
        m_reported = true;
    }
    return 0;
}

// m68000-sif.cpp and m68000-sip.cpp (the handlers through an MMU) aren't built: no 68000 here
// has one (device_start picks these tables only with an MMU)
const m68000_device::handler m68000_device::s_handlers_if[] = { nullptr };
const m68000_device::handler m68000_device::s_handlers_ip[] = { nullptr };
