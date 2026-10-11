# HIRAKU

Static recompilation of Konami Firebeat arcade games (beatmania III, pop'n music 4–8 / Animelo / Mickey Tunes, ParaParaParadise, and Keyboardmania as a stretch goal) into native Linux executables.

HIRAKU ships no game data. You provide your own dumps. The tools extract the program from your disc image, translate its PowerPC code to C, and link it against a runtime that stands in for the arcade board.

- Plan and status: [PLAN.md](PLAN.md)
- Kernel notes: [docs/KERNEL.md](docs/KERNEL.md)

## Status

M0 (infrastructure) and M1 (translator and interpreter) are done. M2 (kernel glue, interrupts, devices) is next. See [PLAN.md](PLAN.md).

```bash
tools/ingest.py path/to/program-disc.cue    # → dumps/<game>/program.bin
analyze/ghidra.sh bm3final                  # → build/bm3final/manifest.json
analyze/prep.py bm3final                    # → build/bm3final/recomp.txt
cmake -S . -B build/cmake -G Ninja && ninja -C build/cmake
ctest --test-dir build/cmake                # instruction semantics vs QEMU
build/cmake/hiraku-bm3final --interp-only --shadow 3 --max 4000000000
```

- How translation works: [docs/RECOMP.md](docs/RECOMP.md)
- The MAME oracle: [oracle/README.md](oracle/README.md)

## Licence

GPL-3.0-or-later (see `LICENSE`). Code derived from MAME keeps its BSD-3-Clause notices.
