#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare a runtime trace with a MAME trace, aligned at a common starting point.

    oracle/align.py MAME_RUN OUR_RUN [--start-group uart] [--skip SPEC,...] [--context 8]

MAME's trace also contains the BIOS; HIRAKU starts at the game's entry. Both
streams are cut at the first access to --start-group (default: the first
UART access, which the game makes right after its kernel is up), records to
--skip-addr (default: the status LEDs, written in loops whose folding differs)
are dropped, and the rest is compared record by record (kind, group, address,
mask, data, count). Frames, PCs and times are not compared.

--skip takes ADDR[:MASK[:R|W]] items. The default drops the status LEDs and the
VBlank handler's accesses (GCU IRQ acknowledge, status high-half reads), whose
position in the stream depends on interrupt timing, not on program logic.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace as T  # noqa: E402


def prep(run, start_group, skip):
    import numpy as np
    t = T.load(run)
    gid = T.GROUP_IDS[start_group]
    idx = np.nonzero(t["group"] == gid)[0]
    if not len(idx):
        raise SystemExit(f"{run}: no {start_group} access")
    t = t[idx[0]:]
    drop = np.zeros(len(t), dtype=bool)
    for addr, mask, kind in skip:
        m = t["addr"] == addr
        if mask is not None:
            m &= t["mask"] == mask
        if kind is not None:
            m &= (t["kind"] & 1) == kind
        drop |= m
    return t[~drop]


def parse_skip(spec):
    out = []
    for item in spec.split(","):
        if not item:
            continue
        p = item.split(":")
        addr = int(p[0], 16)
        mask = int(p[1], 16) if len(p) > 1 and p[1] else None
        kind = {"R": 0, "W": 1}[p[2]] if len(p) > 2 else None
        out.append((addr, mask, kind))
    return out


def fmt(x):
    k = int(x["kind"])
    return (f"{'RW'[k & 1]} {T.GROUPS.get(int(x['group']), '?'):5s} {x['addr']:08x} {'#' if k & 2 else '='} "
            f"{x['data']:08x} & {x['mask']:08x} x{x['count']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mame")
    ap.add_argument("ours")
    ap.add_argument("--start-group", default="uart")
    ap.add_argument("--skip", default="7d000804,7e800010,7e800078:ffff0000:R")
    ap.add_argument("--context", type=int, default=8)
    ap.add_argument("--strict-rtc", action="store_true", help="also compare RTC read data")
    a = ap.parse_args()
    skip = parse_skip(a.skip)
    m, r = prep(a.mame, a.start_group, skip), prep(a.ours, a.start_group, skip)
    n = min(len(m), len(r))
    def key(x):
        # RTC read data is host-clock dependent in MAME: compare everything but the data
        if int(x["group"]) == 6 and not (int(x["kind"]) & 1) and not a.strict_rtc:
            return fmt(x).split(" = ")[0].split(" # ")[0] + f" & {x['mask']:08x} x{x['count']}"
        return fmt(x)

    for i in range(n):
        if key(m[i]) != key(r[i]):
            print(f"first difference at aligned record {i} (MAME frame {m[i]['frame']}, ours {r[i]['frame']})")
            for j in range(max(0, i - a.context), min(n, i + a.context + 1)):
                mark = ">>" if j == i else "  "
                print(f"{mark}{j:6d} MAME pc {m[j]['pc']:08x} {fmt(m[j]):58s}| ours pc {r[j]['pc']:08x} {fmt(r[j])}")
            return 1
    print(f"identical over {n} aligned records (MAME {len(m)}, ours {len(r)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
