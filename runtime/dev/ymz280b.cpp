// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// Yamaha YMZ280B PCMD8, ported from MAME's ymz280b.cpp. Modified for HIRAKU: no
// MAME device/stream framework; a voice's end-of-sample status is set as it is
// rendered (MAME defers it to a zero-length timer, i.e. the next CPU slice).
#include "ymz280b.h"

#include <cstring>

namespace hk {

namespace {

constexpr int MAX_SAMPLE_CHUNK = 10000;
constexpr unsigned FRAC_BITS = 9;
constexpr int32_t FRAC_ONE = 1 << FRAC_BITS;

// step size index shift table
constexpr int index_scale[8] = {0x0e6, 0x0e6, 0x0e6, 0x0e6, 0x133, 0x199, 0x200, 0x266};

// precomputed differences
struct DiffLookup {
    int v[16];
    DiffLookup() {
        for (int nib = 0; nib < 16; nib++) {
            int value = (nib & 0x07) * 2 + 1;
            v[nib] = (nib & 0x08) ? -value : value;
        }
    }
};
const DiffLookup diff_lookup;

}  // namespace

Ymz280b::Ymz280b() { reset(); }

void Ymz280b::reset() {
    // initial clear registers
    for (int i = 0xff; i >= 0; i--) {
        current_register_ = (uint8_t)i;
        write_to_register(0);
    }
    current_register_ = 0;
    status_register_ = 0;
    ext_mem_address_ = 0;
    for (auto &v : voice_) {
        v.curr_sample = 0;
        v.last_sample = 0;
        v.output_pos = FRAC_ONE;
        v.playing = 0;
    }
}

void Ymz280b::update_irq_state() {
    int irq_bits = status_register_ & irq_mask_;
    if (!irq_enable_) irq_bits = 0;
    if (irq_bits && !irq_state_) {
        irq_state_ = 1;
        if (irq) irq(true);
    } else if (!irq_bits && irq_state_) {
        irq_state_ = 0;
        if (irq) irq(false);
    }
}

void Ymz280b::update_step(Voice &v) {
    int frequency = v.mode == 1 ? (v.fnum & 0x0ff) : (v.fnum & 0x1ff);
    v.output_step = frequency + 1;   // ((fnum + 1) * (input clock / 384)) / 256
}

void Ymz280b::update_volumes(Voice &v) {
    if (v.pan == 8) {
        v.output_left = v.level;
        v.output_right = v.level;
    } else if (v.pan < 8) {
        v.output_left = v.level;
        // pan 1 is hard-left, what's pan 0? for now assume same as pan 1
        v.output_right = (v.pan == 0) ? 0 : v.level * (v.pan - 1) / 7;
    } else {
        v.output_left = v.level * (15 - v.pan) / 7;
        v.output_right = v.level;
    }
}

int Ymz280b::generate_adpcm(Voice &v, int16_t *buffer, int samples) {
    int position = (int)v.position, signal = v.signal, step = v.step;
    while (samples) {
        int val = byte((uint32_t)position / 2) >> ((~position & 1) << 2);
        signal += (step * diff_lookup.v[val & 15]) / 8;
        if (signal > 32767) signal = 32767;
        else if (signal < -32768) signal = -32768;
        step = (step * index_scale[val & 7]) >> 8;
        if (step > 0x6000) step = 0x6000;
        else if (step < 0x7f) step = 0x7f;
        *buffer++ = (int16_t)signal;
        samples--;
        position++;
        if (v.looping) {
            if ((uint32_t)position == v.loop_start && v.loop_count == 0) {
                v.loop_signal = signal;
                v.loop_step = step;
            }
            if ((uint32_t)position >= v.loop_end && v.keyon) {
                position = (int)v.loop_start;
                signal = v.loop_signal;
                step = v.loop_step;
                v.loop_count++;
            }
        }
        if ((uint32_t)position >= v.stop) {
            v.ended = true;
            break;
        }
    }
    v.position = (uint32_t)position;
    v.signal = signal;
    v.step = step;
    return samples;
}

int Ymz280b::generate_pcm8(Voice &v, int16_t *buffer, int samples) {
    int position = (int)v.position;
    while (samples) {
        int val = byte((uint32_t)position / 2);
        *buffer++ = (int16_t)((int8_t)val * 256);
        samples--;
        position += 2;
        if (v.looping && (uint32_t)position >= v.loop_end && v.keyon) position = (int)v.loop_start;
        if ((uint32_t)position >= v.stop) {
            v.ended = true;
            break;
        }
    }
    v.position = (uint32_t)position;
    return samples;
}

int Ymz280b::generate_pcm16(Voice &v, int16_t *buffer, int samples) {
    int position = (int)v.position;
    while (samples) {
        int val = (int16_t)((byte((uint32_t)position / 2 + 1) << 8) + byte((uint32_t)position / 2 + 0));
        *buffer++ = (int16_t)val;
        samples--;
        position += 4;
        if (v.looping && (uint32_t)position >= v.loop_end && v.keyon) position = (int)v.loop_start;
        if ((uint32_t)position >= v.stop) {
            v.ended = true;
            break;
        }
    }
    v.position = (uint32_t)position;
    return samples;
}

void Ymz280b::render(float *outl, float *outr, int n) {
    constexpr float SCALE = 1.0f / (32768.0f * 256.0f);
    for (int vi = 0; vi < 8; vi++) {
        Voice &v = voice_[vi];
        int16_t prev = v.last_sample, curr = v.curr_sample;
        int16_t *curr_data = scratch_;
        int sampindex = 0, remaining = n;
        int lvol = v.output_left, rvol = v.output_right;

        // quick out if we're not playing and we're at 0
        if (!v.playing && curr == 0 && prev == 0) {
            v.output_pos = FRAC_ONE;   // make sure next sound plays immediately
            continue;
        }
        auto emit = [&]() {
            int s = ((int32_t(prev) * (FRAC_ONE - v.output_pos)) + (int32_t(curr) * v.output_pos)) >> FRAC_BITS;
            outl[sampindex] += (float)(s * lvol / 2) * SCALE;
            outr[sampindex] += (float)(s * rvol / 2) * SCALE;
            sampindex++;
            v.output_pos += v.output_step;
            remaining--;
        };

        // finish off the current sample
        while (remaining > 0 && v.output_pos < FRAC_ONE) emit();
        if (v.output_pos >= FRAC_ONE) v.output_pos -= FRAC_ONE;
        else continue;

        // compute how many new samples we need
        uint32_t final_pos = (uint32_t)v.output_pos + (uint32_t)remaining * (uint32_t)v.output_step;
        uint32_t new_samples = (final_pos + FRAC_ONE) >> FRAC_BITS;
        if (new_samples > MAX_SAMPLE_CHUNK) new_samples = MAX_SAMPLE_CHUNK;
        uint32_t samples_left;

        switch (v.playing << 7 | v.mode) {
        case 0x81: samples_left = (uint32_t)generate_adpcm(v, scratch_, (int)new_samples); break;
        case 0x82: samples_left = (uint32_t)generate_pcm8(v, scratch_, (int)new_samples); break;
        case 0x83: samples_left = (uint32_t)generate_pcm16(v, scratch_, (int)new_samples); break;
        default: samples_left = 0; memset(scratch_, 0, new_samples * sizeof(scratch_[0])); break;
        }

        if (samples_left || v.ended) {
            v.ended = false;
            // if there are leftovers, ramp back to 0
            int base = (int)(new_samples - samples_left);
            int t = (base == 0) ? curr : scratch_[base - 1];
            for (uint32_t i = 0; i < samples_left; i++) {
                if (t < 0) t = -((-t * 15) >> 4);
                else if (t > 0) t = (t * 15) >> 4;
                scratch_[base + i] = (int16_t)t;
            }
            // if we hit the end and IRQs are enabled, signal it
            if (base != 0) {
                v.playing = 0;
                status_register_ |= (uint8_t)(1 << vi);
                update_irq_state();
            }
        }

        // advance forward one sample
        prev = curr;
        curr = *curr_data++;

        // then sample-rate convert with linear interpolation
        while (remaining > 0) {
            while (remaining > 0 && v.output_pos < FRAC_ONE) emit();
            if (v.output_pos >= FRAC_ONE) {
                v.output_pos -= FRAC_ONE;
                prev = curr;
                curr = *curr_data++;
            }
        }
        v.last_sample = prev;
        v.curr_sample = curr;
    }
}

void Ymz280b::write_to_register(int data) {
    if (current_register_ < 0x80) {
        Voice &v = voice_[(current_register_ >> 2) & 7];
        switch (current_register_ & 0xe3) {
        case 0x00:   // pitch low 8 bits
            v.fnum = (uint16_t)((v.fnum & 0x100) | (data & 0xff));
            update_step(v);
            break;
        case 0x01:   // pitch upper 1 bit, loop, key on, mode
            v.fnum = (uint16_t)((v.fnum & 0xff) | ((data & 0x01) << 8));
            v.looping = (uint8_t)((data & 0x10) >> 4);
            if ((data & 0x60) == 0) data &= 0x7f;   // ignore mode setting and set to same state as KON=0
            else v.mode = (uint8_t)((data & 0x60) >> 5);
            if (!v.keyon && (data & 0x80) && keyon_enable_) {
                v.playing = 1;
                v.position = v.start;
                v.signal = v.loop_signal = 0;
                v.step = v.loop_step = 0x7f;
                v.loop_count = 0;
                key_ons++;
            } else if (v.keyon && !(data & 0x80)) {
                v.playing = 0;
            }
            v.keyon = (uint8_t)((data & 0x80) >> 7);
            update_step(v);
            break;
        case 0x02: v.level = (uint8_t)data; update_volumes(v); break;          // total level
        case 0x03: v.pan = (uint8_t)(data & 0x0f); update_volumes(v); break;   // pan
        case 0x20: v.start = (v.start & (0x00ffff << 1)) | (uint32_t)(data << 17); break;
        case 0x21: v.loop_start = (v.loop_start & (0x00ffff << 1)) | (uint32_t)(data << 17); break;
        case 0x22: v.loop_end = (v.loop_end & (0x00ffff << 1)) | (uint32_t)(data << 17); break;
        case 0x23: v.stop = (v.stop & (0x00ffff << 1)) | (uint32_t)(data << 17); break;
        case 0x40: v.start = (v.start & (0xff00ff << 1)) | (uint32_t)(data << 9); break;
        case 0x41: v.loop_start = (v.loop_start & (0xff00ff << 1)) | (uint32_t)(data << 9); break;
        case 0x42: v.loop_end = (v.loop_end & (0xff00ff << 1)) | (uint32_t)(data << 9); break;
        case 0x43: v.stop = (v.stop & (0xff00ff << 1)) | (uint32_t)(data << 9); break;
        case 0x60: v.start = (v.start & (0xffff00 << 1)) | (uint32_t)(data << 1); break;
        case 0x61: v.loop_start = (v.loop_start & (0xffff00 << 1)) | (uint32_t)(data << 1); break;
        case 0x62: v.loop_end = (v.loop_end & (0xffff00 << 1)) | (uint32_t)(data << 1); break;
        case 0x63: v.stop = (v.stop & (0xffff00 << 1)) | (uint32_t)(data << 1); break;
        default: break;
        }
        return;
    }
    switch (current_register_) {
    case 0x80: case 0x81: case 0x82: break;   // DSP (not implemented in MAME either)
    case 0x84: ext_mem_address_hi_ = (uint32_t)data << 16; break;   // ROM readback / RAM write (high)
    case 0x85: ext_mem_address_mid_ = (uint32_t)data << 8; break;   // (middle)
    case 0x86:                                                     // (low) -> update latch
        ext_mem_address_ = ext_mem_address_hi_ | ext_mem_address_mid_ | (uint32_t)data;
        if (ext_mem_enable_) ext_readlatch_ = byte(ext_mem_address_);
        break;
    case 0x87:   // RAM write: the Firebeat's sample memory is flash, read-only from here
        if (ext_mem_enable_) ext_mem_address_ = (ext_mem_address_ + 1) & 0xffffff;
        break;
    case 0xfe: irq_mask_ = (uint8_t)data; update_irq_state(); break;   // IRQ mask
    case 0xff:                                                        // IRQ enable, test, etc
        ext_mem_enable_ = (uint8_t)((data & 0x40) >> 6);
        irq_enable_ = (uint8_t)((data & 0x10) >> 4);
        update_irq_state();
        if (keyon_enable_ && !(data & 0x80)) {
            for (auto &v : voice_) v.playing = 0;
        } else if (!keyon_enable_ && (data & 0x80)) {
            for (auto &v : voice_)
                if (v.keyon && v.looping) v.playing = 1;
        }
        keyon_enable_ = (uint8_t)((data & 0x80) >> 7);
        break;
    default: break;
    }
}

uint8_t Ymz280b::compute_status() {
    if (sync) sync();
    uint8_t result = status_register_;
    status_register_ = 0;   // reading clears the IRQ state
    update_irq_state();
    return result;
}

uint8_t Ymz280b::read(int offset) {
    if ((offset & 1) == 0) {
        if (!ext_mem_enable_) return 0xff;
        uint8_t ret = ext_readlatch_;   // read from external memory
        ext_readlatch_ = byte(ext_mem_address_);
        ext_mem_address_ = (ext_mem_address_ + 1) & 0xffffff;
        return ret;
    }
    return compute_status();
}

void Ymz280b::write(int offset, uint8_t data) {
    if ((offset & 1) == 0) {
        current_register_ = data;
    } else {
        if (sync) sync();
        write_to_register(data);
    }
}

}  // namespace hk
