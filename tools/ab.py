#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A/B timing of two builds of a game binary: alternating runs, medians.

    tools/ab.py A_BIN B_BIN [--runs N] [--a-args "..."] [--b-args "..."] -- ARGS...

Each run is `BIN --out <tmp> ARGS` (give --frames, and --script if wanted), memory-capped
with systemd-run. Runs alternate A, B, A, B... so that drift on a busy machine (other
jobs, clocks, caches) falls on both. Reported per binary: the median and spread of the
process's total CPU time (all threads: what a run costs the PC) and of wall time, and
B against A. A difference smaller than the spread is noise. A and B may be the same
binary with different --a-args/--b-args (a setting against another).
"""
import argparse
import os
import resource
import shutil
import statistics
import subprocess
import sys
import tempfile
import time


def run_once(binary, args, out):
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    r0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.monotonic()
    p = subprocess.run(["systemd-run", "--user", "--scope", "-q", "-p", "MemoryMax=8G", binary, "--out", out] + args,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    wall = time.monotonic() - t0
    r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    if p.returncode != 0:
        sys.exit(f"{binary} failed ({p.returncode}): {p.stderr[-500:]}")
    return (r1.ru_utime - r0.ru_utime) + (r1.ru_stime - r0.ru_stime), wall


def main():
    argv = sys.argv[1:]
    rest = []
    if "--" in argv:
        k = argv.index("--")
        argv, rest = argv[:k], argv[k + 1:]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--runs", type=int, default=5, help="runs per binary (default 5)")
    ap.add_argument("--a-args", default="", help="arguments for A only")
    ap.add_argument("--b-args", default="", help="arguments for B only")
    a = ap.parse_args(argv)
    work = tempfile.mkdtemp(prefix="hk-ab-")
    res = {"A": [], "B": []}
    try:
        for i in range(a.runs):
            for tag, b, extra in (("A", a.a, a.a_args), ("B", a.b, a.b_args)):
                cpu, wall = run_once(b, rest + extra.split(), os.path.join(work, "out"))
                res[tag].append((cpu, wall))
                print(f"run {i + 1} {tag}: cpu {cpu:6.2f} s  wall {wall:6.2f} s", flush=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    med = {}
    for name, tag in ((f"{a.a} {a.a_args}", "A"), (f"{a.b} {a.b_args}", "B")):
        cpu = [c for c, _ in res[tag]]
        wall = [w for _, w in res[tag]]
        med[tag] = statistics.median(cpu)
        print(f"{tag}: cpu median {statistics.median(cpu):6.2f} s (min {min(cpu):.2f}, max {max(cpu):.2f}); "
              f"wall median {statistics.median(wall):6.2f} s   {name}")
    print(f"B/A cpu: {med['B'] / med['A']:.3f} ({(med['B'] / med['A'] - 1) * 100:+.1f} %)")


if __name__ == "__main__":
    main()
