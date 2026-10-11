// license:BSD-3-Clause
// copyright-holders:Ville Linde
//
// Konami 0000057714 "GCU" 2D graphics chip, ported from MAME's k057714.cpp.
// Modified for HIRAKU: no MAME device framework; pixels are addressed through
// pix()/set_pix() instead of an endian-swizzled uint16_t view; output is
// composed into a 32-bit RGB buffer.
#include "k057714.h"

#include <algorithm>
#include <cstring>

namespace hk {

K057714::K057714() : m_vram(new uint32_t[VRAM_SIZE / 4]) { reset(); }

// VRAM is an array of 32-bit words; 16-bit pixel n is the high half of word
// n/2 when n is even (the chip is big-endian, as MAME's swizzle encodes).
inline uint16_t K057714::pix(uint32_t n) const {
    uint32_t w = m_vram[(n >> 1) & (VRAM_SIZE / 4 - 1)];
    return (n & 1) ? (uint16_t)w : (uint16_t)(w >> 16);
}
inline void K057714::set_pix(uint32_t n, uint16_t v) {
    uint32_t &w = m_vram[(n >> 1) & (VRAM_SIZE / 4 - 1)];
    w = (n & 1) ? ((w & 0xffff0000u) | v) : ((w & 0x0000ffffu) | ((uint32_t)v << 16));
}

void K057714::reset() {
    // beatmania III relies on the 640x480 default (it never programs the CRTC)
    m_display_h_visarea = 640; m_display_h_frontporch = 16; m_display_h_backporch = 48; m_display_h_syncpulse = 96;
    m_display_v_visarea = 480; m_display_v_frontporch = 10; m_display_v_backporch = 33; m_display_v_syncpulse = 2;
    m_pixclock = 25'175'000.0;
    m_vram_read_addr = 0;
    m_command_fifo0_ptr = m_command_fifo1_ptr = 0;
    m_vram_fifo0_addr = m_vram_fifo1_addr = 0;
    m_fb_origin_x = m_fb_origin_y = 0;
    m_reg_6c = 0;
    for (auto &f : m_frame) f = Frame{0, 0, 0, 0, 0, (uint16_t)((16 << 7) | (16 << 2))};
    memset(m_vram.get(), 0, VRAM_SIZE);
}

int K057714::htotal() const { return m_display_h_visarea + m_display_h_frontporch + m_display_h_backporch + m_display_h_syncpulse; }
int K057714::vtotal() const { return m_display_v_visarea + m_display_v_frontporch + m_display_v_backporch + m_display_v_syncpulse; }

uint32_t K057714::read(uint32_t reg) {
    if (reg >= 0x80 && reg < 0x100) return m_vram[(m_vram_read_addr + (reg >> 2) - 0x20) & (VRAM_SIZE / 4 - 1)];
    if (reg == 0x78) return 0xffff0005;   // GCU status (constant in MAME)
    return 0xffffffff;
}

void K057714::write(uint32_t reg, uint32_t data, uint32_t mem_mask) {
    const bool hi = mem_mask & 0xffff0000u, lo = mem_mask & 0x0000ffffu;
    switch (reg) {
    case 0x00:
        if (hi) m_display_h_visarea = ((data >> 16) & 0xffff) + 1;
        if (lo) { m_display_h_frontporch = ((data >> 8) & 0xff) + 1; m_display_h_backporch = (data & 0xff) + 1; }
        break;
    case 0x04:
        if (hi) m_display_v_visarea = ((data >> 16) & 0xffff) + 1;
        if (lo) { m_display_v_frontporch = ((data >> 8) & 0xff) + 1; m_display_v_backporch = (data & 0xff) + 1; }
        break;
    case 0x08:
        if (hi) { m_display_h_syncpulse = ((data >> 24) & 0xff) + 1; m_display_v_syncpulse = ((data >> 16) & 0xff) + 1; }
        break;
    case 0x10:
        // IRQ clear/enable: games write the bit off then on in response to the interrupt
        if (hi) {
            uint16_t d = data >> 16;
            if (!(d & 1) && irq) irq(false);
            m_irqctrl = d;
        }
        if (lo) m_layer_select = data & 0xffff;
        break;
    case 0x14:
        if (hi) m_frame[0].alpha = (data >> 16) & 0xffff;
        if (lo) m_frame[1].alpha = data & 0xffff;
        break;
    case 0x18:
        if (hi) m_frame[2].alpha = (data >> 16) & 0xffff;
        if (lo) m_frame[3].alpha = data & 0xffff;
        break;
    case 0x1c:
        if ((data >> 16) == 1) { m_ext_fifo_count = 0; m_ext_fifo_line = 0; }
        break;
    case 0x20: case 0x24: case 0x28: case 0x2c: {
        Frame &f = m_frame[(reg - 0x20) / 4];
        if (hi) f.y = (data >> 16) & 0xffff;
        if (lo) f.x = data & 0xffff;
        break;
    }
    case 0x30: case 0x34: case 0x38: case 0x3c: {
        Frame &f = m_frame[(reg - 0x30) / 4];
        if (hi) f.height = (data >> 16) & 0xffff;
        if (lo) f.width = data & 0xffff;
        break;
    }
    case 0x40: case 0x44: case 0x48: case 0x4c:
        m_frame[(reg - 0x40) / 4].base = data;
        break;
    case 0x54:
        if (hi) m_ext_fifo_num_lines = data >> 16;
        if (lo) m_ext_fifo_width = data & 0xffff;
        break;
    case 0x58: m_ext_fifo_addr = data & 0xffffff; break;
    case 0x5c: m_vram_read_addr = (data & 0xffffff) / 2; break;
    case 0x60: m_vram_fifo0_addr = (data & 0xffffff) / 2; break;
    case 0x64: m_vram_fifo1_addr = (data & 0xffffff) / 2; break;
    case 0x68:
        if (hi) m_vram_fifo0_mode = data >> 16;
        if (lo) m_vram_fifo1_mode = data & 0xffff;
        break;
    case 0x70:
        if (m_vram_fifo0_mode & 0x100) {
            m_command_fifo0[m_command_fifo0_ptr++] = data;
            if (m_command_fifo0_ptr >= 4) { execute_command(m_command_fifo0); m_command_fifo0_ptr = 0; }
        } else {
            m_vram[m_vram_fifo0_addr++ & (VRAM_SIZE / 4 - 1)] = data;
        }
        break;
    case 0x74:
        if (m_vram_fifo1_mode & 0x100) {
            m_command_fifo1[m_command_fifo1_ptr++] = data;
            if (m_command_fifo1_ptr >= 4) { execute_command(m_command_fifo1); m_command_fifo1_ptr = 0; }
        } else {
            m_vram[m_vram_fifo1_addr++ & (VRAM_SIZE / 4 - 1)] = data;
        }
        break;
    case 0x6c:
        if (lo) m_reg_6c = data & 0xffff;
        break;
    default:
        break;
    }
}

void K057714::vblank(bool state) {
    if (state && (m_irqctrl & 1) && irq) irq(true);
}

void K057714::draw_frame(int frame, uint16_t *bitmap, int bw, int bh, bool inverse_trans) {
    const Frame &f = m_frame[frame];
    if (f.height == 0 || f.width == 0) return;
    int height = f.height + 1, width = f.width + 1;
    int alpha = f.alpha, blend_mode = alpha & 3;
    int alpha1 = (alpha >> 7) & 0x1f;   // beatmania III uses this for blend mode 1
    int alpha2 = (alpha >> 2) & 0x1f;   // pop'n music has alpha 1 and 2 the same for blend mode 1
    if (blend_mode == 2 && alpha1) alpha1 = (alpha2 * 16) / alpha1;
    const uint16_t trans_value = inverse_trans ? 0x8000 : 0x0000;
    const int max_y = bh - 1, max_x = bw - 1;
    if (f.y + height > max_y) height = max_y - f.y;
    if (f.x + width > max_x) width = max_x - f.x;
    for (int j = 0; j <= height; j++) {
        if (j + f.y < 0 || j + f.y >= bh) continue;
        uint16_t *d = bitmap + (j + f.y) * bw + f.x;
        int li = j * 1024;
        for (int i = 0; i <= width && f.x + i < bw; i++) {
            uint16_t p = pix(f.base + li + i);
            if ((p & 0x8000) != trans_value) {
                uint32_t r = std::min(((p >> 10) & 0x1fu) * alpha1 >> 4, 0x1fu);
                uint32_t g = std::min(((p >> 5) & 0x1fu) * alpha1 >> 4, 0x1fu);
                uint32_t b = std::min(((p >> 0) & 0x1fu) * alpha1 >> 4, 0x1fu);
                d[i] = (uint16_t)((r << 10) | (g << 5) | b);
            }
        }
    }
}

void K057714::draw(uint32_t *out, int w, int h) {
    static thread_local std::unique_ptr<uint16_t[]> bmp;
    static thread_local int bmp_size = 0;
    if (bmp_size < w * h) { bmp.reset(new uint16_t[w * h]); bmp_size = w * h; }
    std::fill(bmp.get(), bmp.get() + w * h, 0);
    // most likely wrong, inverse transparency is only used by kbm;
    // beatmania III sets reg 6c to 0xfff but doesn't use inverse transparency
    bool inverse_trans = (m_reg_6c & 0xf) != 0 && m_reg_6c != 0xfff;
    draw_frame((m_layer_select >> 8) & 3, bmp.get(), w, h, inverse_trans);
    draw_frame((m_layer_select >> 10) & 3, bmp.get(), w, h, inverse_trans);
    draw_frame((m_layer_select >> 12) & 3, bmp.get(), w, h, inverse_trans);
    draw_frame((m_layer_select >> 14) & 3, bmp.get(), w, h, inverse_trans);
    for (int k = 0; k < w * h; k++) {
        uint16_t p = bmp[k];
        uint32_t r = (p >> 10) & 0x1f, g = (p >> 5) & 0x1f, b = p & 0x1f;
        out[k] = 0xff000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
    }
}

void K057714::draw_object(const uint32_t *cmd) {
    uint32_t address_x = cmd[0] & 0x3ff;
    uint32_t address_y = (cmd[0] >> 10) & 0x3fff;
    bool relative_coords = cmd[0] & 0x10000000;
    int x = cmd[1] & 0x3ff;
    int y = (cmd[1] >> 10) & 0x3fff;
    bool xflip = cmd[1] & 0x04000000, yflip = cmd[1] & 0x08000000;
    int blend_mode = (cmd[1] >> 28) & 3;
    bool trans_enable = cmd[1] & 0xc0000000;
    uint16_t trans_value = (cmd[1] & 0x80000000) ? 0x0000 : 0x8000;
    int width = (cmd[2] & 0x1ff) + 1;
    int xscale = ((cmd[2] >> 10) & 0x7ff) * (((cmd[2] >> 10) & 0x800) ? -1 : 1);
    int alpha1_1 = (cmd[2] >> 22) & 0x1f, alpha1_2 = (cmd[2] >> 27) & 0x1f;
    int height = (cmd[3] & 0x3ff) + 1;
    int yscale = ((cmd[3] >> 10) & 0x7ff) * (((cmd[3] >> 10) & 0x800) ? -1 : 1);
    int alpha2_1 = (cmd[3] >> 22) & 0x1f, alpha2_2 = (cmd[3] >> 27) & 0x1f;
    if (xscale == 0 || yscale == 0) return;
    if (xflip && (uint32_t)(4 - ((width - 1) % 4)) <= (address_x % 4)) address_x -= 4;   // pop'n music 8 @ 0x800b30d0
    if (yflip) y -= (((height * 64) - 1) / yscale) - (((height - 1) * 64) / yscale);    // pop'n music 8 @ 0x800b3140
    if (relative_coords) { x += m_fb_origin_x; y += m_fb_origin_y; }
    uint32_t address = (address_y << 10) | address_x;
    int orig_height = height;
    width = (((width * 65536) / xscale) * 64) / 65536;
    height = (((height * 65536) / yscale) * 64) / 65536;
    if (height <= 0 || width <= 0) return;
    int v = 0, xinc = xflip ? -1 : 1;
    for (int j = 0; j < height; j++) {
        uint32_t fbaddr = ((j + y) * 1024) + x;
        int index = yflip ? address + ((orig_height - 1 - (v >> 6)) * 1024) : address + ((v >> 6) * 1024);
        if (xflip) fbaddr += width - 1;
        int u = 0;
        for (int i = 0; i < width; i++) {
            uint16_t p = pix((index + (u >> 6)) & 0xffffff);
            bool draw = !trans_enable || ((p & 0x8000) == trans_value);
            if (fbaddr < VRAM_SIZE_HALF && draw) {
                if (blend_mode) {
                    uint16_t s = pix(fbaddr);
                    uint32_t sr = (s >> 10) & 0x1f, sg = (s >> 5) & 0x1f, sb = s & 0x1f;
                    uint32_t r = (p >> 10) & 0x1f, g = (p >> 5) & 0x1f, b = p & 0x1f;
                    if (blend_mode == 1) {
                        sr = ((sr * alpha2_2) + (r * alpha1_2)) >> 4;
                        sg = ((sg * alpha2_2) + (g * alpha1_2)) >> 4;
                        sb = ((sb * alpha2_2) + (b * alpha1_2)) >> 4;
                    } else if (blend_mode == 2) {   // Keyboardmania glow effects
                        sr = ((sr * alpha2_1) + (r * alpha1_1)) >> 4;
                        sg = ((sg * alpha2_1) + (g * alpha1_1)) >> 4;
                        sb = ((sb * alpha2_1) + (b * alpha1_1)) >> 4;
                    }
                    sr = std::min(sr, 0x1fu); sg = std::min(sg, 0x1fu); sb = std::min(sb, 0x1fu);
                    p = (uint16_t)((sr << 10) | (sg << 5) | sb | (p & 0x8000));
                }
                set_pix(fbaddr, p);
            }
            fbaddr += xinc;
            u += xscale;
        }
        v += yscale;
    }
}

void K057714::fill_rect(const uint32_t *cmd) {
    int x = cmd[1] & 0x3ff, y = (cmd[1] >> 10) & 0x3fff;
    int width = (cmd[0] & 0x3ff) + 1, height = ((cmd[0] >> 10) & 0x3ff) + 1;
    if (cmd[0] & 0x10000000) { x += m_fb_origin_x; y += m_fb_origin_y; }
    uint16_t color[4] = {(uint16_t)(cmd[2] >> 16), (uint16_t)cmd[2], (uint16_t)(cmd[3] >> 16), (uint16_t)cmd[3]};
    for (int j = y; j < y + height; j++)
        for (int i = x; i < x + width; i++) set_pix((uint32_t)(j * 1024 + i), color[i & 3]);
}

void K057714::draw_character(const uint32_t *cmd) {
    int x = cmd[1] & 0x3ff, y = (cmd[1] >> 10) & 0x3fff;
    uint32_t address = cmd[0] & 0xffffff;
    if (cmd[0] & 0x10000000) { x += m_fb_origin_x; y += m_fb_origin_y; }
    bool double_height = cmd[1] & 0x01000000, trans_enable = cmd[1] & 0x40000000;
    uint16_t color[4] = {(uint16_t)(cmd[2] >> 16), (uint16_t)cmd[2], (uint16_t)(cmd[3] >> 16), (uint16_t)cmd[3]};
    int height = double_height ? 16 : 8;
    for (int j = 0; j < height; j++) {
        uint32_t fbaddr = (y + j) * 1024;
        uint16_t line = pix(address);
        address += 4;
        for (int i = 0; i < 8; i++) {
            int p = (line >> ((7 - i) * 2)) & 3;
            if (!trans_enable || (color[p] & 0x8000)) set_pix(fbaddr + x + i, color[p]);
        }
    }
}

void K057714::fb_config(const uint32_t *cmd) {
    m_fb_origin_x = cmd[2] & 0x3ff;
    m_fb_origin_y = cmd[3] & 0x3fff;
}

void K057714::execute_display_list(uint32_t addr) {
    bool end = false;
    int counter = 0;
    addr /= 2;
    while (!end && counter < 0x1000 && addr < VRAM_SIZE / 4) {
        const uint32_t *cmd = &m_vram[addr];
        addr += 4;
        switch ((cmd[0] >> 29) & 7) {
        case 0: break;
        case 1: execute_display_list(cmd[0] & 0xffffff); break;
        case 2: end = true; break;
        case 3: fb_config(cmd); break;
        case 4: fill_rect(cmd); break;
        case 5: draw_object(cmd); break;
        default: draw_character(cmd); break;   // 6, 7
        }
        counter++;
    }
}

void K057714::execute_command(const uint32_t *cmd) {
    switch ((cmd[0] >> 29) & 7) {
    case 0: case 2: break;
    case 1: execute_display_list(cmd[0] & 0xffffff); break;
    case 3: fb_config(cmd); break;
    case 4: fill_rect(cmd); break;
    case 5: draw_object(cmd); break;
    default: draw_character(cmd); break;
    }
}

}  // namespace hk
