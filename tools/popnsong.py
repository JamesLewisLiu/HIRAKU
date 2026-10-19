#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""pop'n music 14's songs on its disk (Konami Python 1): list the song slots, extract their parts.

    tools/popnsong.py IMAGE list              every slot in use: its number and parts' sizes
    tools/popnsong.py IMAGE extract SLOT OUT  a slot's parts into folder OUT, descrambled
                                              (the charts unpacked too)

The songs are not in the disk's file system (tools/popnfs.py) but in slots after it, worked out
from the game's program (FUN_017c0438, FUN_017c04a8, FUN_017c1be0):
  - slot n is the 20 MB from sector 0x100000 + n * 0xa000 (byte 0x20000000 + n * 0x1400000);
  - everything in it is scrambled: XOR with bits 5-12 of an LCG (state = slot + 13, then
    state = state * 0x5d588b65 + 13 before each byte), restarted at each read. Those bits repeat
    every 8 KB and every part starts on an 8 KB boundary, so it's one 8 KB pattern per slot;
  - its first 0x1ec bytes are its header: "LB13", the slot number, 12, then 12 parts of three
    u32s: first sector, size in bytes, a tag (the part's kind, three letters little-endian):
      0 the header, 1 "ele" the keysounds (SPU ADPCM), 2 "pwm" the keysounds' table (rate 44100),
      3-9 the charts ("np", "hp", "ep", "op", "bp", "xep", "xnp": a u32 size and LZSS as
      tools/popnfs.py; events of 8 bytes: time in ms, then a command and its data; a sheet a song
      lacks repeats its "np"), 10 "bt" the music (SPU ADPCM, streamed), 11 "pre" the music
      select's preview.
A slot whose charts are all one 2 KB placeholder isn't a playable song.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pythonfs  # noqa: E402
import popnfs  # noqa: E402

SLOT_BASE, SLOT_SIZE = 0x20000000, 0x1400000


def key(slot):
    """the slot's 8 KB XOR pattern"""
    s, out = (slot + 13) & 0xFFFFFFFF, bytearray()
    for _ in range(0x2000):
        s = (s * 0x5D588B65 + 13) & 0xFFFFFFFF
        out.append((s >> 5) & 0xFF)
    return bytes(out)


def descramble(data, k, at=0):
    """data read at offset `at` of a part (a multiple of nothing in particular)"""
    return bytes(b ^ k[(at + i) & 0x1FFF] for i, b in enumerate(data))


def tag(t):
    return t.to_bytes(4, "little").rstrip(b"\0").decode("latin-1")


def header(img, slot):
    """the slot's parts [(sector, size, tag)], or None when the slot holds no song"""
    base = SLOT_BASE + slot * SLOT_SIZE
    raw = img.read(base, 0x1EC)
    if len(raw) < 0x1EC:
        return None
    h = descramble(raw, key(slot))
    magic, n, count = struct.unpack_from("<4sII", h)
    if magic != b"LB13" or n != slot or count != 12:
        return None
    return [struct.unpack_from("<3I", h, 12 + 12 * i) for i in range(12)]


def disk_size(path):
    """the disk's size: a CHD's logical size (its v5 header's), else the file's"""
    with open(path, "rb") as f:
        h = f.read(40)
    if h[:8] == b"MComprHD":
        return struct.unpack_from(">Q", h, 32)[0]
    return os.path.getsize(path)


def slots(img, size):
    for slot in range((size - SLOT_BASE) // SLOT_SIZE):
        parts = header(img, slot)
        if parts:
            yield slot, parts


def main():
    a = sys.argv[1:]
    if len(a) < 2 or a[1] not in ("list", "extract"):
        sys.exit(__doc__)
    img = pythonfs.open_image(a[0])
    if a[1] == "list":
        size = disk_size(a[0])
        n = 0
        for slot, parts in slots(img, size):
            charts = [p[1] for p in parts[3:10]]
            kind = "placeholder" if len(set(charts)) == 1 and charts[0] == 0x87D else "song"
            print("slot %4d  %-11s keysounds %8d  music %8d  preview %7d  charts %s" %
                  (slot, kind, parts[1][1], parts[10][1], parts[11][1],
                   " ".join("%s:%d" % (tag(p[2]), p[1]) for p in parts[3:10])))
            n += 1
        print("%d slots" % n, file=sys.stderr)
        return
    if len(a) != 4:
        sys.exit(__doc__)
    slot, out = int(a[2], 0), a[3]
    parts = header(img, slot)
    if not parts:
        sys.exit("slot %d holds no song" % slot)
    k = key(slot)
    os.makedirs(out, exist_ok=True)
    for i, (sector, size, t) in enumerate(parts):
        data = descramble(img.read(sector * 512, size), k)
        name = "%02d_%s" % (i, tag(t) or "header")
        if 3 <= i <= 9:
            data = popnfs.lzss_decode(data[4:], struct.unpack_from("<I", data)[0])
        with open(os.path.join(out, name + ".bin"), "wb") as f:
            f.write(data)
    print("slot %d: 12 parts to %s" % (slot, out), file=sys.stderr)


if __name__ == "__main__":
    main()
