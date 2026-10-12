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


def checksum_ok(u: dict, data: bytes) -> bool:
    c = u.get("checksum")
    if not c:
        return True
    return sum(data[c["from"]:c["to"]]) & 0xffff == int.from_bytes(data[c["at"]:c["at"] + 2], "big")


def current(u: dict, nvram: str):
    """The value in the save, or None (no save yet); Error if the copies disagree."""
    path = os.path.join(nvram, u["nvram"])
    if not os.path.exists(path):
        return None
    data = open(path, "rb").read()
    vals = {data[o] for o in u["offsets"] if o < len(data)}
    if len(vals) != 1:
        raise Error(f"{path}: the copies of {u['id']} disagree ({sorted(vals)}): not a save this tool knows")
    if not checksum_ok(u, data):
        raise Error(f"{path}: its checksum doesn't match: not a save this tool knows")
    return vals.pop()


def progress(p: dict, nvram: str, unlocks: dict) -> dict | None:
    """An event's state from the save (games/<id>/unlocks.toml [[progress]]), or None (no save)."""
    path = os.path.join(nvram, p["nvram"])
    if not os.path.exists(path):
        return None
    data = open(path, "rb").read()
    def field(off):
        vals = {int.from_bytes(data[c + off:c + off + 4], "big") for c in p.get("copies", [0])}
        if len(vals) != 1:
            raise Error(f"{path}: the copies of {p['id']} disagree: not a save this tool knows")
        return vals.pop()
    def matches(cond):
        return cond is not None and (current(unlocks[cond["unlock"]], nvram) & cond.get("mask", 0xffffffff)) in cond["values"]
    active = p.get("active") is None or matches(p.get("active"))
    if p.get("kind") == "bingo":
        return bingo(p, data, active, matches(p.get("released")))
    stage, unl, missed, pos, lap = (field(p[k]) for k in ("stage", "unlocked", "missed", "position", "lap"))
    st = p["stages"][min(stage, len(p["stages"]) - 1)]
    goal = st["goal"]
    here = [c for c in p["cities"] if c["stages"] >> stage & 1]
    ahead = sorted(here, key=lambda c: (c["space"] - pos - 1) % p["spaces"])
    nxt = ahead[0] if ahead else None
    return {
        "active": active, "stage": stage + 1, "stages": len(p["stages"]), "stage_name": st["name"],
        "unlocked": bin(unl & goal).count("1"), "goal": bin(goal).count("1"),
        "position": pos, "spaces": p["spaces"], "lap": lap,
        "next": nxt and nxt["name"], "next_romaji": nxt and nxt["romaji"],
        "next_in": nxt and (nxt["space"] - pos - 1) % p["spaces"] + 1,
        "next_unlocked": bool(nxt and nxt["masks"][stage] & unl),
        "missed": [c["name"] for c in here if c["masks"][stage] & missed & ~unl],
        "done": unl & goal == goal and stage == len(p["stages"]) - 1,
    }


def bingo(p: dict, data: bytes, active: bool, released: bool) -> dict:
    """A bingo card (pop'n 7): which squares are open (bits of a 32-bit word, kept in copies)."""
    words = {int.from_bytes(data[o:o + 4], "big") for o in p["offsets"]}
    if len(words) != 1:
        raise Error(f"the copies of {p['id']} disagree: not a save this tool knows")
    word = words.pop()
    cells = p["cells"]
    open_ = [c.get("center", False) or bool(word >> c["bit"] & 1) for c in cells]
    lines = [ln["name"] for ln in p["lines"] if all(open_[i] for i in ln["cells"])]
    closed = [c for c, o in zip(cells, open_) if not o]
    near = sorted({ln["name"] for ln in p["lines"] if sum(not open_[i] for i in ln["cells"]) == 1})
    return {"active": active, "released": released, "open": sum(open_) - 1, "squares": len(cells) - 1,
            "lines": lines, "line_count": len(p["lines"]), "near": near,
            "closed": [{"shown": c["shown"], "condition": c["condition"]} for c in closed]}


def describe_progress(p: dict, s: dict) -> str:
    if s is None:
        return "no save yet"
    if p.get("kind") == "bingo":
        lines = [] if s["active"] else ["the event is not running (BINGO de 7: NO)"]
        if s["released"]:
            lines.append("BINGO de 7 DATA RELEASE is on: its songs are unlocked anyway")
        lines.append(f"{s['open']} of {s['squares']} squares open; lines {len(s['lines'])} of {s['line_count']}"
                     + (f" ({', '.join(s['lines'])})" if s["lines"] else ""))
        if s["near"]:
            lines.append("one square from a line: " + ", ".join(s["near"]))
        if s["closed"]:
            lines.append("still closed (the number on the card, and the game's name for its condition):")
            lines += [f"  {c['shown']}: {c['condition']}" for c in s["closed"]]
        return "\n".join(lines)
    lines = [] if s["active"] else ["the event is not running (it runs at phase 1 or 2)"]
    if s["done"]:
        return "\n".join(lines + ["all done: everything unlocked"])
    lines.append(f"Stage {s['stage']} of {s['stages']} ({s['stage_name']}): {s['unlocked']} of {s['goal']} unlocked")
    lines.append(f"KRAFT: space {s['position']} of {s['spaces']}, lap {s['lap'] + 1}; next city {s['next']} "
                 f"({s['next_romaji']}), {s['next_in']} spaces away" + (" (already unlocked)" if s["next_unlocked"] else ""))
    if s["missed"]:
        lines.append("passed without landing (given on the next pass): " + ", ".join(s["missed"]))
    return "\n".join(lines)


def level_name(u: dict, v) -> str:
    for lv in u.get("level", []):
        if lv["value"] == v:
            return lv["name"]
    return "none" if v is None else f"other ({v:#x}: set in the game's test mode)"


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
    c = u.get("checksum")   # a 16-bit sum of the bytes from..to, kept at `at` (big-endian)
    if c:
        data[c["at"]:c["at"] + 2] = (sum(data[c["from"]:c["to"]]) & 0xffff).to_bytes(2, "big")
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
        prog = []
        for p in spec.get("progress", []):
            s = progress(p, nvram, unlocks)
            prog.append({"id": p["id"], "title": p["title"], "state": s, "text": describe_progress(p, s)})
        if a.json:
            print(json.dumps({"game": a.game, "nvram": nvram, "unlocks": state, "progress": prog,
                              "notes": spec.get("note", [])}, ensure_ascii=False))
        elif not a.set:
            print(f"{a.game}, save {nvram}")
            for u in state:
                print(f"\n{u['title']} ({u['id']}): now {level_name(u, u['current'])}")
                for lv in u.get("level", []):
                    mark = "*" if lv["value"] == u["current"] else " "
                    code = f"  code {lv['code']}" if lv.get("code") else ""
                    print(f" {mark} {u['id']}={lv['value']}  {lv['name']}{code}\n      {lv['text']}")
            for p in prog:
                print(f"\n{p['title']}:\n{p['text']}")
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
