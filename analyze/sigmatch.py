#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Match functions across Firebeat programs by position-independent signature.

    analyze/sigmatch.py REF OTHER [OTHER...] [--range LO:HI] [--list]

REF/OTHER are game ids with dumps/<id>/program.bin and build/<id>/manifest.json.
A function's signature is its instruction words with everything that moves
between builds masked out:
  - branch displacements (b/bl/bc);
  - 16-bit immediates and displacements of D-form instructions (addresses
    of globals, stack offsets, constants).
What stays is the opcode/register skeleton plus the length. Only signatures
that are UNIQUE within each image count as a match, so a hit is a real
correspondence and labels can be carried over.

--range limits REF to functions whose entry is in [LO, HI), e.g. the kernel.
--list prints every match as `ref_addr -> other_addr`.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# D-form primary opcodes whose low 16 bits are an immediate or displacement
DFORM = {7, 8, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29,
         32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47}


def mask(w: int) -> int:
    op = w >> 26
    if op == 18:            # b / bl: keep AA/LK
        return w & 0xFC000003
    if op == 16:            # bc: keep BO/BI/AA/LK
        return w & 0xFFFF0003
    if op in DFORM:
        return w & 0xFFFF0000
    return w


def load(gid: str) -> tuple[bytes, dict]:
    with open(os.path.join(ROOT, "dumps", gid, "program.bin"), "rb") as fh:
        img = fh.read()
    with open(os.path.join(ROOT, "build", gid, "manifest.json")) as fh:
        man = json.load(fh)
    return img, man


def signatures(gid: str) -> dict[int, tuple[str, int]]:
    img, man = load(gid)
    base = int(man["base"], 16)
    out = {}
    for f in man["functions"]:
        words = []
        for lo, hi in f["ranges"]:
            a, b = int(lo, 16) - base, int(hi, 16) - base
            words += [mask(w) for w in struct.unpack(f">{(b - a) // 4}I", img[a:b - (b - a) % 4])]
        if len(words) < 6:        # too short to be distinctive
            continue
        h = hashlib.sha1(struct.pack(f">{len(words)}I", *words)).hexdigest()
        out[int(f["entry"], 16)] = (h, len(words))
    return out


def unique(sigs: dict[int, tuple[str, int]]) -> dict[str, int]:
    c = collections.Counter(h for h, _ in sigs.values())
    return {h: a for a, (h, _) in sigs.items() if c[h] == 1}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref")
    ap.add_argument("others", nargs="+")
    ap.add_argument("--range")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    ref = signatures(a.ref)
    if a.range:
        lo, hi = (int(x, 16) for x in a.range.split(":"))
        ref = {k: v for k, v in ref.items() if lo <= k < hi}
    ref_u = unique(signatures(a.ref))
    ref_u = {h: addr for h, addr in ref_u.items() if addr in ref}
    words_total = sum(n for _, n in ref.values())
    print(f"{a.ref}: {len(ref)} functions (>=6 insns) in range, {len(ref_u)} with a unique signature")
    for o in a.others:
        oth_u = unique(signatures(o))
        hits = {ref_u[h]: oth_u[h] for h in ref_u if h in oth_u}
        w = sum(ref[r][1] for r in hits)
        print(f"  {o:10s} {len(hits):5d} matched ({100 * len(hits) / max(1, len(ref)):5.1f}% of functions,"
              f" {100 * w / max(1, words_total):5.1f}% of instructions)")
        if a.list:
            for r in sorted(hits):
                print(f"    {r:08x} -> {hits[r]:08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
