// license:BSD-3-Clause
// copyright-holders:Raphael Nabet, R. Belmont
//
// Epson RTC-65271 real-time clock + 4 KiB battery-backed extended RAM.
// Ported to HIRAKU from MAME's rtc65271.cpp (BSD-3-Clause).
//
// The clock runs on virtual time and starts from a fixed date unless told
// otherwise, so runs are reproducible (MAME seeds it from the host clock).
#pragma once

#include <cstdint>
#include <string>

namespace hk {

class Rtc65271 {
public:
    Rtc65271();
    // NVRAM image in MAME's .nv format (4152 bytes; the ROM sets ship a default as "rtc").
    bool load_nvram(const uint8_t *data, size_t size);
    std::string save_nvram() const;
    void set_time(int year, int month, int day, int weekday, int hour, int minute, int second);

    uint8_t read(bool xram, uint32_t offset);
    void write(bool xram, uint32_t offset, uint8_t data);

    // driven by the once-per-second update timers
    void begin_update();
    void end_update();

private:
    void field_interrupts();

    uint8_t m_regs[64]{};
    uint8_t m_cur_reg = 0;
    uint8_t m_xram[4096]{};
    uint8_t m_cur_xram_page = 0;
};

}  // namespace hk
