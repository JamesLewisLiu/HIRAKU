# Mods

A mod changes a game without changing its data: HIRAKU lays it over the game as the game runs.
Mods are off unless chosen, a game with mods keeps its saves apart from the clean game's, and the
regression cases always run without them (PLAN.md section 20).

What a mod can do is the same on every board, because it works on the parts HIRAKU emulates:

| layer | what | how |
|---|---|---|
| **memory** (`[[patch]]`) | code and data in the game's RAM | bytes at an address, put there whenever the bytes the mod expects are there (so a patch waits for code loaded later, and comes back if it's loaded again) |
| **media** (`[[bytes]]`, `[[file]]`) | anything the game reads from its discs or disks | the bytes it reads at an offset, or a whole file (by its path on an ISO 9660 disc, or on a Konami Python 1 disk), served from the mod's own files |

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
# while_rate = true             # in place only while a [[rate]] is on, original back otherwise
# between_ticks = true          # ... and then only on the vsyncs between the cabinet's ticks (below)
# Without `original`, replace is written every frame, whatever is there (a cheat: MAME's and
# PCSX2's write their values so); with once = true, only the first frame. Careful with that: from
# the very first frame means while the game is still loading or unpacking its code there too,
# and pop'n music 14's unpacker copies earlier bytes forward, spreading a write made too early
# into its neighbours. With `original`, a patch waits for the bytes it was made for. Bytes are in the
# machine's own order: big-endian on DJ Main (68000) and Firebeat (PowerPC), little-endian on
# System 573, Twinkle and Konami Python (MIPS).

[[rate]]                        # the machine's vsync at another rate while the game is somewhere
hz = "display"                  # the display's refresh rate (no window: 119.88), or a number (30-1000)
while_changing = 0x018ff668     # ... while this word of its memory changes from frame to frame

[[bytes]]                       # bytes the game reads from one of its media
medium = "hdd"                  # the medium's key in dumps/<game>/machine.txt
offset = 0x1234000              # where in it (as the image reads: chdman's extracted bytes)
from = "data/blob.bin"          # the mod's file (its length is how much is replaced)

[[file]]                        # a whole file on an ISO 9660 disc (CDs and DVDs)
medium = "cd_master"
path = "DATA/TITLE.BIN"
from = "files/title.bin"        # as long as the original, or shorter (the rest is zeros)

[[file]]                        # or on a Konami Python 1 disk (pop'n music 14 and its family)
medium = "hdd"
path = "image/256_title/0.gzz"  # the game's name for it (tools/popnfs.py list), or #hash
from = "files/0.gzz"            # the file as the game uses it (tools/popnfs.py extract): any length
```

Numbers are decimal or `0x` hex. A mod is checked when it's loaded: an unknown key, a `game` other
than the one being started, a patch whose `original` and `replace` differ in length, or a
missing `from` file stops the game with a message saying which.

## High frame rates (`[[rate]]`)

A game draws one picture per vsync. `[[rate]]` makes the vsync come `hz` times a second of the
machine's own time while a word of the game's memory keeps changing (two frames in a row: on;
two still: back to the machine's rate). `hz = "display"` follows the display's refresh rate, so
the game's frames meet the display's one for one (display pacing then shows each for one
refresh). The CPUs keep their clocks, so each frame has less time; a game that keeps up draws
more pictures a second. What it computes from a clock (a song's position, timing windows) stays
right and moves more smoothly; what it counts in frames (animations, effects, menu timers) would
run faster, which is why the rate is tied to a condition, and what `between_ticks` is for.

The cabinet's ticks: while a rate is on, HIRAKU keeps count of the machine's time, and a vsync
that completes another 1/59.94 s is a tick (every second one at 119.88 Hz, every fourth at 239.76,
5 in 12 at 144). A `[[patch]]` with `between_ticks` is in place on the vsyncs between ticks only:
an increment patched to +0 there advances once per cabinet frame at any rate. Patches are put
in place and taken out between frames (the Python core clears the EE recompiler's blocks over
them each time).

pop'n music 14 (`games/popn14/mods/smooth-notes`, 2026-10-19):
- the play screen computes the song's position from a hardware timer on every frame
  (FUN_01740298, the word at 0x018ff668), draws the notes from it and judges from it: while that
  word changes (from the song's start to its end) the vsync follows the display;
- checked headless at 119.88, 239.76 and 479.52 Hz: a new picture on every vsync (the EE keeps
  up even at 480), the notes where they are at 60 Hz for the same moment of the song, and the
  play case's results identical with its presses at the same moments;
- the animated objects (the characters, the hits' sparks, many effects) go through one update
  (around 0x01736fb4) that adds each object's speed (percent, +0x20) to its frame counter (+8,
  with the remainder at +0xa); reading the speed as 0 between ticks (`between_ticks` on the load
  at 0x01736fbc) holds every object at the cabinet's pace: the characters' counter advances 500
  times in 1,000 vsyncs at 120 Hz, 250 at 240, 125 at 480, and their poses match 60 Hz's at the
  same moments. (Taking the objects' "paused" branch instead skips their drawing: they blink.)
- left: the GREAT/GOOD/BAD text shows for half its time at 120 Hz. Its timer is a counter in a
  long-lived stack frame (0x01ffea20 in the attract demo), counted through a pointer; not found
  among the increment-and-store sites.

pop'n music 9 to 13 (`games/popn<N>/mods/smooth-notes`, 2026-10-19): the same engine. Their
song clocks were found by the clock function's shape (pop'n 10-13: a pause flag at +0x20, the
timer, a 64-bit subtraction and conversion, the result stored at +0 of the same block; pop'n 9,
an older build, by a RAM search) and checked to move only from a song's start; the objects'
update is the same code in every one (`lh v1, 0x20(sN)` before `addiu a1, zero, 100`), and pop'n
10-13 have the 13-frame effect timer too:

| game | song clock | objects' speed load | effect timer |
|---|---|---|---|
| pop'n 9 | 0x0025a188 | 0x00106ca4 | — |
| pop'n 10 | 0x002fa3e0 | 0x001084dc | 0x001163bc |
| pop'n 11 | 0x002ca180 | 0x00108d94 | 0x00116a84 |
| pop'n 12 | 0x002d2488 | 0x0010847c | 0x00115ccc |
| pop'n 13 | 0x01907fd8 | 0x01745a14 | 0x01753350 |
| pop'n 14 | 0x018ff668 | 0x01736fbc | 0x017448f0 |

Checked on each at 119.88 Hz: the rate on from the song's start, a new picture on every vsync,
the clock at 8.34 ms a vsync; on pop'n 12 an object's frame counter at the cabinet's pace (500
times in 1,000 vsyncs).

The Firebeat pop'n music (4-8) are another case (checked on pop'n 4, 2026-10-19, with the
Firebeat core's frame period halved by hand): the attract demo's song lasts the same number of
frames at twice the rate (about 2,400 at both), so its play goes by frames, not by a clock. Its
notes would run away from its music; smooth notes there need the game's scroll itself patched
(each frame's step split over the vsyncs between ticks), per game, and the core's
`set_vsync_rate` (its frame period is `frame_ticks()`, runtime/firebeat/board.cpp). DJ Main's
pop'n music 1-3 (`FRAME_CYCLES`, runtime/djmain/board.cpp) are older still: not looked at.

Only the Python core changes its rate so far; elsewhere `[[rate]]` says so in the log and does
nothing.

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
- **Files on Konami Python 1 disks** (`runtime/host/pythondisk.cpp`; the file system is told in
  `tools/popnfs.py`): the disk's directory (sector 0x4800) gives a file's place by the hash of its
  name. The mod's file is packed the way the game reads that file (LZSS for pictures, with its
  size first; a folder's `system.idx` as it is, with its size first; the IOP modules as they
  are), unencrypted (the game decrypts only files that carry a tag), and laid in the file's own
  sectors when it fits there, else in the unused room after the last file (checked to be zeros).
  The directory, with the file's new place, size and byte sum, is laid over the disk's (its own
  sum made again), so the loader and the disk check find it. A name the disk doesn't have is
  added to the directory. The game's program (`ee/boot/game.bin`) isn't replaced this way: its
  code and text are `[[patch]]`es. A file of the same content can still change the game's
  timing: packed again it is a little longer or shorter, or somewhere else, so it loads a frame
  sooner or later (pop'n music 14's title loop then shows a different animation; the play case
  joins up again once a song starts).

## Per core

| core | patches | media |
|---|---|---|
| DJ Main | data and code (the 68000 is interpreted) | the hard disk |
| System 573, Twinkle | data and code: pages of code the recompiler's blocks came from are write-protected, so a patch moves their epoch on and the blocks are checked again before they run (a patched block runs interpreted) | CDs (a cue sheet's data file), the hard disk, DVD |
| Firebeat | data only for now: the program runs translated ahead of time (recompiled, or the executor's analysis), and a patch to it says so in the log | CDs, the SPU's disk or DVD |
| Konami Python 1 | data and code (the EE recompiler's blocks over a patch are cleared) | the hard disk, the CF card (PCSX2 Reliquary's reads, through `Hiraku::disk_read`); files by name on pop'n music 14's disk |
| Konami Python 2 | data and code | not yet (its disk is PCSX2's DEV9 ATA) |

Checked (2026-10-19): pop'n music 14, a test-menu string patched once its program had loaded,
and the disk's boot header laid over with its own bytes (the play case's digest unchanged, the
read logged); DDR 2ndMIX, a CD sector range and `PSX.EXE` as a `[[file]]` laid over with their
own bytes (3,000 frames unchanged); the launcher passing the mods and starting the mod set's saves
from a copy.

Checked (2026-10-19): pop'n music 14's `image/256_title/0.gzz` (the attract screens' small
lettering) as a `[[file]]`, unchanged (the play case: the title loop's timing moves, the rest of
the pictures the same) and with its bytes changed (the coin count's digits changed on screen).

pop'n music 13 and 14 keep their program (`ee/boot/game.bin`) encrypted on the disk, so its text
and code are changed with `[[patch]]` once it's loaded, not on the disk.

## pop'n music 14's disk

`tools/popnfs.py IMAGE list` lists its files (10,165: 10,010 named from the game's own strings and
index files, the rest as `#hash`), `tools/popnfs.py IMAGE extract NAME... OUT` writes them as the
game uses them (decrypted with the keys in the disk's own loader, unpacked). What's there:
`image/<folder>/` holds the pictures (`.gzz`: a `GC` header, then the picture), each folder with
its `system.idx` listing them; `iop/modules/` the IOP's drivers; `ee/boot/game.bin` the program.

A picture, for a mod: `tools/popnfs.py IMAGE topng image/256_title/1.gzz title1.png`, edit the PNG
(same size), then `tools/popnfs.py IMAGE frompng image/256_title/1.gzz title1.png files/1.gzz`
and name `files/1.gzz` in a `[[file]]`. 16-bit pictures (song banners `kc/`, backgrounds,
characters) take any colour; the 8-bit ones (the `256_*` folders: the menus' lettering and
parts) share their folder's 256-colour palette, so the PNG's colours become the nearest of it.
The songs are not in these files: each has a 20 MB slot after them, its parts scrambled with
the slot's number. `tools/popnsong.py IMAGE list` lists the 749 songs' slots and
`tools/popnsong.py IMAGE extract SLOT OUT` writes a slot's keysounds, charts, music and preview
(PLAN.md section 20). Mods can't add or change songs yet.

## Cheats

`tools/modimport.py GAME FILE OUTDIR` turns a PCSX2 patch file (`.pnach`) or a MAME cheat file
(`cheat.xml`) into mod folders, one per cheat, in the machine's byte order: copy the ones wanted to
the game's mods folder. Only plain writes come over (no conditions, no parameters); they have no
`original`, so the warning above applies.
