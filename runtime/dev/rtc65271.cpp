// license:BSD-3-Clause
// copyright-holders:Raphael Nabet, R. Belmont
//
// Ported from MAME's rtc65271.cpp. Modified for HIRAKU: plain C++, timers on
// HIRAKU's virtual clock, square-wave/periodic interrupt not implemented (the
// Firebeat does not wire the RTC interrupt).
#include "rtc65271.h"

#include <cstring>

#include "../core/sched.h"

namespace hk {

namespace {
enum { reg_second = 0, reg_alarm_second, reg_minute, reg_alarm_minute, reg_hour, reg_alarm_hour,
       reg_weekday, reg_monthday, reg_month, reg_year, reg_A, reg_B, reg_C, reg_D };
enum : uint8_t {
    reg_A_UIP = 0x80, reg_A_DV = 0x70, reg_A_RS = 0x0f,
    reg_B_SET = 0x80, reg_B_PIE = 0x40, reg_B_AIE = 0x20, reg_B_UIE = 0x10, reg_B_SQW = 0x08,
    reg_B_DM = 0x04, reg_B_24h = 0x02, reg_B_DSE = 0x01,
    reg_C_IRQF = 0x80, reg_C_PF = 0x40, reg_C_AF = 0x20, reg_C_UF = 0x10,
    reg_D_VRT = 0x80
};
uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0xf)); }
uint8_t inc_bin(uint8_t v) { return v + 1; }
uint8_t inc_bcd(uint8_t v) {
    if ((v & 0x0f) < 9) return v + 1;
    return (uint8_t)((v & 0xf0) + 0x10);
}

Rtc65271 *g_rtc = nullptr;   // one RTC per board
Timer g_begin("rtc-begin", [] { if (g_rtc) g_rtc->begin_update(); });
Timer g_end("rtc-end", [] { if (g_rtc) g_rtc->end_update(); });
}  // namespace

Rtc65271::Rtc65271() {
    m_regs[reg_B] |= reg_B_DM;   // Firebeat assumes the chip defaults to binary mode
    g_rtc = this;
    g_begin.in(CPU_HZ);
}

bool Rtc65271::load_nvram(const uint8_t *p, size_t n) {
    if (n < 6 + 50 + 4096 || p[0] != 0) return false;
    m_regs[reg_A] = p[1] & reg_A_DV;
    m_regs[reg_B] = p[2] & (reg_B_SET | reg_B_DM | reg_B_24h | reg_B_DSE);
    m_regs[reg_alarm_second] = p[3];
    m_regs[reg_alarm_minute] = p[4];
    m_regs[reg_alarm_hour] = p[5];
    memcpy(m_regs + 14, p + 6, 50);
    memcpy(m_xram, p + 56, 4096);
    m_regs[reg_D] |= reg_D_VRT;
    return true;
}

std::string Rtc65271::save_nvram() const {
    std::string s(6 + 50 + 4096, '\0');
    s[1] = (char)(m_regs[reg_A] & (reg_A_DV | reg_A_RS));
    s[2] = (char)(m_regs[reg_B] & (reg_B_SET | reg_B_DM | reg_B_24h | reg_B_DSE));
    s[3] = (char)m_regs[reg_alarm_second];
    s[4] = (char)m_regs[reg_alarm_minute];
    s[5] = (char)m_regs[reg_alarm_hour];
    memcpy(&s[6], m_regs + 14, 50);
    memcpy(&s[56], m_xram, 4096);
    return s;
}

void Rtc65271::set_time(int year, int month, int day, int weekday, int hour, int minute, int second) {
    m_regs[reg_second] = second;
    m_regs[reg_minute] = minute;
    if (m_regs[reg_B] & reg_B_24h) {
        m_regs[reg_hour] = hour;
    } else {
        // Firebeat: non-BCD 12-hour mode is 0-based (12 AM = 0x00, 12 PM = 0x80)
        m_regs[reg_hour] = (hour >= 12 ? 0x80 : 0) | (hour % 12);
    }
    m_regs[reg_weekday] = weekday;
    m_regs[reg_monthday] = day;
    m_regs[reg_month] = month;
    m_regs[reg_year] = year % 100;
    if (!(m_regs[reg_B] & reg_B_DM)) {
        m_regs[reg_second] = bin2bcd(m_regs[reg_second]);
        m_regs[reg_minute] = bin2bcd(m_regs[reg_minute]);
        m_regs[reg_hour] = (m_regs[reg_hour] & 0x80) | bin2bcd(m_regs[reg_hour] & 0x7f);
        m_regs[reg_monthday] = bin2bcd(m_regs[reg_monthday]);
        m_regs[reg_month] = bin2bcd(m_regs[reg_month]);
        m_regs[reg_year] = bin2bcd(m_regs[reg_year]);
    }
}

uint8_t Rtc65271::read(bool xram, uint32_t offset) {
    if (xram) return (offset & 0x20) ? m_cur_xram_page : m_xram[(offset & 0x1f) + 0x20 * m_cur_xram_page];
    if (!(offset & 1)) return m_cur_reg;
    uint8_t reply;
    switch (m_cur_reg) {
    case reg_A:
        reply = (m_regs[reg_A] & ~reg_A_DV) | 0x20;   // normal RTC operation
        break;
    case reg_C:
        reply = m_regs[reg_C];
        m_regs[reg_C] = 0;
        field_interrupts();
        break;
    case reg_D:
        reply = m_regs[reg_D];
        m_regs[reg_D] = reg_D_VRT;   // battery OK
        break;
    default:
        reply = m_regs[m_cur_reg];
        break;
    }
    return reply;
}

void Rtc65271::write(bool xram, uint32_t offset, uint8_t data) {
    if (xram) {
        if (offset & 0x20) m_cur_xram_page = data & 0x7f;
        else m_xram[(offset & 0x1f) + 0x20 * m_cur_xram_page] = data;
        return;
    }
    if (!(offset & 1)) {
        m_cur_reg = data & 0x3f;
        return;
    }
    switch (m_cur_reg) {
    case reg_second: m_regs[reg_second] = data & 0x7f; break;
    case reg_A: m_regs[reg_A] = (data & ~reg_A_UIP) | (m_regs[reg_A] & reg_A_UIP); break;
    case reg_B:
        m_regs[reg_B] = data;
        if (data & reg_B_SET) {
            m_regs[reg_A] &= ~reg_A_UIP;
            m_regs[reg_B] &= ~reg_B_UIE;
            field_interrupts();
        }
        break;
    case reg_C:
    case reg_D: break;
    default: m_regs[m_cur_reg] = data; break;
    }
}

void Rtc65271::field_interrupts() {
    if (m_regs[reg_C] & m_regs[reg_B] & (reg_C_PF | reg_C_AF | reg_C_UF)) m_regs[reg_C] |= reg_C_IRQF;
    else m_regs[reg_C] &= ~reg_C_IRQF;
}

void Rtc65271::begin_update() {
    if (((m_regs[reg_A] & reg_A_DV) == 0x20) && !(m_regs[reg_B] & reg_B_SET)) {
        m_regs[reg_A] |= reg_A_UIP;
        g_end.in(ticks_from_us(1984));
    }
    g_begin.in(CPU_HZ);
}

void Rtc65271::end_update() {
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!(m_regs[reg_A] & reg_A_UIP)) return;
    const bool bin = m_regs[reg_B] & reg_B_DM;
    uint8_t (*inc)(uint8_t) = bin ? inc_bin : inc_bcd;
    const int c59 = bin ? 59 : 0x59, c23 = bin ? 23 : 0x23, c12 = bin ? 12 : 0x12, c11 = bin ? 11 : 0x11, c29 = bin ? 29 : 0x29;
    if (m_regs[reg_second] < c59) m_regs[reg_second] = inc(m_regs[reg_second]);
    else {
        m_regs[reg_second] = 0;
        if (m_regs[reg_minute] < c59) m_regs[reg_minute] = inc(m_regs[reg_minute]);
        else {
            m_regs[reg_minute] = 0;
            if (m_regs[reg_B] & reg_B_24h) {
                m_regs[reg_hour] = m_regs[reg_hour] < c23 ? inc(m_regs[reg_hour]) : 0;
            } else if (m_regs[reg_hour] < c12) {
                if ((m_regs[reg_hour] & 0x7f) == c11) m_regs[reg_hour] ^= 0x80;
                m_regs[reg_hour] = (inc(m_regs[reg_hour] & 0x7f) & 0x7f) | (m_regs[reg_hour] & 0x80);
            } else {
                m_regs[reg_hour] = 1 | (m_regs[reg_hour] & 0x80);
            }
            if (m_regs[reg_hour] == ((m_regs[reg_B] & reg_B_24h) ? 0 : c12)) {
                m_regs[reg_weekday] = m_regs[reg_weekday] < 7 ? m_regs[reg_weekday] + 1 : 1;
                int days;
                if (m_regs[reg_month] != 2 || (m_regs[reg_year] & 3))
                    days = bin ? dim[(m_regs[reg_month] - 1) % 12] : bin2bcd(dim[(bcd2bin(m_regs[reg_month]) - 1) % 12]);
                else
                    days = c29;
                if (m_regs[reg_monthday] < days) m_regs[reg_monthday] = inc(m_regs[reg_monthday]);
                else {
                    m_regs[reg_monthday] = 1;
                    if (m_regs[reg_month] < c12) m_regs[reg_month] = inc(m_regs[reg_month]);
                    else {
                        m_regs[reg_month] = 1;
                        m_regs[reg_year] = bin ? (m_regs[reg_year] < 99 ? m_regs[reg_year] + 1 : 0) : inc_bcd(m_regs[reg_year]);
                    }
                }
            }
        }
    }
    m_regs[reg_A] &= ~reg_A_UIP;
    m_regs[reg_C] |= reg_C_UF;
    if (((m_regs[reg_alarm_second] & 0xc0) == 0xc0 || m_regs[reg_alarm_second] == m_regs[reg_second]) &&
        ((m_regs[reg_alarm_minute] & 0xc0) == 0xc0 || m_regs[reg_alarm_minute] == m_regs[reg_minute]) &&
        ((m_regs[reg_alarm_hour] & 0xc0) == 0xc0 || m_regs[reg_alarm_hour] == m_regs[reg_hour]))
        m_regs[reg_C] |= reg_C_AF;
    field_interrupts();
}

}  // namespace hk
