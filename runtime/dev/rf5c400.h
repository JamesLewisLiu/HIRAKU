// license:BSD-3-Clause
// copyright-holders:Ville Linde
//
// Ricoh RF5C400 32-channel PCM sound chip.
// Ported to HIRAKU from MAME's rf5c400.cpp (BSD-3-Clause; written by Ville
// Linde, improvements by the hoot development team). Sample generation and
// register semantics are unchanged; the MAME stream is replaced by render().
#pragma once

#include <cstdint>
#include <functional>

namespace hk {

class Rf5c400 {
public:
    explicit Rf5c400(uint32_t clock = 16934400);

    // wave memory: 16-bit words, word-addressed (the board's 32 MiB wave RAM)
    std::function<uint16_t(uint32_t word)> read_word;
    std::function<void(uint32_t word, uint16_t v)> write_word;

    uint16_t read(uint32_t offset);               // offset in 16-bit registers
    void write(uint32_t offset, uint16_t data);

    // Render n samples (clock/384 Hz) of the 4 outputs: main L, main R, effect L, effect R.
    void render(float *out4, int n);
    uint32_t sample_rate() const { return clock_ / 384; }

    // Called before a register access, so register changes land at the right sample.
    std::function<void()> sync;

private:
    struct Channel {
        uint16_t startH = 0, startL = 0, freq = 0, endL = 0, endHloopH = 0, loopL = 0;
        uint16_t pan = 0, effect = 0, volume = 0, attack = 0, decay = 0, release = 0, cutoff = 0;
        uint64_t pos = 0, step = 0;
        uint8_t env_phase = 0;
        double env_level = 0.0, env_step = 0.0, env_scale = 1.0;
    };
    double ar(const Channel &c) const { return m_ar[decode80(c.attack >> 8)]; }
    double dr(const Channel &c) const { return m_dr[decode80(c.decay >> 8)]; }
    double rr(const Channel &c) const { return m_rr[decode80(c.release >> 8)]; }
    static constexpr uint8_t decode80(uint8_t v) { return (v & 0x80) ? ((v & 0x7f) + 0x1f) : v; }

    uint32_t clock_;
    Channel m_channels[32];
    uint16_t m_status = 0;
    uint32_t m_ext_mem_address = 0;
    uint16_t m_ext_mem_data = 0;
    int m_req_channel = 0;
    double m_ar[0x9f]{}, m_dr[0x9f]{}, m_rr[0x9f]{};
};

}  // namespace hk
