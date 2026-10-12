// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
//
// Konami 054539 PCM sound chip, from MAME's devices/sound/k054539.cpp (see k054539.h).
// Not ported: the timer output (DJ Main doesn't wire it) and the analog input pan callback.
#include "k054539.h"

#include <cmath>
#include <cstring>

namespace hk::dj {

namespace {
enum { REVERSE_STEREO = 1, DISABLE_REVERB = 2, UPDATE_AT_KEYON = 4 };
}

K054539::K054539() {
    for (double &g : m_gain) g = 1.0;
    // vol=0 -> no attenuation, vol=0x40 -> -36dB; the 1/4 is for the number of channels
    for (int i = 0; i < 256; i++) m_voltab[i] = pow(10.0, (-36.0 * (double)i / (double)0x40) / 20.0) / 4.0;
    // pan[i]^2 + pan[0xe-i]^2 = 1, pan[0xe] = 1
    for (int i = 0; i < 0xf; i++) m_pantab[i] = sqrt((double)i) / sqrt((double)0xe);
    m_flags = UPDATE_AT_KEYON;   // init_chip(): "make it default until proven otherwise"
    reset();
}

void K054539::reset() {
    // init_chip() then device_reset()
    memset(m_regs, 0, sizeof m_regs);
    memset(m_posreg_latch, 0, sizeof m_posreg_latch);
    memset(m_ram, 0, sizeof m_ram);
    m_reverb_pos = 0;
    m_cur_ptr = 0;
    m_rom_addr = 0;
    for (auto &c : m_channels) c = Channel{};
    commit();
}

void K054539::render(float *lr, int n) {
    static constexpr double VOL_CAP = 1.80;
    static const int16_t dpcm[16] = {
        0 * 0x100, 1 * 0x100, 2 * 0x100, 4 * 0x100, 8 * 0x100, 16 * 0x100, 32 * 0x100, 64 * 0x100,
        0 * 0x100, -64 * 0x100, -32 * 0x100, -16 * 0x100, -8 * 0x100, -4 * 0x100, -2 * 0x100, -1 * 0x100};
    int16_t *rbase = reinterpret_cast<int16_t *>(m_ram);
    if (!(m_regs[0x22f] & 1)) return;   // PCM disabled: silence (MAME leaves the stream's zeros)

    for (int sample = 0; sample != n; sample++) {
        double lval, rval;
        if (!(m_flags & DISABLE_REVERB)) lval = rval = rbase[m_reverb_pos];
        else lval = rval = 0;
        rbase[m_reverb_pos] = 0;

        for (int ch = 0; ch < 8; ch++) {
            if (!(m_regs[0x22c] >> ch & 1)) continue;
            uint8_t *base1 = m_regs + 0x20 * ch;
            uint8_t *base2 = m_regs + 0x200 + 0x2 * ch;
            Channel *chan = m_channels + ch;

            int delta = base1[0x00] | (base1[0x01] << 8) | (base1[0x02] << 16);
            int vol = base1[0x03];
            int bval = vol + base1[0x04];
            if (bval > 255) bval = 255;
            int pan = base1[0x05];
            // DJ Main: 81-87 right, 88 middle, 89-8f left
            if (pan >= 0x81 && pan <= 0x8f) pan -= 0x81;
            else if (pan >= 0x11 && pan <= 0x1f) pan -= 0x11;
            else pan = 0x18 - 0x11;

            double cur_gain = m_gain[ch];
            double lvol = m_voltab[vol] * m_pantab[pan] * cur_gain;
            if (lvol > VOL_CAP) lvol = VOL_CAP;
            double rvol = m_voltab[vol] * m_pantab[0xe - pan] * cur_gain;
            if (rvol > VOL_CAP) rvol = VOL_CAP;
            double rbvol = m_voltab[bval] * cur_gain / 2;
            if (rbvol > VOL_CAP) rbvol = VOL_CAP;

            int rdelta = (base1[6] | (base1[7] << 8)) >> 3;
            rdelta = (rdelta + m_reverb_pos) & 0x3fff;

            int cur_pos = (base1[0x0c] | (base1[0x0d] << 8) | (base1[0x0e] << 16));
            int fdelta, pdelta;
            if (base2[0] & 0x20) {
                delta = -delta;
                fdelta = +0x10000;
                pdelta = -1;
            } else {
                fdelta = -0x10000;
                pdelta = +1;
            }

            int cur_pfrac, cur_val, cur_pval;
            if ((uint32_t)cur_pos != chan->pos) {
                chan->pos = (uint32_t)cur_pos;
                cur_pfrac = 0;
                cur_val = 0;
                cur_pval = 0;
            } else {
                cur_pfrac = chan->pfrac;
                cur_val = chan->val;
                cur_pval = chan->pval;
            }

            switch (base2[0] & 0xc) {
            case 0x0: {   // 8bit pcm
                cur_pfrac += delta;
                while (cur_pfrac & ~0xffff) {
                    cur_pfrac += fdelta;
                    cur_pos += pdelta;
                    cur_pval = cur_val;
                    cur_val = (int16_t)(read_byte((uint32_t)cur_pos) << 8);
                    if (cur_val == (int16_t)0x8000 && (base2[1] & 1)) {
                        cur_pos = (base1[0x08] | (base1[0x09] << 8) | (base1[0x0a] << 16));
                        cur_val = (int16_t)(read_byte((uint32_t)cur_pos) << 8);
                    }
                    if (cur_val == (int16_t)0x8000) {
                        keyoff(ch);
                        cur_val = 0;
                        break;
                    }
                }
                break;
            }
            case 0x4: {   // 16bit pcm lsb first
                pdelta <<= 1;
                cur_pfrac += delta;
                while (cur_pfrac & ~0xffff) {
                    cur_pfrac += fdelta;
                    cur_pos += pdelta;
                    cur_pval = cur_val;
                    cur_val = (int16_t)(read_byte((uint32_t)cur_pos) | read_byte((uint32_t)cur_pos + 1) << 8);
                    if (cur_val == (int16_t)0x8000 && (base2[1] & 1)) {
                        cur_pos = (base1[0x08] | (base1[0x09] << 8) | (base1[0x0a] << 16));
                        cur_val = (int16_t)(read_byte((uint32_t)cur_pos) | read_byte((uint32_t)cur_pos + 1) << 8);
                    }
                    if (cur_val == (int16_t)0x8000) {
                        keyoff(ch);
                        cur_val = 0;
                        break;
                    }
                }
                break;
            }
            case 0x8: {   // 4bit dpcm
                cur_pos <<= 1;
                cur_pfrac <<= 1;
                if (cur_pfrac & 0x10000) {
                    cur_pfrac &= 0xffff;
                    cur_pos |= 1;
                }
                cur_pfrac += delta;
                while (cur_pfrac & ~0xffff) {
                    cur_pfrac += fdelta;
                    cur_pos += pdelta;
                    cur_pval = cur_val;
                    cur_val = read_byte((uint32_t)cur_pos >> 1);
                    if (cur_val == 0x88 && (base2[1] & 1)) {
                        cur_pos = (base1[0x08] | (base1[0x09] << 8) | (base1[0x0a] << 16)) << 1;
                        cur_val = read_byte((uint32_t)cur_pos >> 1);
                    }
                    if (cur_val == 0x88) {
                        keyoff(ch);
                        cur_val = 0;
                        break;
                    }
                    if (cur_pos & 1) cur_val >>= 4;
                    else cur_val &= 15;
                    cur_val = cur_pval + dpcm[cur_val];
                    if (cur_val < -32768) cur_val = -32768;
                    else if (cur_val > 32767) cur_val = 32767;
                }
                cur_pfrac >>= 1;
                if (cur_pos & 1) cur_pfrac |= 0x8000;
                cur_pos >>= 1;
                break;
            }
            default:
                break;
            }
            lval += cur_val * lvol;
            rval += cur_val * rvol;
            rbase[(rdelta + m_reverb_pos) & 0x1fff] += int16_t(cur_val * rbvol);

            chan->pos = (uint32_t)cur_pos;
            chan->pfrac = cur_pfrac;
            chan->pval = cur_pval;
            chan->val = cur_val;
            if (regupdate()) {
                base1[0x0c] = cur_pos & 0xff;
                base1[0x0d] = cur_pos >> 8 & 0xff;
                base1[0x0e] = cur_pos >> 16 & 0xff;
            }
        }
        m_reverb_pos = (m_reverb_pos + 1) & 0x1fff;
        // MAME stream.put_int(ch, sample, lval, 32768) takes an integer sample:
        // the double is truncated, then scaled. (Not yet checked against a MAME WAV.)
        lr[2 * sample] += (float)(int32_t)lval / 32768.0f;
        lr[2 * sample + 1] += (float)(int32_t)rval / 32768.0f;
    }
}

void K054539::commit() {
    m_seen_status = m_regs[0x22c];
    for (int ch = 0; ch < 8; ch++)
        for (int k = 0; k < 3; k++) m_seen_pos[ch][k] = m_regs[0x20 * ch + 0x0c + k];
}

void K054539::write(uint32_t offset, uint8_t data) {
    write_reg(offset, data);
    commit();   // MAME updated the stream first: everything up to here is visible
}

void K054539::write_reg(uint32_t offset, uint8_t data) {
    offset &= 0x3ff;
    if (offset >= sizeof m_regs) return;
    const bool latch = (m_flags & UPDATE_AT_KEYON) && (m_regs[0x22f] & 1);
    if (latch && offset < 0x100) {
        int offs = (int)(offset & 0x1f) - 0xc;
        int ch = (int)(offset >> 5);
        if (offs >= 0 && offs <= 2) {   // latch writes to the position registers
            m_posreg_latch[ch][offs] = data;
            return;
        }
    } else {
        switch (offset) {
        case 0x214:
            for (int ch = 0; ch < 8; ch++)
                if (data & (1 << ch)) {
                    if (latch) {   // update the chip at key-on
                        uint8_t *regptr = m_regs + (ch << 5) + 0xc;
                        regptr[0] = m_posreg_latch[ch][0];
                        regptr[1] = m_posreg_latch[ch][1];
                        regptr[2] = m_posreg_latch[ch][2];
                    }
                    keyon(ch);
                }
            break;
        case 0x215:
            for (int ch = 0; ch < 8; ch++)
                if (data & (1 << ch)) keyoff(ch);
            break;
        case 0x22d:
            if (m_rom_addr == 0x80) {
                uint32_t addr = (uint32_t)(m_cur_ptr & 0x3fff) | (uint32_t)((m_cur_ptr & 0x10000) >> 2);
                m_ram[addr] = data;
            }
            m_cur_ptr = (m_cur_ptr + 1) & 0x1ffff;
            break;
        case 0x22e:
            m_rom_addr = data;
            m_cur_ptr = 0;
            break;
        default:
            break;
        }
    }
    m_regs[offset] = data;
}

uint8_t K054539::read(uint32_t offset) {
    offset &= 0x3ff;
    if (offset >= sizeof m_regs) return 0;
    if (offset == 0x22d) {
        if (m_regs[0x22f] & 0x10) {
            uint32_t addr = (uint32_t)(m_cur_ptr & 0x3fff) | (uint32_t)((m_cur_ptr & 0x10000) >> 2);
            uint8_t res = (m_rom_addr == 0x80) ? m_ram[addr] : read_byte(0x20000u * (uint32_t)m_rom_addr + (uint32_t)m_cur_ptr);
            m_cur_ptr = (m_cur_ptr + 1) & 0x1ffff;
            return res;
        }
        return 0;
    }
    if (offset == 0x22c) return m_seen_status;
    if (offset < 0x100 && (offset & 0x1f) >= 0x0c && (offset & 0x1f) <= 0x0e)
        return m_seen_pos[offset >> 5][(offset & 0x1f) - 0x0c];
    return m_regs[offset];
}

}  // namespace hk::dj
