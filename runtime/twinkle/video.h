// SPDX-License-Identifier: GPL-3.0-or-later
// Twinkle's background video, which MAME doesn't show (PLAN.md section 21, TW-3).
//
// The game drives an external JVC DVD/Video CD player over a serial line; MAME's xvd701 models
// that protocol, and tells HIRAKU the player's state after each command (a HIRAKU edit). This
// plays the Video CD to match, decoded with FFmpeg (libavformat/libavcodec): chapter N is the
// disc's entry N (ENTRIES.VCD; chapter 1 is the attract intro, AVSEQ01), and playback runs from
// the machine time of the command that started it,
// so the picture at a given machine time is always the same. The sub board then shows the video
// where the PlayStation's picture is black, while its overlay bit is on, through the Bt812's
// brightness, contrast, saturation and hue (composite()).
//
// None of this feeds back into the machine: it only changes the picture the host sees.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVIOContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;

namespace hk::twinkle {

class VideoCD {
public:
    ~VideoCD();
    // the Video CD's .cue (one raw MODE2/2352 file); false: printed why
    bool open(const std::string &cue);
    bool is_open() const { return m_bin != nullptr; }
    int chapters() const { return (int)m_entries.size(); }

    // the player's state after a command at machine time t: chapter, STOP/PLAYING/PAUSE (0/1/2),
    // the power, and whether the chapter starts again from its beginning
    void command(double t, int chapter, int status, bool powered, bool from_start);

    // The Bt812's picture controls (its registers 0x08-0x0b)
    struct Picture {
        int brightness = 0;     // signed, added to luma
        int contrast = 0x80;    // luma gain, 0x80 = 1.0
        int saturation = 0x80;  // chroma gain, 0x80 = 1.0
        int hue = 0;            // signed, ~0.35 degree a step
    };

    // Put the video at machine time t into rgb (w x h, 0xffRRGGBB) wherever it is black, scaled to
    // the whole frame. Nothing when the player shows no picture.
    void composite(double t, uint32_t *rgb, int w, int h, const Picture &pic);

    // for the exit report: commands taken, frames decoded
    uint64_t commands() const { return m_commands; }
    uint64_t frames_decoded() const { return m_decoded; }

private:
    static int read(void *self, uint8_t *buf, int size);   // the disc's MPEG data, sector by sector
    void close_decoder();
    void start_chapter(int chapter);
    bool decode_next();          // the next frame into m_y/m_cb/m_cr; false at the end
    bool advance_to(double v);   // decode up to video time v (seconds into the chapter)

    FILE *m_bin = nullptr;
    uint32_t m_sectors = 0;
    std::vector<uint32_t> m_entries;   // each chapter's first sector

    AVFormatContext *m_fmt = nullptr;
    AVIOContext *m_io = nullptr;
    AVCodecContext *m_dec = nullptr;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_frame = nullptr;
    int m_stream = -1;
    bool m_flushing = false;
    std::vector<uint8_t> m_part;       // a sector's data not yet handed to the demuxer
    size_t m_part_pos = 0;
    uint32_t m_next_sector = 0;
    double m_period = 1.0 / 29.97;     // the stream's frame period
    int64_t m_index = -1;              // the frame held in m_y/m_cb/m_cr (-1: none yet)
    bool m_ended = false;
    unsigned m_w = 0, m_h = 0, m_cw = 0;   // display size; chroma plane width
    std::vector<uint8_t> m_y, m_cb, m_cr;

    int m_chapter = 0, m_status = 0;
    bool m_powered = false;
    double m_pos = 0;      // video time while paused
    double m_t0 = 0;       // machine time of video time 0 while playing
    uint64_t m_commands = 0, m_decoded = 0;
};

}  // namespace hk::twinkle
