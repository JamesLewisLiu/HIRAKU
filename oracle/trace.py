#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read HIRAKU oracle traces (mmio.bin) written by oracle/lua/hiraku.lua.

    oracle/trace.py summary RUN_DIR [--frames A:B]
    oracle/trace.py dump    RUN_DIR [--frames A:B] [--group gcu] [--limit N]

Record layout, little-endian, 32 bytes:
    u32 frame, f64 time (emulated seconds), u32 pc, u32 addr, u32 data,
    u32 mask, u8 kind (0 read, 1 write), u8 group, u16 count
`count` is how many consecutive accesses with the same (kind, group, addr,
mask) the record stands for. kind bit 1 set means their data differed and
`data` is the FNV-1a hash (bytes LSB first, per access) of the sequence;
otherwise `data` is the value every one of them carried. `pc` and `time` are
the first access's.
"""
from __future__ import annotations

import argparse
import collections
import os
import sys

import numpy as np

REC = np.dtype([("frame", "<u4"), ("time", "<f8"), ("pc", "<u4"), ("addr", "<u4"),
                ("data", "<u4"), ("mask", "<u4"), ("kind", "u1"), ("group", "u1"),
                ("count", "<u2")])
GROUPS = {1: "gcu", 2: "dpram", 3: "ata", 4: "io", 5: "uart", 6: "rtc", 7: "ext", 8: "flash"}
GROUP_IDS = {v: k for k, v in GROUPS.items()}


def load(run: str, frames: str | None = None) -> np.ndarray:
    t = np.memmap(os.path.join(run, "mmio.bin"), dtype=REC, mode="r")
    if frames:
        a, _, b = frames.partition(":")
        lo = np.searchsorted(t["frame"], int(a or 0), "left")
        hi = np.searchsorted(t["frame"], int(b), "right") if b else len(t)
        t = t[lo:hi]
    return t


def weight(t: np.ndarray) -> np.ndarray:
    return np.maximum(t["count"].astype(np.int64), 1)


def summary(t: np.ndarray, top: int) -> None:
    w = weight(t)
    n = int(w.sum())
    if not len(t):
        print("empty trace")
        return
    print(f"{len(t)} records = {n} accesses, frames {t['frame'][0]}..{t['frame'][-1]}")
    print("\nby group/kind:")
    for g in sorted(set(t["group"].tolist())):
        for k, kn in ((0, "R"), (1, "W")):
            m = (t["group"] == g) & ((t["kind"] & 1) == k)
            if m.any():
                print(f"  {GROUPS.get(g, g):6s} {kn}  {int(w[m].sum()):>12}")
    print(f"\ntop {top} (kind, addr, pc) by accesses:")
    keys = ((t["kind"] & 1).astype(np.uint64) << np.uint64(63)) | (t["addr"].astype(np.uint64) << np.uint64(32)) | t["pc"].astype(np.uint64)
    u, inv = np.unique(keys, return_inverse=True)
    tot = np.bincount(inv, weights=w)
    for i in np.argsort(-tot)[:top]:
        k = int(u[i])
        print(f"  {'RW'[k >> 63]} {(k >> 32) & 0x7fffffff | 0x0:08x} pc {k & 0xffffffff:08x}  {int(tot[i]):>10}")


def dump(t: np.ndarray, group: str | None, limit: int) -> None:
    if group:
        t = t[t["group"] == GROUP_IDS[group]]
    for r in t[:limit]:
        c = max(int(r["count"]), 1)
        k = int(r["kind"])
        print(f"f{r['frame']:<6} {r['time']:11.6f} pc {r['pc']:08x} {'RW'[k & 1]} "
              f"{GROUPS.get(int(r['group']), r['group']):5s} {r['addr']:08x} {'#' if k & 2 else '='} {r['data']:08x}"
              f" & {r['mask']:08x}" + (f" x{c}" if c > 1 else ""))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("summary", "dump"))
    ap.add_argument("run")
    ap.add_argument("--frames")
    ap.add_argument("--group", choices=sorted(GROUP_IDS))
    ap.add_argument("--limit", type=int, default=200)
    ap.add_argument("--top", type=int, default=25)
    a = ap.parse_args()
    t = load(a.run, a.frames)
    if a.cmd == "summary":
        summary(t, a.top)
    else:
        dump(t, a.group, a.limit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
