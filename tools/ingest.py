#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract and identify a Firebeat program from a user-supplied disc image.

    tools/ingest.py DISC [--out dumps/] [--exe FILE] [--check RAW]

DISC is the program CD (.iso, .cue/.bin, or a raw MODE1 track); --exe takes a
loose HIKARU.EXE / FIREBEAT.EXE instead. The unpacked image is identified
against games/*/game.toml by SHA-1 and written to dumps/<game id>/program.bin
(dumps/ is gitignored - nothing derived from a game goes in the repo).
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import os
import sys
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbexe  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def known_games() -> dict[str, dict]:
    out = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "games", "*", "game.toml"))):
        with open(path, "rb") as fh:
            g = tomllib.load(fh)
        out[g["program"]["sha1"]] = g
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("disc", nargs="?", help="program disc image (.iso/.cue/.bin)")
    src.add_argument("--exe", help="a loose HIKARU.EXE / FIREBEAT.EXE")
    ap.add_argument("--out", default=os.path.join(ROOT, "dumps"), help="output root (default: dumps/)")
    ap.add_argument("--check", metavar="RAW", help="compare the unpacked image to a known-good raw dump")
    args = ap.parse_args()

    if args.exe:
        exe_name = os.path.basename(args.exe)
        with open(args.exe, "rb") as fh:
            exe = fh.read()
    else:
        with fbexe.Disc(args.disc) as disc:
            iso = fbexe.Iso9660(disc)
            entry = fbexe.find_exe(iso)
            exe_name = entry.name
            exe = iso.read_file(entry)
            print(f"disc     {args.disc}  volume '{iso.volume_id}'  "
                  f"{2352 if disc.raw else 2048}-byte sectors")

    prog = fbexe.unpack_exe(exe)
    sha1 = hashlib.sha1(prog.image).hexdigest()
    print(f"exe      {exe_name}  {len(exe)} bytes  sha1 {hashlib.sha1(exe).hexdigest()}"
          f"{'  (ParaPara wrapper)' if prog.ppp_wrapped else ''}")
    print(f"program  {len(prog.image)} bytes @ {prog.load_addr:#010x}  entry {prog.entry:#010x}  sha1 {sha1}")

    if args.check:
        with open(args.check, "rb") as fh:
            ref = fh.read()
        if ref == prog.image:
            print(f"check    identical to {args.check}")
        else:
            diff = next((i for i in range(min(len(ref), len(prog.image))) if ref[i] != prog.image[i]), None)
            print(f"check    DIFFERS from {args.check} (sizes {len(ref)}/{len(prog.image)}, first diff {diff})")
            return 1

    game = known_games().get(sha1)
    if game is None:
        print("game     unknown - no games/*/game.toml has this program sha1")
        gid = "unknown-" + sha1[:8]
    else:
        gid = game["id"]
        print(f"game     {gid}: {game['title']}")
        if game["program"]["entry"] != prog.entry:
            print(f"WARNING  game.toml entry {game['program']['entry']:#x} != decoded {prog.entry:#x}")

    outdir = os.path.join(args.out, gid)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "program.bin"), "wb") as fh:
        fh.write(prog.image)
    print(f"wrote    {os.path.relpath(os.path.join(outdir, 'program.bin'), ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
