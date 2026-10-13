// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: stand-in for MAME's machine/pc_lpt.h, the FDC37C665GT's parallel port. Twinkle
// never touches it: reads give 0 and any access is reported once.
#pragma once
#include "emu.h"

DECLARE_DEVICE_TYPE(PC_LPT, pc_lpt_device)
class pc_lpt_device : public device_t {
public:
    pc_lpt_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
    auto irq_handler() { return m_irq_handler.bind(); }
    u8 read(offs_t) { return touched(); }
    void write(offs_t, u8) { touched(); }
private:
    u8 touched();
    devcb_write_line m_irq_handler;
    bool m_reported = false;
};
