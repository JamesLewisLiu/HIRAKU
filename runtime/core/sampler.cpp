// SPDX-License-Identifier: GPL-3.0-or-later
// HK_PROFILE=1: a 1 kHz SIGPROF sampler of the host instruction pointer, for
// finding hot spots without perf. Samples are written at exit to
// hk-profile.txt as executable-relative addresses (symbolise with
// llvm-symbolizer --obj=<binary>).
#include <link.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>

#include <cstdio>
#include <cstdlib>
#include <map>

namespace {
constexpr int MAX = 1 << 21;
uintptr_t g_samples[MAX];
volatile int g_n = 0;
uintptr_t g_base = 0;

void on_prof(int, siginfo_t *, void *uc) {
    auto *ctx = static_cast<ucontext_t *>(uc);
    int n = g_n;
    if (n < MAX) { g_samples[n] = (uintptr_t)ctx->uc_mcontext.gregs[REG_RIP]; g_n = n + 1; }
}

void dump() {
    std::map<uintptr_t, int> hist;
    for (int k = 0; k < g_n; k++) hist[g_samples[k] - g_base]++;
    FILE *f = fopen("hk-profile.txt", "w");
    if (!f) return;
    for (auto &[a, c] : hist) fprintf(f, "%d 0x%lx\n", c, (unsigned long)a);
    fclose(f);
    fprintf(stderr, "profile: %d samples -> hk-profile.txt\n", (int)g_n);
}

struct Init {
    Init() {
        if (!getenv("HK_PROFILE")) return;
        dl_iterate_phdr([](dl_phdr_info *i, size_t, void *) { g_base = i->dlpi_addr; return 1; }, nullptr);
        struct sigaction sa {};
        sa.sa_sigaction = on_prof;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigaction(SIGPROF, &sa, nullptr);
        itimerval t{{0, 1000}, {0, 1000}};
        setitimer(ITIMER_PROF, &t, nullptr);
        atexit(dump);
    }
} g_init;
}  // namespace
