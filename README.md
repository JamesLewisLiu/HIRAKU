# HIRAKU

Static recompilation of Konami Firebeat arcade games (beatmania III, pop'n music 4–8 / Animelo / Mickey Tunes, ParaParaParadise, and Keyboardmania as a stretch goal) into native Linux executables.

Since 2026-10-12 the same front end also runs **DJ Main** games (beatmania 1st mix, pop'n music 1), whose 16 MHz 68EC020 is interpreted rather than recompiled: each platform is a "core" behind one shell (`runtime/shell/core.h`).

HIRAKU ships no game data. You provide your own dumps. The tools extract the program from your disc image, translate its PowerPC code to C, and link it against a runtime that stands in for the arcade board.

- Plan and status: [PLAN.md](PLAN.md)
- Kernel notes: [docs/KERNEL.md](docs/KERNEL.md)

## Status

Done so far:
- **M0:** infrastructure.
- **M1:** translator and interpreter.
- **M2:** kernel glue, interrupts, devices.
- **M3:** rendering checked against MAME.
- **M4:** sound board.
- **M5:** playable in real time.

beatmania III The Final runs natively with sound, keyboard and joystick controls. **How to play: [docs/PLAY.md](docs/PLAY.md).** See [PLAN.md](PLAN.md) for what's next.

## The launcher

`build/cmake/hiraku` (built when Qt 6 is installed) lists the supported games and shows which ones are ready to play. From there you can:
- play a game (double-click), stop it, and read its log;
- map keys and controller inputs to each cabinet's controls: click, then press;
- map cabinet lamps to controller LEDs, and watch a running game's lamps live ([docs/LIGHTS.md](docs/LIGHTS.md));
- set window size, fullscreen, audio and folders.

`hiraku popn7` starts a game straight away. Settings and mappings are kept in `~/.config/hiraku/`.

```bash
tools/ingest.py path/to/program-disc.cue --romset path/to/bm3final.zip --hdd path/to/hdd.img   # → dumps/<game>/
analyze/ghidra.sh bm3final                  # → build/bm3final/manifest.json
analyze/prep.py bm3final                    # → build/bm3final/recomp.txt
cmake -S . -B build/cmake -G Ninja && ninja -C build/cmake
ctest --test-dir build/cmake                # instruction semantics vs QEMU
tools/regress.py record; tools/regress.py check   # runs stay bit-identical (per-frame picture + sound hashes)
tools/regress.py bench                      # share of a core, and host work per frame (mean / p99 / worst)
build/cmake/hiraku                          # the launcher (Qt 6)
build/cmake/hiraku-bm3final --window        # real time with sound; F2 test, 5 coin, 1 start, Z S X D C keys
build/cmake/hiraku-bm3final --script oracle/scripts/bm3final-practice.his --wav out/practice.wav
build/cmake/hiraku-bm3final --frames 1800 --snap-every 300 --trace   # headless, PPM frames + MMIO trace
```

- How translation works: [docs/RECOMP.md](docs/RECOMP.md)
- The MAME oracle: [oracle/README.md](oracle/README.md)

## Licence

GPL-3.0-or-later (see `LICENSE`). Code derived from MAME keeps its BSD-3-Clause notices.
