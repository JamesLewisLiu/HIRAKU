#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Profile-guided optimisation of the whole build, trained on the regress cases.

    tools/pgo.py [--build DIR]     train, then use the profile in DIR (default build/cmake)

HIRAKU's machines are interpreters (the 573's and DJ Main's CPUs, Firebeat's sound 68000),
schedulers and renderers. clang lays out their hot paths far better when it knows which
branches are taken (PLAN.md sections 14 and 19). This script:
1. builds instrumented, in build/pgo-gen (clang, HK_PGO_GENERATE);
2. runs every tools/regress.py case whose game is ingested, headless, from a blank NVRAM
   (the cases are short, and they are what the games do: boot, attract, a song);
3. merges the counts per core into build/pgo/<core>.profdata (firebeat, sys573; DJ Main runs
   slower with a profile, so it gets none): the cores compile some sources into separate
   copies, whose hot paths must not be blended;
4. sets HK_PGO_DIR in DIR's CMake cache, and rebuilds there.

The profile is not committed: it comes from running the games. Run this again after changing
the code (a stale profile still builds, it just helps less). The results stay bit-identical
(tools/regress.py check): a profile only changes how the code is laid out.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILED = ("firebeat", "sys573")   # the cores CMakeLists.txt's hk_pgo() calls use a profile for
GEN = os.path.join(ROOT, "build", "pgo-gen")
OUT = os.path.join(ROOT, "build", "pgo")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=ROOT, **kw)


def capped(cmd, mem):
    """A command in its own memory-capped scope, when systemd-run is there."""
    if shutil.which("systemd-run"):
        return ["systemd-run", "--user", "--scope", "-q", "-p", f"MemoryMax={mem}", *cmd]
    return cmd


def core_of(game: str) -> str:
    with open(os.path.join(ROOT, "games", game, "game.toml"), "rb") as fh:
        return tomllib.load(fh).get("core", "firebeat")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=os.path.join(ROOT, "build", "cmake"), help="the build to optimise")
    args = ap.parse_args()
    for tool in ("clang++", "llvm-profdata", "cmake", "ninja"):
        if not shutil.which(tool):
            print(f"ERROR    {tool} not found (PGO needs clang and its llvm-profdata)")
            return 1

    os.environ["HK_REGRESS_BIN"] = GEN
    # the 573's recompiled blocks off (runtime/sys573/mips.h): the interpreter runs whatever no
    # recording covers, so it is what the profile must know about
    os.environ["HK_573_BLOCKS"] = "0"
    import regress   # noqa: E402  (reads HK_REGRESS_BIN)
    cases = [c for c, (game, _, _) in regress.CASES.items()
             if os.path.exists(os.path.join(ROOT, "dumps", game, "machine.txt")) and core_of(game) in PROFILED]
    if not cases:
        print("ERROR    no ingested game to train on: tools/regress.py's cases need dumps/<game>/")
        return 1
    games = sorted({regress.CASES[c][0] for c in cases})
    print(f"training on {len(cases)} cases: {', '.join(cases)}")

    run(["cmake", "-S", ROOT, "-B", GEN, "-G", "Ninja", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
         "-DHK_PGO_GENERATE=ON"], stdout=subprocess.DEVNULL)
    run(capped(["ninja", "-C", GEN, *[f"hiraku-{g}" for g in games]], "12G"))

    os.makedirs(OUT, exist_ok=True)
    for f in glob.glob(os.path.join(OUT, "*.profraw")):
        os.remove(f)
    for case in cases:   # one at a time: each writes its own counts at exit
        os.environ["LLVM_PROFILE_FILE"] = os.path.join(OUT, f"{core_of(regress.CASES[case][0])}__{case}-%p.profraw")
        print(f"+ {case}", flush=True)
        rc, _, wall, _, log = regress.run(case, None)
        if rc != 0:
            print(f"ERROR    {case}: exit {rc}\n{log[-1500:]}")
            return 1
        print(f"  {wall:.1f} s", flush=True)
    for old in glob.glob(os.path.join(OUT, "*.profdata")):
        os.remove(old)
    for core in sorted({core_of(regress.CASES[c][0]) for c in cases}):
        run(["llvm-profdata", "merge", "-o", os.path.join(OUT, f"{core}.profdata"),
             *glob.glob(os.path.join(OUT, f"{core}__*.profraw"))])

    run(["cmake", "-S", ROOT, "-B", args.build, f"-DHK_PGO_DIR={OUT}"], stdout=subprocess.DEVNULL)
    run(capped(["ninja", "-C", args.build], "12G"))
    print(f"done     {os.path.relpath(OUT, ROOT)}/*.profdata, used by {os.path.relpath(args.build, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
