// SPDX-License-Identifier: GPL-3.0-or-later
// The exception glue and guest threads (docs/KERNEL.md).
//
// The guest's own kernel decides which thread runs: on the way out of every
// exception it points SPRG3 at the 384-byte register frame of the thread to
// resume. HIRAKU keeps that kernel - C and assembly alike - and only adds what
// a static recompilation needs: every guest thread's native call chain lives
// on its own host fiber.
//
//   1. An exception (sc, or an interrupt at a poll point) is taken exactly as
//      the CPU would, and the guest's vector code runs in the interpreter. It
//      saves registers into the frame and calls the (native) C handlers.
//   2. When its rfi returns to user state, SPRG3 names the resumed thread's
//      frame. If that is the frame this thread pushed, we return to the
//      native code that took the exception.
//   3. Otherwise we park this fiber under its frame address and switch to the
//      fiber parked under the target frame. It returns from ITS glue call
//      with the registers the exit path just loaded - its own.
//   4. A frame no fiber is parked under is a new thread: a new fiber starts
//      running guest code at the frame's SRR0.
//
// Only one fiber runs at a time and hk_ctx is "the CPU", shared by all.
#include <sys/mman.h>
#include <ucontext.h>

#include <cstdio>
#include <unordered_map>
#include <vector>

#include "cpu403.h"
#include "prof.h"
#include "runtime.h"
#include "sched.h"

namespace hk {

namespace {

constexpr size_t FIBER_STACK = 1 << 20;

struct Fiber {
    ucontext_t uc{};
    void *stack = nullptr;
    uint32_t frame = 0;        // guest frame it is parked under
    uint32_t resume_pc = 0;    // the SRR0 that frame must carry for it to be ours
    int id = 0;
};

hk_ctx *g_c = nullptr;
Fiber g_main_fiber;                                // the host thread itself (boot + first guest thread)
Fiber *g_cur = &g_main_fiber;
std::unordered_map<uint32_t, Fiber *> g_parked;    // frame -> fiber
std::vector<Fiber *> g_graveyard;                  // fibers whose thread died (frame reused)
int g_next_id = 1;
bool g_fibers_enabled = false;

void free_fiber(Fiber *f) {
    munmap(f->stack, FIBER_STACK);
    delete f;
}

// Entry of a new guest thread's fiber: run guest code from c->pc, forever.
// (A guest thread ends by entering the kernel and never being resumed.)
void thread_main() {
    hk_ctx *c = g_c;
    for (;;) {
        if (hk_func fn = lookup(c->pc)) {
            fn(c);                 // returns with c->pc = LR (HK_RET)
        } else {
            interp_run(c, 0xffffffffu, 0, UINT64_MAX);
            fatal(c, "guest thread interpreter loop ended");
        }
    }
}

Fiber *new_thread_fiber() {
    auto *f = new Fiber;
    f->id = g_next_id++;
    f->stack = mmap(nullptr, FIBER_STACK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (f->stack == MAP_FAILED) fatal(g_c, "cannot allocate a fiber stack");
    getcontext(&f->uc);
    f->uc.uc_stack.ss_sp = f->stack;
    f->uc.uc_stack.ss_size = FIBER_STACK;
    f->uc.uc_link = nullptr;
    makecontext(&f->uc, thread_main, 0);
    return f;
}

void switch_threads(hk_ctx *c, uint32_t my_frame, uint32_t my_resume, uint32_t target_frame) {
    Fiber *me = g_cur;
    me->frame = my_frame;
    me->resume_pc = my_resume;
    if (auto old = g_parked.find(my_frame); old != g_parked.end() && old->second != me)
        g_graveyard.push_back(old->second);      // a dead thread's fiber, its frame reused
    g_parked[my_frame] = me;

    Fiber *next;
    auto it = g_parked.find(target_frame);
    if (it != g_parked.end() && it->second->resume_pc == c->pc) {
        next = it->second;
        g_parked.erase(it);
    } else {
        if (it != g_parked.end()) {              // parked there, but the frame now belongs to a new thread
            g_graveyard.push_back(it->second);
            g_parked.erase(it);
        }
        next = new_thread_fiber();
        g_stats.threads_started++;
    }
    g_stats.thread_switches++;
    g_cur = next;
    swapcontext(&me->uc, &next->uc);
    // resumed: the exit path that chose us has loaded our registers
    g_cur = me;
    for (Fiber *f : g_graveyard)
        if (f != me) free_fiber(f);
    g_graveyard.clear();
}

// Run the guest's vector code natively when it was recompiled: it saves the
// frame, calls the kernel's handlers and ends in rfi (emitted as a return with
// pc = SRR0, msr = SRR1). Anything left (no recompiled vector) is interpreted
// by the caller's loop.
void run_vector(hk_ctx *c) {
    if (g_interp.lle) return;
    if (hk_func fn = lookup(c->pc)) {
        g_stats.native_vectors++;
        fn(c);
    }
}

}  // namespace

void glue_init(hk_ctx *c) {
    g_c = c;
    g_fibers_enabled = true;
}

// Take exception `vector` with return address `ret`, run the guest's vector
// code, and come back when this thread is resumed (possibly much later).
void glue_exception(hk_ctx *c, uint32_t vector, uint32_t ret) {
    const bool from_user = c->msr & msr::PR;
    const uint32_t my_frame = c->r[1] - 384;    // where the vector code will push our frame
    g_stats.exceptions++;
    if (!from_user) {
        // From supervisor state (e.g. an interrupt handler calling a kernel service
        // through `sc`): the vector code pushes the frame on the current stack and
        // its exit path returns to the same context - no thread switch is possible.
        const uint32_t msr0 = c->msr;
        prof::Scope ps(prof::GLUE);
        take_exception(c, vector, ret);
        run_vector(c);
        for (uint64_t n = 0; !(c->pc == ret && c->msr == msr0); n++) {
            interp_step(c);
            if (n > 50'000'000) fatal(c, "supervisor exception %04x from %08x never returned", vector, ret);
        }
        return;
    }
    {
        prof::Scope ps(prof::GLUE);   // the vector code only: not the time other threads run after a switch
        take_exception(c, vector, ret);
        run_vector(c);
        for (uint64_t n = 0; !(c->msr & msr::PR); n++) {
            interp_step(c);
            if (n > 50'000'000) fatal(c, "exception %04x from %08x never returned to user state", vector, ret);
        }
    }
    const uint32_t target = c->sprg[3];
    if (target == my_frame && c->pc == ret) return;
    if (!g_fibers_enabled) fatal(c, "thread switch with fibers disabled");
    switch_threads(c, my_frame, ret, target);
}

}  // namespace hk

using namespace hk;

extern "C" {

// Recompiled code: a poll point ran out of budget.
void hk_poll(hk_ctx *c, uint32_t pc) {
    if (shadow_native_active()) {   // time stands still while a shadow check runs natively
        hk_budget = 1 << 20;
        return;
    }
    sched_sync();
    if ((c->msr & msr::EE) && cpu403::irq_pending()) glue_exception(c, cpu403::pending_vector(), pc);
}

// Recompiled code: `sc`. c->pc is the address after it.
void hk_syscall(hk_ctx *c) {
    if (shadow_native_active()) shadow_abandon();
    glue_exception(c, 0xc00, c->pc);
}

}  // extern "C"
