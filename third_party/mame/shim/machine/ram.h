// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's ram_device (machine/ram.h), configuration and storage only.
#pragma once

#include "emu.h"

class ram_device : public device_t {
public:
    ram_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
    ram_device &set_default_size(const char *s) { m_size = parse(s); return *this; }
    ram_device &set_extra_options(const char *) { return *this; }
    ram_device &set_default_value(u8 v) { m_value = v; return *this; }
    ram_device &set_bits(int) { return *this; }
    u32 size() const { return m_size; }
    u32 mask() const { return m_size - 1; }
    // allocated on first use, as MAME (devices take the pointer in their own device_start)
    u8 *pointer() { allocate(); return m_data.data(); }
    template <typename T> T *pointer() { allocate(); return reinterpret_cast<T *>(m_data.data()); }
    u8 read(offs_t o) { return pointer()[o % m_size]; }
    void write(offs_t o, u8 d) { pointer()[o % m_size] = d; }

protected:
    void device_start() override { allocate(); }

private:
    void allocate() { if (m_data.empty()) m_data.assign(m_size, m_value); }

public:

private:
    static u32 parse(const char *s) {
        char *end;
        unsigned long v = strtoul(s, &end, 10);
        if (*end == 'K' || *end == 'k') v <<= 10;
        else if (*end == 'M' || *end == 'm') v <<= 20;
        return (u32)v;
    }
    u32 m_size = 0;
    u8 m_value = 0;
    std::vector<u8> m_data;
};

DECLARE_DEVICE_TYPE(RAM, ram_device)
