// SPDX-License-Identifier: GPL-3.0-or-later
// A machine core: everything the shell (runtime/main.cpp, the host front end,
// input scripts, lamps) needs from an emulated board, and nothing more.
//
// Each hiraku-<game> binary links exactly one core, which defines hk::core().
// Firebeat (runtime/firebeat/core.cpp) runs recompiled PowerPC; DJ Main
// (runtime/djmain/core.cpp) interprets a 68EC020. The shell never sees which.
//
// Control flow: run() boots the machine and never returns. Once per video
// frame the core calls the frame hook, which is where the shell checks its
// stop conditions and ends the program (after shutdown() and report()).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hk {

class Core {
public:
    struct Options {
        std::string game;        // game id (games/<id>/game.toml, dumps/<id>/)
        std::string root;        // the HIRAKU checkout
        std::string out;         // snapshots, traces
        std::string nvram_dir;   // persistent NVRAM ("" = none: every run starts from the defaults)
        bool trace = false;      // write an oracle-format MMIO trace into `out`
        bool ata_log = false;    // log disk commands
        // a file named in machine.txt: absolute, or relative to dumps/<game>/
        std::string data_path(const std::string &p) const {
            if (p.empty() || p[0] == '/') return p;
            return root + "/dumps/" + game + "/" + p;
        }
    };
    using FrameHook = std::function<void(uint64_t frame, const uint32_t *rgb, int w, int h)>;
    using AudioSink = std::function<void(const float *lr, int frames)>;   // stereo, at audio_rate()

    virtual ~Core() = default;

    // A command-line option only this core knows (e.g. Firebeat's --interp-only).
    // `value` fetches the option's argument. Returns false for an unknown option.
    virtual bool option(const std::string &, const std::function<std::string()> &) { return false; }
    // Usage lines for those options.
    virtual std::string options_help() const { return {}; }

    // Load the machine (dumps/<game>/machine.txt and rom/). false: a message was printed.
    virtual bool init(const Options &o) = 0;
    virtual const std::string &family() const = 0;   // selects controls and lamps (shell/cabinet.h)

    virtual void set_frame_hook(FrameHook h) = 0;
    virtual void set_audio_sink(AudioSink s) = 0;
    // Called by the core when it cannot continue, before it exits.
    virtual void set_on_fatal(std::function<void()> f) = 0;

    [[noreturn]] virtual void run() = 0;
    virtual void shutdown() = 0;        // flush NVRAM and traces (before exiting)
    virtual void report() = 0;          // run statistics, one stdout line each
    virtual int exit_code() const { return 0; }   // non-zero if the run found a fault
    virtual void debug_line(uint64_t /*frame*/) {}  // --debug-every: one line of CPU state

    virtual double seconds() const = 0;           // virtual time since power-on
    // `len` bytes of guest memory from `addr`, in the guest's (big-endian) byte
    // order, for input scripts' watches, hashes and dumps. nullptr if the range is
    // not plain memory.
    virtual const uint8_t *mem(uint32_t addr, uint32_t len) const = 0;
    virtual uint64_t frame_count() const = 0;

    // Inputs by MAME name ("P1 Button 3", "Coin 1", "TURNTABLE_P1/Turntable"):
    // 1/0 for buttons, the position for analog inputs. false for an unknown name.
    virtual bool set_input(const std::string &name, int value) = 0;
    // Frames by which an input script's button changes (press, hold, release) lag
    // behind their frame number, to match the MAME oracle: MAME latches digital
    // inputs in its frame update, which runs just before the Lua script's frame
    // callback, so a button pressed at frame F is seen from frame F+1's latch.
    // Analog values set by a script are read live in MAME and never lag. Host
    // input doesn't go through this. 0 keeps the cores whose recorded runs predate it.
    virtual int script_button_delay() const { return 0; }
    // The sample rate of what the audio sink gets: the machine's own where the
    // host can take it as it is (DJ Main's sound chips run at 48 kHz).
    virtual int audio_rate() const { return 44100; }
    // Move player's turntable by `delta` encoder counts (cores without one ignore it).
    virtual void turntable_add(int player, int delta) = 0;
    // Lamps as the game last drove them, in cab::lamps(family()) order (1 = lit),
    // and a counter that moves on every change.
    virtual const std::vector<uint8_t> &lamp_state() const = 0;
    virtual uint64_t lamp_changes() const = 0;
};

Core &core();   // defined by the core this binary is built with

}  // namespace hk
