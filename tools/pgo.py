#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Profile-guided optimisation for the System 573 core: about 30 % less host time.

    tools/pgo.py [--build DIR]     train, then use the profile in DIR (default build/cmake)

The 573 core's time is MAME's PS1 interpreter, a large switch that clang lays out far better
when it knows which cases are hot (PLAN.md section 19, 573-4). This script:
1. builds the 573 games instrumented, in build/pgo-gen (clang, HK_573_PGO_GENERATE);
2. runs the training scripts headless on the ingested 573 games, from a blank NVRAM:
   oracle/scripts/ddr2m-play.his (first boot, flash install, attract, a song) and
   ddr3mj-boot.his;
3. merges the counts into build/pgo/573.profdata;
4. sets HK_573_PGO_PROFILE in DIR's CMake cache, and rebuilds there.

The profile is not committed: it comes from running the games. Run this again after changing
the 573 code (a stale profile still builds, it just helps less). The results stay
bit-identical (tools/regress.py check): the profile only changes how the code is laid out.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "build", "pgo-gen")
OUT = os.path.join(ROOT, "build", "pgo")

# game: arguments of its training run (the regress cases' scripts and clocks)
TRAINING = {
    "ddr2m": ["--rtc", "1791837575", "--script", "oracle/scripts/ddr2m-play.his"],
    "ddr3mj": ["--rtc", "1791804639", "--script", "oracle/scripts/ddr3mj-boot.his"],
}


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=ROOT, **kw)


def capped(cmd, mem="6G"):
    """A command in its own memory-capped scope, when systemd-run is there."""
    if shutil.which("systemd-run"):
        return ["systemd-run", "--user", "--scope", "-q", "-p", f"MemoryMax={mem}", *cmd]
    return cmd


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=os.path.join(ROOT, "build", "cmake"), help="the build to optimise")
    args = ap.parse_args()
    for tool in ("clang++", "llvm-profdata", "cmake", "ninja"):
        if not shutil.which(tool):
            print(f"ERROR    {tool} not found (PGO needs clang and its llvm-profdata)")
            return 1
    games = [g for g in TRAINING if os.path.exists(os.path.join(ROOT, "dumps", g, "machine.txt"))]
    if not games:
        print("ERROR    no System 573 game ingested to train on (dumps/ddr2m or dumps/ddr3mj)")
        return 1

    run(["cmake", "-S", ROOT, "-B", GEN, "-G", "Ninja", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
         "-DHK_573_PGO_GENERATE=ON"], stdout=subprocess.DEVNULL)
    run(capped(["ninja", "-C", GEN, *[f"hiraku-{g}" for g in games]], "10G"))

    os.makedirs(OUT, exist_ok=True)
    for f in glob.glob(os.path.join(OUT, "*.profraw")):
        os.remove(f)
    for g in games:
        env = dict(os.environ, LLVM_PROFILE_FILE=os.path.join(OUT, f"{g}-%p.profraw"))
        cmd = [os.path.join(GEN, f"hiraku-{g}"), *TRAINING[g]]
        if shutil.which("systemd-run"):   # the scope doesn't inherit the environment
            cmd = ["systemd-run", "--user", "--scope", "-q", "-p", "MemoryMax=4G",
                   f"--setenv=LLVM_PROFILE_FILE={env['LLVM_PROFILE_FILE']}", *cmd]
        run(cmd, env=env, stdout=subprocess.DEVNULL)
    profile = os.path.join(OUT, "573.profdata")
    run(["llvm-profdata", "merge", "-o", profile, *glob.glob(os.path.join(OUT, "*.profraw"))])

    run(["cmake", "-S", ROOT, "-B", args.build, f"-DHK_573_PGO_PROFILE={profile}"], stdout=subprocess.DEVNULL)
    run(capped(["ninja", "-C", args.build], "12G"))
    print(f"done     {os.path.relpath(profile, ROOT)}, used by {os.path.relpath(args.build, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
