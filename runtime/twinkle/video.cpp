// SPDX-License-Identifier: GPL-3.0-or-later
#include "twinkle/video.h"

#include "pl_mpeg.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace hk::twinkle {

namespace {
constexpr uint32_t RAW = 2352;        // a raw CD sector
constexpr uint32_t FORM2_DATA = 2324; // a Mode 2 Form 2 sector's user data (the MPEG tracks)
constexpr uint32_t ENTRIES_LBA = 151; // ENTRIES.VCD, at a fixed place on every Video CD
}

VideoCD::~VideoCD() {
    if (m_plm) plm_destroy(m_plm);
    if (m_bin) fclose(m_bin);
}

bool VideoCD::open(const std::string &cue) {
    // the .cue's one FILE, MODE2/2352 throughout (chdman's extractcd of a Video CD)
    std::ifstream f(cue);
    std::string line, bin;
    while (std::getline(f, line)) {
        auto p = line.find("FILE \"");
        if (p != std::string::npos) {
            auto q = line.find('"', p + 6);
            bin = line.substr(p + 6, q - p - 6);
        }
    }
    if (bin.empty()) { fprintf(stderr, "video: no FILE in %s\n", cue.c_str()); return false; }
    if (bin[0] != '/') {
        auto slash = cue.rfind('/');
        if (slash != std::string::npos) bin = cue.substr(0, slash + 1) + bin;
    }
    m_bin = fopen(bin.c_str(), "rb");
    if (!m_bin) { fprintf(stderr, "video: cannot read %s\n", bin.c_str()); return false; }
    fseek(m_bin, 0, SEEK_END);
    m_sectors = (uint32_t)(ftell(m_bin) / RAW);

    // ENTRIES.VCD (Mode 2 Form 1: 2048 bytes after the 24-byte header): "ENTRYVCD", the version,
    // the count, then each entry's track and start as BCD minutes, seconds, frames
    uint8_t s[RAW];
    fseek(m_bin, (long)ENTRIES_LBA * RAW, SEEK_SET);
    if (fread(s, 1, RAW, m_bin) != RAW || memcmp(s + 24, "ENTRYVCD", 8) != 0) {
        fprintf(stderr, "video: %s has no ENTRIES.VCD (not a Video CD?)\n", bin.c_str());
        fclose(m_bin);
        m_bin = nullptr;
        return false;
    }
    const uint8_t *e = s + 24;
    const int n = (e[10] << 8) | e[11];
    auto bcd = [](uint8_t b) { return (b >> 4) * 10 + (b & 15); };
    for (int i = 0; i < n && i < 500; i++) {
        const uint8_t *r = e + 12 + 4 * i;
        m_entries.push_back((uint32_t)((bcd(r[1]) * 60 + bcd(r[2])) * 75 + bcd(r[3]) - 150));
    }
    return !m_entries.empty();
}

void VideoCD::load(plm_buffer_t *buf, void *user) {
    // the next stretch of sectors' MPEG data; the disc plays on into the next entry, as a player does
    auto *self = static_cast<VideoCD *>(user);
    uint8_t raw[RAW * 16];
    const uint32_t n = std::min<uint32_t>(16, self->m_sectors - std::min(self->m_sectors, self->m_next_sector));
    if (n == 0) { plm_buffer_signal_end(buf); return; }
    fseek(self->m_bin, (long)self->m_next_sector * RAW, SEEK_SET);
    const size_t got = fread(raw, RAW, n, self->m_bin);
    for (size_t k = 0; k < got; k++) plm_buffer_write(buf, raw + k * RAW + 24, FORM2_DATA);
    self->m_next_sector += n;
    if (got < n) plm_buffer_signal_end(buf);
}

void VideoCD::start_chapter(int chapter) {
    if (m_plm) { plm_destroy(m_plm); m_plm = nullptr; }
    m_index = -1;
    m_ended = false;
    if (chapter < 1 || chapter > (int)m_entries.size()) return;
    m_next_sector = m_entries[chapter - 1];
    plm_buffer_t *buf = plm_buffer_create_with_capacity(256 * 1024);
    plm_buffer_set_load_callback(buf, &VideoCD::load, this);
    m_plm = plm_create_with_buffer(buf, 1);
    plm_set_audio_enabled(m_plm, 0);
    const double fps = plm_get_framerate(m_plm);
    m_period = fps > 1 ? 1.0 / fps : 1.0 / 29.97;
}

void VideoCD::command(double t, int chapter, int status, bool powered, bool from_start) {
    m_commands++;
    double v = m_status == 1 ? t - m_t0 : m_pos;   // the video time when the command came
    if (from_start) {
        start_chapter(chapter);
        v = 0;
    }
    if (status == 1) m_t0 = t - v;
    else m_pos = v;
    m_chapter = chapter;
    m_status = status;
    m_powered = powered;
}

bool VideoCD::advance_to(double v) {
    const int64_t want = (int64_t)std::floor(v / m_period);
    while (m_index < want && !m_ended) {
        plm_frame_t *fr = plm_decode_video(m_plm);
        if (!fr) { m_ended = true; break; }
        m_index++;
        m_decoded++;
        if (m_index < want) continue;   // only the frame on screen needs its planes kept
        m_w = fr->width;
        m_h = fr->height;
        m_cw = fr->cb.width;
        m_y.assign(fr->y.data, fr->y.data + (size_t)fr->y.width * fr->y.height);
        m_cb.assign(fr->cb.data, fr->cb.data + (size_t)fr->cb.width * fr->cb.height);
        m_cr.assign(fr->cr.data, fr->cr.data + (size_t)fr->cr.width * fr->cr.height);
        if (fr->y.width != m_w) {   // planes are padded to whole macroblocks: keep their stride
            std::vector<uint8_t> y((size_t)m_w * m_h);
            for (unsigned r = 0; r < m_h; r++) memcpy(&y[(size_t)r * m_w], fr->y.data + (size_t)r * fr->y.width, m_w);
            m_y.swap(y);
        }
    }
    return m_index >= 0 && !m_y.empty();
}

void VideoCD::composite(double t, uint32_t *rgb, int w, int h, const Picture &pic) {
    if (!m_plm || !m_powered || m_status == 0) return;
    const double v = m_status == 1 ? t - m_t0 : m_pos;
    if (v < 0 || !advance_to(v)) return;
    static const bool trace = getenv("HK_TW_VIDEO") != nullptr;   // HK_TW_VIDEO=1: what is on screen
    if (trace) {
        static int64_t last = -1000;
        if (m_index - last >= 60) {
            uint32_t sum = 0;
            for (uint8_t b : m_y) sum = sum * 31 + b;
            fprintf(stderr, "video t %.3f v %.3f frame %lld y %08x\n", t, v, (long long)m_index, sum);
            last = m_index;
        }
    }

    // the Bt812's adjustments (BT.601 limited range in, full range out)
    const double con = pic.contrast / 128.0, sat = pic.saturation / 128.0;
    const double th = pic.hue * (45.0 / 128.0) * M_PI / 180.0, hc = std::cos(th) * sat, hs = std::sin(th) * sat;
    auto clamp8 = [](double x) { return (uint32_t)std::clamp((int)std::lround(x), 0, 255); };
    const unsigned cw = m_cw ? m_cw : (m_w + 1) / 2;
    for (int y = 0; y < h; y++) {
        const unsigned sy = (unsigned)((uint64_t)y * m_h / h);
        uint32_t *row = rgb + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            if (row[x] & 0x00ffffffu) continue;   // the PlayStation's picture, not black: on top
            const unsigned sx = (unsigned)((uint64_t)x * m_w / w);
            const double yy = 1.164 * ((m_y[(size_t)sy * m_w + sx] - 16) * con + pic.brightness);
            const double cb0 = m_cb[(size_t)(sy / 2) * cw + sx / 2] - 128.0, cr0 = m_cr[(size_t)(sy / 2) * cw + sx / 2] - 128.0;
            const double cb = cb0 * hc - cr0 * hs, cr = cr0 * hc + cb0 * hs;
            row[x] = 0xff000000u | clamp8(yy + 1.596 * cr) << 16 | clamp8(yy - 0.392 * cb - 0.813 * cr) << 8 | clamp8(yy + 2.017 * cb);
        }
    }
}

}  // namespace hk::twinkle
