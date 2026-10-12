// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's screen timing (screen.cpp), reimplemented: see screen.h.
#include "screen.h"

DEFINE_DEVICE_TYPE(SCREEN, screen_device, "screen", "Video Screen")

screen_device::screen_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
    : device_t(mconfig, SCREEN, tag, owner, clock), m_screen_vblank(*this) {}

screen_device &screen_device::set_raw(u32 pixclock, u16 htotal, u16 hbend, u16 hbstart, u16 vtotal, u16 vbend, u16 vbstart) {
    m_refresh = HZ_TO_ATTOSECONDS(pixclock) * htotal * vtotal;
    m_vblank = m_refresh / vtotal * (vtotal - (vbstart - vbend));
    m_width = htotal;
    m_height = vtotal;
    m_visarea.set(hbend, hbstart - 1, vbend, vbstart - 1);
    return *this;
}


screen_device &device_video_interface::screen() const {
    if (!m_screen_dev) {
        device_t *owner = m_vdev->owner() ? m_vdev->owner() : m_vdev;
        m_screen_dev = hk_find_device(owner, m_screen_tag.empty() ? "screen" : m_screen_tag.c_str());
        if (!m_screen_dev) fatalerror("%s: no screen", m_vdev->tag());
    }
    return *dynamic_cast<screen_device *>(m_screen_dev);
}

void screen_device::device_start() {
    m_vblank_begin_timer = timer_alloc(&screen_device::vblank_begin, this);
    m_vblank_end_timer = timer_alloc(&screen_device::vblank_end, this);
    m_scanline0_timer = timer_alloc(&screen_device::first_scanline_tick, this);
    m_frame_period = m_refresh;
    configure(m_width, m_height, m_visarea, attotime::raw(m_frame_period));
    m_vblank_start_time = attotime::zero;
    m_vblank_end_time = attotime::raw(m_vblank_period);
}

void screen_device::configure(int width, int height, const rectangle &visarea, attotime frame_period) {
    m_width = width;
    m_height = height;
    m_visarea = visarea;
    if (m_bitmap.width() < width || m_bitmap.height() < height) m_bitmap.allocate(std::max(width, m_bitmap.width()), std::max(height, m_bitmap.height()));

    m_frame_period = frame_period.as_attoseconds();
    m_scantime = m_frame_period / height;
    m_pixeltime = m_frame_period / (height * width);
    m_vblank_period = m_oldstyle_vblank_supplied ? m_vblank : m_scantime * (height - visarea.height());

    // past the end of a now shorter frame: VBLANK starts now
    attoseconds_t delta = (attoseconds_t)(machine().time() - m_vblank_start_time).raw_value();
    if (delta >= m_frame_period) vblank_begin(0);
    else m_vblank_begin_timer->adjust(time_until_vblank_start());

    // MAME: reset_partial_updates() on scanline 0, else the scanline 0 timer; both set that timer
    m_scanline0_timer->adjust(time_until_pos(0));
}

void screen_device::reset_origin(int beamy, int beamx) {
    attotime curtime = machine().time();
    m_vblank_end_time = curtime - attotime::raw((attoseconds_t)beamy * m_scantime + (attoseconds_t)beamx * m_pixeltime);
    m_vblank_start_time = m_vblank_end_time - attotime::raw(m_vblank_period);
    if (beamy == ((m_visarea.bottom() + 1) % m_height) && beamx == 0) vblank_begin(0);
    else m_vblank_begin_timer->adjust(time_until_vblank_start());
    m_scanline0_timer->adjust(time_until_pos(0));
}

int screen_device::vpos() const {
    attoseconds_t delta = (attoseconds_t)(machine().time() - m_vblank_start_time).raw_value();
    delta += m_pixeltime / 2;
    if (delta < 0) delta += m_frame_period;
    int vpos = (int)(delta / m_scantime);
    return (m_visarea.bottom() + 1 + vpos) % m_height;
}

int screen_device::hpos() const {
    attoseconds_t delta = (attoseconds_t)(machine().time() - m_vblank_start_time).raw_value();
    delta += m_pixeltime / 2;
    if (delta < 0) delta += m_frame_period;
    int vpos = (int)(delta / m_scantime);
    delta -= vpos * m_scantime;
    return (int)(delta / m_pixeltime);
}

attotime screen_device::time_until_pos(int vpos, int hpos) const {
    vpos += m_height - (m_visarea.bottom() + 1);
    vpos %= m_height;
    attoseconds_t targetdelta = (attoseconds_t)vpos * m_scantime + (attoseconds_t)hpos * m_pixeltime;
    attoseconds_t curdelta = (attoseconds_t)(machine().time() - m_vblank_start_time).raw_value();
    if (targetdelta <= curdelta + m_pixeltime / 2) targetdelta += m_frame_period;
    while (targetdelta <= curdelta) targetdelta += m_frame_period;
    return attotime::raw(targetdelta - curdelta);
}

attotime screen_device::time_until_vblank_end() const {
    attotime target = m_vblank_end_time;
    if (!vblank()) target += attotime::raw(m_frame_period);
    return target - machine().time();
}

void screen_device::vblank_begin(s32) {
    m_vblank_start_time = machine().time();
    m_vblank_end_time = m_vblank_start_time + attotime::raw(m_vblank_period);

    // MAME's frame update: the screen drawn, then the frame notifiers
    if (!m_update.isnull()) m_update(*this, m_bitmap, m_visarea);
    if (m_hk_frame) m_hk_frame(*this);

    for (auto &cb : m_vblank_callbacks) cb(*this, true);
    m_screen_vblank(1);

    m_vblank_begin_timer->adjust(time_until_vblank_start());
    if (m_vblank_period == 0) vblank_end(0);
    else m_vblank_end_timer->adjust(time_until_vblank_end());
}

void screen_device::vblank_end(s32) {
    for (auto &cb : m_vblank_callbacks) cb(*this, false);
    m_screen_vblank(0);
    m_frame_number++;
}

void screen_device::first_scanline_tick(s32) {
    m_scanline0_timer->adjust(time_until_pos(0));
}
