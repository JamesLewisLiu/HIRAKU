#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn existing cheats into HIRAKU mods (docs/MODS.md): one mod folder per cheat.

    tools/modimport.py GAME FILE OUTDIR

GAME is HIRAKU's game id (games/<id>): it gives the machine's byte order. FILE is
  - a PCSX2 patch file (.pnach, Konami Python games): every `patch=PLACE,EE,ADDR,TYPE,VALUE`
    line (byte, short, word, double); PLACE 0 is written once, 1 every frame. One mod, named
    after the file's gametitle or comment lines, or the file's name;
  - a MAME cheat file (cheat.xml): each <cheat desc=...> whose actions are all plain writes
    `CPU.pX@ADDRESS=VALUE` (X: b, w, d, q; constants only) becomes a mod; the others are left
    out, with a line saying why. A script run once (state="on") is written once; "run" every
    frame; "off" scripts are ignored.
OUTDIR gets a folder per mod (each with its mod.toml): copy the ones wanted to
<saves>/mods/<game>/ and turn them on in the launcher (right-click the game: Mods...).

Cheats write their values whatever is there, so the patches have no `original` (they're forced
writes): fine for the data cheats are made of; a code cheat for another version of the game
would write over the wrong code.
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDTHS = {"b": 1, "w": 2, "d": 4, "q": 8, "byte": 1, "short": 2, "word": 4, "double": 8}


def byte_order(game):
    """big for DJ Main (68000) and Firebeat (PowerPC), little for the MIPS machines"""
    toml = os.path.join(ROOT, "games", game, "game.toml")
    if not os.path.exists(toml):
        sys.exit(f"no games/{game}/game.toml")
    core = re.search(r'^core\s*=\s*"([^"]+)"', open(toml, encoding="utf-8").read(), re.M)
    return "little" if core and core.group(1) in ("sys573", "twinkle", "python") else "big"


def folder_name(text):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text).strip("_")[:60] or "cheat"


def write_mod(outdir, folder, game, name, description, patches):
    d = os.path.join(outdir, folder)
    os.makedirs(d, exist_ok=True)
    q = lambda s: s.replace("\\", "\\\\").replace('"', '\\"')
    lines = [f'name = "{q(name)}"', f'game = "{game}"']
    if description:
        lines.append(f'description = "{q(description)}"')
    for addr, data, once in patches:
        lines += ["", "[[patch]]", f"address = 0x{addr:08x}", f'replace = "{data.hex()}"']
        if once:
            lines.append("once = true")
    with open(os.path.join(d, "mod.toml"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"mod      {folder}: {name} ({len(patches)} writes)")


def pnach(path, game, order, outdir):
    patches, title, notes = [], "", []
    for n, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        line = line.strip()
        if line.startswith("gametitle="):
            title = line.split("=", 1)[1].strip()
        elif line.startswith("comment="):
            notes.append(line.split("=", 1)[1].strip())
        elif line.startswith("patch="):
            f = [x.strip() for x in line.split("=", 1)[1].split(",")]
            if len(f) != 5 or f[1].upper() != "EE" or f[3].lower() not in WIDTHS:
                print(f"skipped  {path}:{n}: only EE byte/short/word/double writes ({line})")
                continue
            w = WIDTHS[f[3].lower()]
            patches.append((int(f[2], 16), int(f[4], 16).to_bytes(w, "little"), f[0] == "0"))
    if not patches:
        sys.exit(f"{path}: no patch= lines this can use")
    name = title or os.path.splitext(os.path.basename(path))[0]
    write_mod(outdir, folder_name(name), game, name, "; ".join(notes), patches)


def cheat_xml(path, game, order, outdir):
    root = ET.parse(path).getroot()
    action_re = re.compile(r"^\s*(\w+)\.p([bwdq])@(0x[0-9a-fA-F]+|[0-9a-fA-F]+)\s*=\s*(0x[0-9a-fA-F]+|[0-9a-fA-F]+)\s*$")
    made = 0
    for cheat in root.iter("cheat"):
        desc = cheat.get("desc", "cheat")
        patches, why = [], ""
        for script in cheat.iter("script"):
            state = script.get("state", "run")
            if state == "off":
                continue
            for act in script.iter("action"):
                if act.get("condition"):
                    why = "a condition"
                    break
                m = action_re.match(act.text or "")
                if not m:
                    why = f"not a plain write ({(act.text or '').strip()[:50]})"
                    break
                if m.group(1) != "maincpu":
                    why = f"not the main CPU ({m.group(1)})"
                    break
                w = WIDTHS[m.group(2)]
                addr, val = int(m.group(3), 16), int(m.group(4), 16)
                patches.append((addr, val.to_bytes(w, order), state == "on"))
            if why:
                break
        if cheat.find("parameter") is not None:
            why = why or "it takes a parameter"
        if why or not patches:
            print(f"skipped  {desc}: {why or 'nothing written'}")
            continue
        write_mod(outdir, folder_name(desc), game, desc, "from " + os.path.basename(path), patches)
        made += 1
    if not made:
        sys.exit(f"{path}: no cheat this can use")


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    game, path, outdir = sys.argv[1:]
    order = byte_order(game)
    if path.lower().endswith(".pnach"):
        pnach(path, game, order, outdir)
    else:
        cheat_xml(path, game, order, outdir)


if __name__ == "__main__":
    main()
