// SPDX-License-Identifier: GPL-3.0-or-later
// The DJ Main board. Address decoding, handlers, interrupts, inputs and screen
// composition follow MAME's djmain.cpp (BSD-3-Clause, copyright-holders: smf);
// the sprite drawing follows its draw_sprites() and MAME's drawgfx (BSD-3-Clause,
// Nicola Salmoria, Aaron Giles).
#include "board.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

#include "../core/prof.h"
#include "../dev/ata.h"
#include "../shell/cabinet.h"
#include "k054539.h"
#include "k056832.h"
#include "m68k.h"

namespace hk::dj {

namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
Config g_cfg;
std::vector<uint8_t> g_rom(0x100000), g_ram(0x10000);
std::vector<uint8_t> g_palram(0x4440);                 // xBGR_888 words, big-endian
std::vector<uint8_t> g_objram(0x800);                  // sprite list, big-endian
std::vector<uint8_t> g_sndram(0x1000000);              // both K054539s' sample RAM
std::vector<uint8_t> g_objrom, g_tilerom;              // raw regions (POST readthrough)
std::vector<uint8_t> g_sprite_pens;                    // decoded 16x16 sprites, 256 pens each
uint32_t g_sprite_count = 0;
uint32_t g_obj_regs[0xa0 / 4]{};
uint8_t g_k055555[48]{};
K056832 g_tiles;
K054539 g_snd[2];
AtaBus g_ata;
uint32_t g_ata_disk_sectors = 0;

uint8_t g_sndram_bank = 0, g_pending_vb_int = 0;
uint16_t g_v_ctrl = 0, g_light_ctrl[2]{};
uint8_t g_pcb_leds = 0xff;
uint8_t g_turntable_select = 0, g_turntable_last_pos[2]{};
uint16_t g_turntable_pos[2]{};
// Inputs as MAME's ports: DSW3 BTN3 BTN2 BTN1 DSW1 DSW2 (input_r's order), active low
uint8_t g_in[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
uint8_t g_dial[2]{};                                   // TT1/TT2: MAME's 8-bit dial ports
int g_dial_frac[2]{};

// interrupts: MAME HOLD_LINE (cleared when the CPU acknowledges)
bool g_hold4 = false, g_hold1 = false;

uint64_t g_cycles = 0;          // CPU clocks at the start of the current m68k_execute
bool g_in_execute = false;
uint64_t g_frame = 0;
FrameHook g_frame_hook;
AudioSink g_audio;
Stats g_stats;
std::vector<uint8_t> g_lamps;
uint64_t g_lamp_changes = 0;
std::vector<uint32_t> g_screen(SCREEN_W * SCREEN_H);

// sound: both chips render at 48 kHz up to the present on every access
uint64_t g_snd_done = 0;        // 48 kHz samples rendered so far
std::vector<float> g_snd48;     // rendered, not yet resampled (stereo)
double g_rs_pos = 0;            // resampler position in g_snd48 (input samples)

uint64_t now() { return g_cycles + (g_in_execute ? (uint64_t)m68k_cycles_run() : 0); }

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
void update_irq() { m68k_set_irq(g_hold4 ? 4 : g_hold1 ? 1 : 0); }

void ide_interrupt(bool state) {
    if (state) {
        g_hold1 = true;
        g_stats.irq1++;
    } else {
        g_hold1 = false;
    }
    update_irq();
}

void vb_interrupt() {
    if (g_v_ctrl & 0x8000) {
        g_hold4 = true;
        g_stats.irq4++;
        g_pending_vb_int = 0;
        update_irq();
    } else {
        g_pending_vb_int = 1;
    }
}

// ---------------------------------------------------------------------------
// Hard disk timing. AtaBus answers at once; MAME's drive (atastorage.cpp) is
// busy first: a read seeks (13 ms per full stroke, scaled by the cylinders
// travelled, plus 1.3 ms of rotation), simple commands take 10 us, READ
// SECTORS spaces its sectors 400 ns apart. The game's IDE task sleeps until the
// interrupt, so an instant interrupt (before it sleeps) would be lost. Here
// the drive shows BSY and the interrupt waits until the busy time is over.
// ---------------------------------------------------------------------------
constexpr int HDD_HEADS = 16, HDD_SECTORS = 63;
uint64_t g_ata_busy_until = 0;       // CPU clock when the drive stops being busy
bool g_ata_irq_deferred = false;      // AtaBus raised its interrupt while busy
bool g_ata_event = false;             // a busy period is pending
uint8_t g_ata_regs[8]{};              // the task file as written (for the LBA)
uint8_t g_ata_command = 0;
uint64_t g_ata_cur_lba = 0;           // MAME m_cur_lba: where the heads are
uint32_t g_hdd_cylinders = 1;

uint64_t us_to_cycles(double us) { return (uint64_t)(us * CPU_HZ / 1e6 + 0.999999); }

bool g_ide_line = false;   // the drive's INTRQ as MAME's ATA interface sees it

void ata_irq_from_bus(bool state) {
    // MAME (atahle.h set_irq, ataintf.cpp) passes the line on only when it changes
    if (state == g_ide_line) return;
    g_ide_line = state;
    if (g_ata_event && now() < g_ata_busy_until) {
        g_ata_irq_deferred = state;
        return;
    }
    ide_interrupt(state);
}

void ata_start_busy(uint64_t cycles) {
    g_ata_busy_until = now() + cycles;
    g_ata_event = true;
    g_ata_irq_deferred = false;
    m68k_end_timeslice();   // let the run loop stop at the end of the busy time
}

void ata_busy_done() {
    g_ata_event = false;
    if (g_ata_irq_deferred) ide_interrupt(true);
    g_ata_irq_deferred = false;
}

uint64_t ata_lba() {
    const uint8_t dev = g_ata_regs[6];
    if (dev & 0x40) return (uint64_t)(dev & 0x0f) << 24 | (uint64_t)g_ata_regs[5] << 16 | (uint64_t)g_ata_regs[4] << 8 | g_ata_regs[3];
    const uint32_t cyl = (uint32_t)g_ata_regs[5] << 8 | g_ata_regs[4];
    return ((uint64_t)cyl * HDD_HEADS + (dev & 0x0f)) * HDD_SECTORS + (g_ata_regs[3] ? g_ata_regs[3] - 1 : 0);
}

// MAME seek_time(): from the current cylinder to the command's
uint64_t ata_seek_cycles() {
    const uint64_t spc = HDD_HEADS * HDD_SECTORS, lba = ata_lba();
    const int64_t diff = std::llabs((int64_t)(g_ata_cur_lba / spc) - (int64_t)(lba / spc));
    g_ata_cur_lba = lba;
    if (diff == 0) return us_to_cycles(0.4);
    return us_to_cycles(13000.0 * (double)diff / (double)g_hdd_cylinders + 1300.0);
}

void ata_write(int cs, int reg, uint16_t v) {
    if (cs == 0 && reg >= 1 && reg <= 6) g_ata_regs[reg] = (uint8_t)v;
    if (cs == 0 && reg == 7) {
        g_ata_command = (uint8_t)v;
        if (g_cfg.ata_log)
            fprintf(stderr, "djmain ata: frame %llu t %.6f pc %06x command %02x lba %llu count %u\n", (unsigned long long)g_frame,
                    (double)now() / CPU_HZ, m68k_get_reg(nullptr, M68K_REG_PPC), g_ata_command, (unsigned long long)ata_lba(),
                    g_ata_regs[2]);
        switch (g_ata_command) {
        case 0x20: case 0x21: case 0xc4: case 0xc8: case 0xc9: case 0x40: case 0x41: {
            ata_start_busy(ata_seek_cycles());   // read_first_sector()
            // MAME next_sector(): the heads follow every sector but the last
            const uint32_t count = g_ata_regs[2] ? g_ata_regs[2] : 256;
            g_ata_cur_lba += count - 1;
            break;
        }
        case 0xec: case 0x10: case 0x91: case 0xef: case 0xe7:
            ata_start_busy(us_to_cycles(10));      // MINIMUM_COMMAND_TIME
            break;
        default:
            break;   // SET MULTIPLE, IDLE, ...: the interrupt is immediate
        }
    }
    g_ata.write(cs, reg, v);
}

uint16_t ata_read(int cs, int reg) {
    const bool status = (cs == 0 && reg == 7) || (cs == 1 && reg == 6);
    if (g_ata_event && now() < g_ata_busy_until) {
        if (status) return 0xd0;   // BSY | DRDY | DSC
        if (cs == 0 && reg == 0) return 0;
    }
    const bool irq_before = g_hold1;
    const uint16_t v = g_ata.read(cs, reg);
    // READ SECTORS: the next sector's interrupt comes 400 ns after the last word
    if (cs == 0 && reg == 0 && (g_ata_command == 0x20 || g_ata_command == 0x21) && !irq_before && g_hold1) {
        g_hold1 = false;
        update_irq();
        ata_start_busy(us_to_cycles(0.4));
        g_ata_irq_deferred = true;
    }
    return v;
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------
void sound_sync() {
    // MAME's stream update: every sample whose time has come
    const uint64_t target = now() * (uint64_t)K054539::RATE / CPU_HZ;
    if (target <= g_snd_done) return;
    const size_t n = (size_t)(target - g_snd_done), at = g_snd48.size();
    g_snd48.resize(at + 2 * n, 0.0f);
    for (auto &c : g_snd) c.render(g_snd48.data() + at, (int)n);
    g_snd_done = target;
    g_stats.sound_samples += n;
}

// Resample what has been rendered to 44.1 kHz (linear) and hand it to the shell.
void sound_flush() {
    sound_sync();
    const size_t have = g_snd48.size() / 2;
    if (have < 2) return;
    static std::vector<float> out;
    out.clear();
    const double step = (double)K054539::RATE / 44100.0;
    while (g_rs_pos + 1 < (double)have) {
        size_t i = (size_t)g_rs_pos;
        float f = (float)(g_rs_pos - (double)i);
        for (int ch = 0; ch < 2; ch++) out.push_back(g_snd48[2 * i + ch] * (1 - f) + g_snd48[2 * i + 2 + ch] * f);
        g_rs_pos += step;
    }
    const size_t used = (size_t)g_rs_pos;
    g_snd48.erase(g_snd48.begin(), g_snd48.begin() + 2 * (ptrdiff_t)used);
    g_rs_pos -= (double)used;
    if (g_audio && !out.empty()) g_audio(out.data(), (int)(out.size() / 2));
}

// ---------------------------------------------------------------------------
// Lamps
// ---------------------------------------------------------------------------
void update_lamps() {
    const auto &defs = cab::lamps(g_cfg.family);
    g_lamps.resize(defs.size());
    bool changed = false;
    for (size_t k = 0; k < defs.size(); k++) {
        uint32_t reg = defs[k].reg == 0 ? g_light_ctrl[0] : defs[k].reg == 1 ? g_light_ctrl[1] : g_pcb_leds;
        uint8_t on = (uint8_t)(((reg >> defs[k].bit) & 1) ^ (defs[k].active_low ? 1 : 0));
        if (g_lamps[k] != on) g_lamps[k] = on, changed = true;
    }
    if (changed) g_lamp_changes++;
}

// ---------------------------------------------------------------------------
// The bus, as MAME's 32-bit big-endian address space for the 68EC020 (main_map_j)
// ---------------------------------------------------------------------------
inline uint32_t be32(const std::vector<uint8_t> &v, uint32_t a) {
    return (uint32_t)v[a] << 24 | (uint32_t)v[a + 1] << 16 | (uint32_t)v[a + 2] << 8 | v[a + 3];
}
inline void put_be32(std::vector<uint8_t> &v, uint32_t a, uint32_t data, uint32_t mask) {
    for (int b = 0; b < 4; b++)
        if (mask >> (24 - 8 * b) & 0xff) v[a + b] = (uint8_t)(data >> (24 - 8 * b));
}

// A 16-bit device on the 32-bit bus: the upper half is offset*2, the lower offset*2+1.
template <class R>
uint32_t read16x2(uint32_t offset, uint32_t mask, R r) {
    uint32_t v = 0;
    if (mask & 0xffff0000u) v |= (uint32_t)r(offset * 2, (uint16_t)(mask >> 16)) << 16;
    if (mask & 0x0000ffffu) v |= r(offset * 2 + 1, (uint16_t)mask);
    return v;
}
template <class W>
void write16x2(uint32_t offset, uint32_t data, uint32_t mask, W w) {
    if (mask & 0xffff0000u) w(offset * 2, (uint16_t)(data >> 16), (uint16_t)(mask >> 16));
    if (mask & 0x0000ffffu) w(offset * 2 + 1, (uint16_t)data, (uint16_t)mask);
}

uint8_t input_r(uint32_t offset) {
    offset = (offset & 3) | ((offset >> 15) & 1) << 2;
    return offset < 6 ? g_in[offset] : 0xff;
}

uint32_t turntable_r(uint32_t mask) {
    uint32_t result = 0;
    if (mask & 0x0000ff00u) {
        const int sel = g_turntable_select;
        const uint8_t pos = g_dial[sel];
        int delta = pos - g_turntable_last_pos[sel];
        if (delta < -128) delta += 256;
        if (delta > 128) delta -= 256;
        g_turntable_pos[sel] = (uint16_t)(g_turntable_pos[sel] + delta * 70);
        g_turntable_last_pos[sel] = pos;
        result |= g_turntable_pos[sel] & 0xff00;
    }
    return result;
}

uint32_t bus_read32(uint32_t a, uint32_t mask) {
    a &= 0xfffffc;   // 24-bit address bus (68EC020), dword aligned
    if (a < 0x100000) return be32(g_rom, a);
    if (a >= 0x400000 && a < 0x410000) return be32(g_ram, a - 0x400000);
    if (a >= 0x480000 && a < 0x484440) return be32(g_palram, a - 0x480000);
    if (a >= 0x500000 && a < 0x580000) {   // sndram_r: a window into the sample RAM
        uint32_t off = ((a - 0x500000) >> 2) | 0x20000u * g_sndram_bank;
        return be32(g_sndram, (off * 4) & 0xffffff);
    }
    if (a >= 0x580000 && a < 0x580040)
        return read16x2((a - 0x580000) >> 2, mask, [](uint32_t o, uint16_t) { return (uint32_t)g_tiles.word_r(o); });
    if (a >= 0x5b0000 && a < 0x5b0500) {   // K054539 x2: umask 0xff00ff00 / 0x00ff00ff
        sound_sync();
        const uint32_t o = ((a - 0x5b0000) >> 2) * 2;
        uint32_t v = 0;
        if (mask & 0xff000000u) v |= (uint32_t)g_snd[0].read(o) << 24;
        if (mask & 0x00ff0000u) v |= (uint32_t)g_snd[1].read(o) << 16;
        if (mask & 0x0000ff00u) v |= (uint32_t)g_snd[0].read(o + 1) << 8;
        if (mask & 0x000000ffu) v |= g_snd[1].read(o + 1);
        return v;
    }
    if ((a & ~0x8000u) == 0x5c0000) {      // inputs: 0x5c0000-3, select bit 15
        uint32_t v = 0;
        for (int b = 0; b < 4; b++)
            if (mask >> (24 - 8 * b) & 0xff) v |= (uint32_t)input_r((a - 0x5c0000) + (uint32_t)b) << (24 - 8 * b);
        return v;
    }
    if (a == 0x5e0000) return turntable_r(mask);
    if (a >= 0x600000 && a < 0x602000) {   // v_rom_r: tile ROM readthrough (POST)
        const int bank = g_tiles.word_r(0x34 / 2) & 0xff;
        uint32_t offset = ((a - 0x600000) >> 2) * 2;
        if (!(mask & 0xff000000u)) offset += 1;
        offset += (uint32_t)bank * 0x800 * 4;
        if (g_v_ctrl & 0x020) offset += 0x800 * 2;
        return g_tilerom.empty() ? 0 : (uint32_t)g_tilerom[offset % g_tilerom.size()] * 0x01010000u;
    }
    if (a >= 0x801000 && a < 0x801800) return be32(g_objram, a - 0x801000);
    if (a >= 0x803000 && a < 0x8030a0) return g_obj_regs[(a - 0x803000) >> 2];
    if (a >= 0x803800 && a < 0x804000) {   // obj_rom_r: sprite ROM readthrough (POST)
        const int bank = g_obj_regs[0x28 / 4] >> 16 & 0x3ff;
        uint32_t offset = ((a - 0x803800) >> 2) + (uint32_t)bank * 0x200;
        offset *= 4;
        if (mask & 0x0000ffffu) offset += 2;
        if (mask & 0xff00ff00u) offset++;
        return g_objrom.empty() ? 0 : (uint32_t)g_objrom[offset % g_objrom.size()] * 0x01010101u;
    }
    if (a >= 0xc00000 && a < 0xc02000)
        return read16x2((a - 0xc00000) >> 2, mask, [](uint32_t o, uint16_t) { return (uint32_t)g_tiles.ram_word_r(o); });
    if (a >= 0xf00000 && a < 0xf00010)
        return read16x2((a - 0xf00000) >> 2, mask, [](uint32_t o, uint16_t) { return (uint32_t)ata_read(0, (int)o); });
    if (a >= 0xf40000 && a < 0xf40010)
        return read16x2((a - 0xf40000) >> 2, mask, [](uint32_t o, uint16_t) { return (uint32_t)ata_read(1, (int)o); });
    g_stats.unmapped++;
    return 0;
}

void bus_write32(uint32_t a, uint32_t data, uint32_t mask) {
    a &= 0xfffffc;
    if (a >= 0x400000 && a < 0x410000) return put_be32(g_ram, a - 0x400000, data, mask);
    if (a >= 0x480000 && a < 0x484440) return put_be32(g_palram, a - 0x480000, data, mask);
    if (a >= 0x500000 && a < 0x580000) {
        sound_sync();   // the chips play from this RAM
        uint32_t off = ((a - 0x500000) >> 2) | 0x20000u * g_sndram_bank;
        return put_be32(g_sndram, (off * 4) & 0xffffff, data, mask);
    }
    if (a >= 0x580000 && a < 0x580040)
        return write16x2((a - 0x580000) >> 2, data, mask, [](uint32_t o, uint16_t d, uint16_t m) { g_tiles.word_w(o, d, m); });
    if (a >= 0x590000 && a < 0x590008) return;   // unknown (M66011FP?)
    if (a >= 0x5a0000 && a < 0x5a0060) {          // K055555_long_w: bytes 31-24 and 15-8
        const uint32_t offset = (a - 0x5a0000) >> 2;
        if (mask & 0xff000000u) g_k055555[(offset << 1) % 48] = (uint8_t)(data >> 24);
        if (mask & 0x0000ff00u) g_k055555[((offset << 1) + 1) % 48] = (uint8_t)(data >> 8);
        return;
    }
    if (a >= 0x5b0000 && a < 0x5b0500) {
        sound_sync();
        const uint32_t o = ((a - 0x5b0000) >> 2) * 2;
        if (mask & 0xff000000u) g_snd[0].write(o, (uint8_t)(data >> 24));
        if (mask & 0x00ff0000u) g_snd[1].write(o, (uint8_t)(data >> 16));
        if (mask & 0x0000ff00u) g_snd[0].write(o + 1, (uint8_t)(data >> 8));
        if (mask & 0x000000ffu) g_snd[1].write(o + 1, (uint8_t)data);
        return;
    }
    if (a == 0x5d0000 || a == 0x5d2000) {         // light_ctrl_1_w / _2_w (upper half)
        if (mask & 0xffff0000u) {
            uint16_t &r = g_light_ctrl[a == 0x5d2000];
            const uint16_t m = (uint16_t)(mask >> 16);
            r = (uint16_t)((r & ~m) | ((data >> 16) & m));
            update_lamps();
        }
        return;
    }
    if (a == 0x5d4000) {                          // v_ctrl_w
        if (mask & 0xffff0000u) {
            const uint16_t m = (uint16_t)(mask >> 16);
            g_v_ctrl = (uint16_t)((g_v_ctrl & ~m) | ((data >> 16) & m));
            if ((g_v_ctrl & 0x8000) && g_pending_vb_int) {
                g_pending_vb_int = 0;
                g_hold4 = true;
                g_stats.irq4++;
                update_irq();
            }
        }
        return;
    }
    if (a == 0x5d6000) {
        if (mask & 0xff000000u) { g_pcb_leds = (uint8_t)(data >> 24); update_lamps(); }   // pcb_leds_w
        if (mask & 0x00ff0000u) g_sndram_bank = (uint8_t)(data >> 16) & 0x1f;            // sndram_bank_w
        return;
    }
    if (a == 0x5e0000) {                          // turntable_select_w
        if (mask & 0x00ff0000u) g_turntable_select = (data >> 19) & 1;
        return;
    }
    if (a >= 0x801000 && a < 0x801800) return put_be32(g_objram, a - 0x801000, data, mask);
    if (a >= 0x802000 && a < 0x803000) return;   // unknown
    if (a >= 0x803000 && a < 0x8030a0) {
        uint32_t &r = g_obj_regs[(a - 0x803000) >> 2];
        r = (r & ~mask) | (data & mask);
        return;
    }
    if (a >= 0xc00000 && a < 0xc02000)
        return write16x2((a - 0xc00000) >> 2, data, mask, [](uint32_t o, uint16_t d, uint16_t m) { g_tiles.ram_word_w(o, d, m); });
    if (a >= 0xc02000 && a < 0xc02048) return;   // unknown
    if (a >= 0xf00000 && a < 0xf00010)
        return write16x2((a - 0xf00000) >> 2, data, mask, [](uint32_t o, uint16_t d, uint16_t) { ata_write(0, (int)o, d); });
    if (a >= 0xf40000 && a < 0xf40010)
        return write16x2((a - 0xf40000) >> 2, data, mask, [](uint32_t o, uint16_t d, uint16_t) { ata_write(1, (int)o, d); });
    g_stats.unmapped++;
}

// Byte lanes of address a within its dword (big-endian)
inline uint32_t lane8(uint32_t a) { return 0xff000000u >> (8 * (a & 3)); }

uint32_t read8(uint32_t a) { return bus_read32(a, lane8(a)) >> (24 - 8 * (a & 3)) & 0xff; }
uint32_t read16(uint32_t a) {
    if (a & 1) return read8(a) << 8 | read8(a + 1);   // MAME splits unaligned words into bytes
    const uint32_t m = (a & 2) ? 0x0000ffffu : 0xffff0000u;
    return bus_read32(a, m) >> ((a & 2) ? 0 : 16) & 0xffff;
}
uint32_t read32(uint32_t a) {
    if ((a & 3) == 0) return bus_read32(a, 0xffffffffu);
    if ((a & 1) == 0) return read16(a) << 16 | read16(a + 2);
    return read8(a) << 24 | read16(a + 1) << 8 | read8(a + 3);
}
void write8(uint32_t a, uint32_t v) { bus_write32(a, (v & 0xff) << (24 - 8 * (a & 3)), lane8(a)); }
void write16(uint32_t a, uint32_t v) {
    if (a & 1) { write8(a, v >> 8); write8(a + 1, v); return; }
    if (a & 2) bus_write32(a, v & 0xffff, 0x0000ffffu);
    else bus_write32(a, (v & 0xffff) << 16, 0xffff0000u);
}
void write32(uint32_t a, uint32_t v) {
    if ((a & 3) == 0) return bus_write32(a, v, 0xffffffffu);
    if ((a & 1) == 0) { write16(a, v >> 16); write16(a + 2, v); return; }
    write8(a, v >> 24); write16(a + 1, v >> 8); write8(a + 3, v);
}

// ---------------------------------------------------------------------------
// Video: MAME djmain_state::screen_update()
// ---------------------------------------------------------------------------
constexpr int NPENS = 0x4440 / 4;
uint32_t g_pens[NPENS];

void update_pens() {
    // palette_device::xBGR_888: red in bits 0-7, green 8-15, blue 16-23
    for (int i = 0; i < NPENS; i++) {
        uint32_t v = be32(g_palram, (uint32_t)i * 4);
        g_pens[i] = 0xff000000u | (v & 0xff) << 16 | (v & 0xff00) | (v >> 16 & 0xff);
    }
}

// MAME gfx_element::transpen / zoom_transpen (drawgfxzoom_core), pen 0 transparent
void draw_sprite(int code, int color, int colorbase, bool flipx, bool flipy, int sx, int sy, uint32_t scalex, uint32_t scaley) {
    const uint8_t *src = &g_sprite_pens[(size_t)((uint32_t)code % g_sprite_count) * 256];
    const int base = colorbase + 16 * (color % (NPENS / 16));
    auto put = [&](uint32_t *d, uint8_t pen) {
        if (pen && base + pen < NPENS) *d = g_pens[base + pen];
    };
    if (scalex == 0x10000 && scaley == 0x10000) {   // drawgfx_core
        for (int y = 0; y < 16; y++) {
            const int dy = sy + y;
            if (dy < 0 || dy >= SCREEN_H) continue;
            const uint8_t *row = src + (flipy ? 15 - y : y) * 16;
            for (int x = 0; x < 16; x++) {
                const int dx = sx + x;
                if (dx < 0 || dx >= SCREEN_W) continue;
                put(&g_screen[(size_t)dy * SCREEN_W + (size_t)dx], row[flipx ? 15 - x : x]);
            }
        }
        return;
    }
    const uint32_t dstwidth = (scalex * 16 + 0x8000) >> 16, dstheight = (scaley * 16 + 0x8000) >> 16;
    if (dstwidth < 1 || dstheight < 1) return;
    int32_t dx = (int32_t)((16u << 16) / dstwidth), dy = (int32_t)((16u << 16) / dstheight);
    int32_t destx = sx, desty = sy;
    int32_t destendx = destx + (int32_t)dstwidth - 1;
    if (destx > SCREEN_W - 1 || destendx < 0) return;
    int32_t srcx = 0;
    if (destx < 0) { srcx = (0 - destx) * dx; destx = 0; }
    if (destendx > SCREEN_W - 1) destendx = SCREEN_W - 1;
    int32_t destendy = desty + (int32_t)dstheight - 1;
    if (desty > SCREEN_H - 1 || destendy < 0) return;
    int32_t srcy = 0;
    if (desty < 0) { srcy = (0 - desty) * dy; desty = 0; }
    if (destendy > SCREEN_H - 1) destendy = SCREEN_H - 1;
    if (flipx) { srcx = (int32_t)(dstwidth - 1) * dx - srcx; dx = -dx; }
    if (flipy) { srcy = (int32_t)(dstheight - 1) * dy - srcy; dy = -dy; }
    for (int32_t cury = desty; cury <= destendy; cury++) {
        uint32_t *d = &g_screen[(size_t)cury * SCREEN_W + (size_t)destx];
        const uint8_t *row = src + (srcy >> 16) * 16;
        int32_t cursrcx = srcx;
        srcy += dy;
        for (int32_t curx = destx; curx <= destendx; curx++, d++) {
            put(d, row[cursrcx >> 16]);
            cursrcx += dx;
        }
    }
}

void draw_sprites() {
    constexpr int NUM_SPRITES = 128;
    const int colorbase = g_k055555[29] * 0x400;   // K55_PALBASE_SUB2
    int sortedlist[NUM_SPRITES];
    for (int &s : sortedlist) s = -1;
    auto obj = [](int i) { return be32(g_objram, (uint32_t)i * 4); };
    for (int offs = 0; offs < NUM_SPRITES * 4; offs += 4)
        if (obj(offs) & 0x00008000) {
            if (obj(offs) & 0x80000000) continue;
            sortedlist[obj(offs) & (NUM_SPRITES - 1)] = offs;
        }
    static const int xoffset[8] = {0, 1, 4, 5, 16, 17, 20, 21};
    static const int yoffset[8] = {0, 2, 8, 10, 32, 34, 40, 42};
    static const int sizetab[4] = {1, 2, 4, 8};
    for (int pri_code = NUM_SPRITES - 1; pri_code >= 0; pri_code--) {
        const int offs = sortedlist[pri_code];
        if (offs == -1) continue;
        const int code = (int)(obj(offs) >> 16);
        const bool flipx = obj(offs) >> 10 & 1, flipy = obj(offs) >> 11 & 1;
        const int size = sizetab[(obj(offs) >> 8) & 3];
        int ox = (int16_t)(obj(offs + 1) & 0xffff);
        int oy = (int16_t)(obj(offs + 1) >> 16);
        int xscale = (int)(obj(offs + 2) >> 16), yscale = (int)(obj(offs + 2) & 0xffff);
        if (!xscale || !yscale) continue;
        xscale = (0x40 << 16) / xscale;
        yscale = (0x40 << 16) / yscale;
        ox -= (size * xscale) >> 13;
        oy -= (size * yscale) >> 13;
        const int color = (int)(obj(offs + 3) >> 16) & 15;
        for (int x = 0; x < size; x++)
            for (int y = 0; y < size; y++) {
                int c = code + (flipx ? xoffset[size - x - 1] : xoffset[x]) + (flipy ? yoffset[size - y - 1] : yoffset[y]);
                if (xscale != 0x10000 || yscale != 0x10000) {
                    const int sx = ox + ((x * xscale + (1 << 11)) >> 12);
                    const int sy = oy + ((y * yscale + (1 << 11)) >> 12);
                    const int zw = ox + (((x + 1) * xscale + (1 << 11)) >> 12) - sx;
                    const int zh = oy + (((y + 1) * yscale + (1 << 11)) >> 12) - sy;
                    draw_sprite(c, color, colorbase, flipx, flipy, sx, sy, (uint32_t)(zw << 12), (uint32_t)(zh << 12));
                } else {
                    draw_sprite(c, color, colorbase, flipx, flipy, ox + (x << 4), oy + (y << 4), 0x10000, 0x10000);
                }
            }
    }
}

void screen_update() {
    prof::Scope ps(prof::DRAW);
    update_pens();
    const int enables = g_k055555[45];   // K55_INPUT_ENABLES
    int pri[3] = {g_k055555[7], g_k055555[10], g_k055555[17]};   // K55_PRIINP_0, _3, _10
    int order[3] = {0, 1, 2};
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (pri[order[i]] > pri[order[j]]) std::swap(order[i], order[j]);
    std::fill(g_screen.begin(), g_screen.end(), g_pens[0]);
    for (int i = 0; i < 3; i++) {
        const int layer = order[i];
        if (layer == 2) {
            if (enables & 0x40) draw_sprites();   // K55_INP_SUB2
        } else if (enables & (0x01 << layer)) {
            g_tiles.draw_dj(layer, g_screen.data(), SCREEN_W, SCREEN_H, g_pens, NPENS);
        }
    }
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

// MAME ROM_LOAD16_BYTE pairs: file 2k at even bytes, file 2k+1 at odd bytes
bool load_interleaved(const std::vector<std::string> &files, std::vector<uint8_t> &region, const char *what) {
    region.clear();
    for (size_t k = 0; k + 1 < files.size(); k += 2) {
        std::vector<uint8_t> even, odd;
        if (!read_file(g_cfg.rom_dir + "/" + files[k], even) || !read_file(g_cfg.rom_dir + "/" + files[k + 1], odd) ||
            even.size() != odd.size()) {
            fprintf(stderr, "djmain: cannot load the %s ROMs %s / %s from %s\n", what, files[k].c_str(), files[k + 1].c_str(),
                    g_cfg.rom_dir.c_str());
            return false;
        }
        const size_t at = region.size();
        region.resize(at + 2 * even.size());
        for (size_t i = 0; i < even.size(); i++) region[at + 2 * i] = even[i], region[at + 2 * i + 1] = odd[i];
    }
    return !region.empty();
}

void decode_sprites() {
    // MAME gfx_8x8x4_row_2x2_group_packed_lsb: 16x16, 128 bytes, low nibble first;
    // the four 8x8 quarters are left/right 32 bytes apart, top/bottom 64 apart
    g_sprite_count = (uint32_t)(g_objrom.size() / 128);
    g_sprite_pens.assign((size_t)g_sprite_count * 256, 0);
    for (uint32_t t = 0; t < g_sprite_count; t++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                const size_t byte = (size_t)t * 128 + (y < 8 ? (size_t)y * 4 : 64 + (size_t)(y - 8) * 4) + (x < 8 ? 0 : 32) +
                                    (size_t)((x & 7) >> 1);
                const uint8_t b = g_objrom[byte];
                g_sprite_pens[(size_t)t * 256 + (size_t)y * 16 + (size_t)x] = (x & 1) ? b >> 4 : b & 0x0f;
            }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
bool init(const Config &cfg) {
    g_cfg = cfg;
    std::vector<uint8_t> prog;
    if (!load_interleaved(cfg.program, prog, "program")) return false;
    std::copy(prog.begin(), prog.begin() + (ptrdiff_t)std::min(prog.size(), g_rom.size()), g_rom.begin());
    if (!load_interleaved(cfg.sprites, g_objrom, "sprite")) return false;
    if (!load_interleaved(cfg.tiles, g_tilerom, "tile")) return false;
    decode_sprites();
    g_tiles.init(g_tilerom);
    // MAME video_start()
    g_tiles.set_layer_offs(0, -92, -27);
    g_tiles.set_layer_offs(1, -88, -27);
    for (auto &c : g_snd) c.set_rom(g_sndram.data(), (uint32_t)g_sndram.size() - 1);

    auto disk = std::make_unique<AtaDevice>("hdd", false);
    disk->disk = std::make_unique<DiskImage>();
    if (!disk->disk->open(cfg.hdd)) {
        fprintf(stderr, "djmain: cannot open the hard disk image '%s'\n", cfg.hdd.c_str());
        return false;
    }
    g_ata_disk_sectors = disk->disk->sectors();
    g_ata.attach(0, std::move(disk));
    g_ata.log = cfg.ata_log;
    g_ata.pio_end_irq = false;   // MAME's drive
    g_ata.irq = ata_irq_from_bus;
    g_hdd_cylinders = std::max<uint32_t>(1, std::min<uint32_t>(g_ata_disk_sectors / (HDD_HEADS * HDD_SECTORS), 16383));
    update_lamps();

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68EC020);
    m68k_pulse_reset();
    return true;
}

void set_frame_hook(FrameHook h) { g_frame_hook = std::move(h); }
void set_audio_sink(AudioSink s) { g_audio = std::move(s); }

void run() {
    // MAME: the vblank interrupt at line 384 of each frame; the frame (screen
    // update, Lua frame notifier, snapshot) completes at the start of the next one.
    auto run_until = [](uint64_t t) {
        while (g_cycles < t) {
            uint64_t stop = t;
            if (g_ata_event) stop = std::min(stop, std::max(g_ata_busy_until, g_cycles + 1));
            g_in_execute = true;
            int ran = m68k_execute((int)(stop - g_cycles));
            g_in_execute = false;
            g_cycles += (uint64_t)ran;
            if (g_ata_event && g_cycles >= g_ata_busy_until) ata_busy_done();
        }
    };
    for (;;) {
        const uint64_t frame_start = g_frame * FRAME_CYCLES;
        run_until(frame_start + VBLANK_CYCLE);
        vb_interrupt();
        run_until(frame_start + FRAME_CYCLES);
        g_frame++;
        screen_update();
        sound_flush();
        if (g_frame_hook) g_frame_hook(g_frame, g_screen.data(), SCREEN_W, SCREEN_H);
    }
}

uint64_t cycles() { return now(); }
uint64_t frame_count() { return g_frame; }
const std::string &family() { return g_cfg.family; }
const Stats &stats() { return g_stats; }
const std::vector<uint8_t> &lamp_state() { return g_lamps; }
uint64_t lamp_changes() { return g_lamp_changes; }
uint32_t pc() { return m68k_get_reg(nullptr, M68K_REG_PC); }

const uint8_t *mem(uint32_t addr, uint32_t len) {
    auto in = [&](uint32_t lo, const std::vector<uint8_t> &v) {
        return addr >= lo && addr - lo <= v.size() && len <= v.size() - (addr - lo) ? v.data() + (addr - lo) : nullptr;
    };
    if (auto p = in(0x000000, g_rom)) return p;
    if (auto p = in(0x400000, g_ram)) return p;
    if (auto p = in(0x480000, g_palram)) return p;
    if (auto p = in(0x801000, g_objram)) return p;
    return nullptr;
}

bool set_input(const std::string &name, int value) {
    struct Bit { const char *name; int port, mask; };
    // ports: 0 DSW3, 1 BTN3, 2 BTN2, 3 BTN1, 4 DSW1, 5 DSW2
    static const Bit common[] = {{"Test", 1, 0x01}, {"Reset", 1, 0x02}, {"Service", 1, 0x04}, {"Coin 1", 2, 0x80}};
    static const Bit bm[] = {{"P1 Button 1", 3, 0x10}, {"P1 Button 2", 3, 0x08}, {"P1 Button 3", 3, 0x04},
                             {"P1 Button 4", 3, 0x02}, {"P1 Button 5", 3, 0x01}, {"P2 Button 1", 3, 0x40},
                             {"P2 Button 2", 3, 0x80}, {"P2 Button 3", 2, 0x01}, {"P2 Button 4", 2, 0x02},
                             {"P2 Button 5", 2, 0x04}, {"1 Player Start", 2, 0x10}, {"2 Players Start", 2, 0x20},
                             {"Effect", 2, 0x40}};
    static const Bit popn[] = {{"P1 Button 1", 3, 0x10}, {"P1 Button 2", 3, 0x08}, {"P1 Button 3", 3, 0x04},
                               {"P1 Button 4", 3, 0x02}, {"P1 Button 5", 3, 0x01}, {"P1 Button 6", 3, 0x40},
                               {"P1 Button 7", 3, 0x80}, {"P1 Button 8", 2, 0x01}, {"P1 Button 9", 2, 0x02}};
    auto try_set = [&](const Bit *b, size_t n) {
        for (size_t k = 0; k < n; k++)
            if (name == b[k].name) {
                if (value) g_in[b[k].port] &= (uint8_t)~b[k].mask;
                else g_in[b[k].port] |= (uint8_t)b[k].mask;
                return true;
            }
        return false;
    };
    if (try_set(common, std::size(common))) return true;
    if (g_cfg.family == "djpopn") return try_set(popn, std::size(popn));
    if (try_set(bm, std::size(bm))) return true;
    // MAME's dials: "TT1/P1 Dial" (absolute 8-bit position), as the oracle scripts set them
    if (name == "TT1/P1 Dial" || name == "TT2/P2 Dial") {
        g_dial[name[2] - '1'] = (uint8_t)value;
        return true;
    }
    return false;
}

// Host turntables arrive in Firebeat units (1024 counts a turn); MAME's dial
// port has 256. (MAME then scales each dial step by 70 into the board's counter.)
void turntable_add(int player, int delta) {
    if (player < 0 || player > 1 || g_cfg.family != "djbm") return;
    g_dial_frac[player] += delta;
    const int whole = g_dial_frac[player] / 4;
    g_dial_frac[player] -= whole * 4;
    g_dial[player] = (uint8_t)(g_dial[player] + whole);
}

}  // namespace hk::dj

// Musashi: memory and interrupt acknowledge
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return hk::dj::read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return hk::dj::read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return hk::dj::read32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { hk::dj::write8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { hk::dj::write16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { hk::dj::write32(a, v); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return hk::dj::read8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return hk::dj::read16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return hk::dj::read32(a); }

int hk_dj_int_ack(int level) {
    // HOLD_LINE: the acknowledge clears the line
    if (level == 4) hk::dj::g_hold4 = false;
    if (level == 1) hk::dj::g_hold1 = false;
    hk::dj::update_irq();
    return (int)M68K_INT_ACK_AUTOVECTOR;
}
}
