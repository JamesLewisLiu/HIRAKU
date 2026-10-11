#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn build/<game>/manifest.json (+ games/<game>/game.toml) into the plain-text
spec the recompiler reads, and check the control flow on the way.

    analyze/prep.py <game>        -> build/<game>/recomp.txt

Spec format, one item per line:
    image <path>                  program image
    base <hex>                    load address
    func <hex> [nopoll]           a function entry; following lines belong to it
    range <lo> <hi>               body range [lo, hi)
    entry <hex>                   a secondary entry: something branches or calls
                                  into the middle of this function
    jt <at> <target> ...          bctr at <at> dispatches to these targets
    end

Checks (warnings, and the function-entry fixes below):
  - every `bl` target and every tail-branch target is a function entry;
  - every bctr has a resolved jump table;
  - no function body overlaps another.
A call/tail target inside another function's body becomes a secondary entry of
that function (the recompiler emits an extra C function that starts there).
Targets outside every function are reported.

game.toml may carry:
    [recomp]
    nopoll = [[lo, hi], ...]      kernel ranges: emitted without interrupt polls
    extra_entries = [hex, ...]    force function entries Ghidra missed (applied by
                                  analyze/ghidra.sh when it builds the manifest)
"""
from __future__ import annotations

import json
import os
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def h(x: str) -> int:
    return int(x, 16)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    gid = sys.argv[1]
    with open(os.path.join(ROOT, "build", gid, "manifest.json")) as fh:
        man = json.load(fh)
    with open(os.path.join(ROOT, "games", gid, "game.toml"), "rb") as fh:
        game = tomllib.load(fh)
    rc = game.get("recomp", {})
    nopoll = [(lo, hi) for lo, hi in rc.get("nopoll", [])]

    funcs = {h(f["entry"]): f for f in man["functions"]}
    entries = set(funcs)
    owner: dict[int, int] = {}          # word address -> function entry
    for e, f in funcs.items():
        for lo, hi in f["ranges"]:
            for a in range(h(lo), h(hi), 4):
                if a in owner and owner[a] != e:
                    print(f"warn: {a:08x} in both {owner[a]:08x} and {e:08x}")
                owner.setdefault(a, e)

    problems = 0
    secondary: dict[int, set[int]] = {}
    for e, f in sorted(funcs.items()):
        for kind, targets in (("call", f["calls"]), ("tail", f.get("tails", []))):
            for t in map(h, targets):
                if t in entries or t == e:
                    continue
                if t in owner:
                    secondary.setdefault(owner[t], set()).add(t)
                else:
                    problems += 1
                    print(f"{kind:5s} {e:08x} -> {t:08x} is outside every function")
        for jt in f.get("jumptables", []):
            if not jt["targets"]:
                problems += 1
                print(f"jt    {e:08x}: bctr at {jt['at']} unresolved")

    out = os.path.join(ROOT, "build", gid, "recomp.txt")
    img = os.path.join(ROOT, "dumps", gid, "program.bin")
    with open(out, "w") as fh:
        fh.write(f"image {img}\nbase {man['base'][2:]}\n")
        for e, f in sorted(funcs.items()):
            np = any(lo <= e < hi for lo, hi in nopoll)
            fh.write(f"func {e:08x}{' nopoll' if np else ''}\n")
            for lo, hi in f["ranges"]:
                fh.write(f"range {lo[2:]} {hi[2:]}\n")
            for t in sorted(secondary.get(e, ())):
                fh.write(f"entry {t:08x}\n")
            for jt in f.get("jumptables", []):
                fh.write("jt " + jt["at"][2:] + " " + " ".join(t[2:] for t in jt["targets"]) + "\n")
            fh.write("end\n")
    nsec = sum(len(v) for v in secondary.values())
    print(f"{len(funcs)} functions, {nsec} secondary entries, {problems} control-flow problems"
          f" -> {os.path.relpath(out, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
