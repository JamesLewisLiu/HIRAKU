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
    };
    g_on_fatal = [&]() { fb::shutdown(); report(); };

#ifdef HK_WITH_SDL
    if (window_scale && !host::window_open(("HIRAKU - " + game).c_str(), window_scale)) {
        fprintf(stderr, "cannot open a window\n");
        return 1;
    }
#else
    if (window_scale) { fprintf(stderr, "this build has no SDL3 front end\n"); return 1; }
#endif
    fb::set_frame_hook([&](uint64_t f, const uint32_t *rgb, int w, int h) {
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
