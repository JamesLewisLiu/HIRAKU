#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A slang preset's parameter values as a HIRAKU .params file (a "look" of a shader whose presets
differ only in their values: Lola Retro's, shaders/lola-retro/looks/).

    tools/slangp2params.py PRESET.slangp OUT.params [--skip NAME,...] [--title TEXT]

Writes `name = value` for every parameter the preset sets (its `parameters` list), but those in
--skip. The first line is a comment: --title, else the preset's file name.
"""
import argparse
import os
import re
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("preset")
    ap.add_argument("out")
    ap.add_argument("--skip", default="")
    ap.add_argument("--title")
    a = ap.parse_args()
    keys = {}
    for line in open(a.preset, encoding="utf-8"):
        line = re.sub(r"(^|\s)#.*$", "", line)
        m = re.match(r'^\s*([A-Za-z0-9_]+)\s*=\s*"?([^"]*?)"?\s*$', line)
        if m:
            keys[m.group(1)] = m.group(2)
    names = [n for n in keys.get("parameters", "").split(";") if n]
    if not names:
        sys.exit(f"{a.preset}: no parameters list")
    skip = {n for n in a.skip.split(",") if n}
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(f"# {a.title or os.path.basename(a.preset)}\n")
        for n in names:
            if n in keys and n not in skip:
                f.write(f"{n} = {keys[n]}\n")


if __name__ == "__main__":
    main()
