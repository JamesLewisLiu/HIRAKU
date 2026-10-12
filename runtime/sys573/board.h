// SPDX-License-Identifier: GPL-3.0-or-later
// The Konami System 573: a PlayStation (CXD8530CQ R3000A + GTE, CXD8561Q GPU,
// SPU) with 4 MiB of RAM, 16 MiB of flash, an M48T58 timekeeper, a CR-589
// ATAPI CD-ROM drive, two PC Card slots, a security cassette and, for the music
// games, an analog or digital (MP3) I/O board.
//
// Every device is MAME's own code (third_party/mame, through the framework shim)
// and the board is a port of MAME's ksys573.cpp driver (BSD-3-Clause, R. Belmont,
// smf), so that it runs cycle for cycle as MAME does (PLAN.md section 19).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hk::k573 {

struct Config {
    std::string game;                      // MAME set name
    std::string family;                    // ddr, ...
    std::string rom_dir;                   // dumps/<game>/rom: the BIOS, cassette and I/O board chips
    std::string bios = "700a01.22g";
    std::string h8 = "h8a01.bin";
    std::string cassette;                  // y (X76F100) + i (DS2401), ...: MAME's cassette variant
    std::string cassette_eeprom, cassette_id;   // files in rom_dir
    std::string io_board;                  // analog | digital
    std::string digital_id = "digital-id.bin";
    std::string cd;                        // the CD image (.cue or .iso)
    std::string pccard2;                   // "32mb": a Fujitsu 32 MiB flash card in slot 2
    std::string nvram_dir;                 // flash, timekeeper, cassette, card ("" = blank each run)
    int64_t rtc_time = 0;                  // the timekeeper's time at power-on (seconds since 1970)
};

constexpr double CPU_HZ = 67737600.0 / 4;  // MAME's PS1 CPU: one instruction per 4 clocks

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
uint64_t lamp_changes();
const uint8_t *mem(uint32_t addr, uint32_t len);
uint32_t pc();

}  // namespace hk::k573
