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

The game keeps its rate and runs exactly as before, at its own speed; nothing in the machine
changes. Display pacing keeps the game on the steady clock: each frame has a moment, one frame
period after the last, and gets the display's refreshes from its moment to the next frame's (at
144 Hz, a 58 Hz game's frames get 2 or 3; at 240 Hz 4 or 5; at 60 Hz 1, and every 30th frame 2).
Every refresh shows the frame with what moves carried on to that refresh's moment, a fraction of
a frame along its last move (the first refresh too: it comes after the frame's moment, up to a
refresh):

- `Core::render_between(t)` (runtime/shell/core.h) draws the last frame again, each moving thing
  `t` of a frame further along, into a picture of its own;
- the window (runtime/host/window.cpp) shows them on those refreshes through the same picture
  pipeline (CRT shaders and all), and commits each refresh with the compositor's fifo protocol
  (runtime/host/pacing.cpp: `frame_exact` gives the frame's refreshes, `phase(i)` each one's
  moment, `commit_extra` commits it). The rest is display sync's (the commit's lead, the burst
  budget, the grid kept on the compositor's vblanks; the sound steered to its queue at speed 1).
  Without fifo-v1 (KWin 6.7), or with display sync off, nothing changes;
- the exit report says how many it showed (`video: smooth motion: N in-between pictures shown`).

Use it: `--smooth-motion` (display sync on, the default), or the launcher's Settings > "Smooth
motion (DJ Main and Firebeat games)". Any refresh rate works, 144 Hz too, and the game keeps its
exact speed and pitch (without smooth motion, display sync speeds pop'n 4-8 up 2.6 % to fit
60/120/240 Hz, and leaves DJ Main's 58.00 Hz, 3.4 % off, juddering). The faster the display, the
smoother; at 60 Hz it mostly evens out the frame that would be held twice every 30 frames.
`HK_PACING_DEBUG` prints each frame's refreshes and their moments.

Checked in a virtual KWin (60 Hz): pop'n 3 and pop'n 4 at their own 58.00 and 58.49 Hz, every
refresh on time, each refresh's moment 1/30 of a frame on from the last's (60 against 58 Hz).

It's a guess one frame ahead: a note's next position is where its last move takes it. Notes
scroll at a steady speed, so the guess is right while they keep moving; where something stops,
turns or appears, its in-between pictures carry it on up to a frame too far (at most a
refresh's worth of time) before the next frame puts it right. The judgement, the sound and
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
