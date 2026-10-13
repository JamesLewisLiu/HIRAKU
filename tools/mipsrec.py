#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record the R3000 blocks System 573 games run, for the recompiler (runtime/sys573/mips.h).

    tools/mipsrec.py [GAME...]     every 573 game with regress cases, or these

Runs each game's tools/regress.py cases interpreted, with HK_573_RECORD, and adds what they ran
to build/<game>/mips573.txt (counts add up over runs). The next build recompiles the blocks
(tools/mipsgen.py). Code no recording has seen still runs, in MAME's interpreter, so record what
the game does: boot, attract, songs. Any run adds to the file the same way:

    HK_573_RECORD=build/ddr2m/mips573.txt build/cmake/hiraku-ddr2m --nvram ... --frames 9000

The recording is not committed: it holds the game's code.
"""
import os
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regress   # noqa: E402


def is_573(game):
    try:
        with open(os.path.join(ROOT, "games", game, "game.toml"), "rb") as fh:
            return tomllib.load(fh).get("core") == "sys573"
    except FileNotFoundError:
        return False


def main():
    if any(a.startswith("-") for a in sys.argv[1:]):
        sys.exit(__doc__)
    games = sys.argv[1:] or sorted({g for g, _, _ in regress.CASES.values() if is_573(g)})
    for game in games:
        if not is_573(game):
            sys.exit(f"{game} is not a System 573 game")
        cases = [c for c, (g, _, _) in regress.CASES.items() if g == game]
        if not cases:
            print(f"{game}: no regress cases; record a run of your own (see --help)")
            continue
        out = os.path.join(ROOT, "build", game, "mips573.txt")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        os.environ["HK_573_RECORD"] = out
        for case in cases:   # one at a time: each adds to the file at exit
            print(f"+ {case}", flush=True)
            rc, _, wall, _, log = regress.run(case, None)
            if rc != 0:
                sys.exit(f"{case}: exit {rc}\n{log[-1500:]}")
            print(f"  {wall:.1f} s", flush=True)
        print(f"{game}: {sum(1 for l in open(out) if not l.startswith('#'))} blocks in {os.path.relpath(out, ROOT)}")
    print("rebuild (ninja -C build/cmake) to recompile them")


if __name__ == "__main__":
    main()
