#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate PowerPC instruction test cases and their reference results.

    tests/ppc/gen.py OUT_DIR [--per-op N] [--seed S]

Writes OUT_DIR/cases.bin: one 512-byte record per case (layout below), with
the OUTPUT half filled in by running every case on a real PowerPC
implementation: QEMU user mode (qemu-ppc), via a freestanding ELF built here
with clang/ld.lld. tests/ppc/test_ppc.cpp replays the inputs through HIRAKU's
semantics and compares.

Record (big-endian words), at RECORDS + 512*i:
    +0    r0..r31 in       +128 cr, xer, ctr, lr in     +144 instruction word
    +160  64-byte memory at SCRATCH, before
    +256  r0..r31 out      +384 cr, xer, ctr, lr out
    +416  64-byte memory at SCRATCH, after
r31 always holds the record's own address (the harness's anchor), so no test
writes r31 except through memory that carries that same value.
"""
from __future__ import annotations

import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

TEXT = 0x01000000
RECORDS = 0x01200000
SCRATCH = 0x01f00000
REC = 512

EDGE = [0, 1, 2, 0xffffffff, 0xfffffffe, 0x7fffffff, 0x80000000, 0x80000001, 0x0000ffff,
        0xffff0000, 0x00008000, 0xffff8000, 31, 32, 33, 63, 64]


def rnd32(r: random.Random) -> int:
    k = r.random()
    if k < 0.35:
        return r.choice(EDGE)
    if k < 0.5:
        return r.randrange(0, 64)
    return r.getrandbits(32)


def D(op, rt, ra, imm):            return (op << 26) | (rt << 21) | (ra << 16) | (imm & 0xffff)
def X(op, rt, ra, rb, xo, rc=0):   return (op << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1) | rc
def XO(rt, ra, rb, oe, xo, rc):    return (31 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (oe << 10) | (xo << 1) | rc
def M(op, rs, ra, sh, mb, me, rc): return (op << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1) | rc


class Gen:
    def __init__(self, seed: int):
        self.r = random.Random(seed)

    def reg(self, avoid=()):        # destination/source register, never r31
        while True:
            x = self.r.randrange(0, 31)
            if x not in avoid:
                return x

    # ---- one instruction word per call, plus input tweaks -------------------
    def forms(self):
        r = self.r
        g = self
        rc = lambda: r.randrange(2)
        imm = lambda: r.choice([0, 1, -1, 0x7fff, -0x8000, r.getrandbits(16)])
        f = []
        for name, xo in [("add", 266), ("addc", 10), ("adde", 138), ("subf", 40), ("subfc", 8),
                         ("subfe", 136), ("mullw", 235), ("divwu", 459), ("divw", 491)]:
            f.append((name, lambda xo=xo: XO(g.reg(), g.reg(), g.reg(), r.randrange(2), xo, rc())))
        for name, xo in [("addme", 234), ("addze", 202), ("subfme", 232), ("subfze", 200), ("neg", 104)]:
            f.append((name, lambda xo=xo: XO(g.reg(), g.reg(), 0, r.randrange(2), xo, rc())))
        for name, xo in [("mulhw", 75), ("mulhwu", 11)]:
            f.append((name, lambda xo=xo: XO(g.reg(), g.reg(), g.reg(), 0, xo, rc())))
        for name, op in [("addi", 14), ("addis", 15), ("addic", 12), ("addic.", 13), ("subfic", 8), ("mulli", 7)]:
            f.append((name, lambda op=op: D(op, g.reg(), g.reg(), imm())))
        for name, op in [("ori", 24), ("oris", 25), ("xori", 26), ("xoris", 27), ("andi.", 28), ("andis.", 29)]:
            f.append((name, lambda op=op: D(op, g.reg(), g.reg(), imm())))
        for name, xo in [("and", 28), ("andc", 60), ("or", 444), ("orc", 412), ("xor", 316), ("nand", 476),
                         ("nor", 124), ("eqv", 284), ("slw", 24), ("srw", 536), ("sraw", 792)]:
            f.append((name, lambda xo=xo: X(31, g.reg(), g.reg(), g.reg(), xo, rc())))
        for name, xo in [("extsb", 954), ("extsh", 922), ("cntlzw", 26)]:
            f.append((name, lambda xo=xo: X(31, g.reg(), g.reg(), 0, xo, rc())))
        f.append(("srawi", lambda: X(31, g.reg(), g.reg(), r.randrange(32), 824, rc())))
        f.append(("rlwinm", lambda: M(21, g.reg(), g.reg(), r.randrange(32), r.randrange(32), r.randrange(32), rc())))
        f.append(("rlwimi", lambda: M(20, g.reg(), g.reg(), r.randrange(32), r.randrange(32), r.randrange(32), rc())))
        f.append(("rlwnm", lambda: M(23, g.reg(), g.reg(), g.reg(), r.randrange(32), r.randrange(32), rc())))
        f.append(("cmp", lambda: X(31, r.randrange(8) << 2, g.reg(), g.reg(), 0)))
        f.append(("cmpl", lambda: X(31, r.randrange(8) << 2, g.reg(), g.reg(), 32)))
        f.append(("cmpi", lambda: D(11, r.randrange(8) << 2, g.reg(), imm())))
        f.append(("cmpli", lambda: D(10, r.randrange(8) << 2, g.reg(), imm())))
        for name, xo in [("crand", 257), ("crandc", 129), ("creqv", 289), ("crnand", 225), ("crnor", 33),
                         ("cror", 449), ("crorc", 417), ("crxor", 193)]:
            f.append((name, lambda xo=xo: X(19, r.randrange(32), r.randrange(32), r.randrange(32), xo)))
        f.append(("mcrf", lambda: X(19, r.randrange(8) << 2, r.randrange(8) << 2, 0, 0)))
        f.append(("mcrxr", lambda: X(31, r.randrange(8) << 2, 0, 0, 512)))
        f.append(("mfcr", lambda: X(31, g.reg(), 0, 0, 19)))
        f.append(("mtcrf", lambda: (31 << 26) | (g.reg() << 21) | (r.getrandbits(8) << 12) | (144 << 1)))
        for spr in (1, 8, 9):   # XER, LR, CTR
            sprf = ((spr & 31) << 16) | ((spr >> 5) << 11)
            f.append((f"mfspr{spr}", lambda sprf=sprf: (31 << 26) | (g.reg() << 21) | sprf | (339 << 1)))
            f.append((f"mtspr{spr}", lambda sprf=sprf: (31 << 26) | (g.reg() << 21) | sprf | (467 << 1)))
        # memory: rA is set up by mem() to point into SCRATCH
        for name, op in [("lbz", 34), ("lhz", 40), ("lha", 42), ("lwz", 32), ("stb", 38), ("sth", 44), ("stw", 36),
                         ("lbzu", 35), ("lhzu", 41), ("lhau", 43), ("lwzu", 33), ("stbu", 39), ("sthu", 45), ("stwu", 37)]:
            f.append((name, lambda op=op: ("memD", op)))
        for name, xo in [("lbzx", 87), ("lhzx", 279), ("lhax", 343), ("lwzx", 23), ("stbx", 215), ("sthx", 407),
                         ("stwx", 151), ("lbzux", 119), ("lhzux", 311), ("lhaux", 375), ("lwzux", 55), ("stbux", 247),
                         ("sthux", 439), ("stwux", 183), ("lhbrx", 790), ("lwbrx", 534), ("sthbrx", 918),
                         ("stwbrx", 662), ("lwarx", 20), ("stwcx.", 150)]:
            f.append((name, lambda xo=xo: ("memX", xo)))
        f.append(("lmw", lambda: ("lmw",)))
        f.append(("stmw", lambda: ("stmw",)))
        f.append(("lswi", lambda: ("lswi",)))
        f.append(("stswi", lambda: ("stswi",)))
        f.append(("lswx", lambda: ("lswx",)))
        f.append(("stswx", lambda: ("stswx",)))
        return f

    def case(self, idx: int, form) -> bytes:
        r = self.r
        regs = [rnd32(r) for _ in range(32)]
        rec_addr = RECORDS + REC * idx
        regs[31] = rec_addr
        cr, xer = r.getrandbits(32), (r.getrandbits(3) << 29) | r.randrange(0, 33)
        ctr, lr = rnd32(r), rnd32(r) & ~3
        mem = bytearray(r.getrandbits(8) for _ in range(64))
        w = form()

        def base(ra, off_max=48):          # point rA into SCRATCH
            regs[ra] = SCRATCH + r.randrange(0, off_max)

        if isinstance(w, tuple):
            kind = w[0]
            if kind == "memD":
                op = w[1]
                rt, ra = self.reg(), self.reg(avoid=(0,))
                if op in (33, 35, 41, 43) and ra == rt:   # load with update: rA != rT
                    ra = self.reg(avoid=(0, rt))
                base(ra, 24)
                w = D(op, rt, ra, r.randrange(0, 24))
            elif kind == "memX":
                xo = w[1]
                rt, ra, rb = self.reg(), self.reg(avoid=(0,)), self.reg()
                if xo in (119, 311, 375, 55) and ra == rt:
                    ra = self.reg(avoid=(0, rt))
                rb = self.reg(avoid=(ra,))
                base(ra, 24)
                regs[rb] = r.randrange(0, 24)
                if xo in (20, 150):                        # lwarx/stwcx.: aligned
                    regs[ra] &= ~3
                    regs[rb] &= ~3
                if xo == 150:
                    xer &= ~0x80000000     # keep SO out of CR0 ambiguity
                w = X(31, rt, ra, rb, xo, 1 if xo == 150 else 0)
            elif kind in ("lmw", "stmw"):
                rt = r.randrange(20, 32)
                ra = self.reg(avoid=(0,) + tuple(range(rt, 32)))
                base(ra, 64 - 4 * (32 - rt) + 1)
                off = regs[ra] - SCRATCH
                if kind == "lmw":   # the word loaded into r31 must be the anchor
                    p = off + 4 * (31 - rt)
                    mem[p:p + 4] = struct.pack(">I", rec_addr)
                w = D(46 if kind == "lmw" else 47, rt, ra, 0)
            elif kind in ("lswi", "stswi", "lswx", "stswx"):
                nb = r.randrange(1, 33)
                nregs = (nb + 3) // 4
                rt = r.randrange(0, 31 - nregs + 1)          # never reaches r31, never wraps
                ra = self.reg(avoid=tuple(range(rt, rt + nregs)) + (0,))
                base(ra, 64 - nb + 1)
                if kind in ("lswi", "stswi"):
                    w = X(31, rt, ra, nb % 32, 597 if kind == "lswi" else 725)
                else:
                    rb = self.reg(avoid=tuple(range(rt, rt + nregs)) + (ra,))
                    regs[rb] = 0
                    xer = (xer & ~0x7f) | nb
                    w = X(31, rt, ra, rb, 533 if kind == "lswx" else 661)
        # divw/divwu by zero and INT_MIN / -1 are architecturally undefined: QEMU's
        # answer is not the 403's. Those are checked against MAME instead.
        if (w >> 26) == 31 and ((w >> 1) & 0x1ff) in (459, 491):
            ra_, rb_ = (w >> 16) & 31, (w >> 11) & 31
            while regs[rb_] == 0 or (regs[ra_] == 0x80000000 and regs[rb_] == 0xffffffff):
                regs[rb_] = rnd32(r)
                if ra_ == rb_:
                    regs[ra_] = regs[rb_]
        # never let a test write r31
        rt = (w >> 21) & 31
        assert not (rt == 31 and (w >> 26) not in (36, 37, 38, 39, 44, 45, 46, 47, 10, 11, 19)), hex(w)
        rec = bytearray(REC)
        struct.pack_into(">32I", rec, 0, *regs)
        struct.pack_into(">5I", rec, 128, cr, xer, ctr, lr, w)
        rec[160:224] = mem
        return bytes(rec)


def build_harness(cases: list[bytes], workdir: str) -> bytes:
    """Return the records with outputs filled in, by running them on qemu-ppc.

    One 64-byte stub per case, reached by `bctr` with LR/CR/XER already set
    from the record: load CTR and r0..r31, run the instruction, store
    everything to the record's output half, return through the address the
    loop left at record+508.
    """
    stubs = []
    for i, rec in enumerate(cases):
        insn = struct.unpack_from(">I", rec, 144)[0]
        stubs.append(f"""
stub{i}:
    lwz 0, 136(31)
    mtctr 0
    lmw 0, 0(31)
    .long 0x{insn:08x}
    stmw 0, 256(31)
    mfcr 0
    stw 0, 384(31)
    mfxer 0
    stw 0, 388(31)
    mfctr 0
    stw 0, 392(31)
    mflr 0
    stw 0, 396(31)
    lwz 0, 508(31)            # back to the loop
    mtlr 0
    blr
    .balign 64
""")
    asm = HARNESS_SIMPLE.format(records=RECORDS, scratch=SCRATCH, count=len(cases), bytes=len(cases) * REC, rec=REC)
    asm += "    .section .text\n    .balign 64\nstubs:\n" + "".join(stubs)
    asm += f"""
    .section .records, "aw"
records_data:
"""
    for rec in cases:
        asm += "    .byte " + ",".join(str(b) for b in rec) + "\n"
    asm += f"""
    .section .scratch, "aw"
    .space 4096
"""
    s = os.path.join(workdir, "h.s")
    o = os.path.join(workdir, "h.o")
    e = os.path.join(workdir, "h.elf")
    ld = os.path.join(workdir, "h.ld")
    with open(s, "w") as fh:
        fh.write(asm)
    with open(ld, "w") as fh:
        fh.write(f"""ENTRY(_start)
SECTIONS {{
  . = 0x{TEXT:08x}; .text : {{ *(.text) }}
  . = 0x{RECORDS:08x}; .records : {{ *(.records) }}
  . = 0x{SCRATCH:08x}; .scratch : {{ *(.scratch) }}
}}
""")
    subprocess.run(["clang", "--target=powerpc-unknown-linux-gnu", "-c", "-o", o, s], check=True)
    subprocess.run(["ld.lld", "-static", "-T", ld, "-o", e, o], check=True)
    out = subprocess.run(["qemu-ppc", e], check=True, capture_output=True).stdout
    if len(out) != len(cases) * REC:
        raise SystemExit(f"qemu wrote {len(out)} bytes, expected {len(cases) * REC}")
    return out


# The loop: copy the case's memory into SCRATCH, leave the return point in
# rec+508, set CR/XER/LR from the record, branch to the stub (CTR is free for
# that: the stub restores the case's CTR itself), copy SCRATCH back out.
HARNESS_SIMPLE = r"""
    .section .text
    .globl _start
_start:
    lis 30, {records}@ha
    addi 30, 30, {records}@l
    lis 29, {count}@ha
    addi 29, 29, {count}@l
    lis 28, stubs@ha
    addi 28, 28, stubs@l
    b 2f
1:  lis 3, {scratch}@ha
    addi 3, 3, {scratch}@l
    li 4, 16
    mtctr 4
    addi 5, 30, 156
3:  lwzu 6, 4(5)
    stw 6, 0(3)
    addi 3, 3, 4
    bdnz 3b
    bl 7f                    # find our own address, to leave the return point in rec+508
7:  mflr 3
    addi 3, 3, 9f - 7b
    stw 3, 508(30)           # stub returns here via lwz/mtlr/blr
    lwz 0, 128(30)
    mtcrf 0xff, 0
    lwz 0, 132(30)
    mtxer 0
    lwz 0, 140(30)
    mtlr 0
    mtctr 28
    mr 31, 30
    bctr
9:  # the stub clobbered every register but r31 (the record): rebuild the loop state
    mr 30, 31
    lis 3, {records}@ha
    addi 3, 3, {records}@l
    subf 3, 3, 30
    srwi 4, 3, 3             # index * 64
    lis 28, stubs@ha
    addi 28, 28, stubs@l
    add 28, 28, 4
    srwi 4, 3, 9             # index
    lis 29, {count}@ha
    addi 29, 29, {count}@l
    subf 29, 4, 29
    lis 3, {scratch}@ha
    addi 3, 3, {scratch}@l
    li 4, 16
    mtctr 4
    addi 5, 30, 412
6:  lwz 6, 0(3)
    stwu 6, 4(5)
    addi 3, 3, 4
    bdnz 6b
    addi 30, 30, {rec}
    addi 28, 28, 64
    addi 29, 29, -1
2:  cmpwi 29, 0
    bne 1b
    li 0, 4
    li 3, 1
    lis 4, {records}@ha
    addi 4, 4, {records}@l
    lis 5, {bytes}@ha
    addi 5, 5, {bytes}@l
    sc
    li 0, 1
    li 3, 0
    sc
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--per-op", type=int, default=200)
    ap.add_argument("--seed", type=int, default=403)
    a = ap.parse_args()
    g = Gen(a.seed)
    cases, names = [], []
    for name, form in g.forms():
        for _ in range(a.per_op):
            cases.append(g.case(len(cases), form))
            names.append(name)
    keep = os.environ.get("HK_GEN_KEEP")   # keep the harness for debugging
    if keep:
        os.makedirs(keep, exist_ok=True)
        out = build_harness(cases, keep)
    else:
        with tempfile.TemporaryDirectory() as tmp:
            out = build_harness(cases, tmp)
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "cases.bin"), "wb") as fh:
        fh.write(out)
    with open(os.path.join(a.out, "names.txt"), "w") as fh:
        fh.write("\n".join(names) + "\n")
    print(f"{len(cases)} cases ({len(set(names))} forms) -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
