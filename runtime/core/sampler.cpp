// SPDX-License-Identifier: GPL-3.0-or-later
// HK_PROFILE=1: a 1 kHz SIGPROF sampler of the host instruction pointer, for
// finding hot spots without perf. Samples are written at exit to
// hk-profile.txt as executable-relative addresses (symbolise with
// llvm-symbolizer --obj=<binary>). Samples in shared libraries are written as
// "count 0xoffset library [symbol]", with the offset relative to the library.
#include <dlfcn.h>
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
    for (int k = 0; k < g_n; k++) hist[g_samples[k]]++;
    FILE *f = fopen("hk-profile.txt", "w");
    if (!f) return;
    Dl_info self{};
    dladdr((void *)&dump, &self);
    for (auto &[a, c] : hist) {
        Dl_info i{};
        if (dladdr((void *)a, &i) && i.dli_fbase && i.dli_fbase != self.dli_fbase)
            fprintf(f, "%d 0x%lx %s %s\n", c, (unsigned long)(a - (uintptr_t)i.dli_fbase), i.dli_fname,
                    i.dli_sname ? i.dli_sname : "?");
        else
            fprintf(f, "%d 0x%lx\n", c, (unsigned long)(a - g_base));
    }
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
