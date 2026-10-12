#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract and identify a Firebeat program from a user-supplied disc image.

    tools/ingest.py DISC [--out dumps/] [--exe FILE] [--check RAW] [--romset SET.zip]
    tools/ingest.py --romset SET.7z --hdd DISK.img        (DJ Main: no program disc)

DISC is the program CD (.iso, .cue/.bin, or a raw MODE1 track); --exe takes a
loose HIKARU.EXE / FIREBEAT.EXE instead. The unpacked image is identified
against games/*/game.toml by SHA-1 and written to dumps/<game id>/program.bin
(dumps/ is gitignored - nothing derived from a game goes in the repo).

DJ Main games (games/*/game.toml with core = "djmain") run their ROMs as they
are: give the MAME set and the raw hard disk image (chdman extracthd -i X.chd
-o X.img). The game is found by the set's name.

--romset copies the files of a MAME ROM set (.zip, or .7z with the 7z tool) (dongle image, default RTC NVRAM,
SPU program, BIOS) into dumps/<game id>/rom/, where the runtime looks for them.
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import os
import subprocess
import sys
import tomllib
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbexe  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def all_games() -> list[dict]:
    out = []
    for path in sorted(glob.glob(os.path.join(ROOT, "games", "*", "game.toml"))):
        with open(path, "rb") as fh:
            out.append(tomllib.load(fh))
    return out


def known_games() -> dict[str, dict]:
    """Firebeat games by the SHA-1 of their unpacked program."""
    return {g["program"]["sha1"]: g for g in all_games() if "program" in g}


def copy_romset(romset: str, romdir: str) -> list[str]:
    os.makedirs(romdir, exist_ok=True)
    names = []
    if romset.lower().endswith(".7z"):
        # 7-Zip sets (e.g. standalone/non-merged): needs the 7z tool
        listing = subprocess.run(["7z", "l", "-slt", romset], check=True, capture_output=True, text=True).stdout
        names = [os.path.basename(l[7:]) for l in listing.splitlines()[1:]
                 if l.startswith("Path = ") and l[7:] != romset and not l[7:].endswith(".7z")]
        subprocess.run(["7z", "e", "-y", f"-o{romdir}", romset], check=True, capture_output=True)
    else:
        with zipfile.ZipFile(romset) as z:
            for name in z.namelist():
                if name.endswith("/"):
                    continue
                with open(os.path.join(romdir, os.path.basename(name)), "wb") as fh:
                    fh.write(z.read(name))
                names.append(os.path.basename(name))
    for name in names:
        print(f"rom      {name}")
    return names


def ingest_djmain(args) -> int:
    """A DJ Main game: the MAME ROM set (program, sprites, tiles) and the hard disk."""
    set_name = os.path.basename(args.romset).rsplit(".", 1)[0]
    game = next((g for g in all_games() if g.get("core") == "djmain" and g.get("mame") == set_name), None)
    if game is None:
        print(f"game     unknown - no DJ Main games/*/game.toml has mame = \"{set_name}\"")
        return 1
    gid = game["id"]
    print(f"game     {gid}: {game['title']} (DJ Main)")
    outdir = os.path.join(args.out, gid)
    names = set(copy_romset(args.romset, os.path.join(outdir, "rom")))
    roms = game["roms"]
    missing = [f for k in ("program", "sprites", "tiles") for f in roms[k] if f not in names]
    if missing:
        print(f"ERROR    the set lacks {', '.join(missing)}")
        return 1
    machine_path = os.path.join(outdir, "machine.txt")
    machine: dict[str, str] = {}
    if os.path.exists(machine_path):
        with open(machine_path) as fh:
            machine = dict(l.rstrip("\n").split("=", 1) for l in fh if "=" in l)
    machine.update(core="djmain", family=game["family"],
                   program=",".join(roms["program"]), sprites=",".join(roms["sprites"]), tiles=",".join(roms["tiles"]))
    if args.hdd:
        machine["hdd"] = os.path.abspath(args.hdd)
    if not machine.get("hdd"):
        print(f"ERROR    no hard disk: --hdd {game['hdd']['chd']}.img (chdman extracthd -i {game['hdd']['chd']}.chd -o ...)")
        return 1
    with open(machine_path, "w") as fh:
        for k in ["core", "family", "program", "sprites", "tiles", "hdd"]:
            fh.write(f"{k}={machine[k]}\n")
    print(f"wrote    {os.path.relpath(machine_path, ROOT)}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("disc", nargs="?", help="program disc image (.iso/.cue/.bin)")
    src.add_argument("--exe", help="a loose HIKARU.EXE / FIREBEAT.EXE")
    ap.add_argument("--out", default=os.path.join(ROOT, "dumps"), help="output root (default: dumps/)")
    ap.add_argument("--check", metavar="RAW", help="compare the unpacked image to a known-good raw dump")
    ap.add_argument("--romset", metavar="ZIP", help="MAME ROM set (.zip/.7z) to copy into dumps/<game>/rom/")
    ap.add_argument("--hdd", metavar="IMG", help="raw HDD image: on the SPU's ATA bus (beatmania III), or DJ Main's IDE disk")
    ap.add_argument("--dvd", metavar="ISO", help="DVD image on the SPU's ATA bus (pop'n music)")
    ap.add_argument("--cd2", metavar="CUE", help="second disc on the main ATA bus (ParaParaParadise audio CD)")
    args = ap.parse_args()
    if not args.disc and not args.exe:
        if args.romset:
            return ingest_djmain(args)
        ap.error("give a program disc, --exe, or (DJ Main) --romset with --hdd")

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
        load = game["program"].get("load", fbexe.LOAD_ADDR)
        if load != prog.load_addr:   # an older program the BIOS puts elsewhere (Keyboardmania 1st)
            prog = fbexe.Program(prog.image, load, fbexe.branch_target(
                int.from_bytes(prog.image[-4:], "big"), load + len(prog.image) - 4), prog.ppp_wrapped)
            print(f"load     {load:#010x} (from games/{gid}/game.toml), entry {prog.entry:#010x}")
        if game["program"]["entry"] != prog.entry:
            print(f"WARNING  game.toml entry {game['program']['entry']:#x} != decoded {prog.entry:#x}")

    outdir = os.path.join(args.out, gid)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "program.bin"), "wb") as fh:
        fh.write(prog.image)
    print(f"wrote    {os.path.relpath(os.path.join(outdir, 'program.bin'), ROOT)}")

    # what the runtime needs to build the machine (it reads no TOML). A re-run
    # keeps the media paths it is not given (e.g. --hdd from an earlier run).
    machine_path = os.path.join(outdir, "machine.txt")
    machine: dict[str, str] = {}
    if os.path.exists(machine_path):
        with open(machine_path) as fh:
            machine = dict(l.rstrip("\n").split("=", 1) for l in fh if "=" in l)
    board = (game or {}).get("board", {})
    spu = (game or {}).get("spu", {})
    machine["family"] = (game or {}).get("family", machine.get("family", "bm3"))
    if board.get("dongle"):
        machine["dongle"] = board["dongle"]
    if "cabinet" in board:
        machine["cabinet"] = str(board["cabinet"])
    if args.disc:
        machine["cd_master"] = os.path.abspath(args.disc)
    if spu.get("rom"):
        machine["spu_rom"] = spu["rom"]
    if args.cd2:
        machine["cd_slave"] = os.path.abspath(args.cd2)
    if args.hdd:
        machine["spu_hdd"] = os.path.abspath(args.hdd)
    if args.dvd:
        machine["spu_dvd"] = os.path.abspath(args.dvd)
    order = ["family", "dongle", "cabinet", "cd_master", "cd_slave", "spu_rom", "spu_hdd", "spu_dvd"]
    with open(machine_path, "w") as fh:
        for k in order + sorted(set(machine) - set(order)):
            if k in machine:
                fh.write(f"{k}={machine[k]}\n")
    print(f"wrote    {os.path.relpath(machine_path, ROOT)}")

    if args.romset:
        copy_romset(args.romset, os.path.join(outdir, "rom"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
