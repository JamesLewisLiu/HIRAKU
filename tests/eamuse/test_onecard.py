#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One card for all, pop'n music 12-18 (docs/EAMUSE3.md): a card given a player in pop'n 13-18,
each with a clear mark and a best score of its own, and each game's answer checked for the
others' (made-up records only, no game data).

    tests/eamuse/test_onecard.py [build/cmake/hiraku-eamuse]      exit 0 when all hold
"""
import os, re, shutil, socket, struct, subprocess, sys, tempfile, time
SERVER = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'cmake', 'hiraku-eamuse')
D = tempfile.mkdtemp(prefix='hk-onecard-')
REF, CARD = '48524B00000000AA', 'E0040100000000AA'
def w(name, data): open(os.path.join(D, name), 'wb').write(data)
w('cards', f'cardid={CARD}\tcardtype=1\tpass=1234\trefid={REF}\n'.encode())
def rec(size): return bytearray(size)
# pop'n 14: song 5's Normal cleared (word 0x24 + 4n), its Hyper 80000; song 460 (14's own id) EX 70000
r14 = rec(0xdb8); r14[0x24 + 4 * 5] = 0x01; w(f'pdata-F14-{REF}', r14)
w(f'pscore-F14-{REF}', b'num=5\tsheet=1\tscore=80000\nnum=460\tsheet=3\tscore=70000\n')
# pop'n 13: song 460 EX 60000 (a different chart from 14's on: not shared with 15-18)
w(f'pdata-E13-{REF}', bytes(0xd6c)); w(f'pscore-E13-{REF}', b'num=460\tsheet=3\tscore=60000\n')
# pop'n 15: song 10's Hyper a gold medal (3 << 2), its Normal 90000
r15 = rec(0xb38); r15[0x58 + 2 * 10] = 0x0c; w(f'pdata-G15-{REF}', r15)
w(f'pscore-G15-{REF}', b'num=10\tsheet=0\tscore=90000\n')
# pop'n 16: song 20's 5-button cleared (1 << 4), its EX 95000 (0x8dc, 846 a chart)
r16 = rec(0x6784); r16[0x50 + 2 * 20] = 0x10; struct.pack_into('<I', r16, 0x8dc + 4 * (3 * 846 + 20), 95000); w(f'pdata-H16-{REF}', r16)
# pop'n 17: song 700's EX cleared (1 << 6), its Normal 77777 (0xcd4, 950 a chart)
r17 = rec(0x74bc); r17[0x54 + 2 * 700] = 0x40; struct.pack_into('<I', r17, 0xcd4 + 4 * 700, 77777); w(f'pdata-I17-{REF}', r17)
# pop'n 18: song 900's Hyper cleared, 99999 (its <hiscore>: 17 bits a chart)
r18 = rec(0x1700); r18[0x68 + 2 * 900] = 0x04; w(f'pdata-J39-{REF}', r18)
def hs_put(h, i, v):
    for b in range(17):
        bit = i * 17 + b
        if v >> b & 1: h[bit // 8] |= 1 << bit % 8
def hs_get(h, i): return sum(((h[(i * 17 + b) // 8] >> ((i * 17 + b) % 8)) & 1) << b for b in range(17))
h = bytearray((0x1ab7 * 17 + 7) // 8); hs_put(h, 900 * 7 + 1, 99999); w(f'pscore-J39-{REF}', h)

with socket.socket() as s0:
    s0.bind(('127.0.0.1', 0)); port = s0.getsockname()[1]
srv = subprocess.Popen([SERVER, '--port', str(port), '--data', D, '--quiet', '--no-relay'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(300):   # up to 30 s: a freshly built server read from a slow disk
    try:
        socket.create_connection(('127.0.0.1', port)).close(); break
    except OSError:
        time.sleep(0.1)
def call(model, body):
    x = f'<?xml version="1.0"?><call model="{model}" srcid="0"><{body}</call>'.encode()
    s = socket.create_connection(('127.0.0.1', port))
    s.sendall(b'POST / HTTP/1.0\r\nContent-Length: %d\r\n\r\n' % len(x) + x)
    out = b''
    while (d := s.recv(65536)): out += d
    return out.split(b'\r\n\r\n', 1)[1].decode('cp932', 'replace')
def unlzss(b):
    n = struct.unpack_from('<I', b)[0]; i = 4; out = bytearray(); win = bytearray(4096); r = 0xfee
    while len(out) < n:
        flags = b[i]; i += 1
        for bit in range(8):
            if len(out) >= n: break
            if flags >> bit & 1:
                c = b[i]; i += 1; out.append(c); win[r] = c; r = (r + 1) & 0xfff
            else:
                at = b[i] | (b[i + 1] & 0xf0) << 4; ln = (b[i + 1] & 15) + 3; i += 2
                for k in range(ln):
                    c = win[(at + k) & 0xfff]; out.append(c); win[r] = c; r = (r + 1) & 0xfff
    return bytes(out)
def binfield(a, name):
    m = re.search(rf'<{name}[^>]*>([0-9a-f]+)</{name}>', a); return bytes.fromhex(m.group(1)) if m else None
fails = 0
def check(what, got, want):
    global fails
    ok = got == want; fails += not ok
    print(('ok  ' if ok else 'FAIL'), what, got, '' if ok else f'(want {want})')
try:
    # pop'n 18
    a = call('J39:J:A:A:2010040500', f'playerdata method="get" ref_id="{REF}" model=""></playerdata>')
    b, hs = binfield(a, 'b'), binfield(a, 'hiscore')
    check('18 marks: 5 N, 10 H gold, 20 5B, 700 EX, 900 H (own)', [b[0x68 + 2 * n] for n in (5, 10, 20, 700, 900)], [0x01, 0x0c, 0x10, 0x40, 0x04])
    check('18 scores: 5/H 14, 10/N 15, 20/EX 16, 700/N 17, 900/H own, 460/EX 14',
          [hs_get(hs, n * 7 + s) for n, s in ((5, 1), (10, 0), (20, 3), (700, 0), (900, 1), (460, 3))], [80000, 90000, 95000, 77777, 99999, 70000])
    # pop'n 16 (packed): song 900 isn't 16's; 460's EX from 14 (14-18 alike)
    a = call('H16:J:A:A:2008052700', f'playerdata method="get" ref_id="{REF}" model=""></playerdata>')
    b = unlzss(binfield(a, 'b'))
    check('16 record size', len(b), 0x6784)
    check('16 marks: 5, 10, 20 own, 700', [b[0x50 + 2 * n] for n in (5, 10, 20, 700)], [0x01, 0x0c, 0x10, 0x40])
    sc = lambda n, s: struct.unpack_from('<I', b, 0x8dc + 4 * (s * 846 + n))[0]
    check('16 scores: 5/H, 10/N, 20/EX own, 700/N, 460/EX', [sc(5, 1), sc(10, 0), sc(20, 3), sc(700, 0), sc(460, 3)], [80000, 90000, 95000, 77777, 70000])
    a = call('H16:J:A:A:2008052700', f'playerdata method="score" ref_id="{REF}" music_num="700" sheet_num="0" s_challenge="0"></playerdata>')
    check('16 score 700/N', re.search(r'score="(\d+)"', a).group(1), '77777')
    # the stored 16 record keeps only its own
    st = open(os.path.join(D, f'pdata-H16-{REF}'), 'rb').read()
    check('16 kept record unchanged', st == bytes(r16), True)
    # pop'n 17
    a = call('I17:J:A:A:2009022500', f'playerdata method="get" ref_id="{REF}" model=""></playerdata>')
    b = unlzss(binfield(a, 'b'))
    sc = lambda n, s: struct.unpack_from('<I', b, 0xcd4 + 4 * (s * 950 + n))[0]
    check('17 marks: 5, 20, 900 (not 17\'s: 0)', [b[0x54 + 2 * n] for n in (5, 20)], [0x01, 0x10])
    check('17 scores: 20/EX, 900 none', [sc(20, 3), sc(5, 1)], [95000, 80000])
    # pop'n 15 asks one chart: 18's 900 isn't 15's; 16's 20/EX is
    for n, s, want in ((20, 3, '95000'), (700, 0, '77777'), (460, 3, '70000')):
        a = call('G15:J:A:A:2007091800', f'G15 method="playerdata_score" ref_id="{REF}" music_num="{n}" sheet_num="{s}" s_challenge="0"></G15>')
        check(f'15 score {n}/{s}', re.search(r'score="(\d+)"', a).group(1), want)
    # pop'n 13: 460's EX its own 60000, not 14-18's
    a = call('E13:J:A:A:2005', f'E13 type="playerdata" method="score" ref_id="{REF}" music_num="460" sheet_num="3"></E13>')
    m = re.search(r'score="(\d+)"', a); check('13 score 460/EX', m and m.group(1), '60000')
    a = call('E13:J:A:A:2005', f'E13 type="playerdata" method="score" ref_id="{REF}" music_num="20" sheet_num="3"></E13>')
    m = re.search(r'score="(\d+)"', a); check('13 score 20/EX (16\'s)', m and m.group(1), '95000')
finally:
    srv.terminate()
    shutil.rmtree(D, ignore_errors=True)
print('failures:', fails)
sys.exit(1 if fails else 0)
