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

The defaults map `joy0` buttons 0–4 to the keys, axis 0 to the turntable, button 7 to the pedal, 8 to start and 9 to coin. To change anything, write a bindings file and pass `--bindings FILE`. See `docs/bindings-bm3.example`; the format is in `runtime/host/input.h`.

IIDX-style controllers differ in which button and axis numbers they use. Run once, press each button and watch… or just try `joyN:buttonK` values until each key lights its lane in test mode's I/O check.

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

## pop'n music 4–7 and Animelo 1/2 (in progress, M7)

Each game needs its program CD (Disc 1), its data DVD (Disc 2) and its MAME ROM set (`popn4` … `popn7`). The ROM set holds three things:
- the dongle image;
- the SPU's 68000 program, `a02jaa04.3q`;
- the default RTC.

```bash
tools/ingest.py "Pop'n Music 4 (Japan) (Software Disk 1).cue" --dvd "Pop'n Music 4 (Japan) (Software Disk 2).iso" --romset popn4.zip
analyze/ghidra.sh popn4 && analyze/prep.py popn4 && ninja -C build/cmake hiraku-popn4
build/cmake/hiraku-popn4 --window
```

**Status:** pop'n 4, 5 and 7 boot to attract mode with sound: boot checks, loading from the DVD, title, demo play. Standalone `.7z` ROM sets are accepted.

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
- **BACKUP CLEAR**, then **1** to confirm, sets the defaults.

| Key | Action |
|---|---|
| Q W E R T Y U I O A S D F G H J K L Z X C V B N | player 1's 24 keys (C1–B2) |
| 1 / 2 | 1P / 2P start |
| 5 | coin |
| F2 / 9 | test / service |

**Status:** all four boot to attract mode, fully native, with CD and YMZ280B sound.

**Keyboard notes:** they come from the XT446 synth card (an embedded Yamaha MU100B), which is not emulated. HIRAKU plays the game's MIDI through FluidSynth with a General MIDI SoundFont instead. The notes and timing are the game's own, but the instruments are the SoundFont's. Install one:

```bash
sudo pacman -S soundfont-fluid
```

Or point at any `.sf2` with `--soundfont FILE` or `HK_SOUNDFONT`. `HK_MIDI_LOG=1` prints the MIDI stream.
