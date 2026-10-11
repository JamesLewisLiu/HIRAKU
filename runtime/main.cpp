// SPDX-License-Identifier: GPL-3.0-or-later
// hiraku-<game>: run a recompiled Firebeat game.
//
//   hiraku-bm3final [options]
//     --frames N        stop after N video frames (default: run until killed)
//     --snap-every N    write every Nth frame as PPM into --out (default off)
//     --out DIR         output directory for snapshots/trace (default out/run/<game>)
//     --trace           write an oracle-format MMIO trace (out/mmio.bin), see oracle/
//     --interp-only     run everything in the interpreter (a plain emulator; no fibers)
//     --shadow K        compare native vs interpreted for the first K calls of each function
//     --ata-log         log ATA/ATAPI commands
//     --image FILE      program image (default dumps/<game>/program.bin)
//     --script FILE     run an oracle input script (.his): same inputs, same frames as in MAME
//     --wav FILE        record the sound output (16-bit stereo, 44.1 kHz)
//     --bios-seconds S  time the skipped BIOS would have taken (default 21.65, as measured in
//                       MAME for beatmania III); boards like the SPU boot during it
//     --window [SCALE]  show the game in a window, in real time (SDL3 builds);
//                       F2 test, 9 service, 5 coin, 1 start, Z S X D C keys, Esc quits
//
// The machine is described by dumps/<game>/machine.txt and dumps/<game>/rom/,
// both written by tools/ingest.py from the user's own dumps.
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

#include "core/runtime.h"
#include "core/sched.h"
#include "firebeat/board.h"
#include "spu/spu.h"
#include "host/script.h"
#ifdef HK_WITH_SDL
#include "host/window.h"
#endif

#ifndef HK_GAME_ID
#define HK_GAME_ID "unknown"
#endif
#ifndef HK_ROOT
#define HK_ROOT "."
#endif
#ifndef HK_ENTRY
#define HK_ENTRY 0x80003000u
#endif

using namespace hk;

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

void write_ppm(const std::string &path, const uint32_t *rgb, int w, int h) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int k = 0; k < w * h; k++) {
        uint8_t px[3] = {(uint8_t)(rgb[k] >> 16), (uint8_t)(rgb[k] >> 8), (uint8_t)rgb[k]};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

void mkdirs(const std::string &path) {
    for (size_t k = 1; k <= path.size(); k++)
        if (k == path.size() || path[k] == '/') mkdir(path.substr(0, k).c_str(), 0755);
}

}  // namespace

int main(int argc, char **argv) {
    const std::string root = HK_ROOT, game = HK_GAME_ID;
    std::string image = root + "/dumps/" + game + "/program.bin";
    std::string out = root + "/out/run/" + game;
    uint64_t frames = 0, snap_every = 0, debug_every = 0;
    bool trace = false, ata_log = false;
    int window_scale = 0;
    std::string wav_path, script_path;
    double bios_seconds = 21.65;
    for (int k = 1; k < argc; k++) {
        std::string a = argv[k];
        auto val = [&]() -> std::string {
            if (k + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); }
            return argv[++k];
        };
        if (a == "--image") image = val();
        else if (a == "--frames") frames = std::stoull(val());
        else if (a == "--snap-every") snap_every = std::stoull(val());
        else if (a == "--out") out = val();
        else if (a == "--trace") trace = true;
        else if (a == "--interp-only") { g_interp.call_native = false; g_interp.lle = true; }
        else if (a == "--shadow") g_interp.shadow_checks = std::stoul(val());
        else if (a == "--ata-log") ata_log = true;
        else if (a == "--itrace") g_interp.trace = true;
        else if (a == "--debug-every") debug_every = std::stoull(val());
        else if (a == "--wav") wav_path = val();
        else if (a == "--script") script_path = val();
        else if (a == "--bios-seconds") bios_seconds = std::stod(val());
        else if (a == "--window") window_scale = (k + 1 < argc && argv[k + 1][0] != '-') ? std::stoi(val()) : 2;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    mkdirs(out);

    auto machine = read_kv(root + "/dumps/" + game + "/machine.txt");
    if (machine.empty()) {
        fprintf(stderr, "no dumps/%s/machine.txt - run tools/ingest.py on your program disc first\n", game.c_str());
        return 1;
    }

    ram_init();
    if (!ram_load(image, 0)) { fprintf(stderr, "cannot load %s\n", image.c_str()); return 1; }

    fb::Config cfg;
    cfg.game = game;
    cfg.family = machine["family"];
    cfg.rom_dir = root + "/dumps/" + game + "/rom";
    cfg.dongle_file = machine["dongle"];
    cfg.cd_master = machine["cd_master"];
    cfg.cd_slave = machine["cd_slave"];
    cfg.nvram_dir = out + "/nvram";
    mkdirs(cfg.nvram_dir);
    if (trace) cfg.trace_dir = out;
    cfg.ata_log = ata_log;
    fb::init(cfg);

    // the sound board, when the machine has one
    FILE *wav = nullptr;
    uint64_t wav_frames = 0;
    if (!machine["spu_rom"].empty()) {
        spu::Config sc;
        sc.rom = cfg.rom_dir + "/" + machine["spu_rom"];
        sc.hdd = machine["spu_hdd"];
        sc.dvd = machine["spu_dvd"];
        sc.mix_effect = cfg.family == "bm3";
        sc.log = ata_log;
        if (!spu::init(sc)) { fprintf(stderr, "cannot start the SPU (%s)\n", sc.rom.c_str()); return 1; }
        if (!wav_path.empty()) {
            wav = fopen(wav_path.c_str(), "wb");
            uint8_t hdr[44] = {};
            if (wav) fwrite(hdr, 1, 44, wav);   // header written at shutdown
        }
        spu::set_audio_sink([&](const float *lr, int n) {
#ifdef HK_WITH_SDL
            if (window_scale) host::audio_push(lr, n);
#endif
            if (!wav) return;
            for (int k = 0; k < 2 * n; k++) {
                float f = lr[k] * 32767.0f;
                int16_t v = (int16_t)(f > 32767 ? 32767 : f < -32768 ? -32768 : f);
                fwrite(&v, 2, 1, wav);
            }
            wav_frames += (uint64_t)n;
        });
    }
    auto close_wav = [&]() {
        if (!wav) return;
        uint32_t data = (uint32_t)(wav_frames * 4), riff = data + 36, rate = 44100, brate = rate * 4, fmt = 16;
        uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
        fseek(wav, 0, SEEK_SET);
        fwrite("RIFF", 1, 4, wav); fwrite(&riff, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav);
        fwrite(&fmt, 4, 1, wav); fwrite(&pcm, 2, 1, wav); fwrite(&ch, 2, 1, wav); fwrite(&rate, 4, 1, wav);
        fwrite(&brate, 4, 1, wav); fwrite(&align, 2, 1, wav); fwrite(&bits, 2, 1, wav);
        fwrite("data", 1, 4, wav); fwrite(&data, 4, 1, wav);
        fclose(wav);
        wav = nullptr;
    };

    // HIRAKU starts in the game, not the BIOS. On the real machine (and in MAME)
    // everything else powered up with the BIOS: the SPU's 68000, for one, tests
    // its 32 MiB of wave RAM meanwhile. Give the machine that time.
    if (bios_seconds > 0) {
        sched_idle((uint64_t)(bios_seconds * CPU_HZ));
        spu::catch_up();
    }

    static hk_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.pc = HK_ENTRY;
    glue_init(&ctx);
    fb::set_trace_ctx(&ctx);
    printf("hiraku %s: %u recompiled functions, %s\n", game.c_str(), hk_func_count,
           g_interp.lle ? "interpreter only" : "native");

    auto report = [&]() {
        printf("frames %llu, virtual time %.3f s, %llu interpreted insns, %llu native calls, %llu exceptions, "
               "%llu threads, %llu thread switches\n",
               (unsigned long long)fb::frame_count(), (double)now() / CPU_HZ, (unsigned long long)g_stats.insns,
               (unsigned long long)g_stats.native_calls, (unsigned long long)g_stats.exceptions,
               (unsigned long long)g_stats.threads_started + 1, (unsigned long long)g_stats.thread_switches);
        if (g_interp.shadow_checks)
            printf("shadow: %llu identical, %llu MISMATCHED, %llu impure\n", (unsigned long long)g_stats.shadow_ok,
                   (unsigned long long)g_stats.shadow_bad, (unsigned long long)g_stats.shadow_impure);
        if (spu::present()) {
            auto &st = spu::stats();
            printf("spu: pc %06x sr %04x, %.2f M 68000 cycles, IRQ2 %llu IRQ4 %llu IRQ6 %llu, %llu ATA commands, %.1f MiB DMA, %llu key-ons\n",
                   st.pc, st.sr, st.cycles / 1e6, (unsigned long long)st.irq2, (unsigned long long)st.irq4, (unsigned long long)st.irq6,
                   (unsigned long long)st.ata_commands, st.dma_words * 2 / 1048576.0, (unsigned long long)st.key_ons);
        }
        close_wav();
    };
    g_on_fatal = [&]() { script::close(); fb::shutdown(); report(); };

#ifdef HK_WITH_SDL
    if (window_scale && !host::window_open(("HIRAKU - " + game).c_str(), window_scale)) {
        fprintf(stderr, "cannot open a window\n");
        return 1;
    }
#else
    if (window_scale) { fprintf(stderr, "this build has no SDL3 front end\n"); return 1; }
#endif
    if (!script_path.empty() && !script::load(script_path, out)) return 2;
    int script_snaps = 0;
    fb::set_frame_hook([&](uint64_t f, const uint32_t *rgb, int w, int h) {
        if (!script_path.empty()) {
            bool keep = script::frame(f, [&]() {
                char name[64];
                snprintf(name, sizeof name, "/snap%04d-f%06llu.ppm", script_snaps++, (unsigned long long)f);
                write_ppm(out + name, rgb, w, h);
            });
            if (!keep) {
                script::close();
                fb::shutdown();
                report();
                exit(g_stats.shadow_bad ? 1 : 0);
            }
        }
#ifdef HK_WITH_SDL
        if (window_scale && !host::window_frame(rgb, w, h, (double)now() / CPU_HZ)) {
            fb::shutdown();
            report();
            host::window_close();
            exit(0);
        }
#endif
        if (debug_every && f % debug_every == 0)
            printf("f%llu pc %08x lr %08x msr %08x r1 %08x | EXISR %08x EXIER %08x TCR %08x TSR %08x | insns %llu exc %llu sw %llu\n",
                   (unsigned long long)f, ctx.pc, ctx.lr, ctx.msr, ctx.r[1], g_cpu.dcrs[0x40], g_cpu.dcrs[0x42],
                   g_cpu.sprs[0x3da], g_cpu.sprs[0x3d8], (unsigned long long)g_stats.insns,
                   (unsigned long long)g_stats.exceptions, (unsigned long long)g_stats.thread_switches);
        if (snap_every && f % snap_every == 0) {
            char name[64];
            snprintf(name, sizeof name, "/frame%06llu.ppm", (unsigned long long)f);
            write_ppm(out + name, rgb, w, h);
        }
        if (frames && f >= frames) {
            fb::shutdown();
            report();
            exit(g_stats.shadow_bad ? 1 : 0);
        }
    });

    // Boot as the BIOS leaves the CPU: supervisor state at the image's entry.
    // The boot assembly is interpreted; from `main` on, code runs natively.
    interp_run(&ctx, 0xffffffff, 0, UINT64_MAX);
    fatal(&ctx, "the guest stopped running");
}
