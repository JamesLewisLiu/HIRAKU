#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A game's files, for the launcher's details (launcher/mainwindow.cpp): what the game needs, where
each is, and whether it's the right one.

- The MAME games (DJ Main, System 573, Twinkle, Firebeat): every ROM and disk MAME's set needs
  (tools/data/mame-bemani.json), checked against MAME's SHA-1. A CHD by its header's SHA-1 (MAME's
  covers its data and metadata: an image unpacked from a CHD can't be checked against it); a
  Firebeat program by game.toml's SHA-1 of it unpacked.
- Every game: the files its machine.txt names (a Python game's disk, BIOS and keys; a PC game's
  program), with their MD5 and SHA-1 for comparing with other lists.

Files up to 64 MB are hashed at once; bigger ones only with --big (a disk can be gigabytes). The
hashes are kept in dumps/<game>/.checklist.json, by path, size and time, so a file is read once.

    tools/checklist.py GAME [--big]       JSON on stdout: {"game", "items": [{kind, name, path,
                                          size, md5, sha1, expected, status, note}]}
status: ok (the expected SHA-1), bad (another), present (nothing to check against, or not hashed:
"big"), missing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mameset  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SMALL = 64 << 20
# Firebeat's disks, unpacked under their own names: machine.txt's key for each of MAME's regions
FIREBEAT_DISKS = {"ata:0:cdrom": "cd_master", "spu_ata:0:hdd": "spu_hdd", "ata:1:cdrom": "cd2"}


def read_kv(path: str) -> dict[str, str]:
    kv = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                line = line.rstrip("\r\n")
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    kv[k] = v
    except OSError:
        pass
    return kv


def rom_sha1s(name: str) -> dict[str, str]:
    """MAME's SHA-1 of every ROM the set needs, by name: its own, its romof chain's, its devices'."""
    sets = mameset.load()
    out: dict[str, str] = {}
    todo = list(mameset.chain(name))
    for c in mameset.chain(name):
        todo += sets[c].get("refs", [])
    seen = set()
    for s in todo:
        if s in seen or s not in sets:
            continue
        seen.add(s)
        for f, h in sets[s].get("sha1", {}).items():
            out.setdefault(f, h)
        todo += sets[s].get("refs", [])
    return out


def cue_data(cue: str) -> str:
    """A cue sheet's (first) track file, where its data is; the cue itself if it names none"""
    try:
        with open(cue, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = re.match(r'\s*FILE\s+"([^"]+)"', line)
                if m:
                    return os.path.join(os.path.dirname(cue), m.group(1))
    except OSError:
        pass
    return cue


class Hashes:
    """MD5 and SHA-1 of files, kept by path, size and time in the game's .checklist.json"""

    def __init__(self, path: str, big: bool):
        self.path, self.big, self.changed = path, big, False
        try:
            with open(path) as fh:
                self.kept = json.load(fh)
        except (OSError, ValueError):
            self.kept = {}

    def of(self, path: str) -> tuple[str | None, str | None]:
        st = os.stat(path)
        key, stamp = os.path.realpath(path), [st.st_size, st.st_mtime_ns]
        k = self.kept.get(key)
        if k and k.get("stamp") == stamp:
            return k["md5"], k["sha1"]
        if st.st_size > SMALL and not self.big:
            return None, None
        md5, sha1 = hashlib.md5(), hashlib.sha1()
        with open(path, "rb") as fh:
            while chunk := fh.read(1 << 20):
                md5.update(chunk)
                sha1.update(chunk)
        self.kept[key] = {"stamp": stamp, "md5": md5.hexdigest(), "sha1": sha1.hexdigest()}
        self.changed = True
        return self.kept[key]["md5"], self.kept[key]["sha1"]

    def save(self) -> None:
        if self.changed and os.path.isdir(os.path.dirname(self.path)):
            with open(self.path + ".part", "w") as fh:
                json.dump(self.kept, fh)
            os.replace(self.path + ".part", self.path)


def item(hashes: Hashes, kind: str, name: str, path: str | None, expected: str | None = None, note: str = "") -> dict:
    it = {"kind": kind, "name": name, "path": path, "size": None, "md5": None, "sha1": None,
          "expected": expected, "status": "missing", "note": note}
    if not path or not os.path.exists(path):
        it["path"] = None
        return it
    if os.path.isdir(path):
        it["status"] = "present"
        return it
    it["size"] = os.path.getsize(path)
    if path.lower().endswith(".chd") and expected:
        it["note"] = it["note"] or "the CHD's own SHA-1 (header)"
        it["status"] = "ok" if mameset.chd_sha1(path) == expected else "bad"
    it["md5"], it["sha1"] = hashes.of(path)
    if it["status"] == "missing":
        if it["sha1"] is None:
            it["status"], it["note"] = "present", it["note"] or "big"
        elif expected:
            it["status"] = "ok" if it["sha1"] == expected else "bad"
        else:
            it["status"] = "present"
    return it


def checklist(gid: str, big: bool) -> dict:
    with open(os.path.join(ROOT, "games", gid, "game.toml"), "rb") as fh:
        game = tomllib.load(fh)
    dumps = os.path.join(ROOT, "dumps", gid)
    machine = read_kv(os.path.join(dumps, "machine.txt"))
    hashes = Hashes(os.path.join(dumps, ".checklist.json"), big)
    items, listed = [], set()

    def where(v: str) -> str:
        return v if os.path.isabs(v) else os.path.join(dumps, v)

    def add(it: dict) -> None:
        items.append(it)
        if it["path"]:
            listed.add(os.path.realpath(it["path"]))

    set_name = game.get("mame")
    if set_name in mameset.load():
        sha = rom_sha1s(set_name)
        for name, _size, _crc in mameset.needed_roms(set_name):
            add(item(hashes, "ROM", name, os.path.join(dumps, "rom", name), sha.get(name)))
        stems = {os.path.splitext(os.path.basename(v))[0].lower(): where(v) for v in machine.values() if v}
        for name, sha1, region in mameset.needed_disks(set_name):
            path = stems.get(name.lower())
            if not path and game.get("core", "firebeat") == "firebeat" and FIREBEAT_DISKS.get(region) in machine:
                path = where(machine[FIREBEAT_DISKS[region]])
            note = ""
            if path and path.lower().endswith(".cue"):   # its data: the track file the cue names
                listed.add(os.path.realpath(path))
                path = cue_data(path)
            if path and not path.lower().endswith(".chd"):
                note, sha1 = "unpacked from its CHD: MAME's SHA-1 is the CHD's", None
            add(item(hashes, "disk", name, path, sha1, note))
    if "program" in game and os.path.exists(os.path.join(dumps, "program.bin")) or "program" in game and set_name:
        add(item(hashes, "program", game["program"].get("exe", "program") + " (unpacked)",
                 os.path.join(dumps, "program.bin"), game["program"].get("sha1")))
    # the rest of what machine.txt names: files, the keys' folder's files, a PC game's program
    for k, v in machine.items():
        if not v or k in ("core", "family", "name", "drive", "save_drive", "game_id", "io_mode") or "=" in v:
            continue
        p = where(v)
        if not os.path.exists(p) or os.path.realpath(p) in listed:
            continue
        if os.path.isdir(p):
            if k == "keys":
                for f in sorted(os.listdir(p)):
                    if os.path.isfile(os.path.join(p, f)):
                        add(item(hashes, "key", f, os.path.join(p, f)))
            elif k == "program" and machine.get("exe"):
                add(item(hashes, "program", machine["exe"], os.path.join(p, machine["exe"])))
            else:
                add(item(hashes, k, os.path.basename(os.path.normpath(p)) + "/", p, note="a folder"))
            continue
        add(item(hashes, k, os.path.basename(p), p))
        if k == "bios":   # Python 1's BIOS: its .NVM and .MEC beside it
            stem = os.path.splitext(p)[0]
            for ext in (".NVM", ".MEC", ".nvm", ".mec"):
                if os.path.exists(stem + ext) and os.path.realpath(stem + ext) not in listed:
                    add(item(hashes, "bios", os.path.basename(stem + ext), stem + ext))
    hashes.save()
    return {"game": gid, "added": bool(machine), "items": items}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game")
    ap.add_argument("--big", action="store_true", help="hash the files over 64 MB too")
    a = ap.parse_args()
    if not os.path.exists(os.path.join(ROOT, "games", a.game, "game.toml")):
        print(f"no games/{a.game}/game.toml", file=sys.stderr)
        return 2
    json.dump(checklist(a.game, a.big), sys.stdout, ensure_ascii=False)
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
