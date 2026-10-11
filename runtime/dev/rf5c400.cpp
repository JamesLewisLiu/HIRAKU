// license:BSD-3-Clause
// copyright-holders:Ville Linde
//
// Ricoh RF5C400, ported from MAME's rf5c400.cpp. Modified for HIRAKU: no MAME
// device/stream framework; render() produces float samples into a caller buffer.
#include "rf5c400.h"

#include <algorithm>
#include <cmath>

namespace hk {

namespace {
int volume_table[256];
double pan_table[256];
bool tables_ready = false;

void init_static_tables() {
    if (tables_ready) return;
    std::fill(std::begin(pan_table), std::end(pan_table), 0.0);
    double max = 255.0;
    for (int i = 0; i < 256; i++) {
        volume_table[i] = (uint16_t)max;
        max /= std::pow(10.0, (4.5 / (256.0 / 16.0)) / 20);
    }
    for (int i = 0; i < 0x48; i++) pan_table[i] = std::sqrt((double)(0x47 - i)) / std::sqrt((double)0x47);
    tables_ready = true;
}

enum { TYPE_MASK = 0x00c0, TYPE_16 = 0x0000, TYPE_8LOW = 0x0040, TYPE_8HIGH = 0x0080 };
enum { PHASE_NONE = 0, PHASE_ATTACK, PHASE_DECAY, PHASE_RELEASE };
}  // namespace

Rf5c400::Rf5c400(uint32_t clock) : clock_(clock) {
    init_static_tables();
    // envelope parameters (experimental, as in MAME)
    constexpr double ENV_AR_SPEED = 0.1;
    constexpr int ENV_MIN_AR = 0x02, ENV_MAX_AR = 0x80;
    constexpr double ENV_DR_SPEED = 2.0;
    constexpr int ENV_MIN_DR = 0x20, ENV_MAX_DR = 0x73;
    constexpr double ENV_RR_SPEED = 0.7;
    constexpr int ENV_MIN_RR = 0x20, ENV_MAX_RR = 0x54;
    double r = 1.0 / (ENV_AR_SPEED * (clock / 384));
    for (int i = 0; i < ENV_MIN_AR; i++) m_ar[i] = 1.0;
    for (int i = ENV_MIN_AR; i < ENV_MAX_AR; i++) m_ar[i] = r * (ENV_MAX_AR - i) / (ENV_MAX_AR - ENV_MIN_AR);
    for (int i = ENV_MAX_AR; i < 0x9f; i++) m_ar[i] = 0.0;
    r = -5.0 / (ENV_DR_SPEED * (clock / 384));
    for (int i = 0; i < ENV_MIN_DR; i++) m_dr[i] = r;
    for (int i = ENV_MIN_DR; i < ENV_MAX_DR; i++) m_dr[i] = r * (ENV_MAX_DR - i) / (ENV_MAX_DR - ENV_MIN_DR);
    for (int i = ENV_MAX_DR; i < 0x9f; i++) m_dr[i] = 0.0;
    r = -5.0 / (ENV_RR_SPEED * (clock / 384));
    for (int i = 0; i < ENV_MIN_RR; i++) m_rr[i] = r;
    for (int i = ENV_MIN_RR; i < ENV_MAX_RR; i++) m_rr[i] = r * (ENV_MAX_RR - i) / (ENV_MAX_RR - ENV_MIN_RR);
    for (int i = ENV_MAX_RR; i < 0x9f; i++) m_rr[i] = 0.0;
}

void Rf5c400::render(float *out, int n) {
    std::fill(out, out + 4 * n, 0.0f);
    for (int ch = 0; ch < 32; ch++) {
        Channel *channel = &m_channels[ch];
        uint64_t start = ((uint32_t)(channel->startH & 0xff00) << 8) | channel->startL;
        uint64_t end = ((uint32_t)(channel->endHloopH & 0xff) << 16) | channel->endL;
        uint64_t loop = ((uint32_t)(channel->endHloopH & 0xff00) << 8) | channel->loopL;
        uint64_t pos = channel->pos;
        uint8_t vol = channel->volume & 0xff;
        uint8_t lvol = channel->pan & 0xff, rvol = channel->pan >> 8;
        uint8_t effect_lvol = channel->effect & 0xff, effect_rvol = channel->effect >> 8;
        uint8_t type = (channel->volume >> 8) & TYPE_MASK;
        uint8_t env_phase = channel->env_phase;
        double env_level = channel->env_level, env_step = channel->env_step;
        double env_rstep = env_step * channel->env_scale;
        if (start == end) continue;   // pop'n sound test: a non-existent sample
        for (int i = 0; i < n; i++) {
            if (env_phase == PHASE_NONE) break;
            int16_t tmp = (int16_t)read_word((uint32_t)(pos >> 16));
            int32_t sample;
            switch (type) {
            case TYPE_16: sample = tmp; break;
            case TYPE_8LOW: sample = (int16_t)(tmp << 8); break;
            case TYPE_8HIGH: sample = (int16_t)(tmp & 0xff00); break;
            default: sample = 0; break;
            }
            if (sample & 0x8000) sample ^= 0x7fff;   // sign-magnitude
            env_level += env_rstep;
            switch (env_phase) {
            case PHASE_ATTACK:
                if (env_level >= 1.0) {
                    env_phase = PHASE_DECAY;
                    env_level = 1.0;
                    env_step = ((channel->decay & 0x0080) || channel->decay == 0x100) ? 0.0 : dr(*channel);
                    env_rstep = env_step * channel->env_scale;
                }
                break;
            case PHASE_DECAY:
            case PHASE_RELEASE:
                if (env_level <= 0.0) {
                    env_phase = PHASE_NONE;
                    env_level = env_step = env_rstep = 0.0;
                }
                break;
            }
            sample *= volume_table[vol];
            double s = (double)(sample >> 9) * env_level;
            out[4 * i + 0] += (float)(s * pan_table[lvol] / 32768.0);
            out[4 * i + 1] += (float)(s * pan_table[rvol] / 32768.0);
            out[4 * i + 2] += (float)(s * pan_table[effect_lvol] / 32768.0);
            out[4 * i + 3] += (float)(s * pan_table[effect_rvol] / 32768.0);
            pos += channel->step;
            if ((pos >> 16) > end) {
                pos -= loop << 16;
                pos &= 0xffffff0000ull;
                // Firebeat: the loop value is larger than the buffer while DMAs refill it
                if (pos < (start << 16)) pos = start << 16;
            }
        }
        channel->pos = pos;
        channel->env_phase = env_phase;
        channel->env_level = env_level;
        channel->env_step = env_step;
    }
}

uint16_t Rf5c400::read(uint32_t offset) {
    if (offset < 0x400) {
        switch (offset) {
        case 0x00: return m_status;
        case 0x04: return 0;
        case 0x09: {                    // play position (pop'n BGM streaming polls this)
            if (sync) sync();
            const Channel *c = &m_channels[m_req_channel];
            if (c->env_phase == PHASE_NONE) return 0;
            uint32_t start = ((uint32_t)(c->startH & 0xff00) << 8) | c->startL;
            return (uint16_t)(((c->pos >> 16) - start) >> 6);
        }
        case 0x13: return read_word(m_ext_mem_address);
        default: return 0;
        }
    }
    return 0;
}

void Rf5c400::write(uint32_t offset, uint16_t data) {
    if (sync) sync();
    if (offset < 0x400) {
        switch (offset) {
        case 0x00: m_status = data; break;
        case 0x01: {                    // channel control
            int ch = data & 0x1f;
            Channel &c = m_channels[ch];
            switch (data & 0x60) {
            case 0x60:                  // key on
                c.pos = (uint64_t)(((uint32_t)(c.startH & 0xff00) << 8) | c.startL) << 16;
                c.env_phase = PHASE_ATTACK;
                c.env_level = 0.0;
                c.env_step = ar(c);
                break;
            case 0x40:                  // release
                if (c.env_phase != PHASE_NONE) {
                    c.env_phase = PHASE_RELEASE;
                    c.env_step = (c.release & 0x0080) ? 0.0 : rr(c);
                }
                break;
            default:                    // stop
                c.env_phase = PHASE_NONE;
                c.env_level = 0.0;
                c.env_step = 0.0;
                break;
            }
            break;
        }
        case 0x08: m_req_channel = data & 0x1f; break;
        case 0x09:
        case 0x11: m_ext_mem_address = (m_ext_mem_address & ~0xffffu) | data; break;
        case 0x12: m_ext_mem_address = (m_ext_mem_address & 0xffff) | ((uint32_t)data << 16); break;
        case 0x13: m_ext_mem_data = data; break;
        case 0x14:
            if ((data & 3) == 3 && write_word) write_word(m_ext_mem_address, m_ext_mem_data);
            break;
        default: break;                 // reverb/chorus: not emulated (as in MAME)
        }
        return;
    }
    Channel &c = m_channels[(offset >> 5) & 0x1f];
    switch (offset & 0x1f) {
    case 0x00: c.startH = data; break;
    case 0x01: c.startL = data; break;
    case 0x02: c.step = (uint64_t)((data & 0x1fff) << (data >> 13)) * 4; c.freq = data; break;
    case 0x03: c.endL = data; break;
    case 0x04: c.endHloopH = data; break;
    case 0x05: c.loopL = data; break;
    case 0x06: c.pan = data; break;
    case 0x07: c.effect = data; break;   // beatmania III swaps 6/7 on ch 30/31 when effects are on
    case 0x08: c.volume = data; break;
    case 0x09: c.attack = data; break;
    case 0x0c: c.decay = data; break;
    case 0x0e: c.release = data; break;
    case 0x10: c.cutoff = data; break;
    default: break;
    }
}

}  // namespace hk
