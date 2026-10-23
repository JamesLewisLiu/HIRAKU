#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/ingest.py --place (the launcher's "move into HIRAKU's folder" or "leave where they are"),
with a made-up PC game's folder (prog/<date>/popn16.exe and data/: the names are what count).

    tests/tools/test_ingest_place.py      exit 0 when all hold
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INGEST = os.path.join(HERE, "..", "..", "tools", "ingest.py")
T = tempfile.mkdtemp(prefix="hk-place-")
failed = 0


def check(ok, what):
    global failed
    print(("ok    " if ok else "FAIL  ") + what)
    failed += not ok


def pc_game(where):
    os.makedirs(os.path.join(where, "prog", "20080101"))
    os.makedirs(os.path.join(where, "data", "sd"))
    open(os.path.join(where, "prog", "20080101", "popn16.exe"), "wb").write(b"MZ")
    open(os.path.join(where, "data", "sd", "a.bin"), "wb").write(b"\0" * 64)
    return where


def machine(out):
    kv = {}
    for line in open(os.path.join(out, "popn16", "machine.txt")):
        k, _, v = line.rstrip("\n").partition("=")
        kv[k] = v
    return kv


def ingest(out, *a):
    p = subprocess.run([sys.executable, "-I", INGEST, "--out", out, *a], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


try:
    # left where it is: machine.txt names the folder given
    src = pc_game(os.path.join(T, "in", "popn16 game"))
    rc, log = ingest(os.path.join(T, "dumps1"), "--place", "stay", src)
    m = machine(os.path.join(T, "dumps1"))
    check(rc == 0 and os.path.isdir(src), f"stay: added (exit {rc}), the folder still there")
    check(m.get("data") == os.path.join(src, "data"), "stay: machine.txt reads data/ where it is")

    # moved: the folder into dumps/popn16/, read there, gone from where it was
    rc, log = ingest(os.path.join(T, "dumps2"), "--place", "move", src)
    to = os.path.join(T, "dumps2", "popn16", "popn16 game")
    m = machine(os.path.join(T, "dumps2"))
    check(rc == 0 and not os.path.exists(src), f"move: added (exit {rc}), the folder gone from where it was")
    check(os.path.exists(os.path.join(to, "data", "sd", "a.bin")), "move: its files in dumps/popn16/")
    check(m.get("program") == os.path.join(to, "prog", "20080101") and m.get("data") == os.path.join(to, "data"),
          "move: machine.txt reads it there")
finally:
    shutil.rmtree(T, ignore_errors=True)

print("test_ingest_place: " + ("all passed" if not failed else f"{failed} FAILED"))
sys.exit(1 if failed else 0)
