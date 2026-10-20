#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A game's text in its memory, for a translation mod (docs/MODS.md):

    tools/modtext.py strings DUMP [LO HI] [--all] > text.tsv
        the strings in a RAM dump (as the Python core's HK_PY_EE_DUMP writes it, or any core's
        memory saved from address 0): address, room, text. Room is how many bytes a replacement
        may take: the string, its terminating zero and the zeros after it. Without --all, only
        what reads as words: Japanese, or ASCII with a space (not the program's names).
    tools/modtext.py mod GAME text.tsv > mod.toml
        a mod.toml with a [[patch]] for every line given a fourth column, its translation: the
        text there replaced, in the game's Shift-JIS, padded with zeros. A translation longer
        than its room is refused (say less, or move the text: a patch of the code that points
        to it).

The patches name the bytes they replace, so they wait for the program to be loaded and apply
to that game only. Edit text.tsv in any editor that keeps tabs.
"""
import sys


def is_lead(c):
    return 0x81 <= c <= 0x9F or 0xE0 <= c <= 0xFC


def strings(d, lo, hi, everything):
    out, i = [], lo
    while i < hi:
        j = i
        while j < hi:
            c = d[j]
            if 0x20 <= c < 0x7F:
                j += 1
            elif is_lead(c) and j + 1 < hi and 0x40 <= d[j + 1] <= 0xFC:
                j += 2
            else:
                break
        if j - i >= 4 and j < hi and d[j] == 0:
            try:
                text = d[i:j].decode("shift_jis")
            except UnicodeDecodeError:
                text = None
            if text is not None:
                room = j - i + 1
                while j + room - (j - i) < hi and d[i + room] == 0 and room < 4096:
                    room += 1
                words = any(ord(ch) > 0x7F for ch in text) or (" " in text.strip() and any(ch.isalpha() for ch in text))
                if everything or words:
                    out.append((i, room, text))
            i = j + 1
        else:
            i = j + 1 if j == i else j
    return out


def main():
    a = sys.argv[1:]
    if not a or a[0] not in ("strings", "mod"):
        sys.exit(__doc__)
    if a[0] == "strings":
        everything = "--all" in a
        a = [x for x in a if x != "--all"]
        if len(a) not in (2, 4):
            sys.exit(__doc__)
        d = open(a[1], "rb").read()
        lo, hi = (int(a[2], 0), int(a[3], 0)) if len(a) == 4 else (0, len(d))
        print("# address\troom\ttext\t(translation)")
        for at, room, text in strings(d, lo, min(hi, len(d)), everything):
            print("0x%08x\t%d\t%s" % (at, room, text.replace("\t", " ")))
        return
    if len(a) != 3:
        sys.exit(__doc__)
    game = a[1]
    print('name = "Translation"\nversion = "0.1"\ngame = "%s"\ndescription = "Text of the game\'s replaced."\n' % game)
    bad = 0
    for n, line in enumerate(open(a[2], encoding="utf-8"), 1):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) < 4 or not f[3]:
            continue
        at, room, orig, new = int(f[0], 0), int(f[1]), f[2], f[3]
        o, r = orig.encode("shift_jis"), new.encode("shift_jis")
        if len(r) + 1 > room:
            print("%s:%d: %r takes %d bytes, the room is %d" % (a[2], n, new, len(r) + 1, room), file=sys.stderr)
            bad += 1
            continue
        size = max(len(o), len(r)) + 1
        print("[[patch]]                      # %s -> %s" % (orig, new))
        print("address = 0x%08x" % at)
        print('original = "%s"' % (o + bytes(size - len(o))).hex())
        print('replace = "%s"\n' % (r + bytes(size - len(r))).hex())
    if bad:
        sys.exit("%d translations too long" % bad)


if __name__ == "__main__":
    main()
