#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Recompile System 573 R3000 blocks to C++ (runtime/sys573/mips.h, PLAN.md section 19).

  tools/mipsgen.py RECORDING OUTDIR [--files N] [--min-runs N]

RECORDING is what runs with HK_573_RECORD wrote (tools/mipsrec.py): per line a block's pc, how often it ran,
and its instruction words. OUTDIR gets blocks_0.cpp .. blocks_{N-1}.cpp and table.cpp: no blocks
while there is no recording. Run on every build, it does nothing unless something changed.

Each block becomes one C++ function that does what MAME's interpreter (psx.cpp) does for those
instructions, in the same order, with the same results:
- the registers live in locals, written back when the block ends;
- the pipeline (MAME's m_delayr/m_delayv: a load's delay slot, a branch's delay slot) is
  tracked here, at compile time, so its bookkeeping costs nothing at run time; m_delayv is a
  local, `dv`, because MAME keeps whatever a load to r0 left there;
- RAM and ROM are read and written through the shim's page table, anything else through
  MAME's handlers at MAME's m_icount (mips_ops.h);
- whatever a block does not do itself (exceptions, bus errors, COP0 writes, RFE, COP1/3) goes to
  MAME's interpreter for that one instruction, and the block ends (hk_psx::bail);
- a block runs on past a conditional branch that isn't taken, after checking the timeslice has
  room for the next stretch; taken, the block ends, or starts over when the branch goes back to
  its start (a loop), while there is room and no code has been written (hk_psx::epoch).
"""
import argparse
import os
import sys

SPECIAL_OK = {0, 2, 3, 4, 6, 7, 8, 9, 16, 17, 18, 19, 24, 25, 26, 27, 32, 33, 34, 35, 36, 37, 38, 39, 42, 43}
COP0_RI = {0, 1, 2, 4, 10}   # INDEX, RANDOM, ENTRYLO, CONTEXT, ENTRYHI: reserved instruction


def is_branch(w):
    op = w >> 26
    return op in (1, 2, 3, 4, 5, 6, 7) or (op == 0 and (w & 63) in (8, 9))


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


def hexu(v):
    return f'0x{v & 0xffffffff:08x}u'


class Block:
    """Emits one block. MAME's pipeline at each instruction boundary, known here:
    pend: the register a load is still on its way to (MAME's m_delayr 1..31), None (0), or
          'entry' at the first instruction: whatever the CPU was doing when the block started
          (dr0, read at run time);
    br:   inside a branch's delay slot: None, 'tk' (taken is decided at run time) or 'always'."""

    def __init__(self, pc, words, idx, region=None, uses=None, writes=None):
        self.pc, self.words, self.idx = pc, words, idx
        self.lines = []
        self.pend = 'entry'
        self.br = None
        self.uses = set() if uses is None else uses        # registers read or written: locals
        self.writes = set() if writes is None else writes  # registers written: flushed when the block ends
        # the blocks compiled into the same function (pc -> first stretch): a jump to one is a goto
        self.region = region if region is not None else {}
        self.jtarget = None    # an unconditional jump's constant target
        self.n = 0             # instructions compiled
        self.done = False      # the delay slot has run: the next pc is npc
        self.terminal = False  # this instruction always goes to the interpreter
        self.target = None     # a conditional branch's target
        self.last_was_conditional = False
        self.len0 = 0          # the first stretch (hk573_block::len)

    def o(self, s):
        self.lines.append('\t' + s)

    # ---- registers ----
    def R(self, i):
        if i == 0:
            return '0u'
        self.uses.add(i)
        return f'r{i}'

    def W(self, i, expr):
        if i == 0:
            return
        self.uses.add(i)
        self.writes.add(i)
        self.o(f'r{i} = {expr};')

    def pipe(self, rt):
        """MAME's get_register_from_pipeline(rt): m_delayv when m_delayr == rt."""
        if self.br is not None:
            return self.R(rt)
        if self.pend == 'entry':
            return f'(dr0 == {rt}u ? dv : {self.R(rt)})'
        return 'dv' if (self.pend or 0) == rt else self.R(rt)

    # ---- the pipeline ----
    def dr(self):
        """MAME's m_delayr now, as C."""
        if self.br == 'always':
            return '32u'
        if self.br == 'tk':
            return '(tk ? 32u : 33u)'
        if self.pend == 'entry':
            return 'dr0'
        return f'{self.pend or 0}u'

    def bail(self, k, pc):
        return f'{{ FL; X::bail(d, {hexu(pc)}, {self.dr()}, dv, base - {k}); return; }}'

    def advance(self, pc):
        """MAME's advance_pc(): commit a pending load, or leave the branch delay slot."""
        if self.br is not None:
            self.o('npc = dv;' if self.br == 'always' else f'npc = tk ? dv : {hexu(pc + 4)};')
            self.o('dv = 0;')
            self.last_was_conditional = self.br == 'tk'
            self.br = None
            self.done = True
        elif self.pend == 'entry':
            self.o('if (dr0) { COMMIT0; dv = 0; }')   # COMMIT0: filled in when the locals are known
            self.pend = None
        elif self.pend is not None:
            self.W(self.pend, 'dv')
            self.o('dv = 0;')
            self.pend = None

    def load(self, pc, rd, expr):
        """MAME's load(): the result lands as the pipeline moves on."""
        self.o(f'{{ const uint32_t t = {expr};')
        self.advance(pc)
        self.W(rd, 't')
        self.o('}')

    def delayed_load(self, pc, rt, expr):
        """MAME's delayed_load(): the result waits a slot. A second load into the register still
        in the slot drops the first (m_delayr = m_delayv = 0)."""
        self.o(f'{{ const uint32_t t = {expr};')
        if self.br is None and self.pend == 'entry':
            self.o(f'if (dr0 == {rt}u) dv = 0; else if (dr0) {{ COMMIT0; dv = 0; }}')
            self.pend = None
        elif self.br is None and (self.pend or 0) == rt:
            self.o('dv = 0;')
            self.pend = None
        else:
            self.advance(pc)
        self.pend = rt if rt else None
        self.o('dv = t; }')

    def branch(self, pc, cond, target):
        """MAME's branch() (cond None) or conditional_branch(); target as C."""
        if cond is None:
            self.o(f'{{ const uint32_t t = {target};')
            self.advance(pc)
            self.o('dv = t; }')
            self.br = 'always'
            self.jtarget = int(target.rstrip('u'), 16) if target.startswith('0x') else None
        else:
            self.o(f'{{ const bool c = {cond};')
            self.advance(pc)
            self.o(f'tk = c; dv = c ? {target} : 0u; }}')
            self.br = 'tk'
            self.target = int(target.rstrip('u'), 16)

    # ---- the block ----
    def stretch(self, i):
        """instructions from i to the end of its stretch: through the next branch's delay slot (an
        upper bound: the block may stop sooner, which only makes the room check stricter)"""
        for j in range(i, len(self.words)):
            if is_branch(self.words[j]):
                return min(j + 2, len(self.words)) - i
        return len(self.words) - i

    def jump(self, target, executed, last, dr):
        """the block goes on at the constant target after `executed` instructions of this section:
        a goto when the target is compiled into this function and the timeslice has room for its
        first stretch (and no checked code was written since the function started), else the end"""
        if target in self.region:
            return (f'{{ base -= {executed}; if (base >= {self.region[target]} && X::epoch == ep0) '
                    f'{{ dr0 = {dr}; goto L_{target:08x}; }} '
                    f'FL; X::leave(d, {hexu(target)}, {dr}, dv, base, {hexu(last)}); return; }}')
        return f'{{ FL; X::leave(d, {hexu(target)}, {dr}, dv, base - {executed}, {hexu(last)}); return; }}'

    def emit(self):
        pc = self.pc
        self.len0 = self.stretch(0)
        self.lines.append(f'L_{self.pc:08x}:')
        k = 0
        while k < len(self.words):
            w = self.words[k]
            if self.done:
                break
            if self.br is not None and is_branch(w):
                break   # a branch in a delay slot: left to the interpreter
            self.o(f'// {pc:08x}  {w:08x}')
            self.n = k + 1
            self.terminal = False
            in_slot = self.br
            slow = self.insn(k, pc, w)
            if self.terminal:
                return
            if slow:   # a device cut the timeslice short: stop after this instruction, as MAME does
                npc = 'npc' if self.done else hexu(pc + 4)
                self.o(f'if (st) [[unlikely]] {{ FL; X::leave(d, {npc}, {self.dr()}, dv, X::icount(d) - 1, {hexu(pc)}); return; }}')
            if self.done and in_slot == 'tk' and k + 1 < len(self.words):
                # past a conditional branch: taken, the block goes there; not taken, it runs on
                dr = self.dr()
                self.o(f'if (tk) {self.jump(self.target, k + 1, pc, dr)}')
                self.o(f'if (base - {k + 1} < {self.stretch(k + 1)}) [[unlikely]] '
                       f'{{ FL; X::leave(d, {hexu(pc + 4)}, {dr}, dv, base - {k + 1}, {hexu(pc)}); return; }}')
                self.done = False
            k += 1
            pc += 4
        last = self.pc + 4 * (self.n - 1)
        dr = self.dr()
        if self.br is not None:   # stopped before a delay slot: the interpreter runs it
            self.o(f'FL; X::leave(d, {hexu(last + 4)}, {dr}, dv, base - {self.n}, {hexu(last)}); return;')
        elif not self.done:   # ran off the end of its words: on at the next instruction
            self.o(self.jump(last + 4, self.n, last, dr))
        elif self.br is None and self.target is not None and self.last_was_conditional:
            self.o(f'if (tk) {self.jump(self.target, self.n, last, dr)}')
            self.o(self.jump(last + 4, self.n, last, dr))
        elif self.jtarget is not None:
            self.o(self.jump(self.jtarget, self.n, last, dr))
        else:
            self.o(f'FL; X::leave(d, npc, {dr}, dv, base - {self.n}, {hexu(last)}); return;')

    def insn(self, k, pc, w):
        """One instruction. True if it may have gone through MAME's handlers."""
        op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        sa, fn, imm = (w >> 6) & 31, w & 63, w & 0xffff
        simm = sx16(imm)
        R = self.R

        def bail():
            self.o(self.bail(k, pc))
            self.terminal = True

        if op == 0:
            if fn not in SPECIAL_OK or fn in (12, 13):   # reserved, syscall, break
                bail()
            elif fn == 0:
                self.load(pc, rd, f'{R(rt)} << {sa}')
            elif fn == 2:
                self.load(pc, rd, f'{R(rt)} >> {sa}')
            elif fn == 3:
                self.load(pc, rd, f'(uint32_t)((int32_t){R(rt)} >> {sa})')
            elif fn == 4:
                self.load(pc, rd, f'{R(rt)} << ({R(rs)} & 31)')
            elif fn == 6:
                self.load(pc, rd, f'{R(rt)} >> ({R(rs)} & 31)')
            elif fn == 7:
                self.load(pc, rd, f'(uint32_t)((int32_t){R(rt)} >> ({R(rs)} & 31))')
            elif fn == 8:     # jr
                self.branch(pc, None, R(rs))
            elif fn == 9:     # jalr
                self.branch(pc, None, R(rs))
                self.W(rd, hexu(pc + 8))
            elif fn == 16:
                self.load(pc, rd, 'X::hi(d)')
            elif fn == 18:
                self.load(pc, rd, 'X::lo(d)')
            elif fn == 17:
                self.o(f'X::mthi(d, {R(rs)});')
                self.advance(pc)
            elif fn == 19:
                self.o(f'X::mtlo(d, {R(rs)});')
                self.advance(pc)
            elif fn in (24, 25, 26, 27):
                self.o(f'X::muldiv(d, {fn - 23}, {R(rs)}, {R(rt)});')
                self.advance(pc)
            elif fn == 32:    # add: overflow raises
                self.o(f'{{ const uint32_t s = {R(rs)} + {R(rt)};')
                self.o(f'if ((int32_t)(~({R(rs)} ^ {R(rt)}) & ({R(rs)} ^ s)) < 0) [[unlikely]] {self.bail(k, pc)}')
                self.load(pc, rd, 's')
                self.o('}')
            elif fn == 33:
                self.load(pc, rd, f'{R(rs)} + {R(rt)}')
            elif fn == 34:    # sub: overflow raises
                self.o(f'{{ const uint32_t s = {R(rs)} - {R(rt)};')
                self.o(f'if ((int32_t)(({R(rs)} ^ {R(rt)}) & ({R(rs)} ^ s)) < 0) [[unlikely]] {self.bail(k, pc)}')
                self.load(pc, rd, 's')
                self.o('}')
            elif fn == 35:
                self.load(pc, rd, f'{R(rs)} - {R(rt)}')
            elif fn == 36:
                self.load(pc, rd, f'{R(rs)} & {R(rt)}')
            elif fn == 37:
                self.load(pc, rd, f'{R(rs)} | {R(rt)}')
            elif fn == 38:
                self.load(pc, rd, f'{R(rs)} ^ {R(rt)}')
            elif fn == 39:
                self.load(pc, rd, f'~({R(rs)} | {R(rt)})')
            elif fn == 42:
                self.load(pc, rd, f'(uint32_t)((int32_t){R(rs)} < (int32_t){R(rt)})')
            elif fn == 43:
                self.load(pc, rd, f'(uint32_t)({R(rs)} < {R(rt)})')
            return False

        if op == 1:   # REGIMM: bit 0 of rt picks bgez over bltz; rt 16/17 also link
            ge = rt & 1
            self.branch(pc, f'(int32_t){R(rs)} {">=" if ge else "<"} 0', hexu(pc + 4 + (simm << 2)))
            if rt == (17 if ge else 16):
                self.W(31, hexu(pc + 8))
            return False
        if op in (2, 3):
            self.branch(pc, None, hexu(((pc + 4) & 0xf0000000) + ((w & 0x3ffffff) << 2)))
            if op == 3:
                self.W(31, hexu(pc + 8))
            return False
        if op in (4, 5, 6, 7):   # MAME's blez/bgtz compare with rt too
            a, b = R(rs), R(rt)
            cond = {4: f'{a} == {b}', 5: f'{a} != {b}',
                    6: f'(int32_t){a} < 0 || {a} == {b}', 7: f'(int32_t){a} >= 0 && {a} != {b}'}[op]
            self.branch(pc, cond, hexu(pc + 4 + (simm << 2)))
            return False
        if op == 8:   # addi: overflow raises
            self.o(f'{{ const uint32_t s = {R(rs)} + {hexu(simm)};')
            self.o(f'if ((int32_t)(~({R(rs)} ^ {hexu(simm)}) & ({R(rs)} ^ s)) < 0) [[unlikely]] {self.bail(k, pc)}')
            self.load(pc, rt, 's')
            self.o('}')
            return False
        if op == 9:
            self.load(pc, rt, f'{R(rs)} + {hexu(simm)}')
            return False
        if op == 10:
            self.load(pc, rt, f'(uint32_t)((int32_t){R(rs)} < {simm})')
            return False
        if op == 11:
            self.load(pc, rt, f'(uint32_t)({R(rs)} < {hexu(simm)})')
            return False
        if op in (12, 13, 14):
            self.load(pc, rt, f'{R(rs)} {"&|^"[op - 12]} {hexu(imm)}')
            return False
        if op == 15:
            self.load(pc, rt, hexu(imm << 16))
            return False

        if op == 16:   # COP0 (kernel mode: usable): mfc0 here, the rest in the interpreter
            if rs == 0 and rd not in COP0_RI:
                if rd < 16:
                    self.delayed_load(pc, rt, f'X::cp0(d, {rd})')
                else:
                    self.advance(pc)
            else:
                bail()
            return False
        if op == 18:   # COP2, the GTE
            if rs in (8, 12):
                bail()
                return False
            self.o(f'if (!(X::sr(d) & X::SR_CU2)) [[unlikely]] {self.bail(k, pc)}')
            if rs == 0:
                self.delayed_load(pc, rt, f'X::mfc2(d, {hexu(pc)}, {rd})')
            elif rs == 2:
                self.delayed_load(pc, rt, f'X::cfc2(d, {hexu(pc)}, {rd})')
            elif rs == 4:
                self.o(f'X::mtc2(d, {hexu(pc)}, {rd}, {R(rt)});')
                self.advance(pc)
            elif rs == 6:
                self.o(f'X::ctc2(d, {hexu(pc)}, {rd}, {R(rt)});')
                self.advance(pc)
            else:
                if (w >> 25) & 1:
                    self.o(f'X::cop2(d, {hexu(pc)}, {hexu(w & 0x1ffffff)});')
                self.advance(pc)
            return False

        ic = f'base - {k}'
        berr = f'if (st == 2) [[unlikely]] {self.bail(k, pc)}'
        if op in (32, 33, 35, 36, 37):   # lb lh lw lbu lhu
            self.o(f'{{ const uint32_t a = {R(rs)} + {hexu(simm)};')
            if op in (33, 37):
                self.o(f'if (a & 1) [[unlikely]] {self.bail(k, pc)}')
            if op == 35:
                self.o(f'if (a & 3) [[unlikely]] {self.bail(k, pc)}')
            access = {32: f'(uint32_t)(int32_t)(int8_t)X::rd8(d, pg, a, {ic}, st)',
                      36: f'X::rd8(d, pg, a, {ic}, st)',
                      33: f'(uint32_t)(int32_t)(int16_t)X::rd16(d, pg, a, {ic}, st)',
                      37: f'X::rd16(d, pg, a, {ic}, st)',
                      35: f'X::rd32(d, pg, a, {ic}, st)'}[op]
            self.o(f'const uint32_t v = {access};')
            self.o(berr)
            self.delayed_load(pc, rt, 'v')
            self.o('}')
            return True
        if op == 34:   # lwl
            self.o(f'{{ uint32_t a = {R(rs)} + {hexu(simm)}; const uint32_t lt = a & 3; a &= ~3u;')
            self.o(f'uint32_t v = {self.pipe(rt)};')
            self.o(f'switch (lt) {{ case 0: v = (v & 0x00ffffffu) | (X::rd32m(d, pg, a, 0x000000ffu, {ic}, st) << 24); break;')
            self.o(f'case 1: v = (v & 0x0000ffffu) | (X::rd32m(d, pg, a, 0x0000ffffu, {ic}, st) << 16); break;')
            self.o(f'case 2: v = (v & 0x000000ffu) | (X::rd32m(d, pg, a, 0x00ffffffu, {ic}, st) << 8); break;')
            self.o(f'default: v = X::rd32(d, pg, a, {ic}, st); break; }}')
            self.o(berr)
            self.delayed_load(pc, rt, 'v')
            self.o('}')
            return True
        if op == 38:   # lwr
            self.o(f'{{ const uint32_t a = {R(rs)} + {hexu(simm)};')
            self.o(f'uint32_t v = {self.pipe(rt)};')
            self.o(f'switch (a & 3) {{ case 0: v = X::rd32(d, pg, a, {ic}, st); break;')
            self.o(f'case 1: v = (v & 0xff000000u) | (X::rd32m(d, pg, a, 0xffffff00u, {ic}, st) >> 8); break;')
            self.o(f'case 2: v = (v & 0xffff0000u) | (X::rd32m(d, pg, a, 0xffff0000u, {ic}, st) >> 16); break;')
            self.o(f'default: v = (v & 0xffffff00u) | (X::rd32m(d, pg, a, 0xff000000u, {ic}, st) >> 24); break; }}')
            self.o(berr)
            self.delayed_load(pc, rt, 'v')
            self.o('}')
            return True
        if op in (40, 41, 42, 43, 46):   # sb sh swl sw swr
            v = R(rt)
            self.o(f'{{ const uint32_t a = {R(rs)} + {hexu(simm)};')
            if op == 40:
                self.o(f'X::wr32m(d, pg, a, {v} << (8 * (a & 3)), 0xffu << (8 * (a & 3)), {ic}, st);')
            elif op == 41:
                self.o(f'if (a & 1) [[unlikely]] {self.bail(k, pc)}')
                self.o(f'X::wr32m(d, pg, a, {v} << (8 * (a & 2)), 0xffffu << (8 * (a & 2)), {ic}, st);')
            elif op == 43:
                self.o(f'if (a & 3) [[unlikely]] {self.bail(k, pc)}')
                self.o(f'X::wr32(d, pg, a, {v}, {ic}, st);')
            elif op == 42:
                self.o('const uint32_t b = a & ~3u;')
                self.o(f'switch (a & 3) {{ case 0: X::wr32m(d, pg, b, {v} >> 24, 0x000000ffu, {ic}, st); break;')
                self.o(f'case 1: X::wr32m(d, pg, b, {v} >> 16, 0x0000ffffu, {ic}, st); break;')
                self.o(f'case 2: X::wr32m(d, pg, b, {v} >> 8, 0x00ffffffu, {ic}, st); break;')
                self.o(f'default: X::wr32(d, pg, b, {v}, {ic}, st); break; }}')
            else:
                self.o(f'switch (a & 3) {{ case 0: X::wr32(d, pg, a, {v}, {ic}, st); break;')
                self.o(f'case 1: X::wr32m(d, pg, a, {v} << 8, 0xffffff00u, {ic}, st); break;')
                self.o(f'case 2: X::wr32m(d, pg, a, {v} << 16, 0xffff0000u, {ic}, st); break;')
                self.o(f'default: X::wr32m(d, pg, a, {v} << 24, 0xff000000u, {ic}, st); break; }}')
            self.o(berr)
            self.advance(pc)
            self.o('}')
            return True
        if op == 50:   # lwc2: the GTE register is set after the pipeline moves on
            self.o(f'if (!(X::sr(d) & X::SR_CU2)) [[unlikely]] {self.bail(k, pc)}')
            self.o(f'{{ const uint32_t a = {R(rs)} + {hexu(simm)};')
            self.o(f'if (a & 3) [[unlikely]] {self.bail(k, pc)}')
            self.o(f'const uint32_t v = X::rd32(d, pg, a, {ic}, st);')
            self.o(berr)
            self.advance(pc)
            self.o(f'X::mtc2(d, {hexu(pc + 4)}, {rt}, v); }}')
            return True
        if op == 58:   # swc2
            self.o(f'if (!(X::sr(d) & X::SR_CU2)) [[unlikely]] {self.bail(k, pc)}')
            self.o(f'{{ const uint32_t a = {R(rs)} + {hexu(simm)};')
            self.o(f'if (a & 3) [[unlikely]] {self.bail(k, pc)}')
            self.o(f'X::wr32(d, pg, a, X::mfc2(d, {hexu(pc)}, {rt}), {ic}, st);')
            self.o(berr)
            self.advance(pc)
            self.o('}')
            return True
        bail()   # COP1, COP3, LWC/SWC 0, 1, 3, reserved
        return False


def successors(pc, words):
    """the constant places a block can go on to (where its code leads, as emit() compiles it)"""
    out = []
    k = 0
    while k < len(words):
        w = words[k]
        op = w >> 26
        if not is_branch(w):
            k += 1
            continue
        if k + 1 >= len(words) or is_branch(words[k + 1]):
            return out   # its delay slot is left to the interpreter
        if op in (1, 4, 5, 6, 7):
            out.append((pc + 4 * k + 4 + (sx16(w & 0xffff) << 2)) & 0xffffffff)
            k += 2
            continue
        if op in (2, 3):
            out.append(((pc + 4 * k + 4) & 0xf0000000) + ((w & 0x3ffffff) << 2))
        return out   # a jump ends it
    out.append((pc + 4 * len(words)) & 0xffffffff)
    return out


def region_of(entry, by_pc, max_blocks=6, depth=4):
    """the blocks compiled with entry into one function: those on a loop back to it (a polling or
    copy loop spread over a few blocks then runs without returning to the dispatcher)"""
    pc0 = entry[0]
    seen = {pc0: entry}
    frontier = [pc0]
    for _ in range(depth):
        nxt = []
        for pc in frontier:
            for t in successors(pc, seen[pc][1]):
                if t not in seen and len(by_pc.get(t, ())) == 1:
                    seen[t] = by_pc[t][0]
                    nxt.append(t)
        frontier = nxt
    # keep those the entry can be reached from again
    back = {pc0}
    changed = True
    while changed:
        changed = False
        for pc, (_, words) in seen.items():
            if pc not in back and any(t in back for t in successors(pc, words)):
                back.add(pc)
                changed = True
    loop = [pc0] + sorted(back - {pc0})
    return loop if len(loop) <= max_blocks else [pc0]


def region_code(idx, sections):
    """one function: the entry block, then the others of its loop, each under its own label"""
    uses, writes, region = set(), set(), {}
    blocks = [Block(pc, words, idx, region, uses, writes) for pc, words in sections]
    for b in blocks:
        region[b.pc] = b.stretch(0)
    for b in blocks:
        b.emit()
    name = f'b{idx}'
    regs = sorted(uses)
    # a load pending where a section starts lands in MAME's register array, and in the local if
    # the function has one (a local it only reads isn't written back)
    commit0 = 'X::r(d, dr0) = dv;'
    if regs:
        commit0 += ' switch (dr0) { ' + ' '.join(f'case {i}: r{i} = dv; break;' for i in regs) + ' default: break; }'
    e = blocks[0]
    out = [f'// {e.pc:08x}: {e.n} instructions' + (f', looping through {len(blocks) - 1} more blocks' if len(blocks) > 1 else '')]
    out.append(f'static void {name}(psxcpu_device &d) {{')
    out.append('\tusing X = hk_psx;')
    out.append('\tint base = X::icount(d);')
    out.append('\tconst uint64_t ep0 = X::epoch;')
    out.append('\tconst X::page_t *pg = X::pages(d);')
    out.append('\tuint32_t dr0 = X::delayr(d);')
    out.append('\tuint32_t dv = X::delayv(d), npc = 0;')
    out.append('\tint st = 0;')
    out.append('\tbool tk = false;')
    if regs:
        out.append('\tuint32_t ' + ', '.join(f'r{i} = X::r(d, {i})' for i in regs) + ';')
    fl = ' '.join(f'X::r(d, {i}) = r{i};' for i in sorted(writes))
    out.append(f'#define FL do {{ {fl} }} while (0)')
    for b in blocks:
        out.extend(l.replace('COMMIT0', commit0) for l in b.lines)
    out.append('#undef FL')
    out.append('}')
    return name, '\n'.join(out), blocks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('recording')
    ap.add_argument('outdir')
    ap.add_argument('--files', type=int, default=8)
    ap.add_argument('--min-runs', type=int, default=1)
    a = ap.parse_args()

    # nothing to do when the recording, the options and this script are what the output was made from
    stamp = os.path.join(a.outdir, 'stamp')
    try:
        st = os.stat(a.recording)
        key = f'{st.st_size} {st.st_mtime_ns}'
    except FileNotFoundError:
        key = 'none'
    key += f' {os.stat(__file__).st_mtime_ns} {a.files} {a.min_runs}\n'
    try:
        if open(stamp).read() == key and all(os.path.exists(os.path.join(a.outdir, f'blocks_{f}.cpp')) for f in range(a.files)):
            return
    except FileNotFoundError:
        pass

    blocks = []
    for line in (open(a.recording) if key[:4] != 'none' else []):
        if line.startswith('#') or not line.strip():
            continue
        p = line.split()
        if int(p[1]) < a.min_runs:
            continue
        blocks.append((int(p[0], 16), [int(x, 16) for x in p[2:]]))

    os.makedirs(a.outdir, exist_ok=True)
    by_pc = {}
    for pc, words in blocks:
        by_pc.setdefault(pc, []).append((pc, words))
    parts = [[] for _ in range(a.files)]
    for i, b in enumerate(blocks):
        parts[i % a.files].append((i, b))
    header = ('// generated by tools/mipsgen.py from %s: do not edit\n'
              '#include "sys573/mips_ops.h"\n\n' % os.path.basename(a.recording))
    total = looping = 0
    for f, part in enumerate(parts):
        body, table = [], []
        for i, b in part:
            sections = [by_pc[pc][0] if pc != b[0] else b for pc in region_of(b, by_pc)]
            name, code, cb = region_code(i, sections)
            e = cb[0]
            body.append(f'static const uint32_t w{i}[] = {{' + ', '.join(hexu(x) for x in e.words[:e.n]) + '};')
            extra = 'nullptr'
            if len(cb) > 1:
                looping += 1
                for j, s2 in enumerate(cb[1:]):
                    body.append(f'static const uint32_t w{i}_{j}[] = {{' + ', '.join(hexu(x) for x in s2.words[:s2.n]) + '};')
                body.append(f'static const hk573_range x{i}[] = {{' +
                            ', '.join(f'{{{hexu(s2.pc)}, {s2.n}, w{i}_{j}}}' for j, s2 in enumerate(cb[1:])) + '};')
                extra = f'x{i}'
            body.append(code)
            table.append(f'\t{{{hexu(e.pc)}, {min(e.len0, e.n)}, {e.n}, w{i}, {name}, {len(cb) - 1}, {extra}}},')
        total += len(part)
        with open(os.path.join(a.outdir, f'blocks_{f}.cpp.tmp'), 'w') as out:
            out.write(header)
            out.write('\n\n'.join(body))
            out.write(f'\n\nextern const hk573_block hk573_part_{f}[] = {{\n')
            out.write('\n'.join(table) if table else '\t{0, 0, 0, nullptr, nullptr, 0, nullptr},')
            out.write('\n};\n')
            out.write(f'extern const uint32_t hk573_part_{f}_n = {len(part)};\n')
        os.replace(os.path.join(a.outdir, f'blocks_{f}.cpp.tmp'), os.path.join(a.outdir, f'blocks_{f}.cpp'))
    with open(os.path.join(a.outdir, 'table.cpp.tmp'), 'w') as out:
        out.write(header)
        for f in range(a.files):
            out.write(f'extern const hk573_block hk573_part_{f}[];\nextern const uint32_t hk573_part_{f}_n;\n')
        out.write('extern const hk573_block *const hk573_parts[] = {' +
                  ', '.join(f'hk573_part_{f}' for f in range(a.files)) + '};\n')
        out.write('extern const uint32_t hk573_part_sizes[] = {' +
                  ', '.join(str(len(p)) for p in parts) + '};\n')
        out.write(f'extern const uint32_t hk573_nparts = {a.files};\n')
    os.replace(os.path.join(a.outdir, 'table.cpp.tmp'), os.path.join(a.outdir, 'table.cpp'))
    with open(stamp, 'w') as out:
        out.write(key)
    print(f'mipsgen: {total} blocks in {a.files} files, {looping} of them loops over several blocks')


if __name__ == '__main__':
    main()
