// SPDX-License-Identifier: GPL-3.0-or-later
// Dynamic control flow: indirect calls, jumps to computed addresses, and the
// fallback into the interpreter for anything the static pass did not cover.
#include <algorithm>
#include <cstdio>
#include <set>

#include "runtime.h"

namespace hk {

hk_func lookup(uint32_t a) {
    const hk_func_entry *b = hk_func_table, *e = hk_func_table + hk_func_count;
    auto it = std::lower_bound(b, e, a, [](const hk_func_entry &x, uint32_t v) { return x.addr < v; });
    return (it != e && it->addr == a) ? it->fn : nullptr;
}

namespace {
std::set<uint32_t> g_fallbacks;   // addresses entered through the interpreter (reported once)

void run_fallback(hk_ctx *c, uint32_t target, const char *why) {
    if (g_fallbacks.insert(target).second)
        fprintf(stderr, "hiraku: %s to %08x is not a recompiled function - interpreting (add it to the manifest)\n", why, target);
    c->pc = target;
    Stop s = interp_run(c, c->lr & ~3u, c->r[1], UINT64_MAX);
    if (s != Stop::returned) fatal(c, "interpreted code at %08x did not return", target);
}
}  // namespace

}  // namespace hk

using namespace hk;

extern "C" {

void hk_call_dyn(hk_ctx *c, uint32_t target) {
    if (hk_func fn = lookup(target)) fn(c);
    else run_fallback(c, target, "call");
}

void hk_jump(hk_ctx *c, uint32_t target) {
    if (hk_func fn = lookup(target)) fn(c);
    else run_fallback(c, target, "jump");
}

void hk_bad_return(hk_ctx *c, uint32_t expected) {
    fatal(c, "returned to %08x, but the caller expected %08x (longjmp / context switch?)", c->pc, expected);
}

}  // extern "C"
