#!/usr/bin/env python3
# Read-only identification of a BIO2 with BI2X firmware (ACIO2), as a game starts: the master node's
# node assignment (command 0x0001), then the BI2X node's firmware version (command 0x0002). Two
# packets, nothing else; prints every byte sent and received. Packet format from spice2x's acio2emu
# (the board's side): AA, node, tag, size, (flags << 4 | crc4), payload (AA/FF escaped), crc7.
import os, sys, termios, time, select

PORT = "/dev/serial/by-id/usb-KAM_BI2X_0000-if00"

def crc4(data):
    tbl = [0x00, 0x0D, 0x03, 0x0E, 0x06, 0x0B, 0x05, 0x08, 0x0C, 0x01, 0x0F, 0x02, 0x0A, 0x07, 0x09, 0x04]
    crc = 0x0F
    for b in data:
        t = tbl[(b ^ crc) & 0x0F]
        crc = ((((crc >> 4) ^ t) >> 4) ^ tbl[(((crc >> 4) ^ t) ^ (b >> 4)) & 0x0F]) & 0xFF
    return crc

def crc7(data):
    tbl = [0x00, 0x09, 0x12, 0x1B, 0x24, 0x2D, 0x36, 0x3F, 0x48, 0x41, 0x5A, 0x53, 0x6C, 0x65, 0x7E, 0x77]
    crc = 0x7F
    for b in data:
        t = tbl[(b ^ crc) & 0x0F]
        crc = ((((crc >> 4) ^ t) >> 4) ^ tbl[(((crc >> 4) ^ t) ^ (b >> 4)) & 0x0F]) & 0xFF
    return crc

def packet(node, tag, payload):
    hdr = [node, tag, len(payload), 0]
    hdr[3] = crc4(hdr) ^ 0x0F
    body = []
    for b in payload:
        if b in (0xAA, 0xFF):
            body += [0xFF, (~b) & 0xFF]
        else:
            body.append(b)
    return bytes([0xAA] + hdr + body + [crc7(payload) ^ 0x7F])

def hexs(b):
    return " ".join(f"{x:02X}" for x in b)

def exchange(fd, pkt, label):
    print(f"send {label}: {hexs(pkt)}")
    os.write(fd, pkt)
    got, end = b"", time.monotonic() + 2.0
    while time.monotonic() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            got += os.read(fd, 256)
            pass   # listen the whole time: an answer may follow the echo
    print(f"recv {label}: {hexs(got) if got else '(nothing)'}")
    return got

fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0                       # raw: no input/output processing, no echo
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    r1 = exchange(fd, packet(0, 0, [0x00, 0x01]), "assign nodes (master, cmd 0x0001)")
    r2 = exchange(fd, packet(2, 1, [0x00, 0x02]), "firmware version (node 1, cmd 0x0002)")
    if r2:
        print("printable:", "".join(chr(c) if 32 <= c < 127 else "." for c in r2))
finally:
    os.close(fd)
    print("port closed")
