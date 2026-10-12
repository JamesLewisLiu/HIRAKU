# Playing beatmania III The Final with HIRAKU

## Once: build and ingest your dumps

```bash
cmake -S . -B build/cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build/cmake hiraku-bm3final
tools/ingest.py /path/to/cd.cue --romset /path/to/bm3final.zip --hdd /path/to/hdd.img
analyze/ghidra.sh bm3final && analyze/prep.py bm3final && ninja -C build/cmake hiraku-bm3final
```

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
| Esc | quit |

P2 uses M K Comma L Period, Up/Down and 2.

**Navigating:** turn the turntable to choose, and press a white key (1, 3 or 5) to confirm. Start toggles DOUBLE on the option panel.

**Saves:** NVRAM (operator settings, rankings) lives in `~/.local/share/hiraku/bm3final/`. It is saved on Esc, on window close and on Ctrl-C. Use `--nvram DIR` to keep it elsewhere.

**Persistence is unconfirmed.** In MAME and in HIRAKU alike, the scripted sessions so far never made the game write its NVRAM. It probably writes only when a setting or a ranking changes, and none of those sessions changed either. Check whether a test-mode change or a new high score survives a restart.

## Controllers

Joysticks are opened at start and when plugged in. The console lists them as `joy0`, `joy1` and so on.

The defaults map `joy0` buttons 0–4 to the keys, axis 0 to the turntable, button 7 to the pedal, 8 to start and 9 to coin.

IIDX-style controllers differ in which button and axis numbers they use. The easiest way to map one is the launcher's **Controls** dialog: click a binding, then press the button or spin the turntable. Pressing anything already bound lights up its row. The dialog saves `~/.config/hiraku/controls/<cabinet>.txt`, which the launcher passes as `--bindings`.

By hand: write a bindings file and pass `--bindings FILE`. See `docs/bindings-bm3.example`; the format is in `runtime/host/input.h`.

**Lights:** `--lights FILE` drives HID controller LEDs from the cabinet lamps, and `--lights-socket PATH` publishes them for other tools. See [LIGHTS.md](LIGHTS.md).

## Latency

**In the game:** HIRAKU measures it as the time from a key press to the RF5C400 starting the keysound. beatmania III scans its keys once per video frame (60 Hz), so this is **2–20 ms, mean about 9.5 ms**, exactly as on a real cabinet. Everything above that is the PC.

**In real-time mode, audio is the clock.** The machine runs until 8 ms of sound is queued (`--audio-ms N`), and controls are read every couple of milliseconds while it waits. Total latency is therefore roughly:

> 2–20 ms (game, frame-based) + `--audio-ms` (8) + the audio device buffer (128 frames requested = 2.9 ms) + your sound server

On exit, the console prints the buffer it actually got.

**The queue tunes itself.** It starts at `--audio-ms` (8) and grows by 1 ms after each underrun, up to 30 ms, so it settles at the smallest crackle-free lead this PC needs. The exit report prints where it settled, how many underruns there were, and the worst stall.

**Sound server settings:** the first reports came before the performance fixes in this release. Re-test with the defaults first. On PipeWire the server quantum adds its own latency; a smaller one (`PIPEWIRE_LATENCY=256/48000`) is worth trying, but only if the default is already clean.

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

## DJ Main: beatmania, pop'n music 1 (in progress, M13)

DJ Main games aren't recompiled: the board's 68EC020 runs at 16 MHz and is interpreted (Musashi), which costs about 1 ms of host time per frame. Each game needs its MAME ROM set (program, sprite and tile ROMs) and its hard disk as a raw image:

```bash
chdman extracthd -i 753jaa11.chd -o 753jaa11.img     # once; chdman comes with MAME
tools/ingest.py --romset bm1stmix.7z --hdd 753jaa11.img
ninja -C build/cmake hiraku-bm1stmix
build/cmake/hiraku-bm1stmix --window
```

pop'n music 1 (`popn1j`) is the same with `803jaa11` and `popn1j.7z`. The game is found by the set's name (`games/<id>/game.toml`, `core = "djmain"`).

**Status:** HIRAKU runs both games in step with MAME, clock for clock (`oracle/scripts/*-boot.his`, 155 s):
- power-on, self-test, the disk loads and attract mode;
- a whole game of each:
  - beatmania (`oracle/scripts/bm1stmix-play.his`, 217 s): coins, the turntable at the practice-stage prompt, song select, a song played with keys and scratches, game over;
  - pop'n music (`oracle/scripts/popn1j-play.his`, 233 s): coins, game mode and genre select, a song played over all 9 buttons, game over.

In these runs:
- the work RAM, sprites and palette are identical to MAME's on every frame;
- every snapshot is pixel-identical;
- the sound is sample-identical: HIRAKU plays and records it at the chips' own 48 kHz (`--wav` matches MAME's `-wavwrite`).

**Not yet:** a hands-on play test, turntable feel, the ATA password of complete MIX and later, and the other DJ Main sets.

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
