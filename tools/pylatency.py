#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Key-to-picture latency from a run of a latency script (oracle/scripts/*-latency.his).

    tools/pylatency.py DIGEST FIRST PERIOD HOLD [COUNT]

DIGEST is the run's --digest file (frame, picture hash, sound hash). The script presses
a button at FIRST, FIRST+PERIOD, ... for HOLD frames, on a screen that changes only when
the switch does (a test mode's input check). For each press and release: the frames from
the scripted change to the first frame whose picture differs. The shell applies a
script's change in the frame hook of that frame, so 1 means the very next picture.
"""
import statistics
import sys


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    path, first, period, hold = sys.argv[1], *map(int, sys.argv[2:5])
    count = int(sys.argv[5]) if len(sys.argv) > 5 else 20
    pic = {}
    for line in open(path):
        f, p, _ = line.split()
        pic[int(f)] = p
    press, release = [], []
    for k in range(count):
        for start, out in ((first + k * period, press), (first + k * period + hold, release)):
            if start - 1 not in pic:
                continue
            before = pic[start - 1]
            f = start
            while f in pic and pic[f] == before and f < start + period:
                f += 1
            if f in pic and pic[f] != before:
                out.append(f - start + 1)
    for name, v in (("press", press), ("release", release)):
        if v:
            print(f"{name:8s} {len(v):3d} events: frames to the picture: min {min(v)} median {statistics.median(v)} max {max(v)}  {v}")
        else:
            print(f"{name:8s} no change seen")


if __name__ == "__main__":
    main()
