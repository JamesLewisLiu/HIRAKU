#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add a game: give its files, in any order, and ingest finds out the rest.

    tools/ingest.py FILE_OR_FOLDER...            (the usual way: everything you have for the game)
    tools/ingest.py DISC [--romset SET.zip] [--hdd IMG] [--dvd ISO] [--cd2 CUE]   (roles given)
    tools/ingest.py --exe FIREBEAT.EXE           (a loose program, Firebeat)

Files can be MAME ROM sets (.zip, .7z), MAME CHDs, and disc or disk images
(.cue/.bin, .iso, .img); a folder stands for the files in it. CHDs are
extracted with MAME's chdman (memory-capped) into dumps/<game>/.

The game is found by
- its program, for Firebeat: the CD holding HIKARU.EXE / FIREBEAT.EXE, unpacked
  and matched by SHA-1 against games/*/game.toml;
- the MAME set's name otherwise (DJ Main, System 573), or a CHD's name.
Each image gets its role from what it is: a hard disk is beatmania III's sound
board disk or DJ Main's IDE disk, a DVD pop'n music's, a second CD ParaPara's
audio disc.

Everything goes to dumps/<game id>/ (gitignored: nothing derived from a game
goes in the repo): program.bin for Firebeat, rom/ (the set's files), the
extracted images, and machine.txt, which tells the runtime what the machine is.
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import tomllib
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chd  # noqa: E402
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


def set_name_of(romset: str) -> str:
    """A MAME set's name: the archive's name without extension, or the folder's."""
    base = os.path.basename(os.path.normpath(romset))
    return base if os.path.isdir(romset) else base.rsplit(".", 1)[0]


def copy_romset(romset: str, romdir: str) -> list[str]:
    os.makedirs(romdir, exist_ok=True)
    names = []
    if os.path.isdir(romset):
        # a MAME rompath folder: the set's files loose (CHDs and logs left out)
        for n in sorted(os.listdir(romset)):
            src = os.path.join(romset, n)
            if os.path.isfile(src) and not n.lower().endswith(IMAGES + (".log", ".part", ".txt")):
                shutil.copyfile(src, os.path.join(romdir, n))
                names.append(n)
    elif romset.lower().endswith(".7z"):
        # 7-Zip sets (e.g. standalone/non-merged): the 7z tool, or bsdtar (libarchive) when 7z is missing
        if shutil.which("7z"):
            listing = subprocess.run(["7z", "l", "-slt", romset], check=True, capture_output=True, text=True).stdout
            names = [os.path.basename(l[7:]) for l in listing.splitlines()[1:]
                     if l.startswith("Path = ") and l[7:] != romset and not l[7:].endswith(".7z")]
            subprocess.run(["7z", "e", "-y", f"-o{romdir}", romset], check=True, capture_output=True)
        elif shutil.which("bsdtar"):
            listing = subprocess.run(["bsdtar", "-tf", romset], check=True, capture_output=True, text=True).stdout
            names = [os.path.basename(l) for l in listing.splitlines() if l and not l.endswith("/")]
            # flat, as 7z e: every file straight into romdir
            subprocess.run(["bsdtar", "-xf", romset, "-C", romdir, "-s", ",.*/,,"], check=True, capture_output=True)
        else:
            sys.exit(f"{romset}: a .7z set needs 7z (package 7zip or p7zip) or bsdtar (libarchive)")
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
    set_name = set_name_of(args.romset)
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
    machine.update(core="djmain", family=game["family"], map=game.get("map", "j"),
                   program=",".join(roms["program"]), sprites=",".join(roms["sprites"]), tiles=",".join(roms["tiles"]))
    for k in ("ata_master_password", "ata_user_password"):   # complete MIX on (MAME init_<game>())
        if game["hdd"].get(k):
            machine[k] = game["hdd"][k]
        else:
            machine.pop(k, None)
    if args.hdd:
        machine["hdd"] = media_path(args.hdd, outdir)
    if not machine.get("hdd"):
        print(f"ERROR    no hard disk: add {game['hdd']['chd']}.chd (or its raw image)")
        return 1
    with open(machine_path, "w") as fh:
        for k in ["core", "family", "map", "program", "sprites", "tiles", "hdd", "ata_master_password", "ata_user_password"]:
            if k in machine:
                fh.write(f"{k}={machine[k]}\n")
    print(f"wrote    {os.path.relpath(machine_path, ROOT)}")
    return 0


def ingest_sys573(args) -> int:
    """A System 573 game: the MAME set (BIOS, cassette and I/O board chips) and the CD."""
    set_name = set_name_of(args.romset) if args.romset else None
    game = next((g for g in all_games() if g.get("core") == "sys573" and g.get("mame") == set_name), None)
    if game is None:
        print(f"game     unknown - no System 573 games/*/game.toml has mame = \"{set_name}\"")
        return 1
    gid = game["id"]
    print(f"game     {gid}: {game['title']} (System 573)")
    outdir = os.path.join(args.out, gid)
    names = set(copy_romset(args.romset, os.path.join(outdir, "rom")))
    roms = game["roms"]
    keys = ("bios", "h8", "cassette_eeprom", "cassette_id", "digital_id")
    missing = [roms[k] for k in keys if roms.get(k) and roms[k] not in names]
    if missing:
        print(f"ERROR    the set lacks {', '.join(missing)}")
        return 1
    machine_path = os.path.join(outdir, "machine.txt")
    machine: dict[str, str] = {}
    if os.path.exists(machine_path):
        with open(machine_path) as fh:
            machine = dict(l.rstrip("\n").split("=", 1) for l in fh if "=" in l)
    machine.update(core="sys573", family=game["family"])
    for k in keys:
        if roms.get(k):
            machine[k] = roms[k]
    board = game.get("board", {})
    for k in ("io_board", "cassette", "pccard2"):
        if board.get(k):
            machine[k] = board[k]
    if board.get("memory_card_reader"):
        machine["memory_card_reader"] = "1"
    if getattr(args, "cd", None):
        machine["cd"] = media_path(args.cd, outdir)
    if not machine.get("cd"):
        print(f"ERROR    no CD: add {game['cd']['chd']}.chd (or its .cue/.bin)")
        return 1
    order = ["core", "family", *keys, "io_board", "cassette", "pccard2", "memory_card_reader", "cd"]
    with open(machine_path, "w") as fh:
        for k in order:
            if k in machine:
                fh.write(f"{k}={machine[k]}\n")
    print(f"wrote    {os.path.relpath(machine_path, ROOT)}")
    return 0


def ingest_firebeat(args) -> int:
    """A Firebeat game: the program disc (or a loose exe), and its other media."""
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
    for k in ("disc", "hdd", "dvd", "cd2"):   # images extracted before the game was known
        if getattr(args, k, None):
            setattr(args, k, adopt(getattr(args, k), args.out, outdir))
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
        machine["cd_master"] = media_path(args.disc, outdir)
    if spu.get("rom"):
        machine["spu_rom"] = spu["rom"]
    if args.cd2:
        machine["cd_slave"] = media_path(args.cd2, outdir)
    if args.hdd:
        machine["spu_hdd"] = media_path(args.hdd, outdir)
    if args.dvd:
        machine["spu_dvd"] = media_path(args.dvd, outdir)
    order = ["family", "dongle", "cabinet", "cd_master", "cd_slave", "spu_rom", "spu_hdd", "spu_dvd"]
    with open(machine_path, "w") as fh:
        for k in order + sorted(set(machine) - set(order)):
            if k in machine:
                fh.write(f"{k}={machine[k]}\n")
    print(f"wrote    {os.path.relpath(machine_path, ROOT)}")

    if args.romset:
        copy_romset(args.romset, os.path.join(outdir, "rom"))
    # what is still missing for this game to run
    if game:
        need = [f for f in (board.get("dongle"), spu.get("rom")) if f]
        romdir = os.path.join(outdir, "rom")
        lacking = [f for f in need if not os.path.exists(os.path.join(romdir, f))]
        if lacking:
            print(f"NEXT     add the MAME set ({game.get('mame', gid)}.zip or .7z): it holds {', '.join(lacking)}")
    return 0



def media_path(path: str, outdir: str) -> str:
    """A path for machine.txt: relative to dumps/<game>/ when the file is in it."""
    full = os.path.abspath(path)
    base = os.path.abspath(outdir) + os.sep
    return full[len(base):] if full.startswith(base) else full


INCOMING = ".incoming"


def adopt(path: str, out: str, outdir: str) -> str:
    """Move an image extracted into dumps/.incoming/ (game not yet known) to the
    game's folder; a .cue moves with the files it names."""
    inc = os.path.join(os.path.abspath(out), INCOMING) + os.sep
    full = os.path.abspath(path)
    if not full.startswith(inc):
        return path
    moved = os.path.join(outdir, os.path.basename(full))
    if full.lower().endswith(".cue"):
        for f in fbexe_cue_files(full):
            src = os.path.join(os.path.dirname(full), f)
            if os.path.exists(src):
                os.replace(src, os.path.join(outdir, f))
    os.replace(full, moved)
    return moved


def fbexe_cue_files(cue: str) -> list[str]:
    files = []
    with open(cue, errors="replace") as fh:
        for line in fh:
            t = line.strip()
            if t.upper().startswith("FILE "):
                rest = t[5:]
                files.append(rest[1:rest.index('"', 1)] if rest.startswith('"') else rest.split()[0])
    return files


ARCHIVES = (".zip", ".7z")
IMAGES = (".chd", ".cue", ".iso", ".img", ".bin", ".raw")


def gather(paths: list[str], set_names: set[str]) -> list[str]:
    """The files to look at: folders expanded (a folder named after a MAME set is that
    set, as in a MAME rompath); a .bin a .cue names left to the .cue."""
    files = []
    for p in paths:
        if os.path.isdir(p) and os.path.basename(os.path.normpath(p)) in set_names:
            names = sorted(os.listdir(p))
            if any(not n.lower().endswith(IMAGES + (".log", ".part", ".txt")) for n in names):
                files.append(os.path.normpath(p))   # loose ROM files: the set itself
            files += [os.path.join(p, n) for n in names if n.lower().endswith(IMAGES)]
        elif os.path.isdir(p):
            for base, _, names in os.walk(p):
                files += [os.path.join(base, n) for n in sorted(names)]
        else:
            files.append(p)
    files = [f for f in files if f.lower().endswith(ARCHIVES + IMAGES) or os.path.isdir(f)]
    named = set()
    for f in files:
        if f.lower().endswith(".cue"):
            named |= {os.path.abspath(os.path.join(os.path.dirname(f), n)) for n in fbexe_cue_files(f)}
    return [f for f in files if os.path.abspath(f) not in named]


def has_program(path: str) -> bool:
    """A CD image with a Firebeat program on it."""
    try:
        with fbexe.Disc(path) as disc:
            fbexe.find_exe(fbexe.Iso9660(disc))
        return True
    except Exception:
        return False


def image_kind(path: str) -> str:
    low = path.lower()
    if low.endswith((".img", ".raw")):
        return "hd"
    if low.endswith(".iso") and os.path.getsize(path) > 900_000_000:
        return "dvd"
    return "cd"


def ingest_auto(args, inputs: list[str]) -> int:
    games = all_games()
    files = gather(inputs, {g["mame"] for g in games if g.get("mame")})
    romsets = [f for f in files if f.lower().endswith(ARCHIVES) or os.path.isdir(f)]
    if args.romset:
        romsets.insert(0, args.romset)
    chds = [f for f in files if f.lower().endswith(".chd")]
    images = [f for f in files if f.lower().endswith(IMAGES) and not f.lower().endswith(".chd")]
    if not romsets and not chds and not images:
        print("ERROR    nothing to ingest: give ROM sets (.zip/.7z), CHDs or disc images")
        return 1

    # the game, when a set or a CHD names it
    by_set = {g["mame"]: g for g in games if g.get("mame")}
    hint = None
    romset_for = None
    for r in romsets:
        name = os.path.basename(os.path.normpath(r))
        name = name if os.path.isdir(r) else name.rsplit(".", 1)[0]
        if name in by_set:
            hint = by_set[name]
            romset_for = r
    if hint is None:
        for c in chds:
            stem = os.path.splitext(os.path.basename(c))[0]
            for g in games:
                if stem in (g.get("hdd", {}).get("chd"), g.get("cd", {}).get("chd")):
                    hint = g
    if hint:
        print(f"game     {hint['id']}: {hint['title']} (from {'the set' if romsets else 'the CHD name'})")

    # CHDs: extracted next to the game's other data (or set aside until the program says which game)
    dest = os.path.join(args.out, hint["id"] if hint else INCOMING)
    media = []   # (path, kind)
    for c in chds:
        try:
            p, kind = chd.extract(c, dest)
        except chd.ChdError as e:
            print(f"ERROR    {e}")
            return 1
        print(f"image    {os.path.relpath(p, ROOT) if p.startswith(ROOT) else p} ({kind}, from {os.path.basename(c)})")
        media.append((p, kind))
    for f in images:
        media.append((f, image_kind(f)))
    for flag, kind in (("hdd", "hd"), ("dvd", "dvd"), ("cd2", "cd2")):
        if getattr(args, flag):
            media.append((getattr(args, flag), kind))

    # Firebeat: a program CD decides the game
    cds = [p for p, k in media if k == "cd"]
    program = next((p for p in cds if has_program(p)), None)
    if program:
        args.disc = program
        args.romset = romsets[0] if romsets else None
        args.hdd = args.hdd or next((p for p, k in media if k == "hd"), None)
        args.dvd = args.dvd or next((p for p, k in media if k == "dvd"), None)
        args.cd2 = args.cd2 or next((p for p in cds if p != program), None)
        rc = ingest_firebeat(args)
        try:
            os.rmdir(os.path.join(args.out, INCOMING))   # only if empty
        except OSError:
            pass
        return rc
    if hint is None:
        print("ERROR    which game is this? No program disc, and no games/*/game.toml has this set or CHD name")
        for p, k in media:
            print(f"         {k:4} {p}")
        return 1
    core = hint.get("core")
    args.romset = romset_for or (romsets[0] if romsets else None)
    if core == "djmain":
        args.hdd = args.hdd or next((p for p, k in media if k == "hd"), None)
        return ingest_djmain(args)
    if core == "sys573":
        args.cd = next((p for p in cds), None)
        return ingest_sys573(args)
    print(f"ERROR    {hint['id']} is a Firebeat game: its program CD is needed (none of the images holds one)")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="*", help="ROM sets, CHDs, disc/disk images, or folders of them")
    ap.add_argument("--exe", help="a loose HIKARU.EXE / FIREBEAT.EXE (Firebeat)")
    ap.add_argument("--out", default=os.path.join(ROOT, "dumps"), help="output root (default: dumps/)")
    ap.add_argument("--check", metavar="RAW", help="compare the unpacked image to a known-good raw dump")
    ap.add_argument("--romset", metavar="ZIP", help="MAME ROM set (.zip/.7z) to copy into dumps/<game>/rom/")
    ap.add_argument("--hdd", metavar="IMG", help="raw HDD image: on the SPU's ATA bus (beatmania III), or DJ Main's IDE disk")
    ap.add_argument("--dvd", metavar="ISO", help="DVD image on the SPU's ATA bus (pop'n music)")
    ap.add_argument("--cd2", metavar="CUE", help="second disc on the main ATA bus (ParaParaParadise audio CD)")
    args = ap.parse_args()
    args.disc = None
    if args.exe:
        return ingest_firebeat(args)
    if not args.inputs and not args.romset:
        ap.error("give the game's files (ROM set, CHDs, images), or --exe")
    return ingest_auto(args, args.inputs)


if __name__ == "__main__":
    sys.exit(main())
