# Playing beatmania III The Final with HIRAKU

## Adding a game

Drop the game's files on the launcher's window, or use **File > Add game files** (or
**Add a game folder**). From a terminal, `tools/ingest.py FILE...` or `hiraku --add FILE...`
does the same.

The files can be:
- the MAME ROM set (`.zip`, `.7z`, or a folder of its files);
- the game's CHDs, which are unpacked with MAME's chdman (installed, or MAME's flatpak) into
  `dumps/<game>/`;
- disc and disk images (`.cue`/`.bin`, `.iso`, `.img`).

Give them all at once, in any order. The ingester recognises the game from:
- for Firebeat, the program on the CD;
- otherwise, the set's name or a CHD's name.

It also tells each image's role from what it holds: a hard disk, a DVD, or a second CD. The
sections below give each game's files, and the manual form with the roles spelled out
(`--romset`, `--hdd`, `--dvd`, `--cd2`).

CHDs need room once unpacked:
- beatmania III's hard disk: 21 GB;
- a pop'n music DVD: 4.6 GB;
- a CD: under 700 MB.

## Once: build and ingest your dumps

```bash
cmake -S . -B build/cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build/cmake hiraku-bm3final
tools/ingest.py /path/to/cd.cue --romset /path/to/bm3final.zip --hdd /path/to/hdd.img
analyze/ghidra.sh bm3final && analyze/prep.py bm3final && ninja -C build/cmake hiraku-bm3final
```

**Optional, once your games are ingested:** `tools/pgo.py` rebuilds with profile-guided optimisation, trained on the regression cases of the games you have. It takes about 15 minutes. Results are identical, and some games need much less CPU: Keyboardmania −19 %, DDR −27 %, pop'n −3 %. Run it again after updating HIRAKU.

The ingester writes `dumps/bm3final/`: the unpacked program, `machine.txt` (disc and HDD paths) and `rom/` (dongle image, default NVRAM, sound program). It needs:
- the GCC01 program CD;
- the MAME `bm3final` ROM set;
- the raw HDD image.

## Play

The launcher is the easy way:

```bash
build/cmake/hiraku
```

Pick the game and press **Play**. **Controls** and **Lights** map your keyboard, controllers and controller LEDs, and **Settings** holds the window, audio and folder options. A game that isn't ready shows what it still needs. On a first boot, the game's test-mode steps are shown under the list.

Or start a game directly:

```bash
build/cmake/hiraku-bm3final --window
```

The machine boots like a cabinet. The skipped BIOS time passes instantly. Then the game runs its own STARTUP checks for about 20 s; hold **Tab** to fast-forward through them.

| Key | Action |
|---|---|
| Z S X D C | P1 keys 1–5 |
| Left / Right (or Left Shift / A) | P1 turntable |
| Space | P1 foot pedal |
| 5 | coin (the game wants 2 for one player) |
| 1 | P1 start |
| F2 | test mode, 9 service |
| Tab (hold) | fast-forward |
| F11, Alt+Enter | fullscreen |
| F10 | picture: off, CRT light, CRT full |
| Ctrl+1 … Ctrl+9 | type one of the game's codes (below) |
| Esc | quit |

P2 uses M K Comma L Period, Up/Down and 2.

**Navigating:** turn the turntable to choose, and press a white key (1, 3 or 5) to confirm. Start toggles DOUBLE on the option panel.

**Unlocks:** right-click the game > **Unlocks** applies the COLOR CHECK code. It unlocks:
- Miracle Moon, CLOUDY MUSIC, RETROFUTURE, The Least 100 sec and COREDESAT;
- IMPLANTATION's ANOTHER chart.

The title screen then shows the alien icon. The floppy-disk unlocks (RETROFUTURE via SIMON,
CLOUDY MUSIC, sozai mode) are earned per player and listed there too.

**Codes, typed for you:** the button codes players typed on the cabinet (hidden songs, modes),
for the games that have a `games/<id>/macros.txt`.
- **In the window:** Ctrl+1 to Ctrl+9 type the game's first to ninth code where you are now, for
  example on the title screen with credits in. The window title says "typing" until it's done.
- **The list:** right-click the game > **Unlocks** shows each code with its key and where to
  type it. A code the game reads at power-on can be set to type itself at every start there.
- **From a terminal:** `--macro NAME` runs one at its own frame, `--macro NAME@FRAME` at a given
  frame.

**Saves:** NVRAM (operator settings, rankings) lives in `~/.local/share/hiraku/bm3final/`. It is saved on Esc, on window close and on Ctrl-C. Use `--nvram DIR` to keep it elsewhere.

**Persistence is unconfirmed.** In MAME and in HIRAKU alike, the scripted sessions so far never made the game write its NVRAM. It probably writes only when a setting or a ranking changes, and none of those sessions changed either. Check whether a test-mode change or a new high score survives a restart.

## The picture

**Picture** in the toolbar picks how the game looks:

| Mode | What it is | Cost |
|---|---|---|
| **Off** | sharp pixels (the default) | none |
| **Light** | guest(r)'s crt-guest-advanced-fast: scanlines, a phosphor mask, glow | small |
| **Full** | TroggleMonkey's crt-royale: beam shapes, real phosphor-mask textures, bloom, halation, optional curvature | the heaviest |

F10 cycles the three while you play, and the window's title says which one is on.

**Settings for:** you can set the mode, and each shader's settings, for all games or for one.
To do it for one game, select it first, or right-click it > **Picture for…**. A game's own
settings only hold what differs from all games', and **↺** puts a slider back.

**Live changes:** the sliders change a running game at once, and **Cancel** puts everything
back.

**480-line games:** HIRAKU draws Firebeat's 640×480 progressive, with a scanline on every
line, like the cabinets' monitors. Both shaders would otherwise treat 480 lines as interlaced
(480i, as on a TV). To get that look back, set guest's **Interlace Mode** to 1, or turn on
royale's **Interlacing - Toggle**.

**Files and options:**
- The settings live in `~/.config/hiraku/picture/` (all games) and
  `~/.config/hiraku/picture/games/<game>/`. Each holds `<shader>.params` files of
  `name = value` lines, and a RetroArch preset's parameter lines work as is.
- From a terminal: `--picture off|light|full` and `--picture-config DIR` (repeatable: later
  folders win).
- When a game exits, it prints each mode's GPU time per frame. That is the number to watch
  on a small GPU.
- If OpenGL 3.3 isn't available, the game says so and shows sharp pixels.
- The shaders themselves: `shaders/README.md`.

## Controllers

Joysticks are opened at start and when plugged in. The console lists them as `joy0`, `joy1` and so on.

The defaults map `joy0` buttons 0–4 to the keys, axis 0 to the turntable, button 7 to the pedal, 8 to start and 9 to coin.

IIDX-style controllers differ in which button and axis numbers they use. The easiest way to map one is the launcher's **Controls** dialog: click a binding, then press the button or spin the turntable. Pressing anything already bound lights up its row. The dialog saves `~/.config/hiraku/controls/<cabinet>.txt`, which the launcher passes as `--bindings`.

By hand: write a bindings file and pass `--bindings FILE`. See `docs/bindings-bm3.example`; the format is in `runtime/host/input.h`.

**Lights:** `--lights FILE` drives HID controller LEDs from the cabinet lamps, and `--lights-socket PATH` publishes them for other tools. See [LIGHTS.md](LIGHTS.md).

## Latency

**In the game:** HIRAKU measures it as the time from a key press to the RF5C400 starting the keysound. beatmania III scans its keys once per video frame (59.94 Hz), so this is **2–20 ms, mean about 9.5 ms**, exactly as on a real cabinet. Everything above that is the PC.

**In real-time mode, audio is the clock.** The machine runs until 8 ms of sound is queued (`--audio-ms N`), and controls are read every couple of milliseconds while it waits. Total latency is therefore roughly:

> 2–20 ms (game, frame-based) + `--audio-ms` (8) + the audio device buffer (128 frames requested = 2.9 ms) + your sound server

On exit, the console prints the buffer it actually got.

**The queue tunes itself.** It starts at `--audio-ms` (8) and grows by 1 ms after each underrun, up to 30 ms, so it settles at the smallest crackle-free lead this PC needs. The exit report prints where it settled, how many underruns there were, and the worst stall.

**Sound server settings:** the first reports came before the performance fixes in this release. Re-test with the defaults first. On PipeWire the server quantum adds its own latency; a smaller one (`PIPEWIRE_LATENCY=256/48000`) is worth trying, but only if the default is already clean.

## Frame pacing

The games don't run at 60 Hz. Each sets its own video timing, which HIRAKU runs as MAME does:

| Games | Frames per second |
|---|---|
| beatmania III | 59.94 (VGA timing, 25.175 MHz) |
| pop'n music 4–8, Animelo 1/2 | 58.4866 (16.6666 MHz pixel clock) |
| Keyboardmania | 58.51 |

**Smooth frames on a fixed-refresh display (the default).** A 60, 120 or 240 Hz display can't
show 58.49 frames a second evenly: some frames would stay up a refresh longer than others, and the
notes judder. So when the display's rate is within 3 % of a whole multiple of the game's, HIRAKU
runs the game on the display's refreshes:
- Every frame stays up the same number of refreshes (1 at 60 Hz, 2 at 120 Hz, 4 at 240 Hz).
- The game runs at the display's rate: pop'n and Keyboardmania **2.6 % faster** at 60/120/240 Hz,
  beatmania III 0.1 %. The sound plays at the same speed, so it's 2.6 % higher in pitch. The
  game itself is unchanged: its judgement, charts and sound stay in step with each other.
- On Wayland, HIRAKU learns from the compositor when each frame reached the screen, and finds
  how late it can hand a frame over (the lowest latency that still makes the refresh). With
  KWin 6.7's fifo protocol, 99.8-99.97 % of frames stay up exactly their refreshes (measured at
  144 and 240 Hz); the rest are the compositor's own.
- The console says what it chose (`display sync: on: 60.00 Hz display, 1 refresh per frame, the
  game at +2.6 % speed`), and the exit report's `video: display sync` line how it went.

**When it's off:**
- **A display that doesn't fit**, such as 144 Hz (58.49 × 2 = 117, × 3 = 175): frames are shown as
  they come and judder (2 and 3 refreshes alternately). Set the display to 60, 120 or 240 Hz.
- **Variable refresh (FreeSync, G-Sync, HDMI VRR):** turn display sync off (launcher: Settings >
  "Smooth frames on a 60, 120 or 240 Hz display"; or `--display-sync off`). The display then
  follows the game at its exact speed. In KDE: enable FreeSync in the monitor's menu, set System
  Settings > Display > Adaptive sync to Automatic, and play fullscreen (F11).
- **Fast-forward** (Tab).

**Free-running (display sync off):** audio is the clock, and each frame is shown as soon as it's
drawn. The exit report's `video:` line shows how evenly: at the game's rate (mean error
-0.005 ms), within about ±3 ms (sd 1.3 ms, the sound card's 2.8 ms blocks). On a fixed-refresh
display: at 60 Hz one frame shown twice every 0.66 s; at 144 Hz frames alternate 2 and 3
refreshes; at 240 Hz about one frame in ten stays up 5 refreshes instead of 4.

## Recording and testing

```bash
build/cmake/hiraku-bm3final --script oracle/scripts/bm3final-practice.his --wav out/practice.wav   # scripted, headless
build/cmake/hiraku-bm3final --frames 3600 --snap-every 300                                         # PPM snapshots
```

## pop'n music 4–8 and Animelo 1/2 (in progress, M7)

Each game needs its program CD (Disc 1), its data DVD (Disc 2) and its MAME ROM set (`popn4` … `popn7`). The ROM set holds three things:
- the dongle image;
- the SPU's 68000 program, `a02jaa04.3q`;
- the default RTC.

```bash
tools/ingest.py "Pop'n Music 4 (Japan) (Software Disk 1).cue" --dvd "Pop'n Music 4 (Japan) (Software Disk 2).iso" --romset popn4.zip
analyze/ghidra.sh popn4 && analyze/prep.py popn4 && ninja -C build/cmake hiraku-popn4
build/cmake/hiraku-popn4 --window
```

**Status:** pop'n 4, 5, 7 and 8 boot to attract mode with sound (pop'n 8: music select and full songs tested): boot checks, loading from the DVD, title, demo play. Standalone `.7z` ROM sets are accepted.

**pop'n 6, first boot:** it installs SPU data to flash, which takes about 14 minutes of game time (hold **Tab** to fast-forward). Then it boots normally.

Animelo 1 and 2 ingest the same way: `--dvd` takes the `extractdvd` ISO of the `02` CHD.

| Key | Action |
|---|---|
| Z S X D C F V G B | buttons 1–9 (odd = lower row) |
| 5 | coin |
| F2 / 9 | test / service |

Joystick buttons 0–8 are the nine buttons; button 9 is the coin.

**pop'n 8 unlocks:** in the launcher, right-click pop'n music 8 > **Unlocks**, pick the event
phase, and **Apply** (the game must not be running).
- **MAX** unlocks everything: the LIVE songs, EX charts, the Sugoroku de 8 songs and
  characters, expert courses, and a "secret" category with the title-screen songs.
- **Apply** writes into the save exactly what the game writes when an operator types the
  phase codes in EVENT OPTIONS, and the old save is kept beside it as `rtc.nv.<date>.bak`.
- The game needs a save first: start it once, through its first-boot setup.
- From a terminal: `tools/unlock.py popn8` lists the phases, and `tools/unlock.py popn8 phase=4`
  sets one.
- The per-credit codes (the LIVE button code, the title-screen timing) are listed there too.

**Sugoroku de 8, the event itself:** set the phase to **1** (or 2) and the game runs its 2002
unlock event, which HIRAKU plays unchanged:
- **The requirement:** before each round a roulette draws one (スゴロク・ノルマ), such as "clear
  a new song" or "score 70000".
- **The board:** if your round meets it, a dice roll (1–6) moves KRAFT around Japan, a board of
  1,004 spaces through 18 cities.
- **Unlocking a city:** the first time around, you get a city's unlock by landing on it exactly.
  A city you pass is given on the next pass.
- **The three stages:**
  1. the songs;
  2. the courses and island songs;
  3. the characters, ending with the HELL the 8 course.
- **No calendar:** the game never looks at the date, so play at your own pace.
- **Progress** is saved with the game. The Unlocks dialog (or `tools/unlock.py popn8`) shows the
  stage, KRAFT's space and lap, the next city, and the cities passed without landing.
- **Skipping it:** the phase codes were the operators' way to skip the event, and phase 3 or
  MAX releases everything at once.

**pop'n 6 unlocks:** right-click pop'n music 6 > **Unlocks**. Each of the operator's five
switches is a checkbox:
- SECRET MUSIC RELEASE;
- SECRET COURSE RELEASE;
- ALL DATA RELEASE (EX charts, RGB mode);
- STEP UP CHALLENGE and STEP UP CHALLENGE 2.

**Apply** writes what the game writes when they are typed in test mode and switched on
(`tools/unlock.py popn6 operator=all` for all of them). With SECRET MUSIC on, the music select
has the SECRET songs.

**pop'n 7 unlocks:** right-click pop'n music 7 > **Unlocks**:
- **How pop'n 7 differs:** its operator codes don't unlock anything by themselves. Each code adds
  a switch to EVENT OPTIONS, and the switch must be YES. The dialog's levels do both, in the
  operator's order.
- **The levels:** 1 runs the BINGO de 7!! event, 2 adds the secret songs, 3 the bingo songs,
  and MAX the expert-course songs.
- **BINGO de 7!!:** a 5×5 card.
  - After every stage, each square whose condition the stage met opens. The screen shows one
    hint each credit.
  - Each full line (12 of them) gets an item.
  - The dialog lists the closed squares with the game's own name for each condition (for
    example `COMBO 573 CLEAR`), and which lines are one square from done.
  - Like pop'n 8's event, it never looks at the date.
- **The checksum:** the save's checksum is kept right, and the old save stays beside it.

**pop'n 4 and 5 codes:** Ctrl+1 to Ctrl+8 on the title screen, with credits in, then START as
usual. Several can be entered for one credit, about two seconds each.
- **On one title screen:** with credits in, the title still cycles through the KONAMI logo about
  every 25 s, and it forgets the codes when it comes back. Type them and press START before that.
- **pop'n 4:** the hidden songs BRITPOP, HORROR and GROOVE ROCK; the POPS, J-TEKNO, DANCE and
  BNSTRK remixes; the special ending track.
- **pop'n 5:** the hidden songs LESSON and スペシャルエンディング (a service track); ENKA, NEO
  ACO, GIRLY and POSITIVE REMIX; the hidden characters SANAE-chan and Ash.
- Both games' lists hold a special-ending code that RemyWiki doesn't have: `2179223718`.

**Animelo 2 codes:** Ctrl+1 (DOUBLE EXCITE) or Ctrl+2 (TRIPLE EXCITE) on the title screen with
credits in, then START and choose EXCITE. NON STOP mode isn't there: this version (JAC) still
checks its code but never turns it on.

## ParaParaParadise (in progress, M8)

You need the program CD (`a11jaa01`), the audio CD (`a11jaa02`) and the MAME `ppp1mp` ROM set, which holds the dongle image `gqa11-ja`. To unpack MAME's CHDs:

```bash
chdman extractcd -i a11jaa01.chd -o a11jaa01.cue -ob a11jaa01.bin
chdman extractcd -i a11jaa02.chd -o a11jaa02.cue -ob a11jaa02.bin
tools/ingest.py a11jaa01.cue --cd2 a11jaa02.cue --romset ppp1mp.zip
analyze/ghidra.sh ppp1mp && analyze/prep.py ppp1mp && ninja -C build/cmake hiraku-ppp1mp
```

**First boot, as on a new cabinet:** the STARTUP PROGRAM reports a blank backup RAM (E160) and blank sound flash (E124). Press **F2** for test mode, then:
1. run **SOUND FLASH INIT**, which copies the YMZ280B samples from the CD;
2. run **ALL FACTORY SETTINGS**.

With `--window` both are saved to `~/.local/share/hiraku/ppp1mp`.

**Status:** with the ROM set, 1st Mix Plus plays: coin, song select, a full song with its CD music. ParaParaParadise (`ppp`), v1.1 (`ppp11`) and ParaParaDancing (`ppd`) ingest the same way with their own discs and sets.

| Key | Action |
|---|---|
| Z X C V B | sensor bars 1–5 (left to right) |
| Left / Right | the two buttons (test menu: up / down) |
| 1 | start / OK |
| 5 | coin |
| F2 / 9 | test / service |

## Keyboardmania 1st/2nd/3rd, Keyboardheaven (in progress, M10)

Each game needs its two discs (the program CD and a second disc with CD audio) and its MAME ROM set (`kbm`, `kbh`, `kbm2nd`, `kbm3rd`):

```bash
tools/ingest.py gq974-ja-c01.cue --cd2 gq974-ja-a02.cue --romset kbm.7z
analyze/ghidra.sh kbm && analyze/prep.py kbm && ninja -C build/cmake hiraku-kbm
```

**Screens:** both portrait monitors are shown side by side (MAME's ROT270).

**First boot:** the backup data is blank (E161) and, on some versions, the sound flash too. Press **F2** for test mode:
- **1 / 2** move the cursor;
- **1 + 2 together** enter an item;
- **BACKUP CLEAR**, then **1** to confirm, sets the defaults;
- **SOUND TEST → SOUND FLASH INIT**, then **1**, copies the sound effects and menu sounds from the CD into the sound flash. Without it the coin and menu sounds are silent.

2nd and 3rd Mix have a different menu with the same two steps:
- **2nd Mix:** SOUND CHECK → INITIALIZE SOUND FLASH, and ALL FACTORY SETTINGS.
- **3rd Mix:** SOUND OPTIONS → INITIALIZE SOUND FLASH, and ALL FACTORY SETTINGS.

Each asks three times. Answer "YES?" and "ARE YOU SURE?" with **1** (move to YES) then **1 + 2**, and "PRESS 1P&2P" with **1 + 2**.

| Key | Action |
|---|---|
| Q W E R T Y U I O A S D F G H J K L Z X C V B N | player 1's 24 keys (C1–B2) |
| Keypad 7 8 9 4 5 6 1 2 3 0 . Enter | player 2, C1–B1 |
| Insert Home PageUp Delete End PageDown, Left Down Right Up, Keypad / * | player 2, C2–B2 |
| 1 / 2 | 1P / 2P start |
| 5 | coin |
| F2 / 9 | test / service |

**Status:** all four boot to attract mode, fully native, with CD and YMZ280B sound.

**Keyboard notes:** they come from the XT446 synth card, an embedded Yamaha MU100B. HIRAKU runs the card itself: the MU100 firmware on MAME's H8S/2655 and SWP30 emulation (`runtime/dev/xt446.cpp`, `third_party/mame`). It needs the card's ROMs from the MAME set in `dumps/<game>/rom/`, which `tools/ingest.py --romset` copies: `xu50710-m27c160.bin`, the six wave ROMs `sx518b0.ic34` … `xt463a0.ic38`, and `sin-table.bin`. It costs about a third of a CPU core.

- **No card ROMs, or `--synth-stand-in`:** the MIDI plays through FluidSynth with a General MIDI SoundFont instead. The notes and timing are the game's own, but the instruments are the SoundFont's.
  - Install a SoundFont with `sudo pacman -S soundfont-fluid`, or point at any `.sf2` with `--soundfont FILE` or `HK_SOUNDFONT`.
  - `HK_SYNTH_GAIN` sets the stand-in's level (default 0.25).
- `HK_MIDI_LOG=1` prints the MIDI stream.
- `build/cmake/hiraku-xt446 ROMDIR OUT.wav --midi LOG` runs the card on its own. LOG has one `<seconds> <hex byte>` per line.
- The card is MAME's emulation from a later master commit than the 0.289 oracle, for the SWP30's accuracy fixes (third_party/mame/README.md).

**Unlocks:** right-click the game > **Unlocks**.
- **Keyboardmania 3rd:** the startup code (PRESSURE mode and three hidden songs).
- **Keyboardmania 2nd:** PRESSURE mode, normally unlocked by a long wheel hold on the 2P side.
- **Playing PRESSURE:** once it's unlocked, hold the wheel down on SELECT PLAY MODE until
  PRESSURE appears below FREE.

## DJ Main: beatmania, pop'n music 1 (in progress, M13)

DJ Main games aren't recompiled: the board's 68EC020 runs at 16 MHz and is interpreted (Musashi), which costs about 1 ms of host time per frame. Each game needs its MAME ROM set (program, sprite and tile ROMs) and its hard disk as a raw image:

```bash
chdman extracthd -i 753jaa11.chd -o 753jaa11.img     # once; chdman comes with MAME
tools/ingest.py --romset bm1stmix.7z --hdd 753jaa11.img
ninja -C build/cmake hiraku-bm1stmix
build/cmake/hiraku-bm1stmix --window
```

pop'n music 1 (`popn1j`) is the same with `803jaa11` and `popn1j.7z`. The game is found by the set's name (`games/<id>/game.toml`, `core = "djmain"`).

**Every MAME DJ Main set has a `games/<set>/game.toml`** (29, generated from MAME's driver), so any of them is ingested the same way:
- beatmania 1st mix → THE FINAL with the hiphopmania and beatstage versions;
- pop'n music 1–3 and pop'n stage EX.

The disk each set needs is its `[hdd] chd`; clones may share their parent's disk. Things to know:
- **complete MIX and later lock their hard disk with an ATA password.** HIRAKU unlocks it as MAME does: the passwords from MAME's driver are in the `game.toml`, and the game sends them itself.
- **The UA/KA/AA versions and pop'n music 1 TA/AA/KA use other address maps** (`map = "u"` / `"a"`).
- **pop'n stage EX** has ten buttons and three selection buttons (left, OK, right).
- **Run so far:** bm1stmix, bm2ndmix, bm3rdmix (and EA), 4th–7th MIX, complete MIX (AA), complete MIX 2, CORE REMIX, DCT, THE FINAL, pop'n 1–3. All reach attract mode. 1st–3rd MIX and pop'n 1 match MAME clock for clock; the later ones drift from MAME after about 75 s (PLAN M13).

**Status:** HIRAKU runs both games in step with MAME, clock for clock (`oracle/scripts/*-boot.his`, 155 s):
- power-on, self-test, the disk loads and attract mode;
- a whole game of each:
  - beatmania (`oracle/scripts/bm1stmix-play.his`, 217 s): coins, the turntable at the practice-stage prompt, song select, a song played with keys and scratches, game over;
  - pop'n music (`oracle/scripts/popn1j-play.his`, 233 s): coins, game mode and genre select, a song played over all 9 buttons, game over.

In these runs:
- the work RAM, sprites and palette are identical to MAME's on every frame;
- every snapshot is pixel-identical;
- the sound is sample-identical: HIRAKU plays and records it at the chips' own 48 kHz (`--wav` matches MAME's `-wavwrite`).

**Not yet:** a hands-on play test, turntable feel, and lockstep with MAME for 4th MIX and later.

| Key | beatmania | pop'n music |
|---|---|---|
| Z S X D C | player 1 keys 1–5 | buttons 1–5 (Z S X D C F V G B: 1–9) |
| Left / Right, Left Shift / A | turntable −/+ | — |
| M K , L . | player 2 keys | — |
| 1 / 2 | 1P / 2P start | — |
| E | effect | — |
| 5 | coin | coin |
| F2 / 9 | test / service | test / service |

A controller's analog axis drives the turntable (`P1 Turntable = joy0:axis0`). MAME models the turntable as an 8-bit dial whose steps the board counts 70 at a time; HIRAKU does the same for now, so scripted runs match MAME.

**Checking against MAME:**

```bash
HIRAKU_ROMS=~/bm3mame/roms-cores HIRAKU_TAPS=none oracle/mame.sh bm1stmix oracle/scripts/bm1stmix-boot.his boot
build/cmake/hiraku-bm1stmix --script oracle/scripts/bm1stmix-boot.his --out out/run/bm1stmix-boot
oracle/snapcmp.py out/run/bm1stmix-boot out/oracle/bm1stmix/boot
```

The same with `bm1stmix-play.his` or `popn1j-play.his` checks a game played through.

## System 573: Dance Dance Revolution 2ndMIX (in progress, M14)

DDR 2ndMIX (`ddr2m`) runs on HIRAKU's System 573 core: MAME's PS1 devices with the CPU interpreted, about 2–3.5 ms of host time a frame. It needs MAME's `ddr2m` set (BIOS, the cassette's X76F041) and the CD `895jaa02`:

```bash
tools/ingest.py ddr2m.7z 895jaa02.chd
ninja -C build/cmake hiraku-ddr2m
build/cmake/hiraku-ddr2m --window
```

**Faster with profile-guided optimisation:** `tools/pgo.py` (see "Once: build and ingest your dumps") makes the 573 games need ~27 % less CPU (ddr2m: 1.76 ms a frame instead of 2.41 on the desktop).

**The first boot installs the game to the board's flash:**
1. The game asks "DO YOU WANT TO INITIALIZE FLASH-ROM?": press Test (F2).
2. Erasing and writing take about 2.5 minutes.
3. "FLASH-ROM INITIALIZE COMPLETE": press Test again.

Later boots check the flash (about 80 s, as the real board does) and go to attract mode. The flash lives in the game's NVRAM folder like any other save.

**Coins:** two coins make a credit.

| Key | Dance Dance Revolution |
|---|---|
| Arrows | player 1's panels (a dance pad's buttons 0–3: left, down, up, right) |
| Keypad 4 2 8 6 | player 2's panels |
| 1 / 2 | 1P / 2P start |
| 5 | coin |
| F2 / 9 | test / service |

A dance pad is bound in the launcher's Controls (Dance Dance Revolution page): click a panel, then step on it. Pads that send their arrows as buttons, a hat or axes all work. The panel buttons 1–3 are unbound by default.

**Lamps:** the analog I/O board's outputs as MAME names them (foot panels, start lamps, the four body lamps, the speaker neon), through the lights socket and HID lamps (docs/LIGHTS.md).

**Sound:** the SPU and the CD's music in MAME's balance, the CD at the volume the game sets with MODE SELECT.
- **Played at half level, which never clips.** MAME sums the two at full level and clips the sum, and in DDR they reach twice full scale. That crackles on loud songs: about 1 % of all samples in a game, and a third of them on the loudest. The real board mixes them in analog.
- `--mame-mix` gives MAME's level and clipping back, for comparing with MAME's recordings (`oracle/audiosync.py`).
- The exit report's `sound:` line gives the mix's peak and how much of it clipped.

**Checked against MAME, clock for clock** (`oracle/scripts/ddr2m-boot.his`, `ddr2m-play.his`): the first boot and flash install, attract mode, and a game. That game is coins, mode and song select, and a song stepped through to CLEARED. Main RAM is identical on every frame, every snapshot is pixel-identical, and the sound matches MAME's recording (with `--mame-mix`). Both are regress cases.

**Not yet:** a hands-on test with a pad.

## Konami Python 1: pop'n music 9 (in progress, M16)

pop'n music 9 (`popn9`) runs on HIRAKU's Python core: PCSX2 Reliquary's emulator hosted by HIRAKU's shell (PLAN.md §21, docs/PYTHON.md). It is built inside Reliquary's own build. You need:
- Reliquary's source, `pcsx2-reliquary-1.9.4-reliquary.zip`, unpacked to `$HK_PYTHON_WORK/pcsx2-reliquary` (default `/run/media/roganis/DATA/Python-work`);
- the set as Reliquary takes it: `popn9.py1` and its folder (the disk CHD, the interface board's ROMs, BBSRAM, dongle, the KN00002 memory card), plus the arcade BIOS (`COH-H31000.BIN`, `.NVM`, `.MEC`) and the MagicGate key files. The ingester finds `bios/` and `keys/` next to the sets' folder, or takes `--bios DIR --keys DIR`.

```bash
oracle/pcsx2/build-reliquary.sh all     # once: Reliquary with HIRAKU's patch, and hiraku-popn9 (~3 min, memory-capped)
tools/build-libchdr.sh                  # once: the CHD reader the ingester uses
tools/ingest.py ~/Downloads/konami-python-1-collection/kpython1/popn9.py1
build/cmake/hiraku-popn9 --window --renderer vk
```

- `--renderer vk` draws with Vulkan: less CPU. The default, `sw` (software), is what the reference runs and what `tools/regress.py` checks.
- The CHD stays where it is (linked, not unpacked). The game's writes (BBSRAM, dongle, the disk's own writes as an overlay) go to the NVRAM folder, `~/.local/share/hiraku/popn9`.
- The keys and the BIOS are copied into `dumps/popn9/`, which is never committed. Reliquary's habit of printing the derived keys is off in HIRAKU's build.
- Boot takes about 50 s (BIOS, card, disk), as on the real cabinet.

| Key | pop'n music |
|---|---|
| Z S X D C F V G B | buttons 1–9, left to right (white, yellow, green, blue, red, blue, green, yellow, white) |
| 5 | coin (two coins make a credit) |
| F2 / 9 | test / service |

In test mode, blue (buttons 4/6) moves and red (5) selects; I/O CHECK > INPUT CHECK shows SW1–SW9 lighting for buttons 1–9.

**Not yet:** lamps (the interface board's outputs aren't decoded), the board's own CPU (Reliquary answers its FireWire commands directly), other Python games (they need their own `games/<id>/game.toml`; the ingester tells you the program's SHA-1).
