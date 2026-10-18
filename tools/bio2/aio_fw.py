#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# The board firmwares inside a game's libaio-iob.dll (beatmania IIDX 28's), decrypted, for reading
# offline: nothing here talks to a board. The layout, the start/end markers, the XOR chain and the
# AC_LZ (LZSS) step are from Feiryn's bio2-tools (MIT, github.com/Feiryn/bio2-tools: aio_iob_dumper,
# lib/crypt/src/aio_iob_firmware.rs, lib/compression/src/ac_lz.rs); this is our own code for them.
#
#   tools/bio2/aio_fw.py LIBAIO-IOB.DLL OUTDIR
#
# Writes OUTDIR/bio2wrfirm.bin (the BI2X loader), bi2a_804c.bin and bi2x.bin. They are Konami's code:
# keep them out of the repository (out/ is never committed).
import os
import sys

PARTS = {   # name: (first bytes, last bytes) of the encrypted image in the DLL
    "bio2wrfirm": (bytes.fromhex("FFE36A036202"), bytes.fromhex("774DBC9AFC4E00")),
    "bi2a_804c": (bytes.fromhex("BF728E86646A"), bytes.fromhex("F6C7200F78FF")),
    "bi2x": (bytes.fromhex("FF0D7FFCFE7F"), bytes.fromhex("C7201F70FF")),
}


def find(data: bytes, first: bytes, last: bytes) -> bytes | None:
    a = data.find(first)
    if a < 0:
        return None
    b = data.find(last, a)
    return data[a:b + len(last)] if b >= 0 else None


def unxor(enc: bytes) -> bytes:
    """each byte XOR the key; the next key is the plain byte rotated right by one, inverted"""
    out, key = bytearray(), 0
    for c in enc:
        p = c ^ key
        out.append(p)
        key = ~((p >> 1) | ((p & 1) << 7)) & 0xFF
    return bytes(out)


def lzss(src: bytes) -> bytes:
    """Okumura LZSS: a flag byte per 8 items (bit set: a literal; clear: two bytes, position in the
    low 12 bits of hi<<8|lo, length (hi>>4)+3), a 4 KB ring of zeros written from 0xFEE"""
    ring, w, out, i, n = bytearray(4096), 0xFEE, bytearray(), 0, len(src)
    while i < n:
        flags = src[i]
        i += 1
        for bit in range(8):
            if i >= n:
                break
            if flags >> bit & 1:
                b = src[i]
                i += 1
                out.append(b)
                ring[w] = b
                w = (w + 1) & 0xFFF
            else:
                if i + 1 >= n:
                    i = n
                    break
                hi, lo = src[i], src[i + 1]
                i += 2
                pos, length = ((hi & 0x0F) << 8) | lo, (hi >> 4) + 3
                for k in range(length):
                    b = ring[(pos + k) & 0xFFF]
                    out.append(b)
                    ring[w] = b
                    w = (w + 1) & 0xFFF
    return bytes(out)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__ or "usage: aio_fw.py LIBAIO-IOB.DLL OUTDIR", file=sys.stderr)
        return 2
    data = open(sys.argv[1], "rb").read()
    os.makedirs(sys.argv[2], exist_ok=True)
    for name, (first, last) in PARTS.items():
        enc = find(data, first, last)
        if enc is None:
            print(f"{name}: not found")
            continue
        plain = lzss(unxor(enc))
        open(os.path.join(sys.argv[2], name + ".bin"), "wb").write(plain)
        print(f"{name}: {len(enc)} bytes at 0x{data.find(first):x}, {len(plain)} bytes decrypted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
