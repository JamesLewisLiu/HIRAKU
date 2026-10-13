// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's device_rom_interface (dirom.h), the part the RF5C400 uses: a device reading
// its sample memory through its own address space 0, which the machine configures with
// set_addrmap(0, ...) (Twinkle's wave RAM). MAME's region-backed form isn't needed.
#pragma once
#include "emu.h"

template <int AddrWidth, int DataWidth = 0, int AddrShift = 0, endianness_t Endian = ENDIANNESS_LITTLE>
class device_rom_interface : public device_interface {
public:
    device_rom_interface(const machine_config &, device_t &device)
        : device_interface(device, "rom"),
          m_rom_config("rom", Endian, 8 << DataWidth, AddrWidth, AddrShift) {
        device.m_hk_interface_spaces.emplace_back(0, &m_rom_config);
    }
    u8 read_byte(offs_t addr) { return m_rom_cache.read_byte(addr); }
    u16 read_word(offs_t addr) { return m_rom_cache.read_word(addr); }
    u32 read_dword(offs_t addr) { return m_rom_cache.read_dword(addr); }
    u64 read_qword(offs_t addr) { return m_rom_cache.read_qword(addr); }
    void set_rom_bank(int) { rom_bank_pre_change(); rom_bank_post_change(); }

protected:
    typename memory_access<AddrWidth, DataWidth, AddrShift, Endian>::cache m_rom_cache;
    virtual void rom_bank_pre_change() {}
    virtual void rom_bank_post_change() {}
    // MAME's interface_pre_start: the space exists (and its shares are registered) before the
    // devices start
    void interface_pre_start() override { device().space(0).cache(m_rom_cache); }

private:
    address_space_config m_rom_config;
};
