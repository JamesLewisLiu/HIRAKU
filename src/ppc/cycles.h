// SPDX-License-Identifier: GPL-3.0-or-later
// What an instruction costs in time, as MAME charges a PPC403 (its PowerPC front
// end, src/devices/cpu/powerpc/ppcfe.cpp, the 403 class): one cycle, except
//   - branch folding: b costs 0 (1 when it branches to itself); a bc that tests
//     the CR costs 0 when the instruction before it didn't modify the CR, as do
//     mcrf and the CR logical instructions;
//   - mulli, mullw, mulhw, mulhwu: 4; divw, divwu: 33; sc, rfi: 3;
//   - lmw / stmw: one per register; lswi / stswi: one per word.
// "The instruction before" is the one at pc - 4 in MAME's straight-line scan of
// the code: none when that one ends a sequence (unconditional branches, sc,
// rfi, rfci, isync, icbi, mtmsr), since MAME's scan stops there and resumes at
// a branch target with no previous instruction.
// The interpreter and the recompiler both charge these, so the two agree.
#pragma once

#include <cstdint>

namespace hk::ppc {

// Does MAME's scan stop after this instruction (desc.end_sequence())?
inline bool ends_sequence(uint32_t op) {
    const uint32_t p = op >> 26, bo = (op >> 21) & 31, xo = (op >> 1) & 0x3ff;
    switch (p) {
    case 16: return (bo & 0x14) == 0x14;               // bc, unconditional
    case 17: case 18: return true;                      // sc, b
    case 19: return xo == 50 || xo == 51 || xo == 150   // rfi, rfci, isync
                 || ((xo == 16 || xo == 528) && (bo & 0x14) == 0x14);   // bclr, bcctr, unconditional
    case 31: return xo == 982 || xo == 146;             // icbi, mtmsr
    default: return false;
    }
}

// Does it modify any CR field (desc.cr_modified())?
inline bool modifies_cr(uint32_t op) {
    const uint32_t p = op >> 26, xo = (op >> 1) & 0x3ff;
    switch (p) {
    case 10: case 11: case 13: case 28: case 29: return true;   // cmpli, cmpi, addic., andi., andis.
    case 20: case 21: case 23: return op & 1;                     // rlwimi., rlwinm., rlwnm.
    case 19: return xo == 0 || xo == 33 || xo == 129 || xo == 193 || xo == 225 || xo == 257 || xo == 289
                    || xo == 417 || xo == 449;                    // mcrf, the CR logical instructions
    case 31:
        if (xo == 0 || xo == 32 || xo == 512) return true;        // cmp, cmpl, mcrxr
        if (xo == 144) return ((op >> 12) & 0xff) != 0;           // mtcrf
        return op & 1;                                            // record forms, stwcx.
    default: return false;
    }
}

// Cycles for `op` at `pc`; `prev` is the instruction at pc - 4 when has_prev.
inline int cycles(uint32_t op, uint32_t pc, bool has_prev, uint32_t prev) {
    const uint32_t p = op >> 26;
    const bool fold = !has_prev || !modifies_cr(prev);
    switch (p) {
    case 7: return 4;                                   // mulli
    case 16: {                                          // bc
        int c = 1;
        if (!((op >> 21) & 0x10) && fold) c = 0;        // tests the CR: folded
        const uint32_t target = (uint32_t)(int32_t)(int16_t)(op & 0xfffc) + ((op & 2) ? 0 : pc);
        return target == pc && c == 0 ? 1 : c;
    }
    case 17: return 3;                                  // sc
    case 18: {                                          // b
        uint32_t li = op & 0x03fffffc;
        if (li & 0x02000000) li |= 0xfc000000;
        return li + ((op & 2) ? 0 : pc) != pc ? 0 : 1;
    }
    case 19: {
        const uint32_t xo = (op >> 1) & 0x3ff;
        if (xo == 50) return 3;                         // rfi
        if (xo == 0 || xo == 33 || xo == 129 || xo == 193 || xo == 225 || xo == 257 || xo == 289 || xo == 417
            || xo == 449)
            return fold ? 0 : 1;                        // mcrf, CR logical
        return 1;
    }
    case 31: {
        const uint32_t xo = (op >> 1) & 0x3ff;
        if (xo == 11 || xo == 75 || xo == 235 || xo == 747) return 4;              // mulhwu, mulhw, mullw(o)
        if (xo == 459 || xo == 491 || xo == 971 || xo == 1003) return 33;          // divwu(o), divw(o)
        if (xo == 597 || xo == 725) return (int)((((op >> 11) & 31) - 1) & 31) / 4 + 1;   // lswi, stswi
        return 1;
    }
    case 46: case 47: return 32 - (int)((op >> 21) & 31);   // lmw, stmw
    default: return 1;
    }
}

}  // namespace hk::ppc
