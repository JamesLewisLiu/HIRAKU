// SPDX-License-Identifier: GPL-3.0-or-later
// The Konami Firebeat board around the CPU: address decoding, the devices,
// interrupt wiring and video timing. Memory map and device behaviour follow
// MAME's firebeat.cpp (BSD-3-Clause, Ville Linde et al.).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "hiraku/ppc.h"

namespace hk::fb {

struct Config {
    std::string game = "bm3final";
    std::string family = "bm3";        // bm3 | popn | ppp | kbm
    std::string rom_dir;               // dumps/<game>/rom: dongle image, rtc default NVRAM
    std::string dongle_file;           // file name inside rom_dir (e.g. gcc01-jc)
    std::string cd_master, cd_slave;   // main ATA bus disc images ("" = drive with no disc)
    std::string nvram_dir;             // persistent RTC/flash images ("" = none)
    std::string trace_dir;             // write an oracle-format MMIO trace here ("" = off)
    bool ata_log = false;
};

// Inputs as the board's ports see them (active levels as on the real hardware).
struct Inputs {
    uint8_t in[4] = {0x0f, 0xff, 0xff, 0xff};      // IN0..IN3 (IN3 = DIP switches)
    uint16_t io[4] = {0x003f, 0x0001, 0x003f, 0xffff};
    uint16_t turntable[2] = {0, 0};
    uint8_t effect[7] = {};
};

void init(const Config &cfg);

// Set an input by its MAME name ("P1 Button 3", "Coin 1", "TURNTABLE_P1/Turntable",
// "Effect 2", ...): 1/0 for buttons, the position for analog inputs. Shared by the
// input scripts and the host controls. Returns false for an unknown name.
bool set_input(const std::string &name, int value);
// Move a turntable by `delta` counts (it is a 10-bit wrapping counter).
void turntable_add(int player, int delta);
// Latency instrumentation: the virtual time of the last change to a player key.
void note_input_change();
uint64_t last_input_change();
void shutdown();                       // flush NVRAM and traces
Inputs &inputs();

// Called at the start of each vertical blank, with the composed frame.
using FrameHook = std::function<void(uint64_t frame, const uint32_t *rgb, int w, int h)>;
void set_frame_hook(FrameHook h);
uint64_t frame_count();
void set_trace_ctx(hk_ctx *c);   // PCs in the MMIO trace come from here

}  // namespace hk::fb
