// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: stand-in for MAME's machine/upd765.h, for the FDC37C665GT's floppy controller.
// Twinkle never touches it (no access to its ports in MAME's runs): every register reads
// as MAME's unmapped default, 0, and any access is reported once.
#pragma once
#include "emu.h"

class upd765_family_device : public device_t {
public:
    enum class mode_t { AT, PS2, M30 };
    upd765_family_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
        : device_t(mconfig, type, tag, owner, clock), m_intrq_cb(*this), m_drq_cb(*this) {}
    auto intrq_wr_callback() { return m_intrq_cb.bind(); }
    auto drq_wr_callback() { return m_drq_cb.bind(); }
    void set_mode(mode_t) {}
    u8 sra_r() { return touched(); }
    u8 srb_r() { return touched(); }
    u8 dor_r() { return touched(); }
    u8 tdr_r() { return touched(); }
    u8 msr_r() { return touched(); }
    u8 fifo_r() { return touched(); }
    u8 dir_r() { return touched(); }
    void dor_w(u8) { touched(); }
    void tdr_w(u8) { touched(); }
    void dsr_w(u8) { touched(); }
    void fifo_w(u8) { touched(); }
    void ccr_w(u8) { touched(); }
    u8 dma_r() { return touched(); }
    void dma_w(u8) { touched(); }
    void tc_w(int) {}
private:
    u8 touched();
    devcb_write_line m_intrq_cb, m_drq_cb;
    bool m_reported = false;
};

DECLARE_DEVICE_TYPE(N82077AA, n82077aa_device)
class n82077aa_device : public upd765_family_device {
public:
    n82077aa_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock, mode_t)
        : upd765_family_device(mconfig, N82077AA, tag, owner, clock) {}
};
