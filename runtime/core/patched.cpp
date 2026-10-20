// SPDX-License-Identifier: GPL-3.0-or-later
// Functions a mod's code patch changed (Core::code_changed, host/mods.h). The program runs
// translated ahead of time (recompiled C, or the executor's steps, both made from the program as
// it was), so a function whose code changed runs in the interpreter from its next entry on: the
// interpreter reads the code as it is now. Its entries (secondary ones too) are kept here; a
// recompiled function's poll at its entry (hiraku/recomp.h, HK_POLL), the executor's calls
// (core/exec.cpp) and the dispatch (core/dispatch.cpp) look them up.
#include <cstdio>
#include <unordered_set>

#include "hiraku/recomp.h"
#include "ppc/spec.h"
#include "runtime.h"

extern "C" {
int hk_patched_any = 0;
}

namespace hk {
namespace {
std::unordered_set<uint32_t> g_patched;
}

void patch_functions(const spec::Program &p, uint32_t lo, uint32_t hi) {
    for (const auto &[entry, f] : p.funcs) {
        bool hit = false;
        for (const auto &[a, b] : f.ranges) hit |= a < hi && lo < b;
        if (!hit) continue;
        if (g_patched.insert(entry).second)
            fprintf(stderr, "mods: the function at %08x changed: from its next call on, it runs interpreted\n", entry);
        for (uint32_t s : f.entries) g_patched.insert(s);
    }
    hk_patched_any = !g_patched.empty();
}

bool patched(uint32_t entry) { return hk_patched_any && g_patched.count(entry); }

}  // namespace hk

extern "C" {
int hk_patched(uint32_t entry) { return hk::patched(entry); }

void hk_interp_function(hk_ctx *c, uint32_t entry) {
    c->pc = entry;
    if (hk::interp_run(c, c->lr & ~3u, c->r[1], UINT64_MAX) != hk::Stop::returned)
        hk::fatal(c, "the changed function at %08x did not return", entry);
}
}
