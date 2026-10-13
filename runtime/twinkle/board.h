// SPDX-License-Identifier: GPL-3.0-or-later
// Konami's Twinkle (beatmania IIDX 1st to 8th style): a PlayStation (CXD8530CQ R3000A + GTE,
// CXD8561Q GPU, SPU) with 4 MiB of RAM, a SCSI CD-ROM drive for the program, an RTC65271 with
// the backup RAM, a security dongle, a super I/O whose UART (or the PS1's SIO1) drives an
// external DVD/Video CD player; and the SPU board: a 68000, an RF5C400 with 24 MiB of wave RAM
// filled from its own ATA hard disk, a dual-port RAM mailbox to the PS1.
//
// Every device is MAME's own code (third_party/mame, through the framework shim) and the board
// is a port of MAME's twinkle.cpp driver (BSD-3-Clause, smf, R. Belmont), so that it runs cycle
// for cycle as MAME does (PLAN.md section 21).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hk::twinkle {

struct Config {
    std::string game;                      // MAME set name
    std::string family;                    // iidx
    std::string rom_dir;                   // dumps/<game>/rom
    std::string bios = "863a03.7b";        // the main board's BIOS
    std::string spu = "863a05.2x";         // the SPU board's 68000 program
    std::string dongle;                    // the dongle's contents (MAME's "security" region)
    std::string dongle_type = "m24c02";    // x76f041 (1st style, Substream, Club 2) | m24c02 (2nd style on)
    std::string dvd_port = "uart";         // sio1 (1st style, MAME twinkle_dvd_type1) | uart (type2)
    std::string cd;                        // the program CD (.cue)
    std::string video_cd;                  // the Video CD in the player
    // the background video (video.h; MAME shows none): the picture changes, the machine doesn't.
    // Off gives MAME's picture, for comparing with it.
    bool video = true;
    std::string hdd;                       // the SPU board's hard disk, raw
    uint32_t hdd_cylinders = 0, hdd_heads = 16, hdd_sectors = 63;   // its geometry (the CHD's GDDD)
    // The output's gain. MAME's mix (the SPU at 0.75, the RF5C400 and CD audio at 1.0) peaks past
    // full scale on loud stretches (1.07 in bmiidx3-play) and clips; 0.8 keeps MAME's balance with
    // headroom. 1.0 (--mame-mix) is MAME's output, for comparing with its WAV.
    float mix_gain = 0.8f;
    std::string nvram_dir;                 // RTC (backup RAM), dongle ("" = blank each run)
    int64_t rtc_time = 0;                  // the RTC's time at power-on (seconds since 1970)
    std::string trace_dir;                 // --trace: an oracle-format MMIO trace there (mmio.bin)
};

bool init(const Config &cfg);              // false: a message was printed

using FrameHook = std::function<void(uint64_t frame, const uint32_t *rgb, int w, int h)>;
using AudioSink = std::function<void(const float *lr, int frames)>;   // 44.1 kHz stereo
void set_frame_hook(FrameHook h);
void set_audio_sink(AudioSink s);
[[noreturn]] void run();
void save_nvram();

double seconds();
uint64_t frame_count();
const std::string &family();
bool set_input(const std::string &name, int value);
const std::vector<uint8_t> &lamp_state();
std::string display_text();                // the 16-segment display's 9 characters
void turntable_add(int player, int delta);  // host turntable counts (1024 a turn)
uint64_t lamp_changes();
std::string sound_report();
std::string video_report();                // the player's commands and the frames decoded ("" without video)
const uint8_t *mem(uint32_t addr, uint32_t len);
uint32_t pc();
uint32_t audio_pc();

}  // namespace hk::twinkle
