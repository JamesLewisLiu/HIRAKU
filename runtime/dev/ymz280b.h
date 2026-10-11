// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// Yamaha YMZ280B PCMD8, ported from MAME's ymz280b.cpp. Modified for HIRAKU: no
// MAME device/stream framework. The owner renders it in step with virtual time
// (render() at 88.2 kHz, the chip's internal rate) and calls `sync` to do so
// before register accesses, where MAME forces a stream update.
#pragma once

#include <cstdint>
#include <functional>

namespace hk {

class Ymz280b {
public:
    static constexpr double CLOCK = 16934400.0;          // Firebeat
    static constexpr double RATE = CLOCK / 384.0 * 2.0;  // internal sample rate, 88.2 kHz

    std::function<uint8_t(uint32_t)> read_mem;   // external sample memory (24-bit address)
    std::function<void()> sync;                  // render up to the present
    std::function<void(bool)> irq;               // IRQ line (unconnected on the Firebeat)
    uint64_t key_ons = 0;                        // statistics

    Ymz280b();
    void reset();
    uint8_t read(int offset);                    // 0 = external memory data, 1 = status
    void write(int offset, uint8_t data);        // 0 = register select, 1 = register data
    // Add n samples at RATE to l[] and r[] (full scale = 1.0).
    void render(float *l, float *r, int n);

private:
    struct Voice {
        uint8_t playing = 0;
        bool ended = false;
        uint8_t keyon = 0, looping = 0, mode = 0;
        uint16_t fnum = 0;
        uint8_t level = 0, pan = 0;
        uint32_t start = 0, stop = 0, loop_start = 0, loop_end = 0, position = 0;   // in nibbles
        int32_t signal = 0, step = 0, loop_signal = 0, loop_step = 0;
        uint32_t loop_count = 0;
        int32_t output_left = 0, output_right = 0, output_step = 0, output_pos = 0;
        int16_t last_sample = 0, curr_sample = 0;
    };

    uint8_t byte(uint32_t a) { return read_mem ? read_mem(a & 0xffffff) : 0; }
    void update_irq_state();
    void update_step(Voice &v);
    void update_volumes(Voice &v);
    int generate_adpcm(Voice &v, int16_t *buffer, int samples);
    int generate_pcm8(Voice &v, int16_t *buffer, int samples);
    int generate_pcm16(Voice &v, int16_t *buffer, int samples);
    void write_to_register(int data);
    uint8_t compute_status();

    Voice voice_[8];
    uint8_t current_register_ = 0, status_register_ = 0, irq_state_ = 0, irq_mask_ = 0, irq_enable_ = 0;
    uint8_t keyon_enable_ = 0, ext_mem_enable_ = 0, ext_readlatch_ = 0;
    uint32_t ext_mem_address_hi_ = 0, ext_mem_address_mid_ = 0, ext_mem_address_ = 0;
    int16_t scratch_[10000];
};

}  // namespace hk
