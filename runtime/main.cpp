// SPDX-License-Identifier: GPL-3.0-or-later
// hiraku-<game>: M1 bring-up runner.
//
//   hiraku-bm3final [--image program.bin] [--max N] [--shadow K] [--interp-only]
//                   [--trace] [--mmio-log N]
//
// Loads the program image at physical 0 and starts the CPU at the image's
// entry in supervisor mode, as the BIOS would. The boot assembly runs in the
// interpreter; every `bl` into a recompiled function runs the native version
// (unless --interp-only). With --shadow K, the first K calls of each function
// run both ways and are compared. MMIO goes to a logger that returns 0 - there
// are no devices yet (M2), so the run ends when the game waits on hardware.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "core/runtime.h"

#ifndef HK_GAME_ID
#define HK_GAME_ID "unknown"
#endif
#ifndef HK_DEFAULT_IMAGE
#define HK_DEFAULT_IMAGE ""
#endif
#ifndef HK_ENTRY
#define HK_ENTRY 0x80003000u
#endif

using namespace hk;

int main(int argc, char **argv) {
    std::string image = HK_DEFAULT_IMAGE;
    uint64_t max = 200'000'000;
    unsigned mmio_log = 40;
    for (int k = 1; k < argc; k++) {
        std::string a = argv[k];
        auto val = [&]() -> std::string { if (k + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return argv[++k]; };
        if (a == "--image") image = val();
        else if (a == "--max") max = std::stoull(val());
        else if (a == "--shadow") g_interp.shadow_checks = std::stoul(val());
        else if (a == "--interp-only") g_interp.call_native = false;
        else if (a == "--trace") g_interp.trace = true;
        else if (a == "--mmio-log") mmio_log = std::stoul(val());
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }

    setvbuf(stdout, nullptr, _IOLBF, 0);
    ram_init();
    if (!ram_load(image, 0)) { fprintf(stderr, "cannot load %s\n", image.c_str()); return 1; }
    printf("hiraku %s: %s, %u recompiled functions\n", HK_GAME_ID, image.c_str(), hk_func_count);

    std::map<uint32_t, uint64_t> mmio_counts;
    uint64_t mmio_total = 0;
    hk_ctx *cur = nullptr;
    auto log = [&](const char *rw, uint32_t phys, uint32_t v, int size) {
        mmio_total++;
        if (mmio_counts[phys]++ == 0 && mmio_counts.size() <= mmio_log)
            printf("  mmio %s %08x/%d = %08x   (insn %llu, pc %08x)\n", rw, phys, size, v,
                   (unsigned long long)g_stats.insns, cur ? cur->pc : 0);
    };
    // No devices until M2. The one value worth faking: MAME's K057714 status
    // register reads as a constant 0xffff0005 (k057714.cpp), and boot spins on it.
    mmio_set_handler({
        [&](uint32_t p, int s) -> uint32_t {
            log("R", p, 0, s);
            if ((p & ~0x103u) == 0x7e800078) return s == 4 ? 0xffff0005u : s == 2 ? ((p & 2) ? 0x0005u : 0xffffu) : 0;
            return 0;
        },
        [&](uint32_t p, uint32_t v, int s) { log("W", p, v, s); },
    });

    static hk_ctx ctx;
    cur = &ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.pc = HK_ENTRY;
    ctx.msr = 0;

    const char *why = "fatal";
    auto report = [&]() {
        printf("stopped: %s at pc %08x after %llu interpreted insns, %llu native calls, %llu exceptions\n", why, ctx.pc,
               (unsigned long long)g_stats.insns, (unsigned long long)g_stats.native_calls,
               (unsigned long long)g_stats.exceptions);
        printf("mmio: %llu accesses to %zu distinct addresses\n", (unsigned long long)mmio_total, mmio_counts.size());
        if (g_interp.shadow_checks)
            printf("shadow: %llu calls identical, %llu MISMATCHED, %llu impure (entered the kernel; not compared)\n",
                   (unsigned long long)g_stats.shadow_ok, (unsigned long long)g_stats.shadow_bad,
                   (unsigned long long)g_stats.shadow_impure);
    };
    g_on_fatal = report;
    Stop s = interp_run(&ctx, 0xffffffff, 0, max);
    why = s == Stop::halted ? "halted (branch-to-self)" : s == Stop::max_steps ? "step limit" : "returned";
    report();
    dump_ctx(&ctx, stdout);
    return g_stats.shadow_bad ? 1 : 0;
}
