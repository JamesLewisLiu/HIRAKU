// SPDX-License-Identifier: GPL-3.0-or-later
#include "twinkle/video.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

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
constexpr int IO_SIZE = 32768;
}

VideoCD::~VideoCD() {
    close_decoder();
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
    av_log_set_level(AV_LOG_QUIET);
    return !m_entries.empty();
}

// The demuxer's input: each sector's MPEG data (Form 2: 2324 bytes after the header), on from the
// chapter's first sector to the end of the disc, as a player plays on into the next entry
int VideoCD::read(void *user, uint8_t *buf, int size) {
    auto *self = static_cast<VideoCD *>(user);
    int done = 0;
    while (done < size) {
        if (self->m_part_pos >= self->m_part.size()) {
            if (self->m_next_sector >= self->m_sectors) break;
            uint8_t raw[RAW];
            fseek(self->m_bin, (long)self->m_next_sector * RAW, SEEK_SET);
            if (fread(raw, 1, RAW, self->m_bin) != RAW) break;
            self->m_next_sector++;
            self->m_part.assign(raw + 24, raw + 24 + FORM2_DATA);
            self->m_part_pos = 0;
        }
        const int n = std::min<int>(size - done, (int)(self->m_part.size() - self->m_part_pos));
        memcpy(buf + done, self->m_part.data() + self->m_part_pos, (size_t)n);
        self->m_part_pos += (size_t)n;
        done += n;
    }
    return done ? done : AVERROR_EOF;
}

void VideoCD::close_decoder() {
    if (m_dec) avcodec_free_context(&m_dec);
    if (m_fmt) avformat_close_input(&m_fmt);
    if (m_io) {
        av_freep(&m_io->buffer);
        avio_context_free(&m_io);
    }
    if (m_pkt) av_packet_free(&m_pkt);
    if (m_frame) av_frame_free(&m_frame);
    m_stream = -1;
}

void VideoCD::start_chapter(int chapter) {
    close_decoder();
    m_index = -1;
    m_ended = true;
    m_flushing = false;
    if (chapter < 1 || chapter > (int)m_entries.size()) return;
    m_next_sector = m_entries[chapter - 1];
    m_part.clear();
    m_part_pos = 0;

    auto *iobuf = static_cast<uint8_t *>(av_malloc(IO_SIZE));
    m_io = avio_alloc_context(iobuf, IO_SIZE, 0, this, &VideoCD::read, nullptr, nullptr);
    m_fmt = avformat_alloc_context();
    m_fmt->pb = m_io;
    if (avformat_open_input(&m_fmt, nullptr, av_find_input_format("mpeg"), nullptr) < 0) {
        fprintf(stderr, "video: chapter %d: not an MPEG stream\n", chapter);
        m_fmt = nullptr;   // freed by avformat_open_input
        close_decoder();
        return;
    }
    avformat_find_stream_info(m_fmt, nullptr);
    m_stream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_stream < 0) { close_decoder(); return; }
    const AVCodecParameters *par = m_fmt->streams[m_stream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    m_dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_dec, par);
    m_dec->thread_count = 1;   // the frames in order, the same every run
    if (!codec || avcodec_open2(m_dec, codec, nullptr) < 0) { close_decoder(); return; }
    // a Video CD's MPEG-1 is 29.97 frames a second (its stream header can claim 59.94)
    const AVRational fr = m_fmt->streams[m_stream]->avg_frame_rate;
    m_period = fr.num > 0 && fr.den > 0 ? (double)fr.den / fr.num : 1.0 / 29.97;
    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_ended = false;
}

bool VideoCD::decode_next() {
    for (;;) {
        const int r = avcodec_receive_frame(m_dec, m_frame);
        if (r == 0) break;
        if (r == AVERROR_EOF || (r != AVERROR(EAGAIN))) return false;
        if (m_flushing) return false;
        // more input: the next video packet, or the end
        for (;;) {
            if (av_read_frame(m_fmt, m_pkt) < 0) {
                avcodec_send_packet(m_dec, nullptr);
                m_flushing = true;
                break;
            }
            const bool video = m_pkt->stream_index == m_stream;
            if (video) avcodec_send_packet(m_dec, m_pkt);
            av_packet_unref(m_pkt);
            if (video) break;
        }
    }
    m_w = (unsigned)m_frame->width;
    m_h = (unsigned)m_frame->height;
    m_cw = (m_w + 1) / 2;
    const unsigned ch = (m_h + 1) / 2;
    m_y.resize((size_t)m_w * m_h);
    m_cb.resize((size_t)m_cw * ch);
    m_cr.resize((size_t)m_cw * ch);
    for (unsigned r = 0; r < m_h; r++) memcpy(&m_y[(size_t)r * m_w], m_frame->data[0] + (size_t)r * m_frame->linesize[0], m_w);
    for (unsigned r = 0; r < ch; r++) {
        memcpy(&m_cb[(size_t)r * m_cw], m_frame->data[1] + (size_t)r * m_frame->linesize[1], m_cw);
        memcpy(&m_cr[(size_t)r * m_cw], m_frame->data[2] + (size_t)r * m_frame->linesize[2], m_cw);
    }
    av_frame_unref(m_frame);
    return true;
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
        if (!decode_next()) { m_ended = true; break; }
        m_index++;
        m_decoded++;
    }
    return m_index >= 0 && !m_y.empty();
}

void VideoCD::composite(double t, uint32_t *rgb, int w, int h, const Picture &pic) {
    if (!m_dec || !m_powered || m_status == 0) return;
    const double v = m_status == 1 ? t - m_t0 : m_pos;
    if (v < 0 || !advance_to(v)) return;
    static const bool trace = getenv("HK_TW_VIDEO") != nullptr;   // HK_TW_VIDEO=1: what is on screen
    if (trace) {
        static int64_t last = -1000;
        if (m_index - last >= 60) {
            uint32_t sum = 0, cs = 0;
            for (uint8_t b : m_y) sum = sum * 31 + b;
            for (uint8_t b : m_cb) cs = cs * 31 + b;
            for (uint8_t b : m_cr) cs = cs * 31 + b;
            fprintf(stderr, "video t %.3f v %.3f frame %lld y %08x c %08x\n", t, v, (long long)m_index, sum, cs);
            last = m_index;
        }
    }

    // the Bt812's adjustments (BT.601 limited range in, full range out)
    const double con = pic.contrast / 128.0, sat = pic.saturation / 128.0;
    const double th = pic.hue * (45.0 / 128.0) * M_PI / 180.0, hc = std::cos(th) * sat, hs = std::sin(th) * sat;
    auto clamp8 = [](double x) { return (uint32_t)std::clamp((int)std::lround(x), 0, 255); };
    for (int y = 0; y < h; y++) {
        const unsigned sy = (unsigned)((uint64_t)y * m_h / h);
        uint32_t *row = rgb + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            if (row[x] & 0x00ffffffu) continue;   // the PlayStation's picture, not black: on top
            const unsigned sx = (unsigned)((uint64_t)x * m_w / w);
            const double yy = 1.164 * ((m_y[(size_t)sy * m_w + sx] - 16) * con + pic.brightness);
            const double cb0 = m_cb[(size_t)(sy / 2) * m_cw + sx / 2] - 128.0, cr0 = m_cr[(size_t)(sy / 2) * m_cw + sx / 2] - 128.0;
            const double cb = cb0 * hc - cr0 * hs, cr = cr0 * hc + cb0 * hs;
            row[x] = 0xff000000u | clamp8(yy + 1.596 * cr) << 16 | clamp8(yy - 0.392 * cb - 0.813 * cr) << 8 | clamp8(yy + 2.017 * cb);
        }
    }
}

}  // namespace hk::twinkle
