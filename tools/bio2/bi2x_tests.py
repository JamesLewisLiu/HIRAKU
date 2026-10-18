#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Read-only tests A and B of docs/BIO2-BI2X.md ("Proposed read-only tests") against a BIO2 with BI2X
# firmware: each packet sent once, as listed there, then 2 s of listening; answers decoded with the
# doc's rules (size bytes, flags = mode<<5 | obfuscated<<4 | crc4, modes 0/2/3, the LCG scrambling
# seeded with tag ^ 0xAA, CRC4 and CRC7 checked). B only if A was answered. Nothing else is sent.
import os, select, sys, termios, time

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial/by-id/usb-KAM_BI2X_0000-if00"
A = [("sync (empty packet to the master)", "AA 00 01 00 09"),
     ("assign nodes", "AA 00 02 02 00 00 01 4A"),
     ("node 1 FIRMINFO, as libaio asks (00 02 81)", "AA 02 03 03 0F 00 02 81 34")]
B = [("FIRMINFO again, plain", "AA 02 04 03 09 00 02 81 34"),
     ("FIRMINFO again, obfuscated", "AA 02 05 03 1B 49 4C C4 48")]

def crc(data, init, tbl):
    c = init
    for b in data:
        t = tbl[(b ^ c) & 0x0F]
        c = ((((c >> 4) ^ t) >> 4) ^ tbl[(((c >> 4) ^ t) ^ (b >> 4)) & 0x0F]) & 0xFF
    return c
T4 = [0x00, 0x0D, 0x03, 0x0E, 0x06, 0x0B, 0x05, 0x08, 0x0C, 0x01, 0x0F, 0x02, 0x0A, 0x07, 0x09, 0x04]
T7 = [0x00, 0x09, 0x12, 0x1B, 0x24, 0x2D, 0x36, 0x3F, 0x48, 0x41, 0x5A, 0x53, 0x6C, 0x65, 0x7E, 0x77]

class Lcg:
    def __init__(self, seed): self.s = seed & 0xFFFFFFFF
    def xf(self, b):
        if (b & 0xAA) == 0xAA: return b
        self.s = (self.s * 1103515245 + 12345) & 0xFFFFFFFF
        return b ^ (self.s & (0x55 if b & 0x80 else 0x7F))

def decode(raw):
    """the packets in what came back: (node, tag, payload, notes)"""
    out, i = [], 0
    while i < len(raw):
        if raw[i] != 0xAA: i += 1; continue
        j = i + 1
        if j + 2 > len(raw): break
        node, tag = raw[j], raw[j + 1]; j += 2
        size, sizebytes = 0, []
        while j < len(raw) and raw[j] & 0xC0 == 0xC0:
            sizebytes.append(raw[j]); size = (size << 6) | (raw[j] & 0x3F); j += 1
        if j >= len(raw): break
        sizebytes.append(raw[j]); size = (size << 7) | (raw[j] & 0x7F); j += 1
        if size == 0:
            out.append((node, tag, b"", "empty")); i = j; continue
        if j >= len(raw): break
        flags = raw[j]; j += 1
        mode, obf, c4 = flags >> 5, (flags >> 4) & 1, flags & 0x0F
        head_ok = (crc([node, tag] + sizebytes + [flags & 0xF0], 0x0F, T4) ^ 0x0F) == c4
        lcg = Lcg(tag ^ 0xAA) if obf else None
        sub = None
        if mode == 3 and j < len(raw): sub = raw[j]; j += 1
        pay, esc = [], False
        while len(pay) < size and j < len(raw):
            b = raw[j]; j += 1
            if mode == 0 and b == 0xFF and not esc: esc = True; continue
            if esc: b = (~b) & 0xFF; esc = False
            if mode == 3 and b == sub: b = 0xAA
            pay.append(lcg.xf(b) if lcg else b)
        c7 = raw[j] if j < len(raw) else None; j += 1
        if c7 is not None and lcg: c7 = lcg.xf(c7)
        body_ok = c7 is not None and (crc(pay, 0x7F, T7) ^ 0x7F) == c7
        note = f"mode {mode}{', obfuscated' if obf else ''}, head crc {'ok' if head_ok else 'BAD'}, body crc {'ok' if body_ok else 'BAD'}"
        out.append((node, tag, bytes(pay), note)); i = j
    return out

def exchange(fd, label, hexbytes):
    pkt = bytes.fromhex(hexbytes.replace(" ", ""))
    print(f"send {label}: {pkt.hex(' ').upper()}")
    os.write(fd, pkt)
    got, end = b"", time.monotonic() + 2.0
    while time.monotonic() < end:
        if select.select([fd], [], [], 0.1)[0]:
            got += os.read(fd, 512)
    print(f"  recv: {got.hex(' ').upper() if got else '(nothing)'}")
    for node, tag, pay, note in decode(got):
        txt = "".join(chr(c) if 32 <= c < 127 else "." for c in pay)
        print(f"  packet node {node:02X} tag {tag:02X}: {pay.hex(' ').upper()}  [{note}]  {txt}")
    return got

fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    print("== A")
    answered = [exchange(fd, l, h) for l, h in A]
    if all(answered):
        print("== B")
        for l, h in B: exchange(fd, l, h)
    else:
        print("A wasn't fully answered: B not sent")
finally:
    os.close(fd)
    print("port closed")
