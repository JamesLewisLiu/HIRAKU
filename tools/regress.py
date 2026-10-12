#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bit-exactness and speed checks for the runtime.

  tools/regress.py record [CASE...]   run the cases, keep their digests as the baseline
  tools/regress.py check  [CASE...]   run again, compare with the baseline (frames, sound, run statistics)
  tools/regress.py bench  [CASE...]   time each case (no digest), one at a time

A case is a game, a frame count and optional runtime arguments. Each run writes a
per-frame digest (--digest): a hash of the picture and a running hash of the
sound. A change that is meant to be invisible (a faster renderer, idle skipping,
cheaper fibers) must leave every line identical. Baselines live in
out/regress/baseline/ (out/ is never committed: the digests derive from the game).
"""
import concurrent.futures as cf
import os
import re
import resource
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "cmake")
OUT = os.path.join(ROOT, "out", "regress")

# name: (game, frames, extra arguments)
CASES = {
    "bm3final-practice": ("bm3final", 12000, ["--script", "oracle/scripts/bm3final-practice.his"]),
    "popn7-attract": ("popn7", 3600, []),
    "ppp-attract": ("ppp", 3600, []),
    "kbm-attract": ("kbm", 3600, []),
    # DJ Main: power-on, self-test, disk load, attract (in lockstep with MAME, PLAN.md §17)
    "bm1stmix-boot": ("bm1stmix", 9000, ["--script", "oracle/scripts/bm1stmix-boot.his"]),
    "popn1j-boot": ("popn1j", 9000, ["--script", "oracle/scripts/popn1j-boot.his"]),
    "bm1stmix-play": ("bm1stmix", 12600, ["--script", "oracle/scripts/bm1stmix-play.his"]),
    "popn1j-play": ("popn1j", 13500, ["--script", "oracle/scripts/popn1j-play.his"]),
    # System 573: power-on, BIOS, CD boot, the game's checks, ERROR 147 (no install yet);
    # in lockstep with MAME at the pin (PLAN.md section 19). The clock is fixed.
    "ddr3mj-boot": ("ddr3mj", 3600, ["--rtc", "1791804639", "--script", "oracle/scripts/ddr3mj-boot.his"]),
    # the analog board: first boot, the flash install (two Test presses), attract with CD audio
    "ddr2m-boot": ("ddr2m", 24000, ["--rtc", "1791828936", "--script", "oracle/scripts/ddr2m-boot.his"]),
    # ... and a game: coins, mode and song select, a song stepped through to CLEARED, stage 2
    "ddr2m-play": ("ddr2m", 21000, ["--rtc", "1791837575", "--script", "oracle/scripts/ddr2m-play.his"]),
}

# report lines that describe the host, not the machine
HOST_LINES = ("profile:", "audio:", "nvram:")
HOST_TIME = re.compile(r" in [0-9.]+ s of host time")   # kbm's synth line


def run(case, digest_path):
    game, frames, extra = CASES[case]
    exe = os.path.join(BIN, "hiraku-" + game)
    work = os.path.join(OUT, "work", case)
    os.makedirs(work, exist_ok=True)
    cmd = [exe, "--frames", str(frames), "--out", work] + extra
    if digest_path:
        cmd += ["--digest", digest_path]
    t0 = time.monotonic()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    wall = time.monotonic() - t0
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    stats = [HOST_TIME.sub("", l) for l in p.stdout.splitlines()
             if not l.startswith(HOST_LINES) and not l.startswith("frametime")]
    return p.returncode, stats, wall, cpu, p.stdout + p.stderr


def cases_from(argv):
    names = argv or list(CASES)
    for n in names:
        if n not in CASES:
            sys.exit(f"unknown case {n}; known: {', '.join(CASES)}")
    return names


def record_or_check(names, record):
    base = os.path.join(OUT, "baseline")
    cur = os.path.join(OUT, "current")
    os.makedirs(base, exist_ok=True)
    os.makedirs(cur, exist_ok=True)

    def one(case):
        d = os.path.join(base if record else cur, case + ".digest")
        rc, stats, wall, cpu, log = run(case, d)
        with open(d.replace(".digest", ".stats"), "w") as f:
            f.write("\n".join(stats) + "\n")
        return case, rc, wall, log

    bad = 0
    with cf.ThreadPoolExecutor(max_workers=min(len(names), os.cpu_count() or 4)) as ex:
        for case, rc, wall, log in ex.map(one, names):
            if rc != 0:
                print(f"{case}: exit {rc}\n{log[-2000:]}")
                bad += 1
                continue
            if record:
                print(f"{case}: recorded ({wall:.1f} s)")
                continue
            bad += compare(case, base, cur)
    if not record:
        print("all identical" if bad == 0 else f"{bad} case(s) differ")
    return 1 if bad else 0


def compare(case, base, cur):
    try:
        a = open(os.path.join(base, case + ".digest")).read().splitlines()
        sa = open(os.path.join(base, case + ".stats")).read()
    except FileNotFoundError:
        print(f"{case}: no baseline (run: tools/regress.py record {case})")
        return 1
    b = open(os.path.join(cur, case + ".digest")).read().splitlines()
    sb = open(os.path.join(cur, case + ".stats")).read()
    for k in range(min(len(a), len(b))):
        if a[k] != b[k]:
            fa, pa, aa = a[k].split()
            _, pb, ab = b[k].split()
            what = " and ".join(w for w, d in (("picture", pa != pb), ("sound", aa != ab)) if d)
            print(f"{case}: DIFFERS from frame {fa} ({what})")
            return 1
    if len(a) != len(b):
        print(f"{case}: DIFFERS: {len(a)} frames in the baseline, {len(b)} now")
        return 1
    if sa != sb:
        print(f"{case}: frames and sound identical, run statistics DIFFER:")
        for la, lb in zip(sa.splitlines(), sb.splitlines()):
            if la != lb:
                print(f"  was: {la}\n  now: {lb}")
        return 1
    print(f"{case}: identical ({len(a)} frames)")
    return 0


def bench(names):
    print(f"{'case':20} {'frames':>6} {'cpu s':>7} {'x real':>7} {'core %':>7}")
    for case in names:
        game, frames, _ = CASES[case]
        rc, stats, wall, cpu, log = run(case, None)
        if rc != 0:
            print(f"{case}: exit {rc}\n{log[-2000:]}")
            continue
        vt = next((float(l.split("virtual time ")[1].split(" s")[0]) for l in stats if "virtual time " in l), frames / 59.94)
        print(f"{case:20} {frames:6} {cpu:7.2f} {vt / cpu:7.2f} {100 * cpu / vt:6.1f}%")
        for l in log.splitlines():
            if l.startswith("frametime"):
                print("   ", l)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("record", "check", "bench"):
        sys.exit(__doc__)
    names = cases_from(sys.argv[2:])
    if sys.argv[1] == "bench":
        bench(names)
        return 0
    return record_or_check(names, sys.argv[1] == "record")


if __name__ == "__main__":
    sys.exit(main())
