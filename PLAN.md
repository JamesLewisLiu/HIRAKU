# HIRAKU — Static recompilation of Konami Firebeat

Goal: native PC executables of the Firebeat library: beatmania III, pop'n music 4–8 / Animelo / Mickey Tunes, ParaParaParadise, and Keyboardmania as a stretch goal. The approach is to statically translate each game's PowerPC program to C and link it against one shared runtime that replaces the arcade board.

The user supplies their own dumps. The repo never contains ROMs, executables, assets, or recompiled output, because the generated C is derived from the game. This is the N64Recomp/Zelda64Recomp model.

Plan written 2026-10-11. It is based on:
- `BM3-extract/` (REVIEW.md, tooling, Ghidra project);
- `beatmania-III-BGA-engine.zip`;
- the MAME 0.289 Firebeat driver (`firebeat.cpp`, `k057714.cpp`, `rf5c400.cpp`);
- the `~/bm3mame` Lua oracle;
- a fresh headless-Ghidra measurement of the BM3 Final program image (§1).

---

## 0. Why recomp rather than continuing the native-reimplementation route

REVIEW.md recommended the rizu reimplementation ("Route B") for one game. That was right for one game, but it does not scale to roughly 25 releases. Route B needs fresh reverse engineering of every game's flow, judge, scoring, and UI. A recomp reuses one runtime, and each additional game mostly costs a config file plus fixes for that game's quirks. Recomp is also the only route that preserves the original game logic exactly.

Route B (rizu) is dropped (§8). The BM3 format and BGA work, now living in the EZ2 port, serves as an **oracle** and test suite for the recomp.

---

## 1. What the binary tells us (measured, BM3 The Final `GCC01`)

| Fact | Value | Consequence |
|---|---|---|
| Image | 1 MiB flat at `0x80000000`, mirrored at `0x0`, entry `0x80003000` (tail branch) | No relocations, no loader. Load `hikaru_raw.bin` directly and skip the BIOS. |
| Functions (Ghidra auto-analysis) | **2,204**; 137,352 of 138,432 decoded instructions fall inside functions | Small. N64 recomps handle 5–10× this. |
| FPU instructions | **0** (soft-float via libgcc) | No float semantics to get right. |
| Computed jumps `bctr` | **37, all 37 resolved** to jump tables by Ghidra | The control-flow graph is statically recoverable. |
| Indirect calls `blrl` | 149 (function-pointer tables, e.g. BGA VM table at `0x800E560C`) | Needs a guest-address → host-function lookup table. A fallback is still required (§3.4). |
| Exception return `rfi` | 4 (`0x80000DD4`, `0x80003630`, `0x800037B4`, `0x8000393C`) plus 1 `rfci`, all in hand-written asm outside functions | Assembly glue of a preemptive C kernel. The glue is replaced; the C kernel is translated (§3.3, docs/KERNEL.md). |
| System calls `sc` | 2 (`0x800C52D4`, `0x800C96B4`) | One generic "call kernel function pointer" system call. |
| Privileged registers | DMA ch0–3 DCRs, EXIER/EXISR/IOCR, PIT, TCR/TSR, TBLO/TBHI, SRR0/1, SPRG2/3, ICCR/DCCR | Small, fully known 403 surface. |
| Odd 403/GCC-isms | `lswi`/`stswi` ×85, `lmw`/`stmw` ×525, `mtcrf 0xff` | Easy, but they must be implemented exactly. |
| Self-modifying code | None observed (only one `dcbf`, plus `iccci`/`dccci` cache init) | To be confirmed by the runtime's fallback logger. |
| Threads | Preemptive; watchdog/file/charlib/media/game threads (create/cancel/join) | Guest threads → host fibers, switched when the kernel retargets SPRG3 (§3.3). |

The sound CPU is a 68000 with a 512 KB program per SPU revision. It is small enough to **interpret** rather than recompile.

---

## 2. Platform model (from MAME; what the runtime must replace)

```
            ┌──────────── PPC403GCX 66 MHz, 32 MB RAM ────────────┐
 BIOS ──X   │  recompiled game code  +  HLE kernel/threads        │
 (skipped)  └──┬────────┬────────┬───────────┬──────────┬────────┘
               │MMIO    │MMIO    │DPRAM      │MMIO      │403 serial
         K057714 GCU   ATA/ATAPI  mailbox    I/O, lamps, DS1991 dongle
         (1 or 2)      CD/DVD/HDD   │        RTC/NVRAM, flash, UART,
                                    ▼        extend board (FFT, FDC, MIDI)
                      SPU: 68000 + RF5C400 + 32 MB wave RAM + own ATA (HDD/DVD)
                      ── or ── YMZ280B (+ CDDA from 2nd CD)
```

| Family | Audio path | Storage | Extra hardware |
|---|---|---|---|
| beatmania III (bm3, core, 6th, 7th, final) | SPU (68k + RF5C400). 3 SPU ROM revisions | Program CD + HDD on the SPU's ATA | Extend board: spectrum analyser, floppy, MIDI UART to the ST-224 effector; turntable/effect sensors at `7d000340` |
| pop'n 4–8, Animelo 1/2, Mickey Tunes | SPU. BGM is streamed by 1 MB double-buffered DMA keyed on RF5C400 reg 09 | Program CD + DVD on the SPU's ATA | 9 buttons, lamps |
| ParaParaParadise / Dancing / 1.1 / 1st Mix+ | YMZ280B (flash sample ROM) + CDDA from a second CD | 2 CDs | IR sensors at `7d000340`. `!:EXE:00` wrapper around the program |
| Keyboardmania 1st–3rd / Keyboard Heaven | YMZ280B + CDDA + **XT446 (embedded MU100B) synth**: H8S/2655 + SWP30, both in MAME | 2 CDs | 2 GCUs / 2 screens (ROT270), MIDI keyboards on the extend-board UARTs, mod wheels |

Interrupts:
- EXT0 is GCU VBlank (~59.94 Hz, 800×525 @ 25.175 MHz).
- EXT1 is the extend board.
- EXT3 is the SPU mailbox.
- EXT4 is main ATA.
- PIT and FIT come from the 403 itself.

Known imprecision in MAME (these are hazards for us too):
- The SPU 500 Hz tick and 350 ns DMA word rate are tuned by hand ("too fast cuts keysounds, too slow buzzes").
- RF5C400 envelopes, reverb, chorus and filter are approximate or missing.
- The GCU status register is constant, and the reg `0x6c` transparency hack and pop'n 8 flip fixups are workarounds.
- The ST-224 is not emulated. The XT446 runs on MAME's H8S/2655 and SWP30 sources (`third_party/mame`).

---

## 3. Architecture

### 3.1 Repository layout

```
HIRAKU/
  PLAN.md
  docs/                 hardware notes, per-game deltas, DONE-CRITERIA, PORT-DELTAS (EZ2DECOMP conventions)
  tools/                dump ingestion (disc → program image), misc Python
  launcher/             `hiraku`, the Qt 6 front end: game list, controls/lights mapping, settings
  analyze/              Ghidra headless scripts + Python → per-game manifest (functions, jump tables, fn-ptr tables)
  recomp/               C++ PPC403 → C translator (decoder, emitter, CFG checks)
  runtime/
    core/               guest memory, CPU context, MMIO bus, indirect-call table, fallback PPC interpreter
    kernel/             exception glue (frames, SPRG switch → fibers), IRQ delivery, 403 timers/DMA/DCRs
    dev/                gcu (software renderer), spu (68k core + rf5c400 + mb8421), ymz280b, cdda,
                        ata/atapi (image-backed), rtc65271, flash, ds1991, uart, extend board, fft
    host/               SDL3 video / input / audio; low-latency backends (PipeWire, WASAPI-excl, ASIO) later
  games/<set>/game.toml per-game config: image hash, entry, HLE symbol addresses, patches, IO map, disc layout
  oracle/               MAME Lua taps + compare/summarize (ez2oracle-style), deterministic input scripts
  dumps/   (gitignored) user CHDs, ISOs, BIOS/SPU ROMs
  build/   (gitignored) generated C + binaries
```

Languages:
- C++20 for the recompiler and runtime;
- generated C99 for game code;
- Python for analysis;
- Lua for the MAME side;
- CMake for the build.

### 3.2 Recompiler (`recomp/`)

- **Discovery** uses Ghidra headless (12.1.2 is already installed), extended by our own checks. It exports a manifest with:
  - function starts and ends;
  - basic blocks;
  - jump tables;
  - data words that look like code pointers into `.text` (these are the candidate `blrl` targets).
- **Translation** uses its own PPC decoder and emitter rather than Ghidra's P-code decompiler output. A per-instruction emitter gives exact CR/XER/CA semantics and is straightforward to audit. The full integer 403 subset is about 150 opcode forms.
- **Output.** Each guest function becomes one C function `void f_8004bb0(Ctx*)`:
  - guest GPR, CR, LR, CTR and XER live in `Ctx`;
  - branches inside the function become `goto`;
  - `bl` becomes a direct C call;
  - `bctr` through a known table becomes a `switch`.
- **Memory.** Guest RAM stays **big-endian**, and every load/store byteswaps. This is trivially correct with DMA, VRAM uploads and DPRAM, and costs nothing at this code size. Addresses are masked (bit 31 mirror). RAM gets a fast path; everything in `0x7xxxxxxx` goes through the MMIO bus.
- **Indirect calls.** `blrl` and computed `bctrl` look the target up in a sorted guest→host table. A miss falls into the runtime's PPC interpreter and is logged with a hint to add the address to the manifest.
- **Safe points.** Loop back-edges and function prologues get a cheap `if (ctx->irq_pending)` check, so interrupts and context switches are delivered at deterministic points.
- **Idle detection.** Planned: mark known wait-for-VBlank and spin loops in the manifest, and sleep the host instead of burning a core. **Measured 2026-10-12, it doesn't apply as planned (§14):** the guest has no such loop. Its idle time is threads calling the kernel's `yield()` in wait loops, so a bit-exact skip would mean proving that the whole scheduler state repeats. Deferred.

### 3.3 Kernel: translate the C kernel, replace only the assembly glue (`runtime/kernel/`)

Details are in [docs/KERNEL.md](docs/KERNEL.md). The program carries a small **preemptive kernel in C**:
- threads run in user mode;
- there is one generic system call ("run this kernel function pointer");
- context switching works by pointing SPRG3 at another thread's saved 384-byte frame before `rfi`.

That shapes the design (it was settled once the kernel had been read):
- **The kernel's C code is recompiled like everything else**, so scheduling, priorities, semaphores and timers stay authentic, as "match MAME first" requires.
- **Only the assembly glue (~300 instructions) is replaced with host code.** That covers boot, the vectors, frame save/restore, and the SPRG2/3 stack switch. The host glue builds the same frames in guest RAM.
- **Each guest thread is a host fiber.** On the way out of an exception, if SPRG3 now names another thread's frame, the host switches fibers. A frame never seen before starts a new fiber at its SRR0.
- **Interrupts are taken only in user code** (the kernel runs with EE off). Recompiled user code polls `irq_pending` at back-edges and calls. Kernel functions are emitted without polls.
- The 403 surface is emulated exactly: DCRs, 4 DMA channels including software mem→mem, PIT/FIT/WDT, TB and SPRGs. Cache operations are no-ops.
- **Cross-game reuse.** The kernel and the other platform libraries (ATA, GCU, SPU, dongle) are almost certainly a shared Konami Firebeat SDK. BM3 gets labelled first, then **Ghidra FunctionID/BSim signatures** locate the same routines in the pop'n, PPP and KBM images, like N64Recomp's libultra matching.

### 3.4 Fallback interpreter and shadow mode

The runtime embeds a simple PPC403 interpreter, used for three things:
1. executing anything the static pass missed (logged);
2. **shadow mode**, a debug build that runs selected recompiled functions and the interpreter on cloned contexts and diffs registers and memory writes. This catches translator bugs at the exact instruction;
3. bring-up before the manifest is complete.

### 3.5 Devices: register-faithful first, HLE second

Port the BSD-3 MAME devices as plain C++ classes (no MAME framework), so games see the same registers:

- **GCU K057714** (~1k LOC): a software renderer over a 1024×16384 16-bit VRAM. It covers:
  - 4-dword command FIFO;
  - display-list call/end;
  - fill, blit with scale, flip and 2 blend modes;
  - 2bpp chars;
  - 4 display contexts with brightness.

  Software rendering is fast enough and avoids GPU read-after-write hazards. The output is 640×480 (or 2×640×480 for KBM), presented scaled with integer or CRT-shader options.
- **SPU:** a permissively licensed 68000 interpreter core running the dumped SPU ROM, plus the RF5C400 port, MB8421 mailbox, 32 MB wave RAM, and an image-backed SPU-ATA with "instant" DMA. Timing is driven by the **audio clock** (44.1 kHz frames). That replaces MAME's hand-tuned 350 ns / 500 Hz hacks with something that can be reasoned about and measured.
- **YMZ280B + CDDA** for PPP/KBM: port the MAME code, with CDDA read from the user's disc images.
- **ATA/ATAPI:** a minimal PIO/DMA state machine backed by CHD/ISO/raw HDD images. Reuse `bm3.py` knowledge for the BM3 HDD (16-bit swap, 32 MiB chunks).
- **Small devices:**
  - DS1991 dongle (state machine, enclave from user dump, or the existing nodongle patches);
  - RTC65271 + NVRAM (persisted to disk = save data);
  - 29F016A flash;
  - PC16552 UART;
  - extend-board IRQ latch;
  - spectrum analyser (wdl FFT over the RF5C400 output, as in MAME);
  - floppy (stub, or a disk image).
- **Later HLE** for latency: once register-level audio works, optionally intercept the PPC-side keysound-trigger path and start voices immediately. This needs no 68k round-trip and brings latency close to a native engine with preloaded samples.

### 3.6 Host layer

- SDL3 window and gamepad.
- Configurable IO maps per family:
  - IIDX-style controllers / turntable for BM3;
  - pop'n controller or keyboard;
  - **ParaPara:** 5 hand-sensor zones (keyboard or pads; camera or IR as a later extra);
  - **Keyboardmania:** host MIDI-in for the keyboards. The XT446 card runs its own firmware, with a General MIDI SoundFont as the fallback when its ROMs are missing.
- Audio: SDL3 at first, then PipeWire low-latency on Linux and WASAPI-exclusive/ASIO on Windows. The game loop is paced by the audio clock, and VBlank is derived from it.
- NVRAM/RTC/flash are persisted per game. Linux is the primary target and Windows the second (MinGW/clang-cl).

---

## 4. Validation strategy (the part that makes it converge)

1. **Unit:** a translator test suite. Every opcode form is generated, run through the interpreter and the emitted C, and compared. This runs in CI and needs no game data.
2. **Shadow execution** (§3.4) on real game code.
3. **MAME differential oracle.** Extend `~/bm3mame` / the zip's `tools/oracle`:
   - memory taps on `0x7E800000` (GCU), `0x74000000` (DPRAM) and `7d000800` (inputs) log every MMIO access with a frame number;
   - deterministic input scripts in one shared format (ez2oracle `.osc`-style), executed by both MAME (Lua) and the runtime;
   - per frame, compare RAM regions (object list `0x801000cc`, VM vars, measure counter, judge state), the GCU command stream, mailbox traffic, and framebuffer snapshots;
   - `compare.py` / `summarize.py` produce a `report.md` with diff PNGs, as in ez2oracle.
4. **Independent oracles:** the C/Python BGA VM and `bm3audit --selftest` (498 scenes), and the extracted charts and keysounds (the recomp's sample uploads must match `bm3.py`'s TOC).
5. **Coverage:** a `census`/`spine` equivalent. It lists functions reached in the oracle traces that the manifest lacks, and how many fallback-interpreter entries each session hits. The target is 0 per full attract-plus-play loop.

---

## 5. Milestones

Durations are rough, assume one person, and are assisted. Each milestone has a concrete exit test.

| # | Milestone | Exit criterion | Est. |
|---|---|---|---|
| **M0** | Infrastructure | Repo, CMake, `game.toml` for `bm3final`, dump ingestion (CD → `HIKARU.EXE` → LZSS unpack == `hikaru_raw.bin`). Ghidra manifest export. MAME oracle extended with MMIO taps and input scripts. | 1–2 wk — **done 2026-10-11** |
| **M1** | Translator + interpreter | All 2,204 BM3 functions emitted and compiling. Opcode test suite green. Interpreter boots the image to the first MMIO access. | 3–5 wk — **done 2026-10-11** (docs/RECOMP.md) |
| **M2** | Kernel + platform bring-up | Threads, IRQs, PIT, DMA, RTC, dongle, ATA all present. BM3 reaches the `GCC01-JC-A STARTUP` self-test text with GCU output on screen. Shadow mode is clean over boot. | 3–4 wk — **done 2026-10-11** |
| **M3** | Attract and BGA | Attract loop runs. Frame-diff vs MAME is ≤ tolerance on 10 forced scenes (`forcescene.lua`). Zero fallback-interpreter hits. | 2–3 wk — **done 2026-10-11** (docs/M3.md) |
| **M4** | Audio | 68k + RF5C400 + SPU-ATA working. Keysounds and BGM match MAME captures, with no cutoffs or buzz. Spectrum analyser works. | 3–5 wk — **done 2026-10-11** (docs/M4.md) |
| **M5** | BM3 Final playable | Coin → mode select → song → result → name entry with real controls. NVRAM persists. Measured input→sound latency is published. | 2–3 wk — **done 2026-10-11**, except the hands-on test with real audio hardware and controllers. That test now also covers the 2026-10-12 fiber change (never run in `--window`). Waiting on the owner (can't test right now). |
| **M6** | Rest of beatmania III | `bm3`, `bm3core`, `bm36th`, `bm37th` run. Mostly manifests and SDK signatures plus SPU ROM revision differences. | 2–3 wk — **blocked on media**: no 1st mix, Core Remix, 6th or 7th disc on hand (the local `bm3` set is The Final's CHDs renamed) |
| **M7** | pop'n music 4–8, Animelo 1/2, Mickey Tunes | Same SPU stack, plus DVD on the SPU's ATA and BGM streaming via RF5C400 reg 09 (the hardest audio-timing case). | **in progress** (docs/PLAY.md):<br>• pop'n 4, 5, 7 and 8 boot to attract with sound; pop'n 8 also plays music select and full songs.<br>• pop'n 6 boots after a ~14 min first-boot flash install.<br>• Animelo 1/2 boot.<br>• **Left:** check timing and BGM streaming against MAME, and play every title hands-on.<br>• Mickey Tunes: no media. |
| **M8** | ParaParaParadise family | `!:EXE:00` loader, YMZ280B, CDDA, sensor input mapping. | **in progress**:<br>• 1st Mix Plus plays (coin, song select, a full song with CD music).<br>• `ppp`, `ppp11` and `ppd` boot to attract.<br>• **Left:** compare against MAME (CD music sync, sensors), and play the others through.<br>• `ppp2nd` is different hardware, out of scope. |
| **M9** | Native polish | Low-latency audio backends, display options, input remap UI, Windows build, per-game config UX. | ongoing — the `hiraku` launcher (game list, controls and lights mapping, settings) and cabinet lamps (HID LEDs, socket: docs/LIGHTS.md) are in |
| **M10** | Keyboardmania (stretch) | Dual GCU / dual screen, MIDI-in, the XT446 synth card. | **in progress**:<br>• kbm, kbh, kbm2nd and kbm3rd boot to attract (dual GCU, ROT270, MIDI keyboards, CD audio).<br>• The real XT446 card runs (MAME H8S/2655 + SWP30, pinned to MAME master): in lockstep with MAME 0.289 for the first 84.5 s of attract MIDI; SoundFont stand-in when its ROMs are missing.<br>• **Left:** play a song through; cost, 38 % of a desktop core (SWP30 MEG ~40 % of that), the N100 risk. |
| **M11** | Efficiency for N100-class PCs | Every Firebeat game within its budget on an N100-class mini PC (§14): average under 25 % of one core, no frame over ~8 ms of host work. Bit-identical to the current runs (`tools/regress.py`). | **in progress** (§14):<br>• bm3final about 3.4× more efficient than MAME, 9.6 % of a desktop core in gameplay.<br>• **Left:** SWP30 MEG speed-up (kbm); read-ahead for the hard disk and DVD images.<br>• **Blocked** on measuring real N100 hardware (not available right now). |
| **M12** | Cores: the shell split | The host layer (launcher, window, input, lights, scripts, oracle) talks to the machine through one core interface. Firebeat moves behind it unchanged (bit-identical digests). | **done 2026-10-12** (§17): `runtime/shell/core.h`; Firebeat and DJ Main are cores, each binary links one. Firebeat bit-identical. |
| **M13** | DJ Main core | beatmania 1st mix → THE FINAL, pop'n 1–3, pop'n stage: interpreted 68EC020 plus MAME's devices, checked against MAME. Analog turntable and lamps. | **in progress** (§17, docs/PLAY.md):<br>• `bm1stmix` and `popn1j` boot to their titles and attract mode with sound.<br>• beatmania: 129/150 snapshots pixel-identical to MAME at a constant 2-frame lag; sound loudness within 1 %.<br>• **Left:** hands-on test, demo-play note sync, ATA password (complete MIX on), the other sets. |
| **M14** | System 573 core | PS1 core (interpreted, then statically recompiled MIPS), analog-I/O games first (CD audio), then digital I/O (MP3 + FPGA). | months, **unblocked 2026-10-12** (§16):<br>• `ddr3mj` (DDR 3rd Mix, analog I/O): MAME stops at error 147 until the first install from CD (install cassette).<br>• `drmn10m` (DrumMania 10th, digital I/O): installs from CD to flash in MAME, then wants a reboot.<br>• Both are NOT_WORKING in MAME; MAME runs them at ~650–670 % real time. |
| **M15** | Twinkle core (beatmania IIDX 1st–8th) | The M14 PS1 core plus Twinkle's SPU (68000 + RF5C400, already in HIRAKU), SCSI CD, ATA hard disk. Then the background video MAME lacks: Video CD (MPEG-1) for early styles, DVD for 6th–8th. | **unblocked 2026-10-12** (§16):<br>• `bmiidx3` (3rd style) is on hand; MAME passes its device check and stops at the normal first-boot backup-RAM error (E150).<br>• MAME ~570 % real time; its driver lists "emulate dvd player and video mixing" as a TODO. |

Rough total to playable BM3 Final is about 4 months. Each later family should cost weeks, not months, if the SDK-signature bet in §3.3 holds.

---

## 6. Risks and how they're handled

| Risk | Mitigation |
|---|---|
| Thread library is more exotic than it looks (e.g. switching inside ISRs) | Read the `0x80003400–0x80003630` asm early, in M1. Fallback: keep CPU state in `Ctx` and run threads cooperatively on one host thread. |
| Missed `blrl` targets / data-as-code | Fallback interpreter + logging + oracle coverage census. |
| SPU timing (keysound cutoffs, pop'n reg 09 streaming) | An audio-clock-driven scheduler is deterministic. Compare DPRAM/RF5C400 traffic against MAME. HLE the keysound path if needed. |
| GCU unknowns (status bits, reg 6c, flips) | Start from MAME's behaviour and hacks. Fix per-game issues against real-hardware video where available (`BM3-videos/` has 270 reference recordings). |
| Other games don't share the SDK | **Mostly retired for the kernel** (identical glue in 5/5 programs; C kernel matched in pop'n 5–7). Still open for the driver libraries. Where code isn't shared, it costs manual labelling, but the translator and devices carry over unchanged. |
| XT446 synth (KBM) | Done with MAME's H8S/2655 + SWP30 on a framework shim. The SWP30 is pinned to a MAME master commit newer than the 0.289 oracle, for its accuracy fixes (0.289 had e.g. an unstable IIR2). |
| Legal | Ship only the tool and runtime. Users provide dumps. Generated C, assets and ROMs are never committed. The MAME-derived code is BSD-3 (firebeat.cpp, k057714.cpp, rf5c400.cpp), so keep attribution. Check each borrowed core's licence before use. The BGA tools zip is GPLv3, which matches HIRAKU's licence. |

---

## 7. Housekeeping items found during the survey

- **BGA VM opcode base is off by 5 in the old `BM3-extract` notes only.** The dispatch table starts at `0x800E560C` (entry 0 = END `0x80044DBC`; checked in the binary). `ghidra_scripts/DumpVM.java`, `bga_vm.py`, `bm3vm.py` and `REVIEW.md` read it from `0x800E5620`. The EZ2 port (`EZ2-projects/port/BM3-BGA.md` §5, `scene/bm3bga.c`) already uses the correct base, so nothing needs filing there.
- REVIEW.md overstates the judge work. `gates.c`, `miss.c` and `stops.c` are BGA hit/miss hooks, not the gameplay judge. Real judge windows and gauge are still unknown. The recomp gets them for free, and they can be read straight out of traces.
- `bm3mame/patch.lua` uses 1st-mix offsets and does not apply to `bm3final`.

---

## 8. Decisions (settled 2026-10-11)

1. **Order:** BM3 Final → other BM3 → pop'n → ParaPara; Keyboardmania is a stretch goal.
2. **Fidelity:** match MAME first, enhance later. HLE shortcuts (e.g. the direct keysound path) only after the register-faithful path works and is checked against the oracle.
3. **Platform:** Linux first (this machine, CachyOS). Windows later.
4. **Licence:** GPL-3.0-or-later. MAME-derived files keep their BSD-3 notices. The GPLv3 BGA tools may be merged in directly.
5. **rizu / Route B is dropped.** All effort goes to HIRAKU. Bugs found in existing implementations are reported to the EZ2 port (`EZ2-projects/port`, the active project), following its `NEEDS-OWNER.md` / `REQUESTS.md` conventions.

## 9. M1 outcome (2026-10-11)

Details are in docs/RECOMP.md.
- **Semantics:** one header (`include/hiraku/ppc.h`) defines every instruction for both the interpreter and the generated code. 21,600 QEMU-referenced cases over 108 forms pass (`ctest`).
- **Translation:** all of bm3final and pop'n 4–7 translate (2,168–2,487 functions each) and compile in seconds. Two things had to be handled along the way:
  - **return-up epilogue helpers** (GCC `_restgpr_*_x`);
  - **secondary entries**: branches into the middle of functions.
- **Running:** in interpreter mode each program boots through the kernel into its threads. Shadow checks have found **0 mismatches** between native and interpreted functions so far (bm3final: 153 checked calls; each pop'n: 143–151).
- **Next (M2):** host fibers per guest thread, interrupt delivery and devices. Until then:
  - native execution stops at the first thread switch;
  - the game waits on hardware that does not exist.

## 10. M2 outcome (2026-10-11)

The full write-up is in docs/M2.md.
- **Native code runs the real game on host fibers.** BM3 Final boots through the BIOS-less entry into its multithreaded kernel, with 3 guest threads and about 2.3M fiber switches in 30 virtual seconds.
- **The STARTUP self-test runs on screen** (GCU port, software renderer):
  - **OK:** I/O, IBTN (DS1991 dongle over the 403 serial port), VER, BACKUP, RTC, FDD, ST224, ATAPI, ISO (ATAPI CD reading the user's disc image);
  - **E704:** SPU (no sound board until M4); HDD is behind the SPU.
- **Oracle agreement:**
  - The runtime's MMIO stream matches MAME record for record (`oracle/align.py`, with interrupt-handler accesses and host-clock RTC data masked) for **23,707 records**, up to the first SPU mailbox read.
  - The floppy and MIDI-UART sub-streams match for 63 and 128 records.
- **Shadow mode is clean over boot:** 271 native-vs-interpreted calls identical, 0 mismatches. Calls that touch devices or time are abandoned as impure before they act.
- **Real time:** `--window` (SDL3) shows the game paced to the wall clock: 11.7 virtual seconds took 11.8 s.
- **Performance work outstanding (M9):**
  - The boot RAM clear (356M instructions) and the exception vector glue (about 90 instructions per exception) are interpreted, which is most of the CPU time.
  - Recompiling the vector code is the obvious next step.

## 11. M4 outcome (2026-10-11)

The full write-up is in docs/M4.md.
- **The SPU works:** a 68000 (Musashi) running Konami's sound program, a port of MAME's RF5C400, the mailbox, an HDD on the SPU's ATA bus feeding wave RAM by DMA, and the spectrum analyser.
- **beatmania III The Final:** all startup checks pass, attract mode runs, and the oracle's practice script plays a song with sound.
- **Against MAME:** audio is aligned at 0 ms lag, with RMS within about 5–15%.
- **Lesson:** the skipped BIOS time must still pass for the other boards (`--bios-seconds`).

## 12. M3 outcome (2026-10-11)

The full write-up is in docs/M3.md.
- **Renderer:** bit-exact against MAME on 28/28 frames when replaying MAME's GCU writes.
- **BGA engine:** in lockstep with MAME, 98.8% of song frames byte-identical, at a constant 13-frame song-start offset.
- **Coverage:** no interpreter fallbacks over a full session.

## 13. M5 outcome (2026-10-11)

The full write-up is in docs/M5.md; how to play is in docs/PLAY.md.
- **Game loop:** the full loop (attract → coins → stages → results → game over → attract) runs natively.
- **Real time:** `--window` paces on the audio clock and has keyboard and joystick controls.
- **Latency:** the game's own key-to-keysound latency is 2–20 ms, mean 9.5 ms (measured hands-on). The game scans its keys at 60 Hz, as on the cabinet. The rest is the host audio path.
- **Saves:** NVRAM persists.
- **Pending:** a hands-on test with real audio hardware and controllers.

## 14. Efficiency on N100-class PCs (2026-10-12)

**Goal (owner):** what HIRAKU should beat MAME on, besides accuracy:
- **efficiency:** MAME's BEMANI drivers hiccup on small PCs;
- **latency;**
- **I/O:** analog turntables, lamps.

The target machine is an **N100-class x86 mini PC**: 4 cores, roughly 60 % of a desktop core's speed, 6 MB L3 cache. Judge new work by its worst frames on that kind of machine, not by averages on a fast desktop. The 5700X3D used for these numbers has a 96 MB L3 cache, which flatters emulators.

**Tools:**
- `tools/regress.py record|check|bench` runs four cases (bm3final practice, popn7, ppp, kbm attract).
  - `check` compares a hash of every frame's picture, a running hash of the sound, and the run statistics with a recorded baseline.
  - Every change below is **bit-identical**. The GCU replay also stays pixel-identical to MAME on 28/28 frames.
- `--digest FILE` writes those per-frame hashes.
- Every run prints `frametime:`: host work per frame (pacing waits excluded) as mean/p50/p99/p99.9/max. For the worst frame it also gives the split into disk, draw, 68000 and glue time.
- `HK_PROFILE=1` now also names samples inside shared libraries.

**Results** (share of one desktop core, 5700X3D, headless):

| Case | Before | After |
|---|---|---|
| bm3final practice (gameplay) | 17.0 % | 9.6 % |
| popn7 attract | 7.0 % | 5.0 % |
| ppp attract | 5.5 % | 3.0 % |
| kbm attract | 43.8 % | 38.2 % |
| bm3final 90 s attract vs MAME `-bench` | 7.7× real time (MAME 3.6×) | 12.3× real time, **about 3.4× MAME** |

**What it took:**
- **Fibers:** guest thread switches use a small x86-64 context switch instead of `swapcontext`. `swapcontext` made a system call (signal mask) on each of ~48,000 switches per second, which was 14 % of a run.
- **GCU:** blits, fills and scanout run per row wherever a row neither wraps VRAM nor overlaps itself. Same-parity rows are plain array copies; opposite-parity rows use a fixed pair shuffle. Full-brightness scanout uses SSE2. Every other case keeps MAME's per-pixel loop.
- **Spectrum analyser:** twiddle factors are computed once per sample rate instead of per sample (same values).
- **Profiling scopes:** they read the TSC, not `clock_gettime`. The scopes run on every guest exception.
- **Keyboardmania:** the two-screen rotation works in 32×32 tiles.

**Not done, and why:**
- **Idle skipping (§3.2).** bm3final's idle time is ~9.7 M `yield()` system calls in 200 s, from 96 wait-loop call sites (`f_800c787c` → kernel function `0x800c7830`). Skipping them changes kernel state unless the whole scheduler cycle is proven periodic. Deferred.
- **A render thread.** Scanout now costs ~0.18 ms per frame on this PC. A thread would also need VRAM snapshots or write fences, and would move presentation off the vblank. It isn't worth that.

**Where the time goes now:**
- bm3final gameplay:
  - GCU blits ~25 %, close to memory speed;
  - recompiled game code and kernel ~20 %, much of it the yield churn;
  - 68k SPU ~8 %;
  - scanout ~8 %.
- Keyboardmania: the SWP30's MEG effects DSP (MAME code) is ~40 % of its run, so kbm is the one family likely to struggle on an N100.

**Next:**
1. Measure on real N100 hardware, in `--window`, with `HK_LOG_STALLS=1`.
2. Speed up the SWP30 MEG (kbm).
3. Read-ahead for the large images. CD images up to 1.5 GB are already warmed in the background, and every READ gets a `WILLNEED` hint (`ata.cpp`). The hard disk (bm3, 20 GB) and the DVDs (pop'n) only get the hint, issued just before the synchronous read, so it gives little lead time. One run had a single 125 ms frame at a song load that never reproduced, most likely a cold host file cache. Slower storage on an N100 makes that likelier.

## 15. Other BEMANI platforms as cores (discussed 2026-10-12)

**Idea:** one shell (launcher, `host/`, scripts, the MAME oracle, lamps and controls mapping) and several machine **cores** behind a common interface. The interface covers:
- start-up from the game config;
- running to a time;
- the framebuffer, and sound pulled by the host;
- a per-cabinet description of controls and lamps;
- NVRAM and flash.

The coupling to remove is `runtime/host/{window,input,lights,script}.cpp` including `firebeat/board.h`.

| | DJ Main | System 573 |
|---|---|---|
| Hardware | 68EC020 at 16 MHz, 1 MiB EPROM, 64 KiB RAM; K056832 + K055555 video; 2× K054539; ATA hard disk (password-locked from complete MIX on) | PS1 (R3000A + GTE, GPU, SPU) with an ATAPI CD, PCMCIA flash and security cassettes; digital I/O board (FPGA + MAS3507D MP3) |
| Games | beatmania 1st mix → THE FINAL, complete MIX 1/2, Club MIX, CORE REMIX, DCT, hiphopmania/beatstage, pop'n music 1–3, pop'n stage/EX (~20) | DDR 1st → Extreme, GuitarFreaks 1st–11th, DrumMania 1st–10th, Dance Maniax, Mambo a GoGo, Martial Beat (~60) |
| MAME | every set working, no flags | almost every BEMANI set NOT_WORKING, all with imperfect sound |
| CPU plan | interpret (Musashi; trivially cheap) | interpret to bring it up, then statically recompile the MIPS (the N100 budget needs it) |
| Main risk | MAME's tilemap/graphics framework is tangled into K056832 | the MP3/FPGA sync that DDR's timing depends on; the size of a PS1 core |

**Reused from Firebeat:**
- Musashi;
- `ata.cpp`;
- the CD-audio path;
- the `third_party/mame` shim for MAME devices (BSD-3);
- the analog turntable input and lamps;
- the oracle tooling, which works on any MAME driver.

**Licences:** DuckStation's licence (non-commercial, no-derivatives since 2024) is incompatible with HIRAKU. MAME's PS1 devices (BSD-3), Mednafen/Beetle PSX or pcsx-redux (GPL-2+) are compatible.

**Bonus:** Twinkle (beatmania IIDX 1st–8th) is a PS1 board with a 68000 + RF5C400 sound board, so a 573 PS1 core covers most of it (M15). MAME emulates the external DVD/VCD player's serial control but not the video itself: its driver lists "emulate dvd player and video mixing" as a TODO. Showing the background videos would be something HIRAKU does that MAME doesn't.

**Media:** on hand since 2026-10-12 (§16).

## 16. DJ Main, System 573 and Twinkle media (2026-10-12)

**Where it is:**
- MAME rompath `~/bm3mame/roms-cores/`. It holds hard links to the downloads in `~/Downloads`: the `.7z` sets, and each set's CHDs in a folder named after the set.
- Every set is non-merged: each archive carries its BIOS (System 573 `700*.22g`, Twinkle `863a*`) and its parent's files.

**`-verifyroms`:** all five sets pass, and every CHD has a non-zero data SHA-1.
- `bm1stmix`, `popn1j` and `bmiidx3` are "good".
- `ddr3mj` and `drmn10m` are "best available": MAME itself flags their cassette chips (and drmn10m's discs) as BAD_DUMP. The same goes for every copy.

**Boot test in MAME** (MAME 0.289, 90–150 s, headless, no inputs):

| Set | Platform | Disc(s) | Where it stops | MAME speed |
|---|---|---|---|---|
| `bm1stmix` beatmania (1997) | DJ Main | `753jaa11` (HDD) | title screen | 1,961 % |
| `popn1j` pop'n music (1998) | DJ Main | `803jaa11` (HDD) | title screen | 1,921 % |
| `ddr3mj` DDR 3rd Mix | 573, analog I/O | `887jaa02` | ERROR 147: flash checksum. Needs the first install: set MAME's cassette slot to "install", reset, install, then back to "game" (ksys573.cpp, note 3). | 672 % |
| `drmn10m` DrumMania 10th Mix | 573, digital I/O | `d40jaa02` (runtime), `d40jba02` (multisession), `d40jca02` (HDD installer, needs the network unit) | INFO 508: install from CD to flash done, power-cycle to play | 657 % |
| `bmiidx3` IIDX 3rd style | Twinkle | `gc992-jac01` (program CD), `gc992-jaa04` (Video CD), `992hdda01` (HDD) | device check all OK but backup RAM: E150, 1P Start clears it (normal first boot) | 567 % |

**What this means for the plan:**
- **DJ Main is already cheap in MAME**, about 5 % of a desktop core. A DJ Main core is worth building for latency and I/O (analog turntable, lamps, controller mapping), not for efficiency.
- **573 and Twinkle cost about 15–18 % of a desktop core in MAME**: roughly 30 % of an N100 core, before MAME's video and sound output. Efficiency matters there.
- **First boot:** every new platform needs its first-boot steps, as Firebeat did (ppp, pop'n 6). For the MAME oracle, keep a seeded NVRAM per game, as with kbm.

## 17. The core split and the first DJ Main core (2026-10-12)

**M12, the core split:**
- `runtime/shell/core.h` is the machine interface: init, run, frame and audio hooks, inputs by MAME name, lamps, guest memory for scripts, statistics.
- `runtime/main.cpp` is the shell; `runtime/firebeat/core.cpp` and `runtime/djmain/core.cpp` are the cores. The cabinet tables moved to `runtime/shell/cabinet.cpp`.
- Firebeat stays bit-identical (`tools/regress.py`).

**M13, the DJ Main board** (`runtime/djmain/`):
- Musashi as a 68EC020, MAME's memory map and handlers.
- K056832 ported from MAME, with its DJ Main drawing path.
- K055555 priorities, sprites with MAME's zoom.
- Two K054539s ported from MAME.
- The shared ATA code, with MAME's drive timing on top.
- Interpreting the 16 MHz CPU costs about 1 ms of host work per frame (MAME runs these games at ~1,960 % of real time).

**What it took to match MAME, found with frame/time-stamped oracles** (`oracle/snapcmp.py`, MAME Lua taps, MAME debugger traces):

1. **A Musashi bug.** With `M68K_EMULATE_010` off, the EC020 is decoded as a 68000 in places: no scaled index, no memory-indirect addressing. beatmania's `jmp ([8,pc,d3.l*4])` switch went astray and its loader never ran again. Fix: the option is on (third_party/musashi/HIRAKU.md).
2. **MAME's 68020 cycle costs.**
   - Shifts and rotates have no per-bit cost before the 68030.
   - MOVEM stores cost 3 cycles a register, not 4.
   - Musashi's costs made the CPU ~1 % slow, enough to miss a polling window after the sound-data load.
   - Now each disk command lands a constant 1.40 ms from MAME's.
3. **MAME's drive model** (atastorage.cpp):
   - seek time from the head position, which follows every sector read but the last;
   - 10 µs for simple commands, BSY while busy;
   - the interrupt delivered when the busy time ends;
   - no interrupt at the end of a PIO read (`AtaBus::pio_end_irq`, off for DJ Main; Firebeat's SPU depends on it);
   - the interrupt line passed on only when it changes.

**Still open:**
- **A 2-frame lag** after the disk load (from a constant 1.4 ms offset left over from the self-test).
- **Demo-play notes slightly off** (attract frames 6,120–8,280): probably the chart following the K054539 playback position.
- **The 48 → 44.1 kHz resampling** (linear for now).
- **The ATA security unlock**, for complete MIX and later.

## M0 steps (all done)

1. ~~`git init` HIRAKU and add `.gitignore` for `dumps/` and `build/`~~ Done.
2. ~~Write the dump ingester~~ Done (`tools/ingest.py`; verified byte-identical from `.cue/.bin` and `.iso`). The ingester: CD image → ISO9660 → `HIKARU.EXE`/`FIREBEAT.EXE` → optional ParaPara wrapper → LZSS unpack → identify by SHA-1 against `games/*/game.toml`.
3. ~~Turn the scratch Ghidra stats script into a manifest exporter~~ Done (`analyze/ghidra.sh` + `ExportManifest.java`: 2,205 functions, 376 code-pointer candidates, 36 orphan-code ranges). It exports: functions, blocks, jump tables, pointer-table candidates → `build/<game>/manifest.json` (generated, not committed).
4. ~~Disassemble and annotate the kernel stub~~ Done: docs/KERNEL.md. Remaining: the kernel's exact extent and the thread/semaphore API.
5. ~~MAME oracle with MMIO taps, shared input scripts and reference traces~~ Done (`oracle/`, see `oracle/README.md`).
   - **Taps** record every hardware access into a folded binary stream.
   - **`.his` input scripts** are frame-keyed, so the runtime will run the same files.
   - **Tools:** `trace.py` (summary/dump) and `compare.py` (first divergence).
   - **Optional modes:** `EXACT_PC=1` for exact PCs and instruction traces; `WAV=1` for audio.
   - **Reference runs:** `bm3final-boot.his` and `bm3final-practice.his` (boot → attract → coins → mode select → practice song, with a fixed key pattern).
   - **Findings:** MAME runs are deterministic except RTC reads seeded from the host clock. The runtime will replay RTC values when compared against a trace.
6. ~~Check SDK sharing on pop'n~~ Done. The redump bundle holds pop'n 4–7 (program CD + DVD each); program CDs are extracted to `DATA/Firebeat-dumps/` and ingested (`games/popn4..7/game.toml`).
   - **Kernel:** the assembly glue is identical in all five programs. The BM3 C kernel matches pop'n 5/6/7 at 115/118 unique functions (`analyze/sigmatch.py`). pop'n 4 has the same glue but a differently compiled C kernel. Details are in docs/KERNEL.md.
   - **Game code** shares about 10% with BM3, and pop'n 6↔7 share 35%.
   - The "SDK sharing" risk in §6 is mostly retired for the kernel. The ATA/GCU/SPU driver libraries are still to be checked the same way once they are labelled in BM3.
