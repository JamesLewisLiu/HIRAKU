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
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get("HK_REGRESS_BIN") or os.path.join(ROOT, "build", "cmake")   # HK_REGRESS_BIN: another build
OUT = os.path.join(ROOT, "out", "regress")

# name: (game, frames, extra arguments[, after]). A case with an `after` starts from the NVRAM
# that case left (a power cycle between them: ddr3mj's install, then the game); the two run in
# that order.
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
    # ... and its first boot with the install cassette: the CD to the flash and the PC Card, INFO 506
    "ddr3mj-install": ("ddr3mj", 16200, ["--cassette", "install", "--rtc", "1791878546",
                                         "--script", "oracle/scripts/ddr3mj-install.his"]),
    # ... then with the game cassette, from what the install left: attract, with the MP3 music
    "ddr3mj-attract": ("ddr3mj", 9000, ["--rtc", "1791880031", "--script", "oracle/scripts/ddr3mj-attract.his"],
                       "ddr3mj-install"),
    # ... and a game from there: XANADU stepped through to its result, then stage 2 (with its MP3)
    "ddr3mj-play": ("ddr3mj", 16000, ["--rtc", "1791880871", "--script", "oracle/scripts/ddr3mj-play.his"],
                    "ddr3mj-install"),
    # DrumMania 10th Mix (digital board): the first boot installs the CD to flash (INFO 508); then from
    # what it left, NOTE 605 (Test), the boot checks, and "Please call attendant": the multi-session
    # unit, a stub in MAME, is as far as MAME goes
    "drmn10m-install": ("drmn10m", 8400, ["--rtc", "1791883090", "--script", "oracle/scripts/drmn10m-install.his"]),
    "drmn10m-boot": ("drmn10m", 10800, ["--rtc", "1791883869", "--script", "oracle/scripts/drmn10m-boot.his"],
                     "drmn10m-install"),
    "drmn10m-play": ("drmn10m", 13000, ["--rtc", "1791884700", "--script", "oracle/scripts/drmn10m-play.his"],
                     "drmn10m-boot"),
    "drmn10m-hdd": ("drmn10m", 70200, ["--rtc", "1791884700", "--cd", "hdd", "--script", "oracle/scripts/drmn10m-hdd.his"],
                    "drmn10m-boot"),
    "drmn10m-npu": ("drmn10m", 14400, ["--rtc", "1791889000", "--npu", "--script", "oracle/scripts/drmn10m-npu.his"],
                    "drmn10m-hdd"),
    # e-AMUSEMENT (PLAN.md section 19): turned ON in test mode; then a boot on the virtual LAN and the
    # server in the core, and test mode's NETWORK CHECK (CONNECTED)
    "drmn10m-eaon": ("drmn10m", 14860, ["--rtc", "1791890000", "--npu", "--script", "oracle/scripts/drmn10m-eaon.his"],
                     "drmn10m-npu"),
    "drmn10m-net": ("drmn10m", 15600, ["--rtc", "1791891000", "--npu", "--script", "oracle/scripts/drmn10m-net.his"],
                    "drmn10m-eaon"),
    # the card unit (step 3): a new card registered and a game played with it; then the card again
    "drmn10m-card": ("drmn10m", 24000, ["--rtc", "1791892000", "--npu", "--script", "oracle/scripts/drmn10m-card.his"],
                     "drmn10m-net"),
    "drmn10m-card2": ("drmn10m", 15400, ["--rtc", "1791893000", "--npu", "--script", "oracle/scripts/drmn10m-card2.his"],
                      "drmn10m-card"),
    "gtfrk11m-install": ("gtfrk11m", 28800, ["--rtc", "1791884700", "--script", "oracle/scripts/gtfrk11m-install.his"]),
    "gtfrk11m-boot": ("gtfrk11m", 10800, ["--rtc", "1791885700", "--script", "oracle/scripts/gtfrk11m-boot.his"],
                      "gtfrk11m-install"),
    # ... and a game from there: two coins, Start through the session WELCOME (alone), the mode and
    # music select, the first stage (ルージュの伝言) played with no input to GAME OVER, attract
    "gtfrk11m-play": ("gtfrk11m", 19800, ["--rtc", "1791886700", "--script", "oracle/scripts/gtfrk11m-play.his"],
                      "gtfrk11m-boot"),
    # ... its network unit: the data install, e-AMUSEMENT ON, and NETWORK CHECK (as drmn10m's)
    "gtfrk11m-hdd": ("gtfrk11m", 60000, ["--rtc", "1791892000", "--cd", "hdd", "--script", "oracle/scripts/gtfrk11m-hdd.his"],
                     "gtfrk11m-boot"),
    "gtfrk11m-eaon": ("gtfrk11m", 14310, ["--rtc", "1791893000", "--npu", "--script", "oracle/scripts/gtfrk11m-eaon.his"],
                      "gtfrk11m-hdd"),
    "gtfrk11m-net": ("gtfrk11m", 15000, ["--rtc", "1791894000", "--npu", "--script", "oracle/scripts/gtfrk11m-net.his"],
                     "gtfrk11m-eaon"),
    # session play: two processes on one session cable (runtime/sys573/link.h), each case naming
    # its partner (run together, in parallel chains)
    "drmn10m-session": ("drmn10m", 16800, ["--rtc", "1791884700", "--script", "oracle/scripts/drmn10m-session.his",
                                           "--session-host", "out/regress/work/session.sock"], "drmn10m-boot", "gtfrk11m-session"),
    "gtfrk11m-session": ("gtfrk11m", 16800, ["--rtc", "1791884700", "--script", "oracle/scripts/gtfrk11m-session.his",
                                             "--session-join", "out/regress/work/session.sock"], "gtfrk11m-boot", "drmn10m-session"),
    # the analog board: first boot, the flash install (two Test presses), attract with CD audio
    "ddr2m-boot": ("ddr2m", 24000, ["--rtc", "1791828936", "--script", "oracle/scripts/ddr2m-boot.his"]),
    # ... and a game: coins, mode and song select, a song stepped through to CLEARED, stage 2
    "ddr2m-play": ("ddr2m", 21000, ["--rtc", "1791837575", "--script", "oracle/scripts/ddr2m-play.his"]),
    # Twinkle (PLAN.md section 21): beatmania IIDX 3rd style's first power-on, E150, 1P Start,
    # attract; in lockstep with MAME at the pin (out/oracle/bmiidx3/boot)
    "bmiidx3-boot": ("bmiidx3", 9000, ["--rtc", "1791906310", "--video", "off", "--script", "oracle/scripts/bmiidx3-boot.his"]),
    # ... and a game from what it left: coins, Start, mode and sound select, the first stage played
    # with keys and turntable spins, the result, game over (out/oracle/bmiidx3/play)
    "bmiidx3-play": ("bmiidx3", 10000, ["--rtc", "1791916974", "--video", "off", "--script", "oracle/scripts/bmiidx3-play.his"],
                     "bmiidx3-boot"),
    # the same game with the background video (MAME shows none): Video CD chapter 36 under the stage
    "bmiidx3-video": ("bmiidx3", 10000, ["--rtc", "1791916974", "--script", "oracle/scripts/bmiidx3-play.his"],
                      "bmiidx3-boot"),
    # beatmania IIDX 1st style, with the DVD player's CTS on SIO1 (TW-5; stock MAME stops at E600):
    # the first boot, test mode's DVD check and backup clear, the reset, attract (out/oracle/bmiidx/clear,
    # a MAME with the same SIO1: oracle/mame-patches); then a game from what it left (.../play)
    "bmiidx-clear": ("bmiidx", 10820, ["--rtc", "1791951737", "--video", "off", "--script", "oracle/scripts/bmiidx-clear.his"]),
    "bmiidx-play": ("bmiidx", 14000, ["--rtc", "1791957993", "--video", "off", "--script", "oracle/scripts/bmiidx-play.his"],
                    "bmiidx-clear"),
    # the same game with the background video and the graffiti layer the game draws over it
    "bmiidx-video": ("bmiidx", 14000, ["--rtc", "1791957993", "--script", "oracle/scripts/bmiidx-play.his"],
                     "bmiidx-clear"),
    # Konami Python 1 (PLAN.md section 23): Reliquary hosted; BIOS, card boot, disk boot,
    # attract, coins, NORMAL mode, a song played through to CLEARED. Software GS.
    "popn9-play": ("popn9", 14000, ["--script", "oracle/scripts/popn9-play.his"]),
    # the same with the Vulkan GS, the one played with: deterministic on a given GPU and
    # driver (re-record on another PC); keeps GS settings honest (PLAN.md section 23, PY-3)
    "popn9-play-vk": ("popn9", 14000, ["--renderer", "vk", "--script", "oracle/scripts/popn9-play.his"]),
    # pop'n music 10-14 on the same core: coins, mode, buttons, a song played (one button every 10 frames)
    "popn10-play": ("popn10", 14000, ["--script", "oracle/scripts/popn10-play.his"]),
    "popn11-play": ("popn11", 14000, ["--script", "oracle/scripts/popn11-play.his"]),
    "popn12-play": ("popn12", 14000, ["--script", "oracle/scripts/popn12-play.his"]),
    "popn13-play": ("popn13", 16000, ["--script", "oracle/scripts/popn13-play.his"]),
    "popn14-play": ("popn14", 14000, ["--script", "oracle/scripts/popn14-play.his"]),
}

# report lines that describe the host, not the machine
HOST_LINES = ("profile:", "audio:", "nvram:")
HOST_TIME = re.compile(r" in [0-9.]+ s of host time")   # kbm's synth line


def after_of(case):
    return CASES[case][3] if len(CASES[case]) > 3 else None


def partner_of(case):
    """a session case's other cabinet: the two run at once, joined by the session cable"""
    return CASES[case][4] if len(CASES[case]) > 4 else None


def chain(case):
    """the cases to run, in order, for this one: the ones it starts after, then itself"""
    c = [case]
    while after_of(c[0]):
        c.insert(0, after_of(c[0]))
    return c


def run(case, digest_path):
    game, frames, extra = CASES[case][:3]
    exe = os.path.join(BIN, "hiraku-" + game)
    work = os.path.join(OUT, "work", case)
    os.makedirs(work, exist_ok=True)
    cmd = [exe, "--frames", str(frames), "--out", work] + extra
    if after_of(case) or any(after_of(c) == case for c in CASES):
        # its own NVRAM directory: fresh, or what the case it comes after left
        nv = os.path.join(work, "nvram")
        shutil.rmtree(nv, ignore_errors=True)
        if after_of(case):
            shutil.copytree(os.path.join(OUT, "work", after_of(case), "nvram"), nv)
        else:
            os.makedirs(nv)
        cmd += ["--nvram", nv]
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
    return names + [p for p in (partner_of(n) for n in names) if p and p not in names]


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

    def group(cases):
        # cases that run after one another, in order; the ones not asked for only set up the next
        out = []
        for c in cases:
            if c in names:
                out.append(one(c))
            else:
                rc, _, wall, _, log = run(c, None)
                if rc != 0:
                    out.append((c, rc, wall, log))
                    break
        return out

    groups = []
    for n in names:
        ch = chain(n)
        g = next((g for g in groups if set(g) & set(ch)), None)
        if g is None:
            groups.append(ch)
        else:
            g.extend(c for c in ch if c not in g)
            g.sort(key=lambda c: len(chain(c)))
    bad = 0
    with cf.ThreadPoolExecutor(max_workers=min(len(groups), os.cpu_count() or 4)) as ex:
        for case, rc, wall, log in (r for rs in ex.map(group, groups) for r in rs):
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
        if partner_of(case):
            print(f"{case:20} skipped: a session case (it needs {partner_of(case)} at once)")
            continue
        game, frames, _ = CASES[case][:3]
        for c in chain(case)[:-1]:   # what it starts after, untimed
            run(c, None)
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
