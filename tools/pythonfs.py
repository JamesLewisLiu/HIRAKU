# SPDX-License-Identifier: GPL-3.0-or-later
"""Konami Python 1 boot-media header ("PythonFS") reader + Konami LZ decoder.

The same header appears at byte 0 of a raw HDD image and at byte 0 of the single
file inside the CF card's FAT16 partition. Three boot records, each stored twice:
  A @0x20 (big-endian u32s)    : start, end (256-byte units), 0, 0, comp_size, sum, flags?, ?
  B @0xa0 (little-endian u32s) : start, end, 0, 0, comp_size, raw_size, sum, 0
  C @0x100 (little-endian u32s): start, end/copy2, copy3, 0, comp_size, sum, load_addr, entry
Observed: A = TMPR3927 (big-endian MIPS) sub-board program, B = IOPRP image,
C = PS2 EE program (load 0x100000 / entry 0x100008 on most titles).

LZ: flag byte, bits LSB-first, 1 = literal byte, 0 = 2-byte token b0 b1:
    length = (b0 & 0x0f) + 3, distance = ((b0 >> 4) << 8) | b1  (back-reference,
    reads before the start of output return 0).

Opens a raw HDD image, a CHD (via tools/chdread.py) or a CF card: an MBR + FAT16
partition holding one image file, which is read through its cluster chain.
Record A's checksum is the sum of its compressed bytes as big-endian u16 words;
B's and C's are not simple sums (the START loader on the memory card checks them).
Checked: pop'n 9's decoded C equals Reliquary's EE RAM at 0x100000 in all its code.

usage: pythonfs.py <image.chd|raw image|CF card image> [outdir]
"""
import os, struct, sys, hashlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def lz_decode(src, maxout=1 << 26):
    out = bytearray()
    pos = 0
    cnt = 0
    flags = 0
    n = len(src)
    while pos < n and len(out) < maxout:
        if cnt == 0:
            flags = src[pos]
            pos += 1
            cnt = 8
            continue
        bit = flags & 1
        flags >>= 1
        cnt -= 1
        if bit:
            if pos >= n:
                break
            out.append(src[pos])
            pos += 1
        else:
            if pos + 1 >= n:
                break
            b0, b1 = src[pos], src[pos + 1]
            pos += 2
            ln = (b0 & 0x0F) + 3
            d = ((b0 >> 4) << 8) | b1
            for _ in range(ln):
                out.append(out[-d] if 0 < d <= len(out) else 0)
    return bytes(out)


class _File:
    def __init__(self, p):
        self.f = open(p, 'rb')

    def read(self, o, n):
        self.f.seek(o)
        return self.f.read(n)


class _FatFile:
    """The one file in a CF card's FAT16 partition, as a byte space (cluster chain mapped)."""

    def __init__(self, img):
        mbr = img.read(0, 512)
        part = struct.unpack_from('<I', mbr, 0x1C6)[0] * 512
        bpb = img.read(part, 512)
        bps, spc, rsv, nfat, nroot, _, _, spf = struct.unpack_from('<HBHBHHBH', bpb, 11)
        fat_at = part + rsv * bps
        root_at = fat_at + nfat * spf * bps
        data_at = root_at + nroot * 32
        csize = spc * bps
        fat = img.read(fat_at, spf * bps)
        for i in range(nroot):
            e = img.read(root_at + 32 * i, 32)
            if e[0] in (0, 0xE5) or e[11] & 0x18:      # free, deleted, volume label or directory
                continue
            cl, size = struct.unpack_from('<HI', e, 26)
            if size:
                break
        else:
            raise ValueError('no file in the FAT16 root')
        self.name = e[:8].decode('ascii', 'replace').strip() + '.' + e[8:11].decode('ascii', 'replace').strip()
        self.img, self.csize, self.size, self.runs = img, csize, size, []
        while 2 <= cl < 0xFFF8:
            self.runs.append(data_at + (cl - 2) * csize)
            cl = struct.unpack_from('<H', fat, 2 * cl)[0]

    def read(self, o, n):
        out = bytearray()
        while n > 0 and o < self.size:
            c, off = divmod(o, self.csize)
            take = min(n, self.csize - off, self.size - o)
            out += self.img.read(self.runs[c] + off, take)
            o += take
            n -= take
        return bytes(out)


def open_image(path):
    """A Python 1 disk: raw HDD image, CHD, or CF card (MBR + FAT16 holding one image file)."""
    if path.lower().endswith('.chd'):
        from chdread import Chd
        img = Chd(path)
    else:
        img = _File(path)
    s0 = img.read(0, 512)
    if s0[510:512] == b'\x55\xaa' and s0[0x1C2] in (0x04, 0x06, 0x0E):
        return _FatFile(img)
    return img


def records(img):
    h = img.read(0, 0x200)
    A = struct.unpack_from('>8I', h, 0x20)
    B = struct.unpack_from('<8I', h, 0xA0)
    C = struct.unpack_from('<8I', h, 0x100)
    return {'A': (A[0] * 256, A[4], A), 'B': (B[0] * 256, B[4], B), 'C': (C[0] * 256, C[4], C)}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    img = open_image(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else None
    for name, (off, size, raw) in records(img).items():
        comp = img.read(off, size)
        dec = lz_decode(comp)
        print(f"{name}: offset={off:#x} comp={size:#x} raw_fields={[hex(x) for x in raw]} "
              f"-> dec={len(dec):#x} sha1={hashlib.sha1(dec).hexdigest()} head={dec[:16].hex()}")
        if out:
            os.makedirs(out, exist_ok=True)
            open(os.path.join(out, f"rec{name}.bin"), 'wb').write(dec)


if __name__ == '__main__':
    main()
