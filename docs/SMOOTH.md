# Smooth notes on fast displays

A 120 or 240 Hz display can show notes moving two or four times as smoothly as the cabinet's
60 Hz monitor. A game draws one picture per vsync, though, so the extra smoothness has to come
from somewhere. HIRAKU has two ways, and which one fits depends on how the game keeps time:

| The game's play goes by | Way | Where | Games so far |
|---|---|---|---|
| a clock (a hardware timer read each frame) | more vsyncs: the game draws more pictures (`[[rate]]` mods) | per game: docs/MODS.md | pop'n music 9-14 (Python) |
| frames (each frame moves the notes a fixed step) | in-between pictures: HIRAKU draws them (`--smooth-motion`) | per core: every game on it | DJ Main, Firebeat |

To tell which one a game is: run its attract demo (or a play script) with the vsync at twice the
rate. If the demo song takes the same number of frames (it plays twice as fast, out of step with
its music), the game counts frames. If it takes twice the frames, it follows a clock. pop'n music
4 (Firebeat) and pop'n music 1 (DJ Main) count frames; pop'n music 9-14 follow a clock.

## In-between pictures (`--smooth-motion`)

The game keeps its rate and runs exactly as before; nothing in the machine changes. When display
sync shows each frame for k refreshes (2 at 120 Hz, 4 at 240), the k - 1 refreshes after the frame
show pictures of it with what moves carried on 1/k, 2/k... of a frame along its last move:

- `Core::render_between(t)` (runtime/shell/core.h) draws the last frame again, each moving thing
  `t` of a frame further along, into a picture of its own;
- the window (runtime/host/window.cpp) shows them on those refreshes through the same picture
  pipeline (CRT shaders and all), and commits each refresh with the compositor's fifo protocol
  (runtime/host/pacing.cpp, `commit_extra`). Without fifo-v1 (KWin 6.7), or with display sync
  off, nothing changes;
- the exit report says how many it showed (`video: smooth motion: N in-between pictures shown`).

Use it: `--smooth-motion` (display sync on, the default), or the launcher's Settings > "Smooth
motion on a 120 or 240 Hz display". The display's rate must be a multiple of the game's: within
3 %, or 3.5 % with smooth motion on, as DJ Main's 58.00 Hz needs 3.4 % (the game then runs that
much faster, its sound with it; Firebeat's pop'n 2.6 %). So 120 or 240 Hz for every DJ Main and
Firebeat game; 144 Hz fits none of them (2.48 frames' worth of refreshes).

It's a guess one frame ahead: a note's next position is where its last move takes it. Notes
scroll at a steady speed, so the guess is right while they keep moving; where something stops,
turns or appears, its in-between pictures show it where it was. The judgement, the sound and
the latency are the game's own: the in-between pictures add nothing between pressing a button and
the game seeing it.

### DJ Main (runtime/djmain/board.cpp, `render_between`)

The 68020 builds a sprite list in object RAM each frame (the K055555 mixer shows it with the
K056832's tilemaps). The list is rebuilt each frame, with sprites in different slots from one
frame to the next. So each sprite is matched with the nearest one of the frame before that uses
the same tile, within 16 pixels, and moved on by that step. The picture is drawn from a copy of
object RAM and into a buffer of its own, so the machine is never touched. Checked on pop'n music
1 (notes are sprites moving 2 pixels a frame): halfway, the notes are 1 pixel on, nothing else
moved. The tilemaps (backgrounds) aren't carried on: their scroll registers could be, the same way.

### Firebeat (runtime/dev/k057714.cpp, `draw_between`)

The GCU draws with commands (fill a rectangle, copy an object from VRAM to a frame buffer with
scale, flip and blending, draw a character), from display lists in VRAM or written to its FIFOs.
The frame buffers are in VRAM too, and its four layers show them. The games draw a frame into a
back buffer and flip at vsync. Once in-between pictures are asked for, the GCU keeps, for each
frame:
- its commands, in order, and the frame buffer origin as it began;
- before a command first draws on a VRAM row in that frame, what the row held.

An in-between picture then puts those rows back as they were before the frame was drawn, runs
the frame's commands again with each object moved on, composes the layers as the display shows
them, and puts the rows back as they are. Each object command is matched with the nearest of the
frame before's with the same size, scale and flags, and either its picture or its height (a note
going off the lane is clipped shorter; an animated one changes picture), within 16 pixels.

Checked on pop'n music 4 (the attract demo's notes, objects moving 2 pixels a frame) and pop'n
music 8 (the How to Play, notes and sliding arrows): halfway, only the moving objects differ, 1
pixel on. The regress cases are identical with it built in: nothing is kept until a picture is
asked for. Costs: the rows a frame draws on are copied each frame (about 2.3 MB for pop'n's
three double-buffered layers), and each in-between picture copies them twice more and draws the
frame again (about 100 commands for pop'n).

Not covered:
- Keyboardmania (two GCUs, one a monitor): no in-between pictures.
- A game drawing a frame over two vsyncs (30 frames a second) moves its objects per two vsyncs,
  which the in-between pictures don't know: they would spread a frame's move over one vsync.
  Not seen in pop'n or beatmania III yet.
- Objects drawn from the CPU straight into VRAM (not through commands) aren't moved.

### Other games and boards

Any game on these two cores gets in-between pictures with no per-game work: pop'n music 1-8,
Animelo 1/2, Mickey Tunes, beatmania (DJ Main) and beatmania III. Whether they look right
depends on how each draws its notes (sprites or objects moving a steady step, which is usual).
To check a game:
1. find a stretch where notes move (attract demo or a play script);
2. dump the window's picture and an in-between picture at a few frames: on DJ Main,
   `--smooth-motion --snap-every N` writes `frameNNNNNN-half.ppm` beside each snapshot. Firebeat's
   in-between pictures go with the picture at the end of vblank (after the game's flip), not the
   frame hook's, so they need a dump there (a few lines in `vblank_end_fire`,
   runtime/firebeat/board.cpp, calling `render_between(0.5, w, h)`);
3. compare: only what moves should differ, by half its step.

To see how a Firebeat game draws: log each GCU command with its frame number (type, source,
destination, size, scale) and the layers at each vblank. pop'n music 4 runs one display list a
frame, kicked from its vblank interrupt: fill (clear) the sprite layers, then the objects.

The System 573 (the PlayStation's GPU), Twinkle and Python could have in-between pictures the
same way (match the frame's primitives), not done. For their clock-following games the `[[rate]]`
way is better: the pictures are the game's own.

## More vsyncs (`[[rate]]` mods)

For a game that follows a clock, the vsync itself can come faster: the game draws more pictures
a second, at their true moments. This needs a mod per game (docs/MODS.md: the format, and the
addresses for pop'n music 9-14), because what the game counts in frames (animations, timers,
effects) would run fast with it. Finding what a game needs, as done for pop'n music 9-14:

1. **The clock.** Find the word the play screen recomputes from a hardware timer each frame. On
   pop'n music 10-14 it's a function with a pause flag at +0x20, the timer read, a 64-bit
   subtraction, the result stored at +0 of the same block: a search for that shape in the code
   finds it in each version. Otherwise search RAM for a word that moves at a steady rate during a
   song only. `while_changing` ties the rate to it.
2. **The phase.** The rate must stop when the song ends (results and menus count frames). Find
   the play screen's state (a halfword it switches on, 4 during the song in pop'n music) and give
   it as `while_equal`.
3. **Check the notes.** At 120 and 240 Hz, in the attract demo or a play script stretched to the
   new rate, notes must be where they are at 60 Hz at the same moment of the song, and a play
   case's judgements the same.
4. **Frame counters.** Everything that still runs fast needs its increment patched to +0 on the
   vsyncs between the cabinet's ticks (`between_ticks`). Ways to find them:
   - the objects' update: one function adds each animated object's speed to its frame counter;
     reading the speed as 0 between ticks (pop'n: the `lh rt, 0x20(rs)` before `addiu ..., 100`)
     holds every object at once;
   - try batches of candidate increments (`addiu rX, rX, 1` near a store) as `while_rate` +0
     patches mid-run, and watch the counter (the Python core: `HK_PY_WATCH`); watch the clock too,
     so that a patch that stalls the game isn't taken for a hit;
   - a write watchpoint on the counter (PCSX2's memory checks set to log without pausing, a
     change kept out of the tree): finds counters updated through pointers, which the search
     misses. RAM dumps around the event (`HK_PY_EE_DUMP`) give the candidates;
   - a stack slot that changes isn't necessarily a counter: pop'n 14's "judgement timer" at
     0x01ffea20 was a draw call's spilled argument.
5. **Sound and pacing.** Display pacing follows the rate switch at once and the sound queue is cut
   back after it (runtime/host/pacing.cpp, window.cpp): nothing per game.

The Firebeat and DJ Main cores can change their vsync rate too (`Core::set_vsync_rate` on DJ
Main; Firebeat's frame period is `frame_ticks()`), but their pop'n music count frames, so it would
only make them faster: the in-between pictures are the way there.
