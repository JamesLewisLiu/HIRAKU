#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare two oracle runs (MAME vs MAME, or MAME vs the HIRAKU runtime).

    oracle/compare.py RUN_A RUN_B [--pc] [--time] [--context 5]

Checks, in order, and reports the FIRST divergence of each:
  frames.txt  per-frame watches and RAM hashes
  mmio.bin    the folded MMIO stream (kind, group, addr, mask, data, count, frame)
PCs and emulated times are ignored unless --pc / --time are given: MAME's
recompiler reports approximate PCs unless the run used EXACT_PC=1, and the
runtime is not cycle-timed.
RTC read DATA is ignored unless --strict-rtc: MAME seeds the RTC65271 from the
host clock, so two otherwise identical MAME runs differ only there (the runtime
replays recorded RTC values when compared against a trace).
Exit status 0 = identical, 1 = diverged.
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace as T  # noqa: E402


def frames_table(run: str) -> tuple[list[str], dict[int, list[str]]]:
    rows: dict[int, list[str]] = {}
    header: list[str] = []
    with open(os.path.join(run, "frames.txt")) as fh:
        for line in fh:
            if line.startswith("#"):
                header = line[1:].split()
                continue
            p = line.split()
            rows[int(p[0])] = p
    return header, rows


def compare_frames(a: str, b: str) -> bool:
    ha, ra = frames_table(a)
    hb, rb = frames_table(b)
    if ha != hb:
        print(f"frames: different columns\n  A {ha}\n  B {hb}")
        return False
    common = sorted(set(ra) & set(rb))
    for f in common:
        # column 1 is emulated time - skip
        da = [ra[f][i] for i in range(len(ha)) if i != 1]
        db = [rb[f][i] for i in range(len(hb)) if i != 1]
        if da != db:
            diff = [ha[i] for i in range(len(ha)) if i != 1 and ra[f][i] != rb[f][i]]
            print(f"frames: first divergence at frame {f} in {', '.join(diff)}")
            print(f"  A {' '.join(ra[f])}\n  B {' '.join(rb[f])}")
            return False
    print(f"frames: {len(common)} common frames identical"
          + ("" if len(ra) == len(rb) else f" (lengths {len(ra)} / {len(rb)})"))
    return True


def compare_mmio(a: str, b: str, use_pc: bool, use_time: bool, strict_rtc: bool, context: int) -> bool:
    ta, tb = T.load(a), T.load(b)
    n = min(len(ta), len(tb))
    fields = ["frame", "kind", "group", "addr", "mask", "data", "count"]
    if use_pc:
        fields.append("pc")
    if use_time:
        fields.append("time")
    step = 1 << 22
    first = None
    ignored = 0
    for lo in range(0, n, step):
        hi = min(n, lo + step)
        m = np.zeros(hi - lo, dtype=bool)
        for f in fields:
            d = ta[f][lo:hi] != tb[f][lo:hi]
            if f == "data" and not strict_rtc:
                volatile = (ta["group"][lo:hi] == 6) & ((ta["kind"][lo:hi] & 1) == 0)
                ignored += int((d & volatile).sum())
                d &= ~volatile
            m |= d
        if m.any():
            first = lo + int(np.argmax(m))
            break
    if first is None:
        note = f", {ignored} RTC reads differ (host clock; --strict-rtc)" if ignored else ""
        if len(ta) == len(tb):
            print(f"mmio:   {n} records identical ({int(T.weight(ta).sum())} accesses){note}")
            return True
        print(f"mmio:   identical for {n} records, then lengths differ ({len(ta)} / {len(tb)})")
        return False
    r = ta[first]
    diff = [f for f in fields if ta[f][first] != tb[f][first]]
    print(f"mmio:   first divergence at record {first} (frame {r['frame']}), fields: {', '.join(diff)}")
    lo, hi = max(0, first - context), min(n, first + context + 1)
    for name, t in (("A", ta), ("B", tb)):
        print(f"  --- {name}")
        for i in range(lo, hi):
            x = t[i]
            k = int(x["kind"])
            mark = ">>" if i == first else "  "
            print(f"  {mark} #{i:<9} f{x['frame']:<6} pc {x['pc']:08x} {'RW'[k & 1]} "
                  f"{T.GROUPS.get(int(x['group']), x['group']):5s} {x['addr']:08x}"
                  f" {'#' if k & 2 else '='} {x['data']:08x} & {x['mask']:08x} x{x['count']}")
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--pc", action="store_true", help="also compare PCs (both runs EXACT_PC)")
    ap.add_argument("--time", action="store_true", help="also compare emulated times")
    ap.add_argument("--strict-rtc", action="store_true", help="also compare RTC read data")
    ap.add_argument("--context", type=int, default=5)
    args = ap.parse_args()
    ok = compare_frames(args.a, args.b)
    ok = compare_mmio(args.a, args.b, args.pc, args.time, args.strict_rtc, args.context) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
