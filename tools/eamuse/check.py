#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check an e-AMUSE3 server against what pop'n music 9 (Konami Python 1) needs from it.

    tools/eamuse/check.py http://host:port[/path]          the corpus, then a stateful scenario
    tools/eamuse/check.py URL --corpus-only | --scenario-only
    tools/eamuse/check.py URL -v                            each call and answer, in clear

The calls are the game's own (tools/eamuse/corpus.json: captured from the game, or built from
its request formats where not seen yet). Each answer is checked against the rules the game's
parsers apply (docs/EAMUSE3.md): encryption in kind, the <response> envelope, one element per
call, the status codes the game knows, and each method's attributes. Python 3.8+, standard
library only. Exit status 0 = every check passed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import re
import socket
import sys
import xml.etree.ElementTree as ET
from urllib.parse import urlsplit

SECRET = bytes.fromhex("69d74627d985ee2187161570d08d93b12455035b6df0d8205df5")
CORPUS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus.json")
REQUIRED_SERVICES = ["posevent", "pcbtracker", "numbering", "package", "userdata", "userid", "local"]
# status codes the game understands, per method (0 = done)
STATUSES = {
    "userdata.read": {0, 109, 110},       # 109 not registered, 110 expired
    "userid.ctou": {0, 112},              # 112 no KONAMI ID for the card
    "userid.adduser": {0, 113, 114},      # 113 the user exists, 114 the card is someone's
    "userid.addcard": {0, 114, 115, 116}, # 115 no such user, 116 wrong password
    "p_friend.get": {0, 600},             # 600 no (not retried)
    "p_friend.ask": {0, 601},             # 601 no (not retried; the next credit sends get)
}


def rc4(key: bytes, data: bytes) -> bytes:
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 255
        s[i], s[j] = s[j], s[i]
    out = bytearray()
    i = j = 0
    for b in data:
        i = (i + 1) & 255
        j = (j + s[i]) & 255
        s[i], s[j] = s[j], s[i]
        out.append(b ^ s[(s[i] + s[j]) & 255])
    return bytes(out)


def key_for(info: str) -> bytes:
    m = re.fullmatch(r"1-([0-9a-fA-F]{8})-([0-9a-fA-F]{4})", info.strip())
    if not m:
        raise ValueError(f"X-Eamuse-Info {info!r}: not 1-xxxxxxxx-xxxx")
    return hashlib.md5(bytes.fromhex(m.group(1) + m.group(2)) + SECRET).digest()


class Server:
    def __init__(self, url: str, verbose: bool):
        u = urlsplit(url)
        if u.scheme != "http" or not u.hostname:
            raise SystemExit(f"{url}: expected http://host[:port][/path]")
        self.host, self.port = u.hostname, u.port or 80
        self.path = (u.path.rstrip("/") or "") + "/"
        self.verbose = verbose

    def call(self, body: str, encrypted: bool):
        """One call as the game makes it; returns (head, clear body) of the answer."""
        data = body.encode("utf-8")
        head = (f"POST {self.path} HTTP/1.0\r\nHost: {self.host}:{self.port}\r\n"
                f"User-Agent:EAMUSE.Httpac/1.0\r\nContent-length: {len(data)}\r\n")
        if encrypted:
            info = "1-%08x-%04x" % (random.getrandbits(32), random.getrandbits(16))
            head += f"X-Eamuse-Info: {info}\r\n"
            data = rc4(key_for(info), data)
        with socket.create_connection((self.host, self.port), timeout=15) as s:
            s.sendall((head + "\r\n").encode() + data)
            raw = b""
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                raw += chunk
        if b"\r\n\r\n" not in raw:
            raise CheckFailed("no HTTP answer")
        h, b = raw.split(b"\r\n\r\n", 1)
        head_text = h.decode("latin-1")
        m = re.search(r"^x-eamuse-info:\s*(\S+)", head_text, re.I | re.M)
        if m:
            b = rc4(key_for(m.group(1)), b)
        if self.verbose:
            print("  >", body)
            print("  <", head_text.splitlines()[0], "(encrypted)" if m else "(plain)", b.decode("utf-8", "replace"))
        return head_text, b, bool(m)


class CheckFailed(Exception):
    pass


def need(cond, why):
    if not cond:
        raise CheckFailed(why)


def check_answer(name: str, request: str, encrypted: bool, head: str, body: bytes, answered_encrypted: bool):
    """The rules the game's parsers apply to one answer. Returns the answer's module elements."""
    need(re.match(r"HTTP/1\.[01] 200", head), f"HTTP status {head.splitlines()[0]!r} (the game wants 200)")
    need(answered_encrypted == encrypted,
         "answered %s a call sent %s: the game drops the answer before reading it (an "
         "encrypted call wants X-Eamuse-Info and RC4 back; a plain one neither)" %
         ("encrypted" if answered_encrypted else "in the clear", "encrypted" if encrypted else "in the clear"))
    try:
        root = ET.fromstring(body)
    except ET.ParseError as e:
        raise CheckFailed(f"the answer isn't XML: {e}")
    need(root.tag == "response", f"the root is <{root.tag}>, the game wants <response>")
    need("fault" not in root.attrib, f"<response fault=...>: the game fails every call in it")
    calls = re.findall(r"<([a-z_]+) method=\"([a-z_]+)\"", request)
    elems = list(root)
    need(len(elems) == len(calls), f"{len(elems)} element(s) for {len(calls)} call(s): the game wants one each, in order")
    for (module, method), e in zip(calls, elems):
        what = f"{module}.{method}"
        need(e.tag == module, f"{what}: answered <{e.tag}>, the game wants <{module}>")
        need("fault" not in e.attrib, f"{what}: fault={e.attrib.get('fault')}")
        status = int(e.attrib.get("status", "0"), 10)
        need(status in STATUSES.get(what, {0}), f"{what}: status {status} (the game knows {sorted(STATUSES.get(what, {0}))})")
        check_method(what, e, status)
    return elems


def ints(e, *names, base=10):
    for n in names:
        need(n in e.attrib, f"<{e.tag}>: no {n}=")
        try:
            int(e.attrib[n], base)
        except ValueError:
            raise CheckFailed(f"<{e.tag} {n}={e.attrib[n]!r}>: not a number")


def check_method(what: str, e, status: int):
    if status != 0:
        return
    if what == "services.get":
        items = {i.attrib.get("name"): i.attrib.get("url", "") for i in e if i.tag == "item"}
        for n in REQUIRED_SERVICES:
            need(n in items, f"services.get: no <item name=\"{n}\"> (rank, ntp, p_friend and eemall look up \"local\")")
            need(items[n].startswith("http://"), f"services.get: {n}'s url {items[n]!r} isn't http://")
    elif what in ("posevent.sale", "posevent.sales"):
        ints(e, "approve", base=16)
    elif what == "numbering.assign":
        n = e.attrib.get("number", "")
        need(re.fullmatch(r"[0-9A-Fa-f]{16}", n), f"numbering.assign: number {n!r}: the card takes 16 hex digits")
    elif what == "ntp.get":
        t = e.find("time")
        need(t is not None, "ntp.get: no <time>")
        ints(t, "year", "month", "day", "hour", "min", "sec")
    elif what == "userdata.read":
        need("time" in e.attrib, "userdata.read: no time=")
        need(re.fullmatch(r"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d([+-]\d+)?", e.attrib["time"]), f"userdata.read: time {e.attrib['time']!r}")
        need(len(e.findall("b")) > 0, "userdata.read: no <b> data")
    elif what == "userid.ctou":
        need("user" in e.attrib and "active" in e.attrib, "userid.ctou: wants user= and active=")
    elif what in ("rank.get", "rank.get_h", "rank.get_gz"):
        fields = {"rank.get": ("score", "cool"), "rank.get_h": ("music", "combo"), "rank.get_gz": ("prog",)}[what]
        header = False
        for i in e:
            need(i.tag == "item", f"{what}: <{i.tag}> (the game reads <item>)")
            if "entry" in i.attrib:   # a course's (or the board's) header
                ints(i, "entry")
                header = True
                continue
            need(header, f"{what}: an entry before its header <item {'course=.. ' if what != 'rank.get_gz' else ''}entry=..>")
            ints(i, "rank", *fields)
    elif what in ("rank.register", "rank.register_h", "rank.register_gz"):
        items = e.findall("item")
        need(len(items) == 1, f"{what}: wants one <item> (the player's place)")
        ints(items[0], "entry", "rank", "update")
    elif what == "package.list":
        for i in e:   # the game downloads only an item named "message" (its title screen's ticker)
            need(i.tag == "item", f"package.list: <{i.tag}> (the game reads <item>)")
            need(i.attrib.get("url", "").startswith("http://"), f"package.list: url {i.attrib.get('url')!r} isn't http://")
            ints(i, "size")
            need(int(i.attrib["size"]) >= 1, "package.list: size (the game reads size - 1 bytes)")
    elif what == "eemall.i_get":
        for i in e:
            need(i.tag == "item", f"eemall.i_get: <{i.tag}>")
            ints(i, "num")


def run_corpus(server: Server) -> int:
    calls = json.load(open(CORPUS, encoding="utf-8"))["calls"]
    failed = 0
    for c in calls:
        tag = "" if c["captured"] else " (built from the game's format: not captured yet)"
        try:
            head, body, enc = server.call(c["body"], c["encrypted"])
            check_answer(c["name"], c["body"], c["encrypted"], head, body, enc)
            print(f"ok    {c['name']}{tag}")
        except (CheckFailed, OSError) as e:
            failed += 1
            print(f"FAIL  {c['name']}{tag}: {e}")
    return failed


def run_scenario(server: Server) -> int:
    """A card's life, as the game goes through it: the server must remember."""
    src = 'srcid="01401401000100045FEF" model="C00:J:A:A"'
    steps = 0
    failed = 0

    def step(label, inner, encrypted):
        nonlocal steps, failed
        steps += 1
        body = f"<call {src}>{inner}</call>"
        name = " ".join(f"{a}.{b}" for a, b in re.findall(r'<([a-z_]+) method="([a-z_]+)"', inner))
        head, raw, enc = server.call(body, encrypted)
        return check_answer(name, body, encrypted, head, raw, enc)[0]

    try:
        number = step("a new card", '<numbering method="assign" label="cardv1" model="C00:J:A:A" format="card16m10"/>', False).attrib["number"]
        print(f"ok    numbering.assign: card {number}")
        e = step("not registered", f'<userdata method="read" card="{number}" model="C00:J:A:A" label=""/>', False)
        need(e.attrib.get("status") == "109", f"userdata.read of a new card: status {e.attrib.get('status')}, the game wants 109")
        print("ok    userdata.read of a new card: 109")
        data = "AAAAAAsDEAP" + "/" * 40 + "=="
        step("write", f'<userdata method="write" card="{number}" time="2003-06-02 06:42:41+9" model="C00:J:A:A" label=""><b>{data}</b></userdata>', False)
        e = step("read back", f'<userdata method="read" card="{number}" model="C00:J:A:A" label=""/>', False)
        got = "".join(b.text or "" for b in e.findall("b"))
        need(got == data, "userdata.read after write: the <b> data differs from what was written")
        need(e.attrib.get("time") == "2003-06-02 06:42:41+9", f"userdata.read after write: time {e.attrib.get('time')!r}")
        print("ok    userdata write, then read: the same data and time")
        e = step("no KONAMI ID", f'<userid method="ctou" card="{number}" model="C00:J:A:A"/>', True)
        need(e.attrib.get("status") == "112", f"userid.ctou, no KONAMI ID: status {e.attrib.get('status')}, the game wants 112")
        user = "CHECK%04d" % random.randrange(10000)
        step("adduser", f'<userid method="adduser" model="C00:J:A:A" user="{user}" pass="1234" card="{number}"/>', True)
        e = step("ctou", f'<userid method="ctou" card="{number}" model="C00:J:A:A"/>', True)
        need(e.attrib.get("status", "0") == "0" and e.attrib.get("user") == user, f"userid.ctou after adduser: {e.attrib}")
        e = step("wrong password", f'<userid method="addcard" user="{user}" pass="9999" card="9999999999999999" model="C00:J:A:A"/>', True)
        need(e.attrib.get("status") == "116", f"userid.addcard, wrong password: status {e.attrib.get('status')}, the game wants 116")
        print("ok    KONAMI ID: 112, adduser, ctou, a wrong password 116")
        e = step("register", f'<rank method="register" card_id="{number}" konami_id="{user}" gpcb_id="01401401000100045FEF" name="82A082A082A0" course="3" option="0" score="77777" cool="300" great="10" good="1" bad="0" combo="311" shop_name="" area_num="13"/>', False)
        item = e.find("item")
        e = step("get", '<rank method="get" course_offset="3" course_num="1" rank_offset="1" rank_num="100"/>', False)
        scores = [i.attrib.get("score") for i in e if "rank" in i.attrib]
        need("77777" in scores, "rank.get after rank.register: the score isn't on the board")
        print(f"ok    rank: registered (place {item.attrib.get('rank')}), and on the board")
    except (CheckFailed, OSError, KeyError) as e:
        failed += 1
        print(f"FAIL  scenario, step {steps}: {e}")
    return failed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("url")
    ap.add_argument("-v", "--verbose", action="store_true")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--corpus-only", action="store_true")
    g.add_argument("--scenario-only", action="store_true")
    a = ap.parse_args()
    server = Server(a.url, a.verbose)
    failed = 0
    if not a.scenario_only:
        failed += run_corpus(server)
    if not a.corpus_only:
        failed += run_scenario(server)
    print("all checks passed" if not failed else f"{failed} check(s) failed")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
