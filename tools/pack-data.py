#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy games' data to another HIRAKU root (a USB drive, another PC).

    tools/pack-data.py DEST GAME...       copy dumps/<GAME>/ and its discs to DEST/dumps/<GAME>/
    tools/pack-data.py --list [GAME...]   sizes only (all ingested games when none are named)

machine.txt names disc images by absolute path on this PC (cd_master, cd_slave,
spu_hdd, spu_dvd, hdd). The copy puts each disc beside machine.txt (a .cue
with its tracks) and rewrites the path as a relative one, which the runtime
resolves against dumps/<GAME>/. Files already in DEST with the same size are
not copied again.
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISC_KEYS = ("cd_master", "cd_slave", "spu_hdd", "spu_dvd", "hdd")


def read_kv(path):
    out = []
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            k, _, v = line.partition("=")
            out.append((k, v))
    return out


def cue_tracks(cue):
    """The files a .cue names, relative to the .cue's folder."""
    files = []
    with open(cue, errors="replace") as f:
        for line in f:
            s = line.strip()
            if s.upper().startswith("FILE "):
                rest = s[5:]
                if rest.startswith('"'):
                    files.append(rest[1:rest.index('"', 1)])
                else:
                    files.append(rest.split()[0])
    return files


def disc_files(path):
    """(source, name in the copy) for a disc image and, for a .cue, its tracks."""
    if path.lower().endswith(".cue"):
        d = os.path.dirname(path)
        return [(path, os.path.basename(path))] + [(os.path.join(d, t), t) for t in cue_tracks(path)]
    return [(path, os.path.basename(path))]


def game_files(game):
    gdir = os.path.join(ROOT, "dumps", game)
    kv = read_kv(os.path.join(gdir, "machine.txt"))
    files = []   # (source, path relative to dumps/<game>/)
    for base, _, names in os.walk(gdir):
        for n in names:
            src = os.path.join(base, n)
            files.append((src, os.path.relpath(src, gdir)))
    new_kv = []
    for k, v in kv:
        if k in DISC_KEYS and v:
            src = v if os.path.isabs(v) else os.path.join(gdir, v)
            disc = disc_files(src)
            if os.path.isabs(v):
                files += disc
                v = disc[0][1]
        new_kv.append((k, v))
    return files, new_kv


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    listing = args[0] == "--list"
    dest = None if listing else args[0]
    games = args[1:] or (sorted(os.listdir(os.path.join(ROOT, "dumps"))) if listing else [])
    if not games:
        print("name at least one game", file=sys.stderr)
        return 2
    total = 0
    for g in games:
        if not os.path.exists(os.path.join(ROOT, "dumps", g, "machine.txt")):
            print(f"{g}: no dumps/{g}/machine.txt", file=sys.stderr)
            continue
        files, kv = game_files(g)
        size = sum(os.path.getsize(s) for s, _ in files if os.path.exists(s))
        total += size
        print(f"{g:10s} {size / 1e9:7.2f} GB")
        if listing:
            continue
        gdest = os.path.join(dest, "dumps", g)
        for src, rel in files:
            if os.path.basename(rel) == "machine.txt" and os.path.dirname(rel) == "":
                continue
            out = os.path.join(gdest, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            if os.path.exists(out) and os.path.getsize(out) == os.path.getsize(src):
                continue
            print(f"  {rel}")
            shutil.copyfile(src, out)
        with open(os.path.join(gdest, "machine.txt"), "w") as f:
            for k, v in kv:
                f.write(f"{k}={v}\n" if k else "\n")
    print(f"total      {total / 1e9:7.2f} GB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
