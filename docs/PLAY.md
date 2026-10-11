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
