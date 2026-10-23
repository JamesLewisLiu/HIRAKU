#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/ingest.py and the Konami Python games' BIOS and keys (docs/PLAY.md): a game added without
them waits (machine.pending); the files dropped later, loose or in their folders, are kept for every
Python game and finish it. Made-up files only (the names are what count), no game data.

    tests/tools/test_ingest_python.py      exit 0 when all hold
"""
import json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INGEST = os.path.join(HERE, "..", "..", "tools", "ingest.py")
T = tempfile.mkdtemp(prefix="hk-ingest-")
OUT = os.path.join(T, "dumps")
failed = 0


def check(ok, what):
    global failed
    print(("ok    " if ok else "FAIL  ") + what)
    failed += not ok


def touch(path, data=b"\0" * 16):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def waiting(game, board, name):
    d = os.path.join(OUT, game)
    for sub in ("bios", "keys"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    machine = {"name": name, "core": "python", "disk": "disk.img"}
    with open(os.path.join(d, "machine.pending"), "w") as f:
        json.dump({"machine": machine, "order": ["name", "core", "disk", "bios", "keys"], "board": board}, f)
    return d


def ingest(*inputs):
    p = subprocess.run([sys.executable, "-I", INGEST, "--out", OUT, *inputs], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


try:
    p1 = waiting("popn12", 1, "pop'n music 12")
    p2 = waiting("drmnvj", 2, "DrumMania V")
    drop = os.path.join(T, "drop")
    for ext in ("BIN", "NVM", "MEC"):
        touch(os.path.join(drop, "bios", "COH-H31000." + ext))
    for k in ("civ", "cks", "eks", "kek", "arcade_kelf_override_kbit", "arcade_kelf_override_kc"):
        touch(os.path.join(drop, "keys", k + ".bin"))

    # the keys' folder alone: kept; both games still lack a BIOS
    rc, log = ingest(os.path.join(drop, "keys"))
    check(rc != 0, f"keys alone: the games still wait (exit {rc})")
    check("KEPT" in log and os.path.exists(os.path.join(OUT, "_python", "keys", "kek.bin")), "the keys kept in dumps/_python/keys")
    check(os.path.exists(os.path.join(p1, "machine.pending")), "pop'n 12 still waiting")
    check("needs the arcade BIOS" in log, "it says pop'n 12 needs the arcade BIOS")

    # Python 1's BIOS, loose files: pop'n 12 is finished; DrumMania V (Python 2's BIOS) still waits
    rc, log = ingest(*[os.path.join(drop, "bios", "COH-H31000." + e) for e in ("BIN", "NVM", "MEC")])
    check(not os.path.exists(os.path.join(p1, "machine.pending")), "pop'n 12 no longer waiting")
    mt = open(os.path.join(p1, "machine.txt")).read() if os.path.exists(os.path.join(p1, "machine.txt")) else ""
    check("bios=bios/COH-H31000.BIN\n" in mt and "keys=keys\n" in mt, "pop'n 12's machine.txt names its BIOS and keys")
    check(all(os.path.exists(os.path.join(p1, "bios", "COH-H31000." + e)) for e in ("BIN", "NVM", "MEC")),
          "pop'n 12's BIOS copied with its .NVM and .MEC")
    check(len(os.listdir(os.path.join(p1, "keys"))) == 6, "pop'n 12's six key files copied")
    check(os.path.exists(os.path.join(p2, "machine.pending")) and "ps2-0190j" in log,
          "DrumMania V still waits, for ps2-0190j")
    check(rc != 0, f"exit {rc}: a game still waits")

    # Python 2's BIOS: DrumMania V finished with the kept keys (its four)
    touch(os.path.join(drop, "ps2-0190j-20030822.bin"))
    rc, log = ingest(os.path.join(drop, "ps2-0190j-20030822.bin"))
    check(rc == 0, f"Python 2's BIOS: exit {rc}")
    check(not os.path.exists(os.path.join(p2, "machine.pending")) and os.path.exists(os.path.join(p2, "machine.txt")),
          "DrumMania V added")
    check(sorted(os.listdir(os.path.join(p2, "keys"))) == ["civ.bin", "cks.bin", "eks.bin", "kek.bin"],
          "DrumMania V's four key files copied")
finally:
    shutil.rmtree(T, ignore_errors=True)

print("test_ingest_python: " + ("all passed" if not failed else f"{failed} FAILED"))
sys.exit(1 if failed else 0)
