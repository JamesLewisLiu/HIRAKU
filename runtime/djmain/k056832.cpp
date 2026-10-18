// license:BSD-3-Clause
// copyright-holders:David Haywood
//
// Konami 056832 for DJ Main, from MAME's k054156_k054157_k056832.cpp (see k056832.h).
#include "k056832.h"

#include <algorithm>

namespace hk::dj {

void K056832::init(const std::vector<uint8_t> &tiles) {
    // MAME charlayout4dj: 8x8, 4 planes at bit offsets {24, 8, 16, 0} (plane 0 is
    // the pen's MSB), x offsets 0..7, rows 32 bits apart, 32 bytes per tile; bits
    // are numbered MSB first within each byte.
    static const int planeoffs[4] = {24, 8, 16, 0};
    m_tile_count = (uint32_t)(tiles.size() / 32);
    m_pens.assign((size_t)m_tile_count * 64, 0);
    for (uint32_t t = 0; t < m_tile_count; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                uint8_t pen = 0;
                for (int p = 0; p < 4; p++) {
                    uint32_t bit = t * 256 + (uint32_t)planeoffs[p] + (uint32_t)x + (uint32_t)y * 32;
                    if (tiles[bit / 8] & (0x80 >> (bit % 8))) pen |= (uint8_t)(1 << (3 - p));
                }
                m_pens[(size_t)t * 64 + (size_t)y * 8 + (size_t)x] = pen;
            }

    // create_tilemaps()
    for (int i = 0; i < 8; i++) {
        m_lsram_page[i][0] = i;
        m_lsram_page[i][1] = i << 11;
        m_layer_tile_mode[i] = 1;
    }
    for (int i = 0; i < PAGES; i++) m_page_tile_mode[i] = 1;
    // finalize_init()
    update_page_layout();
    change_rambank();
}

void K056832::change_rambank() {
    int bank = m_regs[0x19];
    if (m_regs[0] & 0x02) m_selected_page = PAGES;   // external linescroll enable
    else m_selected_page = ((bank >> 1) & 0xc) | (bank & 3);
    m_selected_page_x4096 = m_selected_page << 12;
}

void K056832::update_page_layout() {
    m_layer_association = m_default_layer_association;
    // disable association if a layer grabs the entire 4x4 map
    for (int layer = 0; layer < 4; layer++)
        if (!m_y[layer] && !m_x[layer] && m_h[layer] == 3 && m_w[layer] == 3) {
            m_layer_association = 0;
            break;
        }
    for (int &a : m_layer_assoc_with_page) a = -1;
    for (int layer = 0; layer < 4; layer++) {
        const int rowspan = m_h[layer] + 1, colspan = m_w[layer] + 1;
        const int setlayer = m_layer_association ? layer : m_active_layer;
        for (int r = 0; r < rowspan; r++)
            for (int c = 0; c < colspan; c++) {
                int page_idx = (((m_y[layer] + r) & 3) << 2) + ((m_x[layer] + c) & 3);
                if (m_layer_assoc_with_page[page_idx] == -1)   // djmain_hack == 1
                    m_layer_assoc_with_page[page_idx] = setlayer;
            }
    }
    // mark_all_tilemaps_dirty(): pages take their layer's tile mode
    for (int i = 0; i < PAGES; i++)
        if (m_layer_assoc_with_page[i] != -1) m_page_tile_mode[i] = m_layer_tile_mode[m_layer_assoc_with_page[i]];
}

void K056832::ram_word_w(uint32_t offset, uint16_t data, uint16_t mem_mask) {
    uint16_t &t = m_videoram[m_selected_page_x4096 + (offset & 0xfff)];
    t = (uint16_t)((data & mem_mask) | (t & ~mem_mask));
}

void K056832::word_w(uint32_t offset, uint16_t data, uint16_t mem_mask) {
    offset &= 0x1f;
    const uint16_t old_data = m_regs[offset];
    m_regs[offset] = (uint16_t)((data & mem_mask) | (old_data & ~mem_mask));
    const uint16_t new_data = m_regs[offset];
    if (new_data == old_data) return;
    switch (offset) {
    case 0x00 / 2:
        // bits 4/5 (screen flip) only retarget MAME's tilemaps; DJ Main never sets them
        if ((new_data & 0x02) != (old_data & 0x02)) change_rambank();
        break;
    case 0x08 / 2:
        for (int layer = 0; layer < 4; layer++) {
            const int mask = 1 << layer, i = new_data & mask;
            if (i != (old_data & mask)) {
                m_layer_tile_mode[layer] = i;
                for (int p = 0; p < PAGES; p++)   // mark_plane_dirty()
                    if (m_layer_assoc_with_page[p] == layer) m_page_tile_mode[p] = i;
            }
        }
        break;
    case 0x32 / 2:
        change_rambank();
        break;
    case 0x34 / 2: case 0x36 / 2:   // ROM bank select: v_rom_r reads the register itself
        break;
    default: {
        const int layer = offset & 3;
        if (offset >= 0x10 / 2 && offset <= 0x16 / 2) {
            m_y[layer] = (new_data & 0x18) >> 3;
            m_h[layer] = new_data & 0x3;
            m_active_layer = layer;
            update_page_layout();
        } else if (offset >= 0x18 / 2 && offset <= 0x1e / 2) {
            m_x[layer] = (new_data & 0x18) >> 3;
            m_w[layer] = new_data & 0x03;
            m_active_layer = layer;
            update_page_layout();
        } else if (offset >= 0x20 / 2 && offset <= 0x26 / 2) {
            m_dy[layer] = (int16_t)new_data;
        } else if (offset >= 0x28 / 2 && offset <= 0x2e / 2) {
            m_dx[layer] = (int16_t)new_data;
        }
        break;
    }
    }
}

void K056832::draw_page(int page, int scrollx, int scrolly, int x0, int y0, int x1, int y1, uint32_t *bitmap, int bw,
                        const uint32_t *pens, int npens) const {
    // get_tile_info(): which layer's settings this page uses
    int layer = m_layer_association ? m_layer_assoc_with_page[page] : m_active_layer;
    if (layer == -1) layer = 0;
    static const struct { int flips, palm1, pals2, palm2; } shiftmasks[4] = {
        {6, 0x3f, 0, 0x00}, {4, 0x0f, 2, 0x30}, {2, 0x03, 2, 0x3c}, {0, 0x00, 2, 0x3f}};
    const auto &sm = shiftmasks[(m_regs[3] >> 6) & 3];
    const int flip_override = (m_regs[1] >> (layer << 1)) & 3;
    const uint16_t *ram = &m_videoram[(size_t)page << 12];

    // MAME tilemap_t: the page repeats every 512x256; screen pixel (x, y) shows
    // page pixel ((x + scrollx) mod 512, (y + scrolly) mod 256)
    auto wrap = [](int v, int m) { return ((v % m) + m) % m; };
    // a tile at a time: its attributes, colour and pixel row are worked out once for its (up to)
    // eight pixels on this line, not per pixel (the same pixels as per pixel)
    for (int y = y0; y <= y1; y++) {
        const int ty = wrap(y + scrolly, PAGE_H);
        const uint16_t *row = ram + (ty >> 3) * 64 * 2;
        uint32_t *d = bitmap + (size_t)y * bw;
        int tx = wrap(x0 + scrollx, PAGE_W);
        for (int x = x0; x <= x1;) {
            const uint16_t attr = row[(tx >> 3) * 2], code = row[(tx >> 3) * 2 + 1];
            const int flip = flip_override & (attr >> sm.flips) & 3;
            const int color = (attr & sm.palm1) | (attr >> sm.pals2 & sm.palm2);
            const int py = (flip & 2) ? 7 - (ty & 7) : (ty & 7);
            const uint8_t *tile = &m_pens[(size_t)(code % m_tile_count) * 64 + (size_t)py * 8];
            const int sub = tx & 7, n = std::min(8 - sub, x1 - x + 1);
            const int limit = npens - color * 16;   // pens[color * 16 + pen] exists for pen < limit
            const uint32_t *pal = pens + color * 16;
            for (int k = 0; k < n; k++) {
                const uint8_t pen = tile[(flip & 1) ? 7 - (sub + k) : sub + k];
                if (pen && pen < limit) d[x + k] = pal[pen];   // pen 0 is transparent
            }
            x += n;
            tx = wrap(tx + n, PAGE_W);
        }
    }
}

void K056832::draw_dj(int layer, uint32_t *bitmap, int bw, int bh, const uint32_t *pens, int npens) {
    const int cminx = 0, cmaxx = bw - 1, cminy = 0, cmaxy = bh - 1;
    const int rowstart = m_y[layer], colstart = m_x[layer];
    const int rowspan = m_h[layer] + 1, colspan = m_w[layer] + 1;
    int dy = m_dy[layer];
    const int scrollbank = ((m_regs[0x18] >> 1) & 0xc) | (m_regs[0x18] & 3);
    const int scrollmode = m_regs[0x05] >> (m_lsram_page[layer][0] << 1) & 3;
    int need_wrap = -1;
    const int height = rowspan * PAGE_H, width = colspan * PAGE_W;

    // flip correction registers (screen flip: unused by DJ Main, kept for fidelity)
    const int flipy = m_regs[0] & 0x20, flipx = m_regs[0] & 0x10;
    int corr = 0;
    if (flipy) {
        corr = m_regs[0x3c / 2];
        if (corr & 0x400) corr |= (int)0xfffff800;
    }
    dy += corr;
    const int ay = (int)((unsigned)(dy - m_layer_offs[layer][1]) % (unsigned)height);
    corr = 0;
    if (flipx) {
        corr = m_regs[0x3a / 2];
        if (corr & 0x800) corr |= (int)0xfffff000;
    }
    corr -= m_layer_offs[layer][0];

    const uint16_t *p_scroll_data;
    uint16_t ram16[2];
    int line_height, sdat_wrapmask, sdat_adv;
    switch (scrollmode) {
    case 0:   // linescroll
        p_scroll_data = &m_videoram[(size_t)scrollbank << 12] + (m_lsram_page[layer][1] >> 1);
        line_height = 1, sdat_wrapmask = 0x3ff, sdat_adv = 2;
        break;
    case 2:   // rowscroll
        p_scroll_data = &m_videoram[(size_t)scrollbank << 12] + (m_lsram_page[layer][1] >> 1);
        line_height = 8, sdat_wrapmask = 0x3ff, sdat_adv = 16;
        break;
    default:  // xyscroll
        p_scroll_data = ram16;
        line_height = PAGE_H, sdat_wrapmask = 0, sdat_adv = 0;
        ram16[0] = 0;
        ram16[1] = (uint16_t)m_dx[layer];
    }
    if (flipy) sdat_adv = -sdat_adv;

    const int last_active = m_active_layer;
    for (int r = 0; r <= rowspan; r++) {
        const int sy = ay;
        int ty;
        if (r == rowspan) {
            if (need_wrap < 0) continue;
            ty = need_wrap * PAGE_H;
        } else {
            ty = r * PAGE_H;
        }
        if ((sy + height <= ty) || (sy - height >= ty)) continue;   // cull off-screen pages
        ty -= sy;

        int cliph, clipy, line_starty, line_endy, sdat_start;
        if (r == rowspan) {   // top-edge wraparound
            cliph = PAGE_H + ty;
            clipy = line_starty = 0;
            line_endy = cliph;
            ty = -ty;
            sdat_start = ty;
            if (scrollmode == 2) {
                sdat_start &= ~7;
                line_starty -= ty & 7;
            }
        } else {
            if (ty < 0) ty += height;
            clipy = ty;
            cliph = PAGE_H;
            if (clipy + cliph > height) {
                cliph = height - clipy;
                need_wrap = r;
            }
            line_starty = ty;
            line_endy = line_starty + cliph;
            sdat_start = 0;
        }
        sdat_start += (r == rowspan ? need_wrap : r) * PAGE_H;
        sdat_start <<= 1;
        const int clipmaxy = clipy + cliph - 1;

        for (int c = 0; c < colspan; c++) {
            const int pageIndex = r == rowspan ? (((rowstart + need_wrap) & 3) << 2) + ((colstart + c) & 3)
                                               : (((rowstart + r) & 3) << 2) + ((colstart + c) & 3);
            if (m_layer_association) {
                if (m_layer_assoc_with_page[pageIndex] != layer) continue;
            } else {
                if (m_layer_assoc_with_page[pageIndex] == -1) continue;
                m_active_layer = layer;
            }
            // update_linemap(): pages in line mode are not drawn (no linemap on DJ Main)
            if (!m_page_tile_mode[pageIndex]) continue;

            uint32_t last_dx = 0x100000;
            bool last_visible = false;
            int last_x0 = 0, last_x1 = -1;
            for (int sdat_walk = sdat_start, line_y = line_starty; line_y < line_endy; sdat_walk += sdat_adv, line_y += line_height) {
                int dminy = line_y, dmaxy = line_y + line_height - 1;
                if (dminy < clipy) dminy = clipy;
                if (dmaxy > clipmaxy) dmaxy = clipmaxy;
                if (dminy > cmaxy || dmaxy < cminy) continue;
                const int sdat_offs = sdat_walk & sdat_wrapmask;
                const int ry0 = std::max(dminy, cminy), ry1 = std::min(dmaxy, cmaxy);
                const int dx = ((int)p_scroll_data[sdat_offs] << 16 | (int)p_scroll_data[sdat_offs + 1]) + corr;

                if (last_dx == (uint32_t)dx) {
                    if (last_visible) draw_page(pageIndex, last_dx, ay, last_x0, ry0, last_x1, ry1, bitmap, bw, pens, npens);
                    continue;
                }
                last_dx = (uint32_t)dx;

                int clipw, clipx;
                if (colspan > 1) {
                    int sx = (int)((unsigned)dx & (unsigned)(width - 1));
                    int tx = c << 9;
                    if (!flipx) {
                        if ((c == 0) && (sx > width - PAGE_W)) sx -= width;   // right-edge wraparound
                        if ((sx + PAGE_W <= tx) || (sx - PAGE_W >= tx)) { last_visible = false; continue; }
                        if ((tx -= sx) <= 0) { clipw = PAGE_W + tx; clipx = 0; }
                        else { clipw = PAGE_W - tx; clipx = tx; }
                    } else {
                        tx += PAGE_W;
                        if ((c == colspan - 1) && (sx < PAGE_W)) sx += width;   // left-edge wraparound
                        if ((sx + PAGE_W <= tx) || (sx - PAGE_W >= tx)) { last_visible = false; continue; }
                        if ((tx -= sx) >= 0) { clipw = PAGE_W - tx; clipx = 0; }
                        else { clipw = PAGE_W + tx; clipx = -tx; }
                    }
                } else {
                    clipw = PAGE_W;
                    clipx = 0;
                }
                last_visible = true;
                last_x0 = std::max(clipx, cminx);
                last_x1 = std::min(clipx + clipw - 1, cmaxx);
                draw_page(pageIndex, dx, ay, last_x0, ry0, last_x1, ry1, bitmap, bw, pens, npens);
            }
        }
    }
    m_active_layer = last_active;
}

}  // namespace hk::dj
