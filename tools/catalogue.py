#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a folder of MAME sets holds, against what HIRAKU runs.

  tools/catalogue.py ROMDIR... [--all] [--json OUT] [--verify]

For every BEMANI set of the boards HIRAKU knows (MAME's data: tools/mameset.py) it says:
  - HIRAKU: runs (a games/*/game.toml), and whether its data is installed here (dumps/<id>);
    else a clone of a game that runs, a board HIRAKU has, or a new board;
  - the files: every ROM the set needs, found by CRC in the set's archive, its parents', its
    BIOS's and its devices', in any of the ROMDIRs (as MAME's rompath); every disk, found by the CHD header's SHA-1 in the set's folder or
    its parents'. A file found by name with other contents is another dump of it (an older
    MAME's), and is reported as such;
  - an archive under an older MAME's name is told by what it holds (`renamed`).

With --verify every CHD found is checked in full with `chdman verify`, one at a time and
niced (a big disk takes minutes; results are kept in out/catalogue/verify.json).
Sets not in ROMDIR are listed only with --all.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import subprocess
import sys
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chd as chdtool  # noqa: E402
import mameset  # noqa: E402

ROOT = mameset.ROOT
ARCHIVES = (".zip", ".7z")


def hiraku_games() -> dict[str, dict]:
    """{MAME set: game.toml} for the games HIRAKU has a manifest for."""
    out = {}
    for path in glob.glob(os.path.join(ROOT, "games", "*", "game.toml")):
        with open(path, "rb") as fh:
            g = tomllib.load(fh)
        out[g.get("mame") or g["id"]] = g
    return out


# the boards HIRAKU has a core for (Python 1: PCSX2 Reliquary hosted)
CORES = {"djmain", "firebeat", "twinkle", "sys573", "python"}


def scan(romdirs: list[str]) -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """({name: [archives or folders of ROMs]}, {name: [CHDs in the folders of that name]})."""
    archives, chds = {}, {}
    for romdir in romdirs:
        for e in sorted(os.listdir(romdir)):
            p = os.path.join(romdir, e)
            if os.path.isdir(p):
                files = sorted(os.listdir(p))
                c = [os.path.join(p, f) for f in files if f.lower().endswith(".chd")]
                if c:
                    chds.setdefault(e, []).extend(c)
                if any(not f.lower().endswith(".chd") for f in files):
                    archives.setdefault(e, []).append(p)
            elif e.lower().endswith(ARCHIVES):
                archives.setdefault(e.rsplit(".", 1)[0], []).append(p)
    return archives, chds


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("romdirs", nargs="+", metavar="romdir")
    ap.add_argument("--all", action="store_true", help="also list the sets ROMDIR lacks")
    ap.add_argument("--json", help="write the report as JSON here")
    ap.add_argument("--verify", action="store_true", help="chdman verify every CHD found (slow)")
    args = ap.parse_args()

    sets = mameset.load()
    games = hiraku_games()
    archives, chds = scan(args.romdirs)

    # archives under an older MAME's name, told by contents (mameset.current_name)
    renamed = {}
    for name, paths in archives.items():
        cur = mameset.current_name(name, paths[0])
        if cur and cur != name:
            renamed[name] = cur
    by_set = {}   # current set name -> the archive names that hold it
    for name in archives:
        by_set.setdefault(renamed.get(name, name), []).append(name)
    for name in chds:
        by_set.setdefault(renamed.get(name, name), [])

    crc_cache: dict[str, dict] = {}

    def crcs(name: str) -> dict:
        if name not in crc_cache:
            merged: dict = {}
            for p in archives.get(name, []):   # the same name in several ROMDIRs: every dump counts
                for f, sc in mameset.archive_crcs(p).items():
                    merged[f if f not in merged or merged[f] == sc else f"{f}@{p}"] = sc
            crc_cache[name] = merged
        return crc_cache[name]

    sha_cache: dict[str, str | None] = {}

    def sha1(path: str) -> str | None:
        if path not in sha_cache:
            sha_cache[path] = mameset.chd_sha1(path)
        return sha_cache[path]

    def archive_names(s: str) -> list[str]:
        """The archives MAME would look in for set s, under their names in ROMDIR."""
        out = []
        for c in mameset.chain(s):
            out += by_set.get(c, [])
        for d in sets[s].get("refs", []):
            out += by_set.get(d, [])
        for c in mameset.chain(s)[1:]:
            for d in sets[c].get("refs", []):
                out += by_set.get(d, [])
        return list(dict.fromkeys(out))

    def chd_paths(s: str) -> list[str]:
        out = []
        for c in mameset.chain(s):
            for n in [c] + [k for k, v in renamed.items() if v == c]:
                out += chds.get(n, [])
        # a clone's disks often sit in its parent's folder; siblings too, in a non-merged archive
        parent = sets[s].get("parent")
        if parent:
            for n, m in sets.items():
                if m.get("parent") == parent:
                    out += chds.get(n, [])
        return list(dict.fromkeys(out))

    report = []
    for s in sorted(sets):
        m = sets[s]
        if not mameset.is_bemani(s):
            continue
        present = s in by_set
        if not present and not args.all:
            continue
        r = {"set": s, "title": m["title"], "board": m["board"], "mame_status": m.get("status", ""),
             "in_romdir": present, "archives": by_set.get(s, [])}
        # HIRAKU's side
        g = games.get(s)
        if g:
            r["hiraku"] = "runs, installed here" if os.path.isdir(os.path.join(ROOT, "dumps", g["id"])) else "runs"
        else:
            family = {m.get("parent") or s} | {n for n, x in sets.items() if x.get("parent") == (m.get("parent") or s)}
            kin = sorted(f for f in family if f in games)
            if kin:
                r["hiraku"] = "clone of " + ", ".join(kin)
            elif m["board"] in CORES:
                r["hiraku"] = "board in HIRAKU"
            else:
                r["hiraku"] = "new board"
        # the files
        if present:
            have = {}
            names = {}
            for a in archive_names(s):
                for f, (size, crc) in crcs(a).items():
                    have[(size, crc)] = a
                    names.setdefault(f.split("@")[0], (size, crc))
            missing, other = [], []
            for f, size, crc in mameset.needed_roms(s):
                if (size, crc) in have:
                    continue
                (other if f in names else missing).append(f)
            r["roms_missing"] = sorted(set(missing))
            r["roms_other_dump"] = sorted(set(other))
            found = {sha1(p): p for p in chd_paths(s)}
            dmissing, dother, dfound = [], [], {}
            local = {os.path.splitext(os.path.basename(p))[0]: p for p in chd_paths(s)}
            for d, dsha, region in mameset.needed_disks(s):
                if dsha in found:
                    dfound[d] = found[dsha]
                elif d in local:
                    dother.append(d)
                else:
                    dmissing.append(d)
            r["disks"] = dfound
            r["disks_missing"] = dmissing
            r["disks_other_dump"] = dother
            r["complete"] = not (missing or other or dmissing or dother)
        report.append(r)

    if args.verify:
        verify_all(report)

    # print
    alias_note = {v: k for k, v in renamed.items()}
    for r in report:
        flags = []
        if r["in_romdir"]:
            if r["complete"]:
                flags.append("complete")
            for k, label in (("roms_missing", "missing ROMs"), ("roms_other_dump", "other ROM dumps"),
                             ("disks_missing", "missing disks"), ("disks_other_dump", "other disk dumps")):
                if r[k]:
                    flags.append(f"{label}: {' '.join(r[k])}")
            if r.get("verify"):
                bad = [d for d, v in r["verify"].items() if v != "ok"]
                flags.append("CHDs verified" if not bad else f"CHD verify FAILED: {' '.join(bad)}")
        else:
            flags.append("not here")
        name = r["set"] + (f" (as {alias_note[r['set']]})" if r["set"] in alias_note else "")
        print(f"{name:24} {r['board']:9} {r['hiraku']:28} {'; '.join(flags)}")
    print()
    print(f"{len(report)} BEMANI sets; renamed archives: " +
          (", ".join(f"{k} = {v}" for k, v in sorted(renamed.items())) or "none"))
    unknown = sorted(n for n in list(archives) + list(chds) if n not in sets and n not in renamed)
    if unknown:
        print("not told: " + " ".join(unknown))
    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        with open(args.json, "w") as fh:
            json.dump({"romdirs": [os.path.abspath(d) for d in args.romdirs], "renamed": renamed,
                       "sets": report}, fh, indent=1)
    return 0


def verify_all(report: list[dict]) -> None:
    """chdman verify, one CHD at a time, niced; cached by path, size and mtime."""
    cache_path = os.path.join(ROOT, "out", "catalogue", "verify.json")
    try:
        with open(cache_path) as fh:
            cache = json.load(fh)
    except (OSError, ValueError):
        cache = {}
    for r in report:
        if not r.get("disks"):
            continue
        r["verify"] = {}
        for d, p in r["disks"].items():
            st = os.stat(p)
            key = f"{p}|{st.st_size}|{int(st.st_mtime)}"
            if key not in cache:
                print(f"verify   {p}", file=sys.stderr, flush=True)
                cmd = chdtool.capped(["nice", "-n", "19", "ionice", "-c", "3"] +
                                     chdtool.chdman_command([os.path.dirname(p)]) + ["verify", "-i", p])
                out = subprocess.run(cmd, capture_output=True, text=True)
                cache[key] = "ok" if out.returncode == 0 else (out.stdout + out.stderr).strip()[-300:]
                os.makedirs(os.path.dirname(cache_path), exist_ok=True)
                with open(cache_path, "w") as fh:
                    json.dump(cache, fh, indent=1)
            r["verify"][d] = cache[key]


if __name__ == "__main__":
    sys.exit(main())
