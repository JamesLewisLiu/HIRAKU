// SPDX-License-Identifier: GPL-3.0-or-later
// A Firebeat program's control flow found by HIRAKU itself, without Ghidra (PLAN.md section 27,
// FB-1): the recompile spec analyze/ghidra.sh + analyze/prep.py make (recomp.txt, src/ppc/spec.h),
// from the program image alone, so a release can run a game it has never seen (the executor,
// runtime/core/exec.h) with nothing installed.
//
// What Ghidra's analysis gives the recompiler, done the way it does it:
//   - the functions: the program's entry, the 4xx exception vectors, every `bl` target, the
//     functions only reached through code pointers (aligned data words pointing at code, and
//     GCC's prologues after another function's end), and game.toml's extra entries;
//   - each function's body: what its flow reaches (fall-through, branches, jump tables) up to
//     another function's entry, as address ranges;
//   - the jump tables: GCC's switch dispatch (cmplwi, slwi, addis/lwz or lis/addi/lwzx, mtctr,
//     bctr), the table's words up to the bound;
// and what prep.py adds: a call or tail target inside another function's body becomes a
// secondary entry of that function; kernel ranges (game.toml's nopoll) are marked.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hk::analyze {

struct Options {
    uint32_t base = 0x80000000u;   // load address
    uint32_t entry = 0x80003000u;  // the program's entry (game.toml [program] entry)
    std::vector<uint32_t> extra;   // game.toml [recomp] extra_entries
    std::vector<std::pair<uint32_t, uint32_t>> nopoll;   // game.toml [recomp] nopoll
    std::string image_path;        // written as the spec's `image` line
};

struct Result {
    std::string spec;              // recomp.txt
    size_t functions = 0, secondary = 0, tables = 0;
    std::vector<std::string> notes;   // what could not be resolved (unresolved bctr, ...)
};

Result run(const std::vector<uint8_t> &image, const Options &o);

}  // namespace hk::analyze
