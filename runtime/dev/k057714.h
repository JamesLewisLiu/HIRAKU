// license:BSD-3-Clause
// copyright-holders:Ville Linde
//
// Konami 0000057714 "GCU" 2D graphics chip.
// Ported to HIRAKU from MAME's k057714.cpp (BSD-3-Clause); the MAME device
// framework is replaced by plain C++, the drawing logic is unchanged.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace hk {

class K057714 {
public:
    static constexpr uint32_t VRAM_SIZE = 0x2000000;        // bytes
    static constexpr uint32_t VRAM_SIZE_HALF = 0x1000000;   // 16-bit pixels

    K057714();
    void reset();

    uint32_t read(uint32_t reg);                            // reg = byte offset, 32-bit aligned
    void write(uint32_t reg, uint32_t data, uint32_t mem_mask);

    void vblank(bool state);                                // start of vertical blank
    std::function<void(bool)> irq;                          // to the CPU's EXT0

    // Compose the visible frame into `out` (xRGB8888, width*height).
    void draw(uint32_t *out, int width, int height);

    // CRTC timing (for the scheduler)
    int htotal() const;
    int vtotal() const;
    int hvisible() const { return m_display_h_visarea; }
    int vvisible() const { return m_display_v_visarea; }
    double pixclock() const { return m_pixclock; }
    void set_pixclock(double hz) { m_pixclock = hz; }

    const uint32_t *vram() const { return m_vram.get(); }

private:
    struct Frame {
        uint32_t base = 0;
        uint16_t width = 0, height = 0, x = 0, y = 0, alpha = 0;
    };

    uint16_t pix(uint32_t index) const;
    void set_pix(uint32_t index, uint16_t v);
    void draw_frame(int frame, uint16_t *bitmap, int bw, int bh, bool inverse_trans);
    void draw_object(const uint32_t *cmd);
    void fill_rect(const uint32_t *cmd);
    void draw_character(const uint32_t *cmd);
    void fb_config(const uint32_t *cmd);
    void execute_display_list(uint32_t addr);
    void execute_command(const uint32_t *cmd);

    std::unique_ptr<uint32_t[]> m_vram;
    uint32_t m_vram_read_addr = 0;
    uint32_t m_vram_fifo0_addr = 0, m_vram_fifo1_addr = 0;
    uint32_t m_vram_fifo0_mode = 0, m_vram_fifo1_mode = 0;
    uint32_t m_command_fifo0[4]{}, m_command_fifo1[4]{};
    uint32_t m_command_fifo0_ptr = 0, m_command_fifo1_ptr = 0;
    uint32_t m_ext_fifo_addr = 0, m_ext_fifo_count = 0, m_ext_fifo_line = 0, m_ext_fifo_num_lines = 0, m_ext_fifo_width = 0;
    Frame m_frame[4];
    int m_fb_origin_x = 0, m_fb_origin_y = 0;
    uint16_t m_layer_select = 0;
    uint16_t m_reg_6c = 0;
    uint16_t m_irqctrl = 0;
    int m_display_h_visarea = 640, m_display_h_frontporch = 16, m_display_h_backporch = 48, m_display_h_syncpulse = 96;
    int m_display_v_visarea = 480, m_display_v_frontporch = 10, m_display_v_backporch = 33, m_display_v_syncpulse = 2;
    double m_pixclock = 25'175'000.0;
};

}  // namespace hk
