#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A game's DIP switches as MAME describes them (its -listxml), into games/<id>/dips.txt, for the
launcher's DIP switches dialog and the cores' --dip PORT=VALUE.

    tools/mamedips.py [--core djmain] [ID ...]

MAME: $MAME_BIN, else the flatpak's (org.mamedev.MAME). Each game's MAME set is its game.toml's
`mame`. dips.txt holds a line per switch, then a line per setting, indented:

    DSW1 0x0f 0x0f SW1:8,7,6,5 Coinage        port, mask, default value, where on the board, name
      0x01 1P 5C / Continue 5C                 a setting's value (within the mask) and name

The port's other bits (no switch) read as 1, as MAME's active-low ports do."""
import os
import re
import shlex
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def mame_cmd():
    m = os.environ.get("MAME_BIN")
    return shlex.split(m) if m else ["flatpak", "run", "org.mamedev.MAME"]


def manifest(game):
    keys = {}
    with open(os.path.join(ROOT, "games", game, "game.toml"), encoding="utf-8") as f:
        for line in f:
            if line.lstrip().startswith("["):
                break
            m = re.match(r'\s*(\w+)\s*=\s*"([^"]*)"', line)
            if m:
                keys[m.group(1)] = m.group(2)
    return keys


def dips_of(mame_set):
    xml = subprocess.run(mame_cmd() + ["-listxml", mame_set], capture_output=True, text=True, check=True).stdout
    machine = ET.fromstring(xml).find(f"machine[@name='{mame_set}']")
    if machine is None:
        raise SystemExit(f"MAME has no set {mame_set}")
    out = []
    for d in machine.findall("dipswitch"):
        where = ",".join(l.get("number") for l in d.findall("diplocation"))
        bank = d.find("diplocation").get("name") if d.find("diplocation") is not None else ""
        values = [(int(v.get("value")), v.get("name"), v.get("default") == "yes") for v in d.findall("dipvalue")]
        default = next((v for v, _, dflt in values if dflt), values[0][0] if values else 0)
        out.append((d.get("tag"), int(d.get("mask")), default, f"{bank}:{where}" if bank else "-", d.get("name"), values))
    return out


def main():
    args = sys.argv[1:]
    core = "djmain"
    if args[:1] == ["--core"]:
        core, args = args[1], args[2:]
    games = args or sorted(g for g in os.listdir(os.path.join(ROOT, "games"))
                           if os.path.isfile(os.path.join(ROOT, "games", g, "game.toml"))
                           and manifest(g).get("core") == core)
    for game in games:
        m = manifest(game)
        dips = dips_of(m.get("mame", game))
        if not dips:
            print(f"{game}: no DIP switches in MAME")
            continue
        lines = [f"# {m.get('title', game)}'s DIP switches, from MAME's -listxml {m.get('mame', game)} "
                 f"(tools/mamedips.py): port, mask, default, where, name; then each setting's value and name"]
        defaults = {}
        for tag, mask, default, where, name, values in dips:
            lines.append(f"{tag} 0x{mask:02x} 0x{default:02x} {where} {name}")
            lines += [f"  0x{v:02x} {n}" for v, n, _ in values]
            defaults[tag] = (defaults.get(tag, 0xff) & ~mask) | default
        with open(os.path.join(ROOT, "games", game, "dips.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{game}: {len(dips)} switches; MAME's defaults " +
              ", ".join(f"{t}=0x{v:02x}" for t, v in sorted(defaults.items())))


if __name__ == "__main__":
    main()
