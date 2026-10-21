// SPDX-License-Identifier: GPL-3.0-or-later
// HK_PROFILE=1: a 1 kHz SIGPROF sampler of the host instruction pointer (HK_PROFILE_AFTER=ms:
// from that much CPU time on; HK_PROFILE_FRAMES=A-B,C-D: only in frames A to B-1 and C to D-1:
// a run's slow frames; HK_PROFILE_MAIN=1: only the main thread's, the one running the machine,
// not helpers such as the PlayStation GPU's drawing thread), for
// finding hot spots without perf. Samples are written at exit to
// hk-profile.txt as executable-relative addresses (symbolise with
// llvm-symbolizer --obj=<binary>). Samples in shared libraries are written as
// "count 0xoffset library [symbol]", with the offset relative to the library.
// HK_PROFILE_WALL=1: instead, the main thread sampled at 1 kHz of wall-clock time, whether it runs
// or waits (a blocking read, a lock), each sample with its frame and call stack, written raw to
// hk-profile-wall.txt ("frame address..."), to find where a slow frame waited.
// Linux only (signals, ucontext, the ELF loader's list): nothing on Windows.
#ifndef _WIN32
#include <dlfcn.h>
#include <execinfo.h>
#include <time.h>
#include <link.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "core/prof.h"

namespace {
constexpr int MAX = 1 << 21;
uintptr_t g_samples[MAX];
volatile int g_n = 0;
uintptr_t g_base = 0;
volatile bool g_on = true;   // HK_PROFILE_FRAMES: in one of them
long g_main = 0;             // HK_PROFILE_MAIN: the main thread's id

// HK_PROFILE_WALL
constexpr int DEPTH = 10, WMAX = 1 << 19;
struct Wall { uint64_t frame; void *at[DEPTH]; int n; };
Wall *g_wall = nullptr;
volatile int g_wn = 0;
volatile uint64_t g_frame = 0;

void on_wall(int, siginfo_t *, void *) {
    const int n = g_wn;
    if (n >= WMAX) return;
    Wall &w = g_wall[n];
    w.frame = g_frame;
    w.n = backtrace(w.at, DEPTH);   // libgcc's unwinder, loaded before the timer starts
    g_wn = n + 1;
}

void dump_wall() {
    FILE *f = fopen("hk-profile-wall.txt", "w");
    if (!f) return;
    for (int k = 0; k < g_wn; k++) {
        fprintf(f, "%llu", (unsigned long long)g_wall[k].frame);
        for (int d = 1; d < g_wall[k].n; d++) {   // [0]: the handler
            Dl_info i{};
            const uintptr_t a = (uintptr_t)g_wall[k].at[d];
            if (dladdr((void *)a, &i) && i.dli_fbase && (uintptr_t)i.dli_fbase != g_base)
                fprintf(f, " %s+0x%lx", strrchr(i.dli_fname, '/') ? strrchr(i.dli_fname, '/') + 1 : i.dli_fname,
                        (unsigned long)(a - (uintptr_t)i.dli_fbase));
            else
                fprintf(f, " 0x%lx", (unsigned long)(a - g_base));
        }
        fputc('\n', f);
    }
    fclose(f);
    fprintf(stderr, "profile: %d wall-clock samples -> hk-profile-wall.txt\n", (int)g_wn);
}

void on_prof(int, siginfo_t *, void *uc) {
    if (!g_on || (g_main && syscall(SYS_gettid) != g_main)) return;
    auto *ctx = static_cast<ucontext_t *>(uc);
    int n = g_n;
#if defined(__x86_64__)
    const uintptr_t pc = (uintptr_t)ctx->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    const uintptr_t pc = (uintptr_t)ctx->uc_mcontext.pc;
#else
    const uintptr_t pc = 0;
#endif
    if (n < MAX) { g_samples[n] = pc; g_n = n + 1; }
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
        if (getenv("HK_PROFILE_WALL")) {
            g_wall = static_cast<Wall *>(calloc(WMAX, sizeof(Wall)));
            void *warm[2];
            backtrace(warm, 2);
            struct sigaction sa {};
            sa.sa_sigaction = on_wall;
            sa.sa_flags = SA_SIGINFO | SA_RESTART;
            sigaction(SIGPROF, &sa, nullptr);
            sigevent ev{};
            ev.sigev_notify = SIGEV_THREAD_ID;
            ev.sigev_signo = SIGPROF;
            ev._sigev_un._tid = (pid_t)getpid();   // the main thread
            timer_t t;
            if (timer_create(CLOCK_MONOTONIC, &ev, &t) == 0) {
                itimerspec its{{0, 1'000'000}, {0, 1'000'000}};
                timer_settime(t, 0, &its, nullptr);
            }
            hk::prof::g_sample_frame = [](uint64_t f) { g_frame = f; };
            atexit(dump_wall);
            return;
        }
        struct sigaction sa {};
        sa.sa_sigaction = on_prof;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigaction(SIGPROF, &sa, nullptr);
        // HK_PROFILE_AFTER=ms: start after that much CPU time (the part of a run that matters)
        const long after = getenv("HK_PROFILE_AFTER") ? atol(getenv("HK_PROFILE_AFTER")) : 0;
        itimerval t{{0, 1000}, {after / 1000, after > 0 ? (after % 1000) * 1000 : 1000}};
        setitimer(ITIMER_PROF, &t, nullptr);
        atexit(dump);
        if (getenv("HK_PROFILE_MAIN")) g_main = (long)getpid();
        if (getenv("HK_PROFILE_FRAMES")) {
            g_on = false;
            hk::prof::g_sample_frame = [](uint64_t f) {
                bool in = false;
                for (const char *q = getenv("HK_PROFILE_FRAMES"); q && *q;) {
                    unsigned long long lo, hi;
                    if (sscanf(q, "%llu-%llu", &lo, &hi) == 2 && f >= lo && f < hi) in = true;
                    q = strchr(q, ',');
                    if (q) q++;
                }
                g_on = in;
            };
        }
    }
} g_init;
}  // namespace
#endif
