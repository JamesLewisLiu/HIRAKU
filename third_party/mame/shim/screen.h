// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's screen_device, its timing only (screen.cpp): the frame period,
// VBLANK begin/end timers placed exactly as MAME places them, the scanline-0
// timer, vpos()/hpos(), and the screen update into a bitmap at VBLANK start.
// Not MAME code: the same behaviour, reimplemented. Needs the exact scheduler.
#pragma once

#include "emu.h"

class screen_device;
using vblank_state_delegate = device_delegate<void(screen_device &, bool)>;
using screen_update_rgb32_delegate = device_delegate<u32(screen_device &, bitmap_rgb32 &, const rectangle &)>;

enum screen_type_enum { SCREEN_TYPE_INVALID = 0, SCREEN_TYPE_RASTER, SCREEN_TYPE_VECTOR, SCREEN_TYPE_LCD, SCREEN_TYPE_SVG };

class screen_device : public device_t {
public:
    screen_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
    screen_device(const machine_config &mconfig, const char *tag, device_t *owner, screen_type_enum)
        : screen_device(mconfig, tag, owner, u32(0)) {}

    // configuration
    screen_device &set_type(screen_type_enum) { return *this; }
    screen_device &set_refresh(attoseconds_t period) { m_refresh = period; return *this; }
    screen_device &set_refresh_hz(double hz) { return set_refresh(HZ_TO_ATTOSECONDS(hz)); }
    screen_device &set_vblank_time(attoseconds_t t) { m_vblank = t; m_oldstyle_vblank_supplied = true; return *this; }
    screen_device &set_size(u16 w, u16 h) { m_width = w; m_height = h; return *this; }
    screen_device &set_visarea(s16 minx, s16 maxx, s16 miny, s16 maxy) { m_visarea.set(minx, maxx, miny, maxy); return *this; }
    screen_device &set_raw(u32 pixclock, u16 htotal, u16 hbend, u16 hbstart, u16 vtotal, u16 vbend, u16 vbstart);
    template <typename D, typename F> screen_device &set_screen_update(D &dev, F f) {
        m_update = screen_update_rgb32_delegate(dev, f);
        return *this;
    }
    bool has_screen_update() const { return !m_update.isnull(); }
    bool has_been_setup() const { return m_refresh != 0; }
    void register_vblank_callback(vblank_state_delegate cb) { m_vblank_callbacks.push_back(std::move(cb)); }
    auto screen_vblank() { return m_screen_vblank.bind(); }

    // run time
    void configure(int width, int height, const rectangle &visarea, attotime frame_period);
    void reset_origin(int beamy = 0, int beamx = 0);
    int width() const { return m_width; }
    int height() const { return m_height; }
    const rectangle &visible_area() const { return m_visarea; }
    const rectangle &cliprect() const { return m_bitmap.valid() ? m_visarea : m_visarea; }
    attotime frame_period() const { return attotime::raw(m_frame_period); }
    attoseconds_t refresh_attoseconds() const { return m_refresh; }
    int vpos() const;
    int hpos() const;
    bool vblank() const { return machine().time() < m_vblank_end_time; }
    bool hblank() const { int h = hpos(); return h < m_visarea.left() || h > m_visarea.right(); }
    attotime time_until_pos(int vpos, int hpos = 0) const;
    attotime time_until_vblank_start() const { return time_until_pos(m_visarea.bottom() + 1); }
    attotime time_until_vblank_end() const;
    u64 frame_number() const { return m_frame_number; }
    bool update_partial(int) { return false; }   // the whole frame is drawn at VBLANK start
    void update_now() {}

    // HIRAKU: the last frame drawn (the visible area of m_bitmap), and a hook at each
    // VBLANK start after it is drawn, before the VBLANK callbacks (MAME's frame update)
    bitmap_rgb32 m_bitmap;
    std::function<void(screen_device &)> m_hk_frame;
    // HIRAKU: the frame drawn again at VBLANK end, as the monitor then shows it (the game's flip
    // during the blank included), before the VBLANK-end callbacks; set only for the window
    // (Core::set_present_hook). The update reads the machine's state and changes none of it.
    std::function<void(screen_device &, bitmap_rgb32 &)> m_hk_vblank_end;
    bitmap_rgb32 m_hk_end_bitmap;

protected:
    void device_start() override;

private:
    void vblank_begin(s32);
    void vblank_end(s32);
    void first_scanline_tick(s32);

    int m_width = 100, m_height = 100;
    rectangle m_visarea = rectangle(0, 99, 0, 99);
    attoseconds_t m_refresh = 0, m_vblank = 0;
    bool m_oldstyle_vblank_supplied = false;
    attoseconds_t m_frame_period = 0, m_scantime = 1, m_pixeltime = 1, m_vblank_period = 0;
    attotime m_vblank_start_time, m_vblank_end_time;
    emu_timer *m_vblank_begin_timer = nullptr, *m_vblank_end_timer = nullptr, *m_scanline0_timer = nullptr;
    u64 m_frame_number = 0;
    screen_update_rgb32_delegate m_update;
    std::vector<vblank_state_delegate> m_vblank_callbacks;
    devcb_write_line m_screen_vblank;
};

DECLARE_DEVICE_TYPE(SCREEN, screen_device)
