#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The file system of Konami Python 1 game disks (pop'n music 14 and its family): list, extract,
and its pictures to and from PNG (for mods: docs/MODS.md).

    tools/popnfs.py IMAGE list                   every file: name (or #hash), sector, size, form
    tools/popnfs.py IMAGE extract NAME... OUT    files as the game uses them, into folder OUT
    tools/popnfs.py IMAGE extract --all OUT      every named file (and #hash.bin for the others)
    tools/popnfs.py IMAGE topng NAME OUT.png     a picture (.gzz) as a PNG
    tools/popnfs.py IMAGE frompng NAME IN.png OUT   a PNG (edited from topng's) made into that
                                                 picture, ready for a mod's [[file]] (needs Pillow)

IMAGE: the disk (a CHD or raw image; tools/pythonfs.py opens it). NAME: a path as the game names
it (image/kc/kc_001.gzz), or #hash for a file whose name isn't known.

The disk (worked out from the loader, boot record C, and the game's program):
  - the directory: 0x1f8 sectors at sector 0x4800, entries of six little-endian u32s: name hash,
    first sector, size in bytes, 0, the sum of the file's bytes, 0. It ends at the first entry
    whose sector, size and third word are 0; that entry's fifth word is the sum of all the bytes
    before it. The loader looks names up by a linear search. (Sector 0x4a00 holds a second one,
    for the names under disk/write/: the game's own saves.)
  - a name's hash: a CRC (polynomial 0x04c11db7, no reflection, start 0) of the low six bits of
    each character, least significant first; case doesn't count. The game opens disk0:image/...,
    the loader looks up image/... (with any "./" taken out).
  - a file is either encrypted (a 4-byte tag such as 13G0, its plain length as a u32, then
    Blowfish in CBC mode, the key and IV picked by the tag from a table in the loader; the IV's
    two words are stored little-endian) or not; the game decrypts when the tag is one it knows.
    What's left is, for pictures and the rest the game reads through its loader (FUN_01730418),
    a u32 unpacked size and an LZSS stream (4 KB window from 0xfee, flag bits LSB first, 1 = a
    literal, 0 = 2 bytes: offset low 8 | (high nibble << 4), length = low nibble + 3); a
    folder's system.idx is stored (its size as a u32, then the bytes); the IOP modules are ELF
    files as they are; ee/boot/game.bin is the program (encrypted, not packed).
The keys are read from the disk's own loader; nothing of the game's is in this file.

Pictures (.gzz): "GC" and two zero bytes, the total size (u32, big-endian), 0, width and height
(u16 BE), 1, the data's size (u32 BE), then the pixels from the top left: 16-bit (A1 B5 G5 R5,
little-endian) when the data is width x height x 2 bytes, else 8-bit indices into the folder's
palette, the last 1 KB of its system.idx: 256 RGBA colours (alpha 0x80 = opaque) in the GS's
order (in every 32, entries 8-15 and 16-23 swapped). The 8-bit folders (92 of them, the 256_*
ones among them) share that one palette; frompng keeps it and takes the nearest of its colours.

Names: the folders are the game's (image/<folder>/system.idx lists the folder's files); the
names come from the strings of the game's program and of the index files on the same disk, so a
few files stay #hash.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pythonfs  # noqa: E402

DIR_SECTOR, DIR_SECTORS = 0x4800, 0x1F8


def name_hash(name):
    v = 0
    for c in name.lower().replace("./", "").lstrip("/").encode():
        for i in range(6):
            v = (((v << 1) | ((c >> i) & 1)) ^ (0x04C11DB7 if v & 0x80000000 else 0)) & 0xFFFFFFFF
    return v


def lzss_decode(src, n):
    """n bytes out; a stream that ends early (some of the disk's largest pictures stop a few
    bytes short of their size: the game decodes on into what follows) is padded with zeros"""
    win = bytearray(4096)
    r, i, flags, out = 0xFEE, 0, 0, bytearray()
    try:
        while len(out) < n:
            flags >>= 1
            if not flags & 0x100:
                flags = src[i] | 0xFF00
                i += 1
            if flags & 1:
                c = src[i]
                i += 1
                out.append(c)
                win[r] = c
                r = (r + 1) & 0xFFF
            else:
                o, ln = src[i] | ((src[i + 1] & 0xF0) << 4), (src[i + 1] & 0x0F) + 3
                i += 2
                for k in range(ln):
                    c = win[(o + k) & 0xFFF]
                    out.append(c)
                    win[r] = c
                    r = (r + 1) & 0xFFF
    except IndexError:
        pass
    return bytes(out[:n]) + bytes(max(0, n - len(out)))


class Blowfish:
    """Blowfish (CBC decryption only), its starting tables taken from the loader: enough for one
    file at a time; `cryptography` is used instead when it's installed."""

    def __init__(self, key, p, s):
        self.p = list(p)
        self.s = [list(s[i * 256:(i + 1) * 256]) for i in range(4)]
        for i in range(18):
            k = 0
            for j in range(4):
                k = (k << 8) | key[(i * 4 + j) % len(key)]
            self.p[i] ^= k
        l = r = 0
        for i in range(0, 18, 2):
            l, r = self._enc(l, r)
            self.p[i], self.p[i + 1] = l, r
        for b in range(4):
            for i in range(0, 256, 2):
                l, r = self._enc(l, r)
                self.s[b][i], self.s[b][i + 1] = l, r

    def _f(self, x):
        s = self.s
        return ((((s[0][x >> 24] + s[1][(x >> 16) & 255]) & 0xFFFFFFFF) ^ s[2][(x >> 8) & 255]) + s[3][x & 255]) & 0xFFFFFFFF

    def _enc(self, l, r):
        for i in range(16):
            l ^= self.p[i]
            r ^= self._f(l)
            l, r = r, l
        l, r = r, l
        return l ^ self.p[17], r ^ self.p[16]

    def _dec(self, l, r):
        for i in range(17, 1, -1):
            l ^= self.p[i]
            r ^= self._f(l)
            l, r = r, l
        l, r = r, l
        return l ^ self.p[0], r ^ self.p[1]

    def decrypt_cbc(self, data, iv):
        out = bytearray()
        pl, pr = struct.unpack(">II", iv)
        for at in range(0, len(data) - 7, 8):
            cl, cr = struct.unpack_from(">II", data, at)
            l, r = self._dec(cl, cr)
            out += struct.pack(">II", l ^ pl, r ^ pr)
            pl, pr = cl, cr
        return bytes(out)


class Disk:
    def __init__(self, path):
        self.img = pythonfs.open_image(path)
        d = self.img.read(DIR_SECTOR * 512, DIR_SECTORS * 512)
        self.entries = []
        for at in range(0, len(d) - 23, 24):
            e = struct.unpack_from("<6I", d, at)
            if e[1] == 0 and e[2] == 0 and e[3] == 0:
                if sum(d[:at]) & 0xFFFFFFFF != e[4]:
                    raise ValueError("the directory at sector 0x%x doesn't add up: not a disk this knows" % DIR_SECTOR)
                break
            self.entries.append(e)
        else:
            raise ValueError("no end to the directory at sector 0x%x" % DIR_SECTOR)
        self.by_hash = {e[0]: e for e in self.entries}
        self._keys = None
        self.names = {}

    def raw(self, e):
        return self.img.read(e[1] * 512, e[2])

    def keys(self):
        """tag -> (key, iv), and the Blowfish tables, from the loader (boot record C)"""
        if self._keys is None:
            off, size, rec = pythonfs.records(self.img)["C"]
            ldr = pythonfs.lz_decode(self.img.read(off, size))
            t = ldr.find(b"13G0\x0013S0\x00")
            if t < 0:
                raise ValueError("no key table in the loader")
            self._keys = {}
            for i in range(40):
                tag = ldr[t + 5 * i:t + 5 * i + 4]
                k = ldr[t + 0xC8 + 0x101 * i:t + 0xC8 + 0x101 * (i + 1)].split(b"\0")[0]
                iv = ldr[t + 0x28F0 + 8 * i:t + 0x28F0 + 8 * (i + 1)]
                self._keys[tag] = (k, iv[3::-1] + iv[7:3:-1])
            p0, s0 = ldr.find(struct.pack("<I", 0x243F6A88)), ldr.find(struct.pack("<I", 0xD1310BA6))
            # the loader keeps each word of the tables in 8 bytes
            self._bf = (struct.unpack_from("<36I", ldr, p0)[::2], struct.unpack_from("<2048I", ldr, s0)[::2])
        return self._keys

    def decrypt(self, b, whole=False):
        """(the file's bytes with the encryption taken off, whether it was encrypted): as long as
        the plain length in its header says, or with whole, all the blocks (the game unpacks from
        all of them, and the LZSS of some files at the size limit runs past that length)"""
        keys = self.keys()
        if b[:4] not in keys:
            return b, False
        key, iv = keys[b[:4]]
        n = struct.unpack_from("<I", b, 4)[0]
        body = b[8:8 + (len(b) - 8) // 8 * 8]
        try:
            from cryptography.hazmat.decrepit.ciphers.algorithms import Blowfish as BF
            from cryptography.hazmat.primitives.ciphers import Cipher, modes
            plain = Cipher(BF(key), modes.CBC(iv)).decryptor().update(body)
        except ImportError:
            plain = Blowfish(key, *self._bf).decrypt_cbc(body, iv)
        return (plain if whole else plain[:n]), True

    def form(self, e, head=None):
        """lzss, stored, program or raw: how the game reads the file"""
        b = head if head is not None else self.img.read(e[1] * 512, min(e[2], 16))
        if b[:4] == b"13P0":
            return "program"
        if b[:4] in self.keys():
            return "lzss"
        if len(b) >= 4 and struct.unpack_from("<I", b)[0] == e[2] - 4:
            return "stored"
        return "raw"

    def file(self, e):
        """the file as the game uses it"""
        b = self.raw(e)
        form = self.form(e, b[:16])
        plain, _ = self.decrypt(b, whole=form == "lzss")
        if form == "lzss":
            return lzss_decode(plain[4:], struct.unpack_from("<I", plain)[0])
        if form == "stored":
            return plain[4:4 + struct.unpack_from("<I", plain)[0]]
        return plain

    def find_names(self):
        """names for the files, from the program's strings and the folders' index files"""
        cand = {"ee/boot/game.bin"}
        prog = self.by_hash.get(name_hash("ee/boot/game.bin"))
        strings = []
        if prog:
            text = self.decrypt(self.raw(prog))[0]
            strings = [m.group().decode() for m in re.finditer(rb"[\x21-\x7e]{2,64}", text)]
        words = {s.lower() for s in strings}
        for s in strings:
            if s.startswith("disk0:") and "%" not in s:
                cand.add(s[6:].lower())
            m = re.fullmatch(r"(/[\w/]+?)%0?(\d?)d(\.\w+)", s)
            if m:
                for n in range(1000):
                    cand.add(("image%s%0*d%s" % (m.group(1), int(m.group(2) or 0), n, m.group(3))).lower())
            m = re.fullmatch(r"(/[\w/]+?)%s(\.\w+)", s)
            if m:
                cand |= {("image%s%s%s" % (m.group(1), w, m.group(2))).lower() for w in words}
        for w in words:
            cand.add("image/%s/system.idx" % w.strip("/"))
            cand.add("iop/modules/%s.irx" % w)
        found = {name_hash(n): n for n in cand if name_hash(n) in self.by_hash}
        for h, n in list(found.items()):
            if n.endswith("/system.idx"):
                for m in re.finditer(rb"/[\x21-\x7e]+?\.[A-Za-z0-9]+(?=\x00)", self.file(self.by_hash[h])):
                    nm = "image" + m.group().decode().lower()
                    if name_hash(nm) in self.by_hash:
                        found[name_hash(nm)] = nm
        self.names = found
        return found

    def lookup(self, name):
        h = int(name[1:], 16) if name.startswith("#") else name_hash(name)
        return self.by_hash.get(h)


def gc_header(f):
    if f[:4] != b"GC\0\0":
        raise ValueError("not a GC picture")
    w, h = struct.unpack_from(">HH", f, 12)
    size = struct.unpack_from(">I", f, 20)[0]
    if size == w * h * 2:
        return w, h, 16
    if size == w * h:
        return w, h, 8
    raise ValueError("a GC picture of %dx%d with %d bytes of data: not a kind this knows" % (w, h, size))


def gs_palette(idx):
    """the 256 colours of a folder's system.idx, by index, as RGBA (alpha 0..255)"""
    pal = idx[-1024:]
    out = []
    for i in range(256):
        j = (i & ~0x18) | ((i & 8) << 1) | ((i & 16) >> 1)
        r, g, b, a = pal[4 * j:4 * j + 4]
        out.append((r, g, b, min(255, a * 255 // 128)))
    return out


def gc_to_rgba(f, idx=None):
    """(width, height, RGBA bytes)"""
    w, h, bits = gc_header(f)
    px = f[24:]
    out = bytearray()
    if bits == 16:
        for (p,) in struct.iter_unpack("<H", px[:w * h * 2]):
            out += bytes(((p & 31) * 255 // 31, ((p >> 5) & 31) * 255 // 31, ((p >> 10) & 31) * 255 // 31, 255 if p >> 15 else 0))
    else:
        if idx is None:
            raise ValueError("an 8-bit picture: its colours are in its folder's system.idx")
        pal = [bytes(c) for c in gs_palette(idx)]
        for v in px[:w * h]:
            out += pal[v]
    return w, h, bytes(out)


def rgba_to_gc(like, rgba, idx=None):
    """a picture of the same size and kind as `like` (a GC picture) from RGBA bytes: 16-bit
    colours rounded to 5 bits, alpha on at 128 and up; 8-bit ones the nearest of the folder's
    palette (the palette itself unchanged)"""
    w, h, bits = gc_header(like)
    if len(rgba) != w * h * 4:
        raise ValueError("the picture is to be %dx%d" % (w, h))
    data = bytearray()
    if bits == 16:
        for i in range(0, len(rgba), 4):
            r, g, b, a = rgba[i:i + 4]
            data += struct.pack("<H", (r * 31 + 127) // 255 | (g * 31 + 127) // 255 << 5 | (b * 31 + 127) // 255 << 10 | (a >= 128) << 15)
    else:
        if idx is None:
            raise ValueError("an 8-bit picture: its colours are in its folder's system.idx")
        pal = gs_palette(idx)
        memo = {}
        for i in range(0, len(rgba), 4):
            c = rgba[i:i + 4]
            v = memo.get(c)
            if v is None:
                r, g, b, a = c
                if a < 8:   # transparent: any colour of the palette's that is (index 0 on the disks seen)
                    v = min(range(256), key=lambda k: (pal[k][3], k))
                else:
                    v = min(range(256), key=lambda k: (pal[k][0] - r) ** 2 * 3 + (pal[k][1] - g) ** 2 * 4 +
                            (pal[k][2] - b) ** 2 * 2 + (pal[k][3] - a) ** 2 * 3)
                memo[c] = v
            data.append(v)
    return like[:24] + bytes(data)


def main():
    a = sys.argv[1:]
    if len(a) < 2 or a[1] not in ("list", "extract", "topng", "frompng"):
        sys.exit(__doc__)
    disk = Disk(a[0])
    if a[1] in ("topng", "frompng"):
        if len(a) != (4 if a[1] == "topng" else 5):
            sys.exit(__doc__)
        from PIL import Image
        e = disk.lookup(a[2])
        if not e:
            sys.exit("%s: not on the disk" % a[2])
        f = disk.file(e)
        ie = disk.lookup(a[2].rsplit("/", 1)[0] + "/system.idx") if "/" in a[2] else None
        idx = disk.file(ie) if ie else None
        if a[1] == "topng":
            w, h, rgba = gc_to_rgba(f, idx)
            Image.frombytes("RGBA", (w, h), rgba).save(a[3])
        else:
            im = Image.open(a[3]).convert("RGBA")
            with open(a[4], "wb") as out:
                out.write(rgba_to_gc(f, im.tobytes(), idx))
        return
    names = disk.find_names()
    label = lambda e: names.get(e[0], "#%08x" % e[0])
    if a[1] == "list":
        rows = sorted(disk.entries, key=lambda e: (e[0] not in names, label(e)))
        for e in rows:
            print("%-48s sector 0x%07x %9d %s" % (label(e), e[1], e[2], disk.form(e)))
        print("%d files, %d named" % (len(rows), sum(e[0] in names for e in rows)), file=sys.stderr)
        return
    if len(a) < 4:
        sys.exit(__doc__)
    out = a[-1]
    wanted = disk.entries if a[2] == "--all" else []
    for n in ([] if a[2] == "--all" else a[2:-1]):
        e = disk.lookup(n)
        if not e:
            sys.exit("%s: not on the disk" % n)
        wanted.append(e)
    for e in wanted:
        n = label(e)
        path = os.path.join(out, *(n[1:] + ".bin" if n.startswith("#") else n).split("/"))
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        try:
            data = disk.file(e)
        except (IndexError, struct.error) as x:
            print("%s: can't unpack it (%s); written as it is on the disk" % (n, x), file=sys.stderr)
            data = disk.raw(e)
        with open(path, "wb") as f:
            f.write(data)
    print("%d files to %s" % (len(wanted), out), file=sys.stderr)


if __name__ == "__main__":
    main()
