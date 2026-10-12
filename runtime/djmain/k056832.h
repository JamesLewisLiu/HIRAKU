// license:BSD-3-Clause
// copyright-holders:David Haywood
//
// Konami 056832 tilemap chip, as DJ Main uses it. Ported to HIRAKU from MAME's
// k054156_k054157_k056832.cpp (BSD-3-Clause): the registers, the paged tile RAM,
// the page layout / layer association, the 4bpp "dj" tile format and
// tilemap_draw_dj(). MAME's tilemap_t (which caches each 512x256 page as a
// pixmap) is replaced by drawing straight from tile RAM, with MAME's scroll and
// wrap arithmetic. Only the DJ Main configuration is kept (K056832_BPP_4dj,
// big = 1, djmain_hack = 1, no linemap, no tile banks).
#pragma once

#include <cstdint>
#include <vector>

namespace hk::dj {

class K056832 {
public:
    static constexpr int PAGES = 16, PAGE_W = 512, PAGE_H = 256;

    // `tiles`: the tile ROM region (MAME "k056832"), 32 bytes per 8x8 tile.
    void init(const std::vector<uint8_t> &tiles);

    uint16_t word_r(uint32_t offset) const { return m_regs[offset & 0x1f]; }
    void word_w(uint32_t offset, uint16_t data, uint16_t mem_mask);
    uint16_t ram_word_r(uint32_t offset) const { return m_videoram[m_selected_page_x4096 + (offset & 0xfff)]; }
    void ram_word_w(uint32_t offset, uint16_t data, uint16_t mem_mask);
    void set_layer_offs(int layer, int x, int y) { m_layer_offs[layer][0] = x; m_layer_offs[layer][1] = y; }

    // MAME tilemap_draw_dj(screen, bitmap, cliprect, layer, 0, priority): draw
    // `layer` into `bitmap` (xRGB, bw x bh, the whole screen is the clip),
    // pens through `pens` (palette as xRGB, 16 per colour).
    void draw_dj(int layer, uint32_t *bitmap, int bw, int bh, const uint32_t *pens, int npens);

private:
    void update_page_layout();
    void change_rambank();
    // Draw one page (a MAME tilemap_t) scrolled by (scrollx, scrolly) into the
    // rectangle [x0, x1] x [y0, y1].
    void draw_page(int page, int scrollx, int scrolly, int x0, int y0, int x1, int y1, uint32_t *bitmap, int bw,
                   const uint32_t *pens, int npens) const;

    uint16_t m_regs[0x20]{};
    std::vector<uint16_t> m_videoram = std::vector<uint16_t>(0x2000 * (PAGES + 1) / 2);
    std::vector<uint8_t> m_pens;           // decoded tiles, 64 pens each
    uint32_t m_tile_count = 0;
    int m_selected_page = 0, m_selected_page_x4096 = 0;
    int m_layer_assoc_with_page[PAGES]{};
    int m_layer_offs[8][2]{};
    int m_lsram_page[8][2]{};
    int m_x[8]{}, m_y[8]{}, m_w[8]{}, m_h[8]{}, m_dx[8]{}, m_dy[8]{};
    int m_layer_tile_mode[8]{};
    int m_page_tile_mode[PAGES]{};
    int m_layer_association = 1, m_default_layer_association = 1, m_active_layer = 0;
};

}  // namespace hk::dj
