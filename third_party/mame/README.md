# MAME sources (XT446 synth card)

Keyboardmania's XT446 synth card is an embedded Yamaha MU100B: a Hitachi
H8S/2655 CPU running the MU100 firmware, and a Yamaha SWP30 tone generator and
effects DSP. HIRAKU runs MAME's own emulation of both. The glue is in
`runtime/dev/xt446.cpp`, a port of MAME's `xt446.cpp`.

- **Source:** MAME master at commit
  [`58f90f43e7d1`](https://github.com/mamedev/mame/tree/58f90f43e7d1e2ae7078ba8a3c6d9f9cf879cb7a) (2026-10-12). Files keep their MAME paths under `src/`.
  - This is newer than the 0.289 oracle (flatpak org.mamedev.MAME), for the SWP30's sake. Between 0.289 and this commit it got about 15 accuracy fixes (Sep–Oct 2026): the IIR2 register order, DPCM, MEG ALU corner cases, absolute-address delay reads, reverb RAM banks, mixer attenuation steps, sample interpolation.
  - The H8 changes in the same span don't affect the H8S/2655: a `forced_inputs` port option that defaults to off, plus logging.
- **Licence:** BSD-3-Clause (`LICENSE`); copyright holders are named in each
  file's header. HIRAKU as a whole is GPL-3.0-or-later.
- **Generated code:** `h8make.py` turns `h8.lst` into the opcode dispatch and
  disassembler tables (`cpu/h8/*.hxx`). CMake runs it at build time into
  `build/.../mame-gen`, so `python3` is a build dependency.

## Files

| Path | What |
|---|---|
| `src/devices/cpu/h8/` | H8 core (`h8`, `h8h`, `h8s2000`, `h8s2600`, `h8s2655`), on-chip peripherals (ADC, DMA, DTC, INTC, ports, SCI, 8/16-bit timers, watchdog), disassemblers, `h8.lst`, `h8make.py` |
| `src/devices/sound/swp30.*` | SWP30: AWM2 rompler, MEG effects DSP, mixer |
| `shim/` | **HIRAKU code, not MAME:** the part of MAME's framework (`emu.h`) these files use (see below) |

## Local changes

`hiraku-edits.py` applies every edit to pristine MAME files. Each edit is marked `HIRAKU` in the source, and the script stops if upstream has moved any code it edits. To update:
1. copy the listed files from a MAME checkout into `src/`;
2. run `./hiraku-edits.py src/devices`;
3. rebuild and run the checks below.

- `swp30.cpp` / `swp30.h`:
  - removed the MEG's DRC (UML recompiler) back-end: the `drc*()` code generators, DRC cache and state, and the DRC branch of `execute_run()`. The MEG runs on MAME's interpreter path, which is the same code MAME uses when `-nodrc` is set.
  - `m_meg` is a plain allocation (MAME's came from the DRC cache).
  - added `hk_on_sample`, called after each sample with the four DAC values that MAME's stream outputs 0–3 carry.
  - **speed (same results):**
    - the MEG reads its program array directly. MAME's `read_qword` goes through the address map to `meg_prg_map_r`, which returns the same word.
    - `meg_state::step_decoded()` is `step()` with the instruction fields extracted once, when a program word changes, rather than on every one of the 17 M steps per second.
    - `step_decoded()` keeps the pipeline's 3- and 2-step delay slots as template constants: `run_decoded()` runs a whole sample's steps, cycling through the 6 phases. It picks the multiplier's operands through pointers set at decode time and keeps the accumulator in a register. About 8 % faster than the first pre-decoded version.
    - `HK_XT446_MEG_REF=1` runs MAME's `step()` instead. The two give bit-identical output.
- The H8 files are unchanged.

## The shim

`shim/emu.h` / `emu.cpp` reimplement the parts of MAME's interfaces that these
files use. No MAME code is involved.

- **Devices:**
  - a device tree built as devices are created: `device_add_mconfig` runs at creation;
  - start and reset in tree order, with `device_reset_after_children`;
  - finders resolve by creation, by `set_tag`, or by tag relative to the owner.
- **devcb:** read, write and line callbacks, with constants and no-ops.
- **Address spaces:**
  - page tables: direct pages for RAM/ROM, and per-byte read/write handler tables for I/O;
  - mirrors, select bits, `umask`, submaps, and handlers narrower than the bus.
- **Scheduler (`running_machine`):**
  - CPUs run in timeslices up to the next timer;
  - `machine().time()` is the running CPU's local time, or the expiry time of the timer that is firing;
  - the same LCG as MAME's `machine().rand()`.
- **Stubs:** save states, debugger state and NVRAM are accepted and ignored.
  `logerror` prints only when `HK_XT446_LOG` is set.

## How close it is to MAME

The test fed MAME 0.289's Keyboardmania attract-mode MIDI into the card, timed like MAME's 16550 (`Xt446::midi`):
- **Lockstep:** MAME and HIRAKU make the same 1.37 M SWP30 register writes, in the same order and at the same microsecond timestamps, through 84.5 s. This holds with both the 0.289 SWP30 and this one: its fixes change the sound, not what the firmware does.
- **Divergence:** after that, a sub-microsecond race on a MIDI byte (the H8's RX interrupt against its main loop) sends the two down different paths. Where it lands depends on MAME's machine-wide scheduling.
- **Audio, with 0.289's SWP30:** in the lockstep stretch, MAME's XT446 output and HIRAKU's have the same RMS to 3–4 digits.
- **Audio, with this SWP30:**
  - about 9 dB louder than 0.289 (the mixer attenuation fix), with a larger DC offset;
  - 0.289's 22 kHz ringing is gone. Its IIR2 a1/b1 were swapped, so the filter was unstable.
