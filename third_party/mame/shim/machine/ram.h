// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's ram_device (machine/ram.h), configuration and storage only.
#pragma once

#include "emu.h"

#include <unistd.h>

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
    u8 *pointer() { allocate(); return m_data.get(); }
    template <typename T> T *pointer() { allocate(); return reinterpret_cast<T *>(m_data.get()); }
    u8 read(offs_t o) { return pointer()[o % m_size]; }
    void write(offs_t o, u8 d) { pointer()[o % m_size] = d; }

protected:
    void device_start() override { allocate(); }

private:
    // aligned to the host's pages (4 KiB on x86-64, 16 KiB on some ARM hosts), so the 573's
    // recompiled blocks can write-protect the pages holding code (runtime/sys573/mips.cpp)
    void allocate() {
        if (m_data) return;
        const size_t page = (size_t)sysconf(_SC_PAGESIZE);
        const size_t n = ((size_t)m_size + page - 1) & ~(page - 1);
        m_data.reset(static_cast<u8 *>(aligned_alloc(page, n ? n : page)));
        memset(m_data.get(), m_value, n);
    }
    struct freer { void operator()(u8 *p) const { free(p); } };

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
    std::unique_ptr<u8, freer> m_data;
};

DECLARE_DEVICE_TYPE(RAM, ram_device)
