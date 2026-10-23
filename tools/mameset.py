# SPDX-License-Identifier: GPL-3.0-or-later
"""MAME's BEMANI sets as data: which ROMs and disks each set needs, and which set a file is.

The data, tools/data/mame-bemani.json, is made from MAME's -listxml (`--refresh`): every
machine of the BEMANI boards' drivers, the BIOS sets they hang from, and the devices whose
ROMs they need (the multi-session unit, the network unit). ROMs keep name, size and CRC;
disks keep name and SHA-1 (as MAME's, the CHD header's). Each set's "sha1" has its ROMs' SHA-1s,
by name (the launcher's checklist of a game's files, tools/checklist.py).

Sets go by their current MAME names, as games/*/game.toml does. An older MAME's set (the
owner's archive is 0.243) can carry an older name, so a set is told by what it holds, not
only by its name: `identify()` takes a zip, 7z or folder and names the set whose own ROMs it
has. A CHD is told by its header's SHA-1 (`disk_owner()`).

  tools/mameset.py --refresh LISTXML [KSYS573_CPP]
                                         remake the data from `mame -listxml` output (and the
                                         System 573 machine configs from MAME's ksys573.cpp)
  tools/mameset.py FILE...               which set each file is
"""
from __future__ import annotations

import json
import os
import re
import struct
import subprocess
import sys
import zipfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "tools", "data", "mame-bemani.json")

# MAME driver source -> HIRAKU's name for the board
BOARDS = {
    "konami/djmain.cpp": "djmain",
    "konami/firebeat.cpp": "firebeat",
    "konami/twinkle.cpp": "twinkle",
    "konami/ksys573.cpp": "sys573",
    "konami/konamigs.cpp": "konamigs",
    "konami/kpython.cpp": "python",
    "konami/kpython2.cpp": "python2",
}

# Titles that make a set BEMANI (the rest of these boards: fishing, Bishi Bashi, soccer, ...)
BEMANI = re.compile(r"beatmania|pop'?n|dance dance|dancing stage|ddr|guitar ?freaks|drum ?mania|"
                    r"percussion freaks|dance (maniax|freaks)|mambo|martial beat|keyboard(mania|heaven)|"
                    r"parapara|hiphopmania|beatstage|toy's march|dance 86|punch mania|fighting mania",
                    re.I)


# ---------------------------------------------------------------- the data

def ksys573_configs(path: str) -> dict[str, dict]:
    """Each System 573 set's machine config as MAME's ksys573.cpp builds it, flattened: the I/O
    board, the cassettes, the PC Cards, the memory card reader, the multi-session unit, the
    output callback and the init. {set: {...}}"""
    src = open(path).read()
    funcs = {}
    for m in re.finditer(r"^void \w+::(\w+)\(machine_config &config\)\s*\{(.*?)^\}", src, re.M | re.S):
        funcs[m.group(1)] = m.group(2)
    cass = {"cassx": ("x", None), "cassxi": ("xi", None), "cassy": ("y", None), "cassyi": ("yi", None),
            "cassyyi": ("yi", "yi"), "cassxzi": ("zi", "xi"), "casszi": ("zi", None)}

    def walk(name, c, seen):
        if name in seen or name not in funcs:
            return
        seen.add(name)
        body = funcs[name]
        for m in re.finditer(r"(\w+)\(config\b|output_callback\(\)\.set\(FUNC\(\w+::(\w+)\)", body):
            call, cb = m.group(1), m.group(2)
            if cb:
                c["output"] = cb
            elif call in ("k573a", "k573d", "k573k"):
                c["io_board"] = {"k573a": "analog", "k573d": "digital", "k573k": "karaoke"}[call]
                walk(call, c, seen)
            elif call in cass:
                c["cassette"], c["install_cassette"] = cass[call]
            elif call in ("pccard1_16mb", "pccard1_32mb"):
                c["pccard1"] = call.split("_")[1]
            elif call == "pccard2_32mb":
                c["pccard2"] = "32mb"
            elif call == "KONAMI_573_MEMORY_CARD_READER":
                c["memory_card_reader"] = True
            elif call == "KONAMI_573_MULTI_SESSION_UNIT":
                c["msu"] = True
            elif call in funcs:
                walk(call, c, seen)

    out = {}
    for m in re.finditer(r"^GAME\(\s*\d+,\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),", src, re.M):
        name, config, inputs, init = m.group(1), m.group(3), m.group(4), m.group(6)
        c = {"config": config, "inputs": inputs, "init": init, "cassette": "x"}   # konami573() adds cassx()
        walk(config, c, set())
        if c.get("install_cassette") is None:
            c.pop("install_cassette", None)
        out[name] = c
    return out


def refresh(listxml: str, out: str = DATA, ksys573: str | None = None) -> int:
    """Remakes the data from MAME's -listxml output (a file), and System 573 configs from
    MAME's ksys573.cpp when given."""
    import xml.etree.ElementTree as ET
    machines: dict[str, dict] = {}
    build = ""
    for ev, el in ET.iterparse(listxml, events=("start", "end")):
        if ev == "start" and el.tag == "mame":
            build = el.get("build", "")
        if ev != "end" or el.tag != "machine":
            continue
        m = {
            "source": el.get("sourcefile", ""),
            "parent": el.get("cloneof"),
            "romof": el.get("romof"),
            "title": (el.findtext("description") or "").strip(),
            "year": el.findtext("year") or "",
        }
        if el.get("isbios") == "yes":
            m["bios"] = True
        if el.get("isdevice") == "yes":
            m["device"] = True
        d = el.find("driver")
        if d is not None:
            m["status"] = d.get("status", "")
        bioses = el.findall("biosset")   # the default is marked, else it is the first
        m["_default_bios"] = next((b.get("name") for b in bioses if b.get("default") == "yes"),
                                  bioses[0].get("name") if bioses else None)
        roms = []
        for r in el.findall("rom"):
            if r.get("status") == "nodump" or not r.get("crc"):
                continue
            roms.append((r.get("bios"), [r.get("name"), int(r.get("size", 0)), r.get("crc")] +
                         ([r.get("merge")] if r.get("merge") else [])))
            if r.get("region"):
                m.setdefault("regions", {})[r.get("name")] = r.get("region")
            if r.get("sha1"):
                m.setdefault("sha1", {})[r.get("name")] = r.get("sha1")
        m["_roms"] = roms
        disks = []
        for k in el.findall("disk"):
            if not k.get("sha1"):
                continue
            e = [k.get("name"), k.get("sha1"), k.get("region", "")]
            if k.get("merge"):
                e.append(k.get("merge"))
            disks.append(e)
        if disks:
            m["disks"] = disks
        refs = sorted({x.get("name") for x in el.findall("device_ref")})
        if refs:
            m["refs"] = refs
        machines[el.get("name")] = m
        el.clear()

    # ROMs of a BIOS choice other than the default are left out; the default may be the BIOS set's
    for n, m in machines.items():
        default, r = m.pop("_default_bios"), m.get("romof")
        while default is None and r in machines:
            default = machines[r].get("_default_bios")
            r = machines[r].get("romof")
        roms = [e for b, e in m.pop("_roms") if not (b and default and b != default)]
        if roms:
            m["roms"] = roms
    keep = {n for n, m in machines.items() if m["source"] in BOARDS and not m.get("device")}
    # their BIOS sets, and the devices (of any source) that carry ROMs
    for n in list(keep):
        r = machines[n].get("romof")
        while r and r not in keep:
            keep.add(r)
            r = machines[r].get("romof")
    devices = set()
    for n in keep:
        for ref in machines[n].get("refs", []):
            if ref in machines and machines[ref].get("roms"):
                devices.add(ref)
    k573 = ksys573_configs(ksys573) if ksys573 else {}
    sets = {}
    for n in sorted(keep | devices):
        m = dict(machines[n])
        m["refs"] = [r for r in m.get("refs", []) if r in devices]
        if not m["refs"]:
            del m["refs"]
        m["board"] = BOARDS.get(m["source"], "")
        if n in k573:
            m["k573"] = k573[n]
        sets[n] = m
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as fh:
        fh.write('{"mame": %s,\n "sets": {\n' % json.dumps(build))
        fh.write(",\n".join(f"  {json.dumps(n)}: {json.dumps(m, separators=(',', ':'))}"
                            for n, m in sets.items()))
        fh.write("\n}}\n")
    print(f"{out}: {len(sets)} sets ({len(devices)} devices) from MAME {build}")
    return 0


_data = None
_build = ""


def load() -> dict:
    """{set name: machine}; see refresh() for the fields."""
    global _data, _build
    if _data is None:
        with open(DATA) as fh:
            d = json.load(fh)
        _data, _build = d["sets"], d.get("mame", "")
    return _data


def load_build() -> str:
    """The MAME version the data came from ("0.289")."""
    load()
    return _build.split()[0] if _build else "?"


def is_bemani(name: str) -> bool:
    m = load()[name]
    return bool(m.get("board")) and not m.get("bios") and bool(BEMANI.search(m["title"]))


def chain(name: str) -> list[str]:
    """The set, then the sets MAME also looks in for its ROMs: its romof chain."""
    sets = load()
    out = [name]
    while sets[out[-1]].get("romof"):
        out.append(sets[out[-1]]["romof"])
    return out


def needed_roms(name: str) -> list[tuple[str, int, str]]:
    """Every ROM the set needs (its own, its parent's, its BIOS's, its devices'): (name, size, crc)."""
    sets = load()
    out = [(r[0], r[1], r[2]) for r in sets[name].get("roms", [])]
    seen = set()
    todo = list(sets[name].get("refs", []))
    for p in chain(name)[1:]:
        todo += sets[p].get("refs", [])
    while todo:
        d = todo.pop()
        if d in seen or d not in sets:
            continue
        seen.add(d)
        out += [(r[0], r[1], r[2]) for r in sets[d].get("roms", [])]
        todo += sets[d].get("refs", [])
    return out


def needed_disks(name: str) -> list[tuple[str, str, str]]:
    """The disks the set needs: (name, sha1, region)."""
    return [(d[0], d[1], d[2]) for d in load()[name].get("disks", [])]


def own_roms(name: str) -> list[tuple[str, int, str]]:
    """The ROMs only this set has (not merged from a parent or BIOS): what a split set's archive holds."""
    return [(r[0], r[1], r[2]) for r in load()[name].get("roms", []) if len(r) < 4]


# ---------------------------------------------------------------- files

def archive_crcs(path: str) -> dict[str, tuple[int, str]]:
    """{member name: (size, crc)} of a zip, 7z or folder of ROMs, without reading the data of
    an archive (zip and 7z keep CRCs in their directory)."""
    out = {}
    if os.path.isdir(path):
        for f in sorted(os.listdir(path)):
            p = os.path.join(path, f)
            if not os.path.isfile(p) or f.lower().endswith(".chd") or os.path.getsize(p) > 64 << 20:
                continue
            crc = 0
            with open(p, "rb") as fh:
                while chunk := fh.read(1 << 20):
                    crc = zlib.crc32(chunk, crc)
            out[f] = (os.path.getsize(p), f"{crc:08x}")
    elif path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            for i in z.infolist():
                if not i.is_dir():
                    out[os.path.basename(i.filename)] = (i.file_size, f"{i.CRC:08x}")
    elif path.lower().endswith(".7z"):
        sz = seven_zip()
        text = subprocess.run([sz, "l", "-slt", path], capture_output=True, text=True).stdout if sz else ""
        for block in text.split("\n\n"):
            f = dict(re.findall(r"^(\w[\w ]*?) = (.*)$", block, re.M))
            if f.get("Path") and f.get("CRC") and f.get("Folder") != "+":
                out[os.path.basename(f["Path"])] = (int(f.get("Size", 0)), f["CRC"].lower())
    return out


def seven_zip() -> str | None:
    """The 7z program: a package's own (bin/7zz on Linux, bin/7zr.exe on Windows: tools/package.sh,
    tools/package-win.sh), on the PATH, else where 7-Zip installs itself on Windows."""
    import shutil
    here = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "bin")
    for name in ("7zz", "7zr.exe", "7za.exe", "7z.exe"):
        if os.path.isfile(os.path.join(here, name)):
            return os.path.join(here, name)
    for name in ("7z", "7za", "7zz"):
        if p := shutil.which(name):
            return p
    for base in (os.environ.get("ProgramFiles", ""), os.environ.get("ProgramFiles(x86)", "")):
        p = os.path.join(base, "7-Zip", "7z.exe")
        if base and os.path.isfile(p):
            return p
    return None


def chd_sha1(path: str) -> str | None:
    """The SHA-1 MAME knows a CHD by (data and metadata), from its header: v3, v4 or v5."""
    with open(path, "rb") as fh:
        h = fh.read(124)
    if len(h) < 16 or h[:8] != b"MComprHD":
        return None
    version = struct.unpack(">I", h[12:16])[0]
    at = {3: 80, 4: 48, 5: 84}.get(version)
    return h[at:at + 20].hex() if at and len(h) >= at + 20 else None


def identify(path: str) -> list[tuple[str, int, int]]:
    """Which sets an archive or folder holds the own ROMs of, best first:
    [(set, own ROMs found by CRC, own ROMs in all)].

    A ROM many sets share (a DS2401 serial, a BIOS) says little, so each own ROM counts by how
    few sets have it. A ROM found by name but with another CRC (an older MAME's dump of the same
    chip) counts for half: that is how an older MAME's set under an older name is still told."""
    files = archive_crcs(path)
    have = {(s, c) for s, c in files.values()}
    names = set(files)
    if not have:
        return []
    sets = load()
    owners: dict[tuple[int, str], int] = {}
    for n in sets:
        for _, s, c in own_roms(n):
            owners[(s, c)] = owners.get((s, c), 0) + 1
    scored = []
    for n, m in sets.items():
        own = own_roms(n)
        if not own:
            continue
        crc_hits = sum(1 for _, s, c in own if (s, c) in have)
        score = sum(1 / owners[(s, c)] if (s, c) in have else 0.5 / owners[(s, c)] if r in names else 0
                    for r, s, c in own)
        if score:
            game = not (m.get("bios") or m.get("device"))
            # a set with all its own ROMs here before one with some: a non-merged clone's archive
            # also holds most of its parent's
            scored.append((not game, crc_hits < len(own), -score, n, crc_hits, len(own)))
    scored.sort()
    return [(n, h, t) for *_, n, h, t in scored]


def current_name(name: str, path: str) -> str | None:
    """The current MAME name of the set an archive (or folder) named `name` holds, or None.

    Its own name, unless it isn't a current set, or it holds all of a sibling's own ROMs but not
    all of its namesake's (an older MAME called the JA-B bmcompmx what is now bmcompmxb)."""
    sets = load()
    ids = identify(path)
    if not ids:
        return name if name in sets else None
    best, hits, total = ids[0]
    if name in sets:
        mine = next((h for n, h, t in ids if n == name), 0)
        kin = (sets[best].get("parent") or best) == (sets[name].get("parent") or name)
        if best != name and kin and hits == total and mine < len(own_roms(name)):
            return best
        return name
    # most of the archive must be the candidate's: a BIOS of another board (kviper) or a set of
    # another board (ppp2nd) shares a stray chip with some set at most
    files = archive_crcs(path)
    roms = needed_roms(best)
    needed = {(s, c) for _, s, c in roms} | {f for f, _, _ in roms}
    explained = sum(1 for f, sc in files.items() if sc in needed or f in needed)
    return best if explained * 2 > len(files) else None


_disk_index = None


def disk_owner(sha1: str) -> list[tuple[str, str]]:
    """The sets (and disk names) that need a disk with this SHA-1."""
    global _disk_index
    if _disk_index is None:
        _disk_index = {}
        for n, m in load().items():
            for d in m.get("disks", []):
                _disk_index.setdefault(d[1], []).append((n, d[0]))
    return _disk_index.get(sha1, [])


def main() -> int:
    args = sys.argv[1:]
    if args[:1] == ["--refresh"] and len(args) in (2, 3):
        return refresh(args[1], ksys573=args[2] if len(args) == 3 else None)
    if not args or args[0].startswith("-"):
        print(__doc__)
        return 2
    for p in args:
        if p.lower().endswith(".chd"):
            s = chd_sha1(p)
            owners = disk_owner(s) if s else []
            print(f"{p}: " + (", ".join(f"{n} ({d})" for n, d in owners) if owners else f"unknown disk {s}"))
        else:
            ids = identify(p)
            best = [f"{n} ({h}/{t})" for n, h, t in ids[:3]]
            print(f"{p}: " + (", ".join(best) if best else "no known ROMs"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
