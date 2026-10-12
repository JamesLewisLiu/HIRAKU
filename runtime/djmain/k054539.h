// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
//
// Konami 054539 PCM sound chip. Ported to HIRAKU from MAME's
// devices/sound/k054539.cpp (BSD-3-Clause): same registers, sample formats,
// reverb and volume/pan tables. The chip renders on demand: the board calls
// render() up to the present before every register access, as MAME's
// stream->update() does, so key-offs and position registers move at the same
// sample as in MAME. DJ Main runs two of them on one shared 16 MiB sample RAM.
#pragma once

#include <cstdint>

namespace hk::dj {

class K054539 {
public:
    static constexpr int RATE = 48000;   // 18.432 MHz / 384

    K054539();
    void reset();
    // `rom`: the sample address space (DJ Main: sndram), `rom_mask` its size - 1.
    void set_rom(const uint8_t *rom, uint32_t rom_mask) { m_rom = rom; m_rom_mask = rom_mask; }

    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t data);
    // Add n samples (48 kHz stereo, the chip's own scale: 1.0 = 32768) to lr.
    void render(float *lr, int n);

private:
    struct Channel { uint32_t pos = 0; int pfrac = 0, val = 0, pval = 0; };
    bool regupdate() const { return !(m_regs[0x22f] & 0x80); }
    void keyon(int ch) { if (regupdate()) m_regs[0x22c] |= (uint8_t)(1 << ch); }
    void keyoff(int ch) { if (regupdate()) m_regs[0x22c] &= (uint8_t)~(1 << ch); }
    uint8_t read_byte(uint32_t a) const { return m_rom ? m_rom[a & m_rom_mask] : 0; }

    double m_voltab[256], m_pantab[0xf];
    double m_gain[8];
    uint8_t m_posreg_latch[8][3]{};
    int m_flags;
    uint8_t m_regs[0x230]{};
    uint8_t m_ram[0x8000]{};   // reverb RAM
    int m_reverb_pos = 0;
    int32_t m_cur_ptr = 0;
    int m_rom_addr = 0;
    Channel m_channels[8];
    const uint8_t *m_rom = nullptr;
    uint32_t m_rom_mask = 0;
};

}  // namespace hk::dj
