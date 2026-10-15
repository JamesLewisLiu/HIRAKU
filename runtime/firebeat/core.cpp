// SPDX-License-Identifier: GPL-3.0-or-later
// The Firebeat core: a recompiled PowerPC program on the Firebeat board, with
// the SPU sound board or the board's YMZ280B, and Keyboardmania's synth card.
//
// Compiled into each hiraku-<game> binary (not a library), because the entry
// point and load address are per game: HK_ENTRY, HK_LOAD, HK_GAME_ID.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

#include "../core/runtime.h"
#include "../core/sched.h"
#include "../shell/core.h"
#include "../spu/spu.h"
#include "board.h"
#include "synth.h"

#ifndef HK_LOAD
#define HK_LOAD 0x80000000u   // where the BIOS puts the program (Keyboardmania 1st: 0x80080000)
#endif
#ifndef HK_ENTRY
#define HK_ENTRY 0x80003000u
#endif

namespace hk {

namespace {

std::map<std::string, std::string> read_kv(const std::string &path) {
    std::map<std::string, std::string> kv;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        auto eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

class FirebeatCore final : public Core {
public:
    bool option(const std::string &a, const std::function<std::string()> &val) override {
        if (a == "--image") image_ = val();
        else if (a == "--interp-only") { g_interp.call_native = false; g_interp.lle = true; }
        else if (a == "--shadow") g_interp.shadow_checks = std::stoul(val());
        else if (a == "--itrace") g_interp.trace = true;
        else if (a == "--bios-seconds") bios_seconds_ = std::stod(val());
        else if (a == "--soundfont") soundfont_ = val(), synth_stand_in_ = true;
        else if (a == "--synth-stand-in") synth_stand_in_ = true;
        else return false;
        return true;
    }
    std::string options_help() const override {
        return "  --image FILE      program image (default dumps/<game>/program.bin)\n"
               "  --interp-only     run everything in the interpreter (a plain emulator; no fibers)\n"
               "  --shadow K        compare native vs interpreted for the first K calls of each function\n"
               "  --itrace          print every interpreted instruction\n"
               "  --bios-seconds S  time the skipped BIOS would have taken (default 21.65, as measured in\n"
               "                    MAME for beatmania III); boards like the SPU boot during it\n"
               "  --synth-stand-in  Keyboardmania: play the MIDI through FluidSynth even when the XT446 ROMs are there\n"
               "  --soundfont FILE  ... with this SoundFont (implies --synth-stand-in)\n";
    }

    bool init(const Options &o) override {
        if (image_.empty()) image_ = o.root + "/dumps/" + o.game + "/program.bin";
        auto machine = read_kv(o.root + "/dumps/" + o.game + "/machine.txt");
        if (machine.empty()) {
            fprintf(stderr, "no dumps/%s/machine.txt - run tools/ingest.py on your program disc first\n", o.game.c_str());
            return false;
        }
        ram_init();
        if (!ram_load(image_, HK_PHYS(HK_LOAD))) { fprintf(stderr, "cannot load %s\n", image_.c_str()); return false; }

        fb::Config cfg;
        cfg.game = o.game;
        cfg.family = machine["family"];
        cfg.rom_dir = o.root + "/dumps/" + o.game + "/rom";
        cfg.dongle_file = machine["dongle"];
        if (!machine["cabinet"].empty()) cfg.cabinet = std::stoi(machine["cabinet"]);
        cfg.cd_master = o.data_path(machine["cd_master"]);
        cfg.cd_slave = o.data_path(machine["cd_slave"]);
        cfg.nvram_dir = o.nvram_dir;
        if (o.trace) cfg.trace_dir = o.out;
        cfg.ata_log = o.ata_log;
        fb::init(cfg);
        if (cfg.family == "kbm") synth::init(cfg.rom_dir, soundfont_, synth_stand_in_);

        // sound: the SPU board when the machine has one, otherwise the board's YMZ280B
        if (!machine["spu_rom"].empty()) {
            spu::Config sc;
            sc.rom = cfg.rom_dir + "/" + machine["spu_rom"];
            sc.hdd = o.data_path(machine["spu_hdd"]);
            sc.dvd = o.data_path(machine["spu_dvd"]);
            sc.mix_effect = cfg.family == "bm3";
            sc.log = o.ata_log;
            if (!spu::init(sc)) {
                fprintf(stderr, "cannot start the SPU (%s)\nthe SPU program comes from the MAME ROM set: tools/ingest.py DISC --romset %s.zip\n",
                        sc.rom.c_str(), o.game.c_str());
                return false;
            }
            if (sink_) spu::set_audio_sink(sink_);
        } else if (sink_) {
            fb::set_audio_sink(sink_);
        }

        // HIRAKU starts in the game, not the BIOS. On the real machine (and in MAME)
        // everything else powered up with the BIOS: the SPU's 68000, for one, tests
        // its 32 MiB of wave RAM meanwhile. Give the machine that time.
        if (bios_seconds_ > 0) {
            sched_idle((uint64_t)(bios_seconds_ * CPU_HZ));
            spu::catch_up();
        }

        memset(&ctx_, 0, sizeof ctx_);
        ctx_.pc = HK_ENTRY;
        glue_init(&ctx_);
        fb::set_trace_ctx(&ctx_);
        printf("hiraku %s: %u recompiled functions, %s\n", o.game.c_str(), hk_func_count,
               g_interp.lle ? "interpreter only" : "native");
        return true;
    }

    const std::string &family() const override { return fb::family(); }
    void set_frame_hook(FrameHook h) override { fb::set_frame_hook(std::move(h)); }
    bool set_present_hook(PresentHook h) override { fb::set_present_hook(std::move(h)); return true; }
    // Before init(): init routes it to the SPU or the board, before the BIOS time runs.
    void set_audio_sink(AudioSink s) override { sink_ = std::move(s); }
    void set_on_fatal(std::function<void()> f) override { g_on_fatal = std::move(f); }

    [[noreturn]] void run() override {
        // Boot as the BIOS leaves the CPU: supervisor state at the image's entry.
        // The boot code is a recompiled function like any other (it never returns);
        // --interp-only runs it, and everything else, in the interpreter.
        if (hk_func boot = g_interp.lle ? nullptr : lookup(HK_ENTRY)) boot(&ctx_);
        else interp_run(&ctx_, 0xffffffff, 0, UINT64_MAX);
        fatal(&ctx_, "the guest stopped running");
    }

    void shutdown() override { fb::shutdown(); }

    void report() override {
        printf("frames %llu, virtual time %.3f s, %llu interpreted insns, %llu native calls, %llu exceptions "
               "(%llu vectors native), %llu threads, %llu thread switches\n",
               (unsigned long long)fb::frame_count(), (double)now() / CPU_HZ, (unsigned long long)g_stats.insns,
               (unsigned long long)g_stats.native_calls, (unsigned long long)g_stats.exceptions,
               (unsigned long long)g_stats.native_vectors, (unsigned long long)g_stats.threads_started + 1, (unsigned long long)g_stats.thread_switches);
        if (g_interp.shadow_checks)
            printf("shadow: %llu identical, %llu MISMATCHED, %llu impure\n", (unsigned long long)g_stats.shadow_ok,
                   (unsigned long long)g_stats.shadow_bad, (unsigned long long)g_stats.shadow_impure);
        if (!spu::present()) printf("ymz280b: %llu key-ons\n", (unsigned long long)fb::ymz_key_ons());
        if (fb::family() == "kbm")
            printf("midi to the synth: %llu bytes (%s)\n", (unsigned long long)fb::midi_out_bytes(), synth::describe().c_str());
        if (spu::present()) {
            auto &st = spu::stats();
            printf("spu: pc %06x sr %04x, %.2f M 68000 cycles, IRQ2 %llu IRQ4 %llu IRQ6 %llu, %llu ATA commands, %.1f MiB DMA, %llu key-ons\n",
                   st.pc, st.sr, st.cycles / 1e6, (unsigned long long)st.irq2, (unsigned long long)st.irq4, (unsigned long long)st.irq6,
                   (unsigned long long)st.ata_commands, st.dma_words * 2 / 1048576.0, (unsigned long long)st.key_ons);
            if (st.lat_n) {
                printf("latency (player key -> RF5C400 key-on, virtual time): n=%llu mean %.2f ms, min %.2f, max %.2f; 2 ms buckets:",
                       (unsigned long long)st.lat_n, st.lat_sum_ms / st.lat_n, st.lat_min_ms, st.lat_max_ms);
                for (int k = 0; k < 16; k++) printf(" %u", st.lat_hist[k]);
                printf("\n");
            }
        }
    }
    int exit_code() const override { return g_stats.shadow_bad ? 1 : 0; }
    void debug_line(uint64_t f) override {
        printf("f%llu pc %08x lr %08x msr %08x r1 %08x | EXISR %08x EXIER %08x TCR %08x TSR %08x | insns %llu exc %llu sw %llu\n",
               (unsigned long long)f, ctx_.pc, ctx_.lr, ctx_.msr, ctx_.r[1], g_cpu.dcrs[0x40], g_cpu.dcrs[0x42],
               g_cpu.sprs[0x3da], g_cpu.sprs[0x3d8], (unsigned long long)g_stats.insns,
               (unsigned long long)g_stats.exceptions, (unsigned long long)g_stats.thread_switches);
    }

    double seconds() const override { return (double)now() / CPU_HZ; }
    const uint8_t *mem(uint32_t addr, uint32_t len) const override {
        const uint32_t p = HK_PHYS(addr);
        return p < HK_RAM_SIZE && len <= HK_RAM_SIZE - p ? hk_ram + p : nullptr;
    }
    uint64_t frame_count() const override { return fb::frame_count(); }

    bool set_input(const std::string &name, int value) override { return fb::set_input(name, value); }
    void turntable_add(int player, int delta) override { fb::turntable_add(player, delta); }
    // Keyboardmania's XT446, rendered on a worker, adds its part at the frame's end (board.cpp
    // audio_release()); otherwise the sound comes every 2 ms of machine time
    bool audio_per_frame() const override { return synth::deferred(); }
    const std::vector<uint8_t> &lamp_state() const override { return fb::lamp_state(); }
    uint64_t lamp_changes() const override { return fb::lamp_changes(); }

private:
    std::string image_, soundfont_;
    bool synth_stand_in_ = false;
    double bios_seconds_ = 21.65;
    AudioSink sink_;
    hk_ctx ctx_{};
};

}  // namespace

Core &core() {
    static FirebeatCore c;
    return c;
}

}  // namespace hk
