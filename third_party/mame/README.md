# MAME sources (XT446 synth card, System 573)

Keyboardmania's XT446 synth card is an embedded Yamaha MU100B: a Hitachi
H8S/2655 CPU running the MU100 firmware, and a Yamaha SWP30 tone generator and
effects DSP. HIRAKU runs MAME's own emulation of both. The glue is in
`runtime/dev/xt446.cpp`, a port of MAME's `xt446.cpp`.

The System 573 core (`runtime/sys573/`, PLAN.md section 19) runs MAME's PlayStation
and 573 devices the same way, on the same shim, with a port of `ksys573.cpp`'s
driver class as the board.

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
| `src/devices/cpu/psx/`, `src/devices/video/psx.*`, `src/devices/sound/spu*` | 573: the PlayStation CPU (GTE, DMA, IRQ, root counters, SIO, MDEC, disassembler), GPU, SPU |
| `src/devices/machine/{intelfsh,timekpr,adc083x,ds2401,x76f041,x76f100,atahle,t10mmc,t10spc,timehelp,jvshost,jvsdev}.*` | 573: flash, M48T58, ADC0834, security chips, the ATA/SCSI command layers, JVS |
| `src/devices/bus/ata/{ataintf,atadev,atapihle,atapicdr,cr589}.*` | 573: the ATA interface and the CR-589 ATAPI CD-ROM drive |
| `src/devices/bus/pccard/{pccard,linflash}.*`, `src/devices/bus/psx/{ctlrport,memcard}.*` | 573: PC Card slots and flash cards; the memory card reader's PS1 ports |
| `src/devices/sound/{cdda,mas3507d,mp3_audio}.*` | 573: CD audio, the digital I/O board's MP3 decoder |
| `src/mame/konami/{k573cass,zs01,k573dio,k573fpga,k573mcr}.*` | 573: security cassettes, the digital I/O board and its FPGA, the JVS memory card reader |
| `src/emu/dirtc.*` | MAME's RTC interface (the M48T58) |
| `src/3rdparty/minimp3/minimp3.h` | minimp3 (CC0), MAME's MP3 decoder backend |
| `shim/screen.*`, `shim/machine/ram.*`, `shim/imagedev/*`, `shim/stubs/*`, `shim/*.h` | **HIRAKU code, not MAME:** MAME's screen timing, RAM device, a CD image class reading `.cue`/`.iso` (MAME reads CHDs), stand-ins for drive and controller types no 573 game selects, small utility headers |
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
- `psx.cpp` / `psx.h` (the PS1 CPU), for the 573's recompiled R3000 blocks (`runtime/sys573/mips.h`):
  - the body of `execute_run()`'s loop is `execute_one()`, so a block can hand a single instruction back to the interpreter;
  - `execute_run()` first offers the instruction to `m_hk_run` when `m_hk_tab` has a block for its address and the CPU isn't in a branch delay slot. `m_hk_rec` sees every instruction, for recording;
  - `friend struct hk_psx` (`runtime/sys573/mips_ops.h`) gives the blocks MAME's registers and pipeline.
  - With no table (`HK_573_BLOCKS=0`, or a game without a recording) the loop is MAME's.
- The H8 files are unchanged.

## The shim

**Two schedulers.** The XT446 uses the simple one (CPUs in timeslices up to the next
timer; its recorded runs were made with it). With `running_machine::m_exact` the
shim is a copy of MAME's `device_scheduler` instead, for cycle-exact lockstep (the
573):
- timers kept in MAME's list order;
- the quantum, and MAME's cycle count per slice: `(delta >> divshift) / divisor`;
- `abort_timeslice` when a timer becomes the next one;
- input lines through a `synchronize()` timer;
- CPU local time through the device clock (MAME's `clocks_to_attotime`, which differs by an attosecond per cycle from the scheduler's own cycle length at 16.9344 MHz);
- the sound manager's 50 Hz stream update;
- NVRAM loaded after start (a file under MAME's name, or `nvram_default()`), then the RTCs set.


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
- **For the 573's recompiled blocks:** the page table is public (`pages()`, `page_bits()`), `version()` changes whenever the map does, and `mem_ptr()` finds the memory behind an address on a mixed page (the scratchpad). `ram_device` allocates page-aligned memory, so code pages can be write-protected.
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
