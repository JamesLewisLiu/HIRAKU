# Mods

A mod changes a game without changing its data: HIRAKU lays it over the game as the game runs.
Mods are off unless chosen, a game with mods keeps its saves apart from the clean game's, and the
regression cases always run without them (PLAN.md section 20).

What a mod can do is the same on every board, because it works on the parts HIRAKU emulates:

| layer | what | how |
|---|---|---|
| **memory** (`[[patch]]`) | code and data in the game's RAM | bytes at an address, put there whenever the bytes the mod expects are there (so a patch waits for code loaded later, and comes back if it's loaded again) |
| **media** (`[[bytes]]`, `[[file]]`) | anything the game reads from its discs or disks | the bytes it reads at an offset, or a whole file (by its path on an ISO 9660 disc), served from the mod's own files |

What it changes is the game's business: which file holds the song list, which function draws the
notes. A mod for one game carries that knowledge; the layers don't need any.

A mod ships only what it adds: new text, songs, pictures, code. Never the game's own data: a
mod's files are the mod author's, and its patches name the bytes they replace so they only apply
to the game they were made for.

## Where mods live

`<saves>/mods/<game>/<mod>/` (the saves' folder: `user/data` in a portable package, else
`~/.local/share/hiraku`, `%LOCALAPPDATA%\hiraku` on Windows), one folder per mod with its
`mod.toml` and the files it names. The launcher lists them under a game's **Mods…** (right-click
the game), in order; the command line takes `--mod FOLDER`, several in order (a later mod's bytes
win where two touch the same place).

A game with mods on keeps its saves in a folder of its own, named after the mods
(`<game>+<mod>+<mod>`), started as a copy of the clean game's saves the first time.

## mod.toml

```toml
name = "Sample"                 # shown in the launcher
version = "1.0"
game = "popn14"                 # the game it's for: games/<id>
author = "..."
description = "What it does, in a line or two."

[[patch]]                       # bytes in the game's memory
address = 0x0191cdb8            # where, as the core's mem() takes it (the main CPU's address)
original = "46524545 20504c41 59"   # hex: what must be there (spaces are ignored)
replace  = "4d4f4444 45442120 21"   # hex: what goes there (as long as original)
# once = true                   # apply the first time only (default: whenever original is there again)
# Without `original`, replace is written every frame, whatever is there (a cheat: MAME's and
# PCSX2's write their values so); with once = true, only the first frame. Careful with that: from
# the very first frame means while the game is still loading or unpacking its code there too,
# and pop'n music 14's unpacker copies earlier bytes forward, spreading a write made too early
# into its neighbours. With `original`, a patch waits for the bytes it was made for. Bytes are in the
# machine's own order: big-endian on DJ Main (68000) and Firebeat (PowerPC), little-endian on
# System 573, Twinkle and Konami Python (MIPS).

[[bytes]]                       # bytes the game reads from one of its media
medium = "hdd"                  # the medium's key in dumps/<game>/machine.txt
offset = 0x1234000              # where in it (as the image reads: chdman's extracted bytes)
from = "data/blob.bin"          # the mod's file (its length is how much is replaced)

[[file]]                        # a whole file on an ISO 9660 disc (CDs and DVDs)
medium = "cd_master"
path = "DATA/TITLE.BIN"
from = "files/title.bin"        # as long as the original, or shorter (the rest is zeros)
```

Numbers are decimal or `0x` hex. A mod is checked when it's loaded: an unknown key, a `game` other
than the one being started, a patch whose `original` and `replace` differ in length, or a
missing `from` file stops the game with a message saying which.

## How the layers work

- **Patches:** every frame, before the game's frame runs, each patch looks at its address: with
  its `replace` bytes already there, nothing to do; with its `original` bytes there, it writes
  `replace` and tells the core (`Core::code_changed`), so a core that runs translated code
  translates that part again; with anything else there (code not loaded yet, another version of
  the game), it waits. `once` patches stop looking once applied. The log says when each patch is
  first applied.
- **Media:** a byte range of an image is replaced as the image is read (`runtime/shell/overlay.h`),
  in the image layer every HIRAKU core reads its media through (`runtime/dev/image.h`), and in
  the Python core's disk reader (PCSX2 Reliquary's, through the `Hiraku::disk_read` hook). Writes
  the game makes there go to the game's own save copy of the disk as before, but reads of a
  replaced range keep returning the mod's bytes.
- **Files on ISO 9660 discs** are found by their path in the disc's directory (data tracks of 2048
  bytes a sector, or 2352 with the sector's 16-byte header and 288-byte trailer around them) and
  replaced as `[[bytes]]` of their sectors.

## Per core

| core | patches | media |
|---|---|---|
| DJ Main | data and code (the 68000 is interpreted) | the hard disk |
| System 573, Twinkle | data and code: pages of code the recompiler's blocks came from are write-protected, so a patch moves their epoch on and the blocks are checked again before they run (a patched block runs interpreted) | CDs (a cue sheet's data file), the hard disk, DVD |
| Firebeat | data only for now: the program runs translated ahead of time (recompiled, or the executor's analysis), and a patch to it says so in the log | CDs, the SPU's disk or DVD |
| Konami Python 1 | data and code (the EE recompiler's blocks over a patch are cleared) | the hard disk, the CF card (PCSX2 Reliquary's reads, through `Hiraku::disk_read`) |
| Konami Python 2 | data and code | not yet (its disk is PCSX2's DEV9 ATA) |

Checked (2026-10-19): pop'n music 14, a test-menu string patched once its program had loaded,
and the disk's boot header laid over with its own bytes (the play case's digest unchanged, the
read logged); DDR 2ndMIX, a CD sector range and `PSX.EXE` as a `[[file]]` laid over with their
own bytes (3,000 frames unchanged); the launcher passing the mods and starting the mod set's saves
from a copy.

pop'n music 13 and 14 keep their program (`ee/boot/game.bin`) packed on the disk, so its text and
code are changed with `[[patch]]` once it's loaded, not on the disk.

## Cheats

`tools/modimport.py GAME FILE OUTDIR` turns a PCSX2 patch file (`.pnach`) or a MAME cheat file
(`cheat.xml`) into mod folders, one per cheat, in the machine's byte order: copy the ones wanted to
the game's mods folder. Only plain writes come over (no conditions, no parameters); they have no
`original`, so the warning above applies.
