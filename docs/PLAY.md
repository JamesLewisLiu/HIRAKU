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

## System 573: Dance Dance Revolution 3rdMIX (in progress, M14)

DDR 3rdMIX (`ddr3mj`) is the first game on the 573's **digital I/O board**: the music is MP3, decrypted by the board's FPGA and decoded by its MAS3507D. It needs MAME's `ddr3mj` set and the CD `887jaa02`. The set carries the BIOS, both cassettes (the game's and the install one, each an X76F100 plus a DS2401) and the board's DS2401.

```bash
tools/ingest.py ddr3mj.7z 887jaa02.chd
ninja -C build/cmake hiraku-ddr3mj
```

**The first boot needs the install cassette**, as on a real cabinet (MAME's note 3 in `ksys573.cpp`):
1. **Play** in the launcher. With no saves yet, it puts the install cassette in the slot (the details pane says so). The game installs itself with no input: the CD to the onboard flash, then to the 32 MiB PC Card. That takes about 4½ minutes of game time (hold **Tab** to fast-forward).
2. At **INFO 506** ("install complete, put the GN887 cassette back and power-cycle"), stop it.
3. **Play** again: it boots with the game's cassette, checks the flash and goes to attract mode.

If an install was stopped before INFO 506, the game shows **ERROR 147**: right-click it, **Boot once with the install cassette**, and Play. From the command line, the install cassette is `--cassette install`:

```bash
build/cmake/hiraku-ddr3mj --cassette install --window
```

Without the install, the game stops at **ERROR 147**. The flash and the PC Card live in the game's NVRAM folder like any other save.

Controls, coins and lamps are DDR's, as for 2ndMIX above. The lamps come from the digital board's outputs.

**Sound:** the SPU plus the MP3, in MAME's balance and played at half level (`--mame-mix` for MAME's level).

**Checked against MAME, clock for clock** (`oracle/scripts/ddr3mj-install.his`, `ddr3mj-attract.his`, `ddr3mj-play.his`): the install, then from what it left, attract mode and a game.
- The game: coins, Start, the selects timed out to XANADU, the song stepped through to its result, then stage 2.
- Main RAM is identical on every frame, and the NVRAM after the install is byte for byte MAME's.
- The sound matches MAME's recording: lag 0, correlation ≥ 0.999.
- Each is a regress case: `ddr3mj-attract` and `ddr3mj-play` start from the NVRAM `ddr3mj-install` leaves.

**Speed** (desktop, recompiled R3000 code): attract 0.48 ms a frame, a song 0.62 ms.

**Not yet:** a hands-on test. MAME flags this set NOT_WORKING without saying why; so far it plays as MAME does.

## System 573: DrumMania 10th Mix (M14, further than MAME)

DrumMania 10th Mix (`drmn10m`) needs MAME's `drmn10m` set, the runtime CD `d40jaa02` and the multi-session unit's CD `d40jba02`:

```bash
tools/ingest.py drmn10m.7z d40jaa02.chd d40jba02.chd
ninja -C build/cmake hiraku-drmn10m
```

**The first boots:**
1. The first boot installs the CD to the onboard flash by itself and shows **INFO 508** (power-cycle): stop it.
2. The next boot shows **NOTE 605** (RTC-RAM cleared): press **Test** (F2).
3. The boot checks load the game's data, then wait for the **multi-session unit**, which boots from its own CD meanwhile: about two and a half minutes from power-on.
4. Then the warning screen, the title and attract. Two coins (5) make a credit.

The unit is a box of its own: a second CPU (a TX3927), a CD drive, four MP3 decoders and four UARTs. The game won't start without it. MAME's is an empty stub, so MAME stops at **"Please call attendant"** (likely why it flags the set NOT_WORKING). HIRAKU runs windyfairy's emulation of it (third_party/mame/README.md). In a one-cabinet game the music comes from the digital board's MP3, as on DDR. The unit's decoders are for session play with GuitarFreaks (below).

**Speed:** about 2.8 ms a frame on the desktop, the unit and the 573 together (p99 6.5 ms). The unit's CPU is interpreted and runs at 25 MHz, as in the fork. Its timers are computed on demand and its RTOS's idle loop is skipped. The real chip's 132 MHz (`HK_MSU_132=1`) changes nothing in the game and costs about 7 ms: the firmware's decoder task polls through the RTOS as fast as the CPU goes.

| Key | DrumMania |
|---|---|
| S / D / J / K / L | hi-hat, snare, high tom, low tom, cymbal |
| Space | bass pedal |
| Left / Right | select |
| 1 or Return | start |
| 5 | coin |
| F2 / 9 | test / service |

**Checked against MAME, clock for clock** (`oracle/scripts/drmn10m-install.his`, `drmn10m-boot.his`):
- the install: main RAM is identical on every frame, and the NVRAM after it is byte for byte MAME's;
- the boot, until the game first uses the link to the unit (28 s in): main RAM is identical on every frame.

From there on the unit answers, which MAME's can't.

The regress cases are `drmn10m-install`, `drmn10m-boot` (now on to attract), `drmn10m-play` (a game: the first stage of 花火), `drmn10m-hdd` (the network unit's whole install) and `drmn10m-npu` (a boot with the unit and its disk, to attract).

**The network PCB unit (e-AMUSEMENT), started:** the e-AMUSEMENT cabinet's network unit, with its hard disk as the save folder's `npu_hdd/` folder (runtime/sys573/npu.h). Ingest the unit's installer CD as well:

```bash
tools/ingest.py drmn10m.7z d40jaa02.chd d40jba02.chd d40jca02.chd
```

1. **Install its data:** `hiraku-drmn10m --cd hdd`. The game formats the disk and installs about 400 MB, about 20 minutes of game time, until **INSTALLATION COMPLETED**. Then quit.
2. **Play with it:** `hiraku-drmn10m --npu`. The boot reads the disk's songs.
3. **e-AMUSEMENT** is OFF by default (test mode > NETWORK OPTIONS > e-AMUSEMENT OPTIONS; hold Start and press Left/Right to change it). With it ON the game tries the network, finds none (センターサーバーと通信できません) and goes on in standby with Service (9).

With the unit fitted the 573 has more to do, and the multi-session unit's link once lost bytes ("Please call attendant") until SIO1 got the PS1's 8-byte receive FIFO. It now boots to attract, and sessions with GuitarFreaks work with it fitted. Without `--npu` the game is as before. `HK_573_NPU_LOG=1` prints the unit's calls (2: their words, 3: every register access).

**Not yet:** a hands-on test.

## System 573: GuitarFreaks 11th Mix, and session play with DrumMania (further than MAME)

GuitarFreaks 11th Mix (`gtfrk11m`) needs MAME's `gtfrk11m` set and its runtime CD `d39jaa02` (`d39jba02` is its network unit's installer, for `--cd hdd`):

```bash
tools/ingest.py gtfrk11m.7z d39jaa02.chd d39jba02.chd
ninja -C build/cmake hiraku-gtfrk11m
```

**The first boots:**
1. The first boot installs the CD to the onboard flash and the flash card by itself (about 7 minutes of game time), then shows **INFO 508**: stop it.
2. The next boot shows **NOTE 605**: press **Test** (F2).
3. Then attract. Two coins (5) make a credit.

| Key | Guitar 1 | Guitar 2 |
|---|---|---|
| Neck R / G / B | A / S / D | J / K / L |
| Pick | Space | Right Shift |
| Wailing | W | I |
| Start | 1 | 2 |

The effect knob is an analog axis (a joystick's first axis). Coin is 5; Test and Service are F2 and 9.

**Session play.** DrumMania 10th Mix and GuitarFreaks 11th Mix play together through DrumMania's multi-session unit, each in its own process, joined by a session cable:

```bash
hiraku-drmn10m --session-host /tmp/gfdm.sock
```

```bash
hiraku-gtfrk11m --session-join /tmp/gfdm.sock
```

- **Start the DrumMania side first;** it waits for the other. A host:port works too (TCP), for two machines (not tried yet).
- **The two run in lockstep,** at the slower one's pace: about 4.1 ms a frame together on the desktop.
- **To play a session:** credits and Start on one cabinet open the **WELCOME** screen, with 15 s for the other to join (credits and Start there). Then both choose the mode and the music together and play the same song.
- **Check the cable** in test mode: SESSION NETWORK CHECK says PORT 1 (DrumMania) and PORT 2 (GuitarFreaks), OK.
- **The session music** comes from the unit, in the DrumMania process. It goes over the cable to the GuitarFreaks side too, so both windows play it, in time with their own sounds.

The regress cases are `gtfrk11m-install`, `gtfrk11m-boot`, and `drmn10m-session` + `gtfrk11m-session` (a session game: both cabinets play the first stage of 花火).

## Twinkle: beatmania IIDX 3rd style (in progress, M15)

beatmania IIDX 3rd style (`bmiidx3`) runs on HIRAKU's Twinkle core in lockstep with MAME (PLAN.md section 21). It needs MAME's `bmiidx3` set and its three CHDs: the program CD `gc992-jac01`, the Video CD `gc992-jaa04` and the sound board's hard disk `992hdda01` (unpacked to about 4 GB):

```bash
tools/ingest.py bmiidx3.7z gc992-jac01.chd gc992-jaa04.chd 992hdda01.chd
ninja -C build/cmake hiraku-bmiidx3
```

**The first boot:** the device check passes and stops at **E150** (backup RAM cleared): press **1P Start** (1) to clear it. Then attract; insert coins (5) and press Start.

| Key | Player 1 | Player 2 |
|---|---|---|
| Keys 1–7 | Z S X D C F V | M K , L . ; / |
| Turntable | ← / → (and Left Shift / A) | ↓ / ↑ |
| Start | 1 | 2 |

Effect is E and VEFX is W. Coin is 5; Test and Service are F2 and 9. A USB IIDX controller has its keys on buttons 0–6 and its turntable on the first axis by default; Start, Effect and VEFX are buttons 8, 9 and 10. The effector's five sliders (Volume 1–5) are analog axes with no default binding. Controls are remapped in the launcher.

**Lamps:** the key lamps, Start, VEFX, Effect and credit lamps, eight spotlights, the neon and the board's LEDs, by name for HID LEDs and the lights socket (docs/LIGHTS.md). The 16-segment display above the screen shows in the window title.

**The background video** plays in the middle of the play field during a stage, from the Video CD (`gc992-jaa04`), as the game drives the DVD player; MAME shows none. `--video off` gives MAME's picture. The video needs FFmpeg's libraries (libavformat, libavcodec) when HIRAKU is built; without them the build goes ahead without it. Which clip goes with which song, and the exact keying and placement, still need checking against a real cabinet (PLAN.md section 21, TW-3).

**Not there yet:**
- **1st style (`bmiidx`)** stops at E600 DVD INIT ERROR, as in MAME (TW-5).

The regress cases are `bmiidx3-boot` (the first boot to attract) and `bmiidx3-play` (a coin-up and a stage, after it).
