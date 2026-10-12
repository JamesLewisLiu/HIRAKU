#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A game's unlocks (games/<id>/unlocks.toml), read from and written to its saved NVRAM.

    tools/unlock.py GAME                     what can be unlocked, and what the save has
    tools/unlock.py GAME ID=VALUE...         set it (e.g. popn8 phase=4); the save is backed up first
    tools/unlock.py GAME --json              the same, for the launcher

  --nvram DIR   the save (default: ~/.local/share/hiraku/GAME, as the game window uses)

The game must not be running: it writes its NVRAM back when it exits. An unlock is the bytes
the game itself writes when an operator enters the code (unlocks.toml says how each was found).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Error(Exception):
    pass


def load(game: str) -> dict:
    path = os.path.join(ROOT, "games", game, "unlocks.toml")
    if not os.path.exists(path):
        raise Error(f"{game}: no unlocks known (no games/{game}/unlocks.toml)")
    with open(path, "rb") as f:
        return tomllib.load(f)


def default_nvram(game: str) -> str:
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "hiraku", game)


def current(u: dict, nvram: str):
    """The value in the save, or None (no save yet); Error if the copies disagree."""
    path = os.path.join(nvram, u["nvram"])
    if not os.path.exists(path):
        return None
    data = open(path, "rb").read()
    vals = {data[o] for o in u["offsets"] if o < len(data)}
    if len(vals) != 1:
        raise Error(f"{path}: the copies of {u['id']} disagree ({sorted(vals)}): not a save this tool knows")
    return vals.pop()


def level_name(u: dict, v) -> str:
    for lv in u.get("level", []):
        if lv["value"] == v:
            return lv["name"]
    return "none" if v is None else str(v)


def apply(game: str, u: dict, nvram: str, value: int) -> str:
    if value not in [lv["value"] for lv in u.get("level", [])]:
        raise Error(f"{u['id']}={value}: not one of {[lv['value'] for lv in u['level']]}")
    path = os.path.join(nvram, u["nvram"])
    if not os.path.exists(path):
        raise Error(f"{path}: no save yet. Start {game} once (with its first-boot setup), quit it, then unlock.")
    current(u, nvram)   # refuses a save it doesn't understand
    data = bytearray(open(path, "rb").read())
    backup = f"{path}.{time.strftime('%Y%m%d-%H%M%S')}.bak"
    shutil.copy2(path, backup)
    for o in u["offsets"]:
        data[o] = value
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    return backup


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game")
    ap.add_argument("set", nargs="*", metavar="ID=VALUE")
    ap.add_argument("--nvram")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    nvram = a.nvram or default_nvram(a.game)
    try:
        spec = load(a.game)
        unlocks = {u["id"]: u for u in spec.get("unlock", [])}
        for s in a.set:
            uid, _, v = s.partition("=")
            if uid not in unlocks or not v:
                raise Error(f"{s}: expected ID=VALUE with ID one of {list(unlocks)}")
            u = unlocks[uid]
            before = current(u, nvram)
            backup = apply(a.game, u, nvram, int(v, 0))
            if not a.json:
                print(f"{uid}: {level_name(u, before)} -> {level_name(u, int(v, 0))} (the old save: {backup})")
        state = []
        for u in spec.get("unlock", []):
            state.append({**u, "current": current(u, nvram)})
        if a.json:
            print(json.dumps({"game": a.game, "nvram": nvram, "unlocks": state, "notes": spec.get("note", [])},
                             ensure_ascii=False))
        elif not a.set:
            print(f"{a.game}, save {nvram}")
            for u in state:
                print(f"\n{u['title']} ({u['id']}): now {level_name(u, u['current'])}")
                for lv in u.get("level", []):
                    mark = "*" if lv["value"] == u["current"] else " "
                    code = f"  code {lv['code']}" if lv.get("code") else ""
                    print(f" {mark} {u['id']}={lv['value']}  {lv['name']}{code}\n      {lv['text']}")
            for n in spec.get("note", []):
                print(f"\n{n['title']}:\n{n['text']}")
    except Error as e:
        if a.json:
            print(json.dumps({"error": str(e)}, ensure_ascii=False))
        else:
            print(f"unlock: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
