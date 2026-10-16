#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare a HIRAKU run with a MAME oracle run of the same input script.

    oracle/snapcmp.py HIRAKU_OUT MAME_OUT [--offset K]

HIRAKU_OUT is the --out directory of `hiraku-<game> --script X.his`;
MAME_OUT is oracle/mame.sh's output (out/oracle/<game>/<run>). Compares:
  - every `hash` column of frames.txt, frame by frame;
  - the snapshots: HIRAKU's snapNNNN-fFFFFFF.ppm by frame number, MAME's
    snap/<set>/NNNN.png by order (MAME takes them at the same script frames).
--offset K compares HIRAKU frame F+K with MAME frame F, for a machine that runs
a few frames behind MAME (e.g. after a longer load). Snapshots pair by order, so
for them run HIRAKU with a script whose snap frames are shifted by K too
("from K every N snap" against MAME's "from 0 every N snap"); MAME's log.txt
gives the frame of each of its snapshots.
"""
import argparse
import glob
import os
import sys

from PIL import Image


def frames(path):
    cols, rows = [], {}
    with open(path) as fh:
        for line in fh:
            if line.startswith("#"):
                cols = [c.lstrip("#") for c in line.split()[3:]]   # "# frame time #a #b ..."
                continue
            t = line.split()
            rows[int(t[0])] = t[2:]
    return cols, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hiraku")
    ap.add_argument("mame")
    ap.add_argument("--offset", type=int, default=0)
    a = ap.parse_args()

    cols, h = frames(os.path.join(a.hiraku, "frames.txt"))
    _, m = frames(os.path.join(a.mame, "frames.txt"))
    common = [f for f in sorted(m) if f + a.offset in h]
    for k, name in enumerate(cols):
        hashed = [f for f in common if m[f][k] != "-"]   # MAME hashes every Nth frame (HIRAKU_HASH_EVERY)
        same = [f for f in hashed if h[f + a.offset][k] == m[f][k]]
        first = next((f for f in hashed if h[f + a.offset][k] != m[f][k]), None)
        common_k = hashed
        print(f"hash {name:8} identical on {len(same)} of {len(common_k)} frames"
              + (f", first difference at MAME frame {first}" if first is not None else ""))

    # snapshots by frame: MAME's log.txt says at which frame it took each ("f60 snap")
    by_frame = {int(p.rsplit("-f", 1)[1][:6]): p for p in glob.glob(os.path.join(a.hiraku, "snap*.ppm"))}
    ms = sorted(glob.glob(os.path.join(a.mame, "snap", "*", "*.png")))
    with open(os.path.join(a.mame, "log.txt")) as fh:
        mframes = [int(l.split()[0][1:]) for l in fh if l.split()[1:2] == ["snap"]]
    same, diff, missing = 0, [], 0
    for f, mp in zip(mframes, ms):
        hp = by_frame.get(f + a.offset)
        if hp is None:
            missing += 1
            continue
        x, y = Image.open(hp).convert("RGB"), Image.open(mp).convert("RGB")
        if x.size == y.size and x.tobytes() == y.tobytes():
            same += 1
        else:
            diff.append(f)
    if missing:
        print(f"{missing} MAME snapshots have no HIRAKU snapshot at frame +{a.offset}")
    print(f"snapshots identical: {same} of {same + len(diff)}"
          + (f"; differing at MAME frames {diff[:20]}{' ...' if len(diff) > 20 else ''}" if diff else ""))
    return 0 if not diff else 1


if __name__ == "__main__":
    sys.exit(main())
