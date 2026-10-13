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

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
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

    // for the exit report: commands taken, frames shown (the worker may have decoded a few more)
    uint64_t commands() const { return m_commands; }
    uint64_t frames_decoded() const { return m_decoded; }

private:
    struct Frame {
        unsigned w = 0, h = 0, cw = 0;   // display size; chroma plane width
        std::vector<uint8_t> y, cb, cr;
    };
    static int read(void *self, uint8_t *buf, int size);   // the disc's MPEG data, sector by sector
    bool open_decoder(uint32_t sector);   // on the worker
    void close_decoder();
    void start_chapter(int chapter);
    void stop_worker();
    void worker(uint32_t sector);     // decodes the chapter in order, a few frames ahead
    bool decode_next(Frame &f);       // the next frame; false at the end
    bool advance_to(double v);   // the frame for video time v (seconds into the chapter)

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
    int64_t m_index = -1;              // the frame on screen, m_cur (-1: none yet)
    Frame m_cur;
    // m_cur in RGB through the Bt812's settings, made once per frame or settings change
    std::vector<uint32_t> m_rgb;
    int64_t m_rgb_index = -1;
    Picture m_rgb_pic;
    std::vector<uint32_t> m_col;       // each screen column's source column (for the last width)

    // The worker decodes into m_queue; the emulation takes frames from it in order, waiting for
    // the one it needs if the worker hasn't got there, so the picture is the same on every run.
    std::thread m_thread;
    std::mutex m_mu;
    std::condition_variable m_cv;
    std::deque<Frame> m_queue;
    bool m_opened = false, m_ended = false, m_stop = false;

    int m_chapter = 0, m_status = 0;
    bool m_powered = false;
    double m_pos = 0;      // video time while paused
    double m_t0 = 0;       // machine time of video time 0 while playing
    uint64_t m_commands = 0, m_decoded = 0;
};

}  // namespace hk::twinkle
