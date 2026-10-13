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
| **M6** | Rest of beatmania III | `bm3`, `bm3core`, `bm36th`, `bm37th` run. Mostly manifests and SDK signatures plus SPU ROM revision differences. | 2–3 wk — **unblocked 2026-10-12**: the real `bm3`, `bm3core`, `bm36th` and `bm37th` sets are on hand (verified CHDs, ~/bm3mame/roms-new). Their program CDs unpack to four distinct 1 MiB HIKARU.EXE images, entry 0x80003000. **Next:** game.toml for each, recompile, boot. |
| **M7** | pop'n music 4–8, Animelo 1/2, Mickey Tunes | Same SPU stack, plus DVD on the SPU's ATA and BGM streaming via RF5C400 reg 09 (the hardest audio-timing case). | **in progress** (docs/PLAY.md):<br>• pop'n 4, 5, 7 and 8 boot to attract with sound; pop'n 8 also plays music select and full songs.<br>• pop'n 6 boots after a ~14 min first-boot flash install.<br>• Animelo 1/2 boot.<br>• Against MAME over attract (§18), pop'n 4–8 and Animelo's DVD-streamed BGM is sample-exact within each piece. Piece starts differ by 8–14 ms, probably DVD read timing.<br>• **Left:** model the DVD drive's read times; play every title hands-on.<br>• Mickey Tunes: `popnmt2` (Mickey Tunes!) is on hand and verified; its FIREBEAT.EXE unpacks (entry 0x80002018, as pop'n 6–8). Not yet added. `popnmt` lacks its program CD (976jaa01). |
| **M8** | ParaParaParadise family | `!:EXE:00` loader, YMZ280B, CDDA, sensor input mapping. | **in progress**:<br>• 1st Mix Plus plays (coin, song select, a full song with CD music).<br>• `ppp`, `ppp11` and `ppd` boot to attract.<br>• ppp's attract music (YMZ280B) keeps time with MAME (lag 0, no drift); levels match below 4 kHz, HIRAKU has a little more above 8 kHz (§18).<br>• **Left:** sensors against MAME, and play the others through.<br>• `ppp2nd` is different hardware, out of scope. |
| **M9** | Native polish | Low-latency audio backends, display options, input remap UI, Windows build, per-game config UX. | ongoing — the `hiraku` launcher (game list, controls and lights mapping, settings) and cabinet lamps (HID LEDs, socket: docs/LIGHTS.md) are in.<br>• Adding a game: drop its files on the launcher (or `tools/ingest.py FILE...`, `hiraku --add`). The game is found from its program or set name, and CHDs are unpacked with chdman, memory-capped (`tools/chd.py`).<br>• A portable build for other Linux PCs: `tools/package.sh`.<br>• The picture: off / light (crt-guest-advanced-fast) / full (crt-royale), for all games or one, with sliders that change a running game (launcher Picture; `--picture`, F10). The slang presets are converted ahead of time (`tools/slang2glsl.py`: glslang + SPIRV-Cross), and `runtime/host/picture.cpp` runs them as RetroArch's filter chain does. |
| **M10** | Keyboardmania (stretch) | Dual GCU / dual screen, MIDI-in, the XT446 synth card. | **in progress**:<br>• kbm, kbh, kbm2nd and kbm3rd boot to attract (dual GCU, ROT270, MIDI keyboards, CD audio).<br>• The real XT446 card runs (MAME H8S/2655 + SWP30, pinned to MAME master): in lockstep with MAME 0.289 for the first 84.5 s of attract MIDI; SoundFont stand-in when its ROMs are missing.<br>• **Left:** play a song through; cost, 38 % of a desktop core (SWP30 MEG ~40 % of that), the N100 risk. |
| **M11** | Efficiency for N100-class PCs | Every Firebeat game within its budget on an N100-class mini PC (§14): average under 25 % of one core, no frame over ~8 ms of host work. Bit-identical to the current runs (`tools/regress.py`). | **in progress** (§14):<br>• bm3final about 3.4× more efficient than MAME, 9.6 % of a desktop core in gameplay.<br>• Keyboardmania: the XT446 synth card runs on its own thread, mean frame 7.2 → 6.5 ms, worst 12.8 → 10.6 ms (bit-identical).<br>• Disk images read ahead in big stretches: a cold-cache bm3 practice run went from a 251 ms worst frame to 9.8 ms.<br>• **Left:** the SWP30 MEG is still ~5 ms a frame on its core (kbm).<br>• **Blocked** on measuring real N100 hardware (not available right now). |
| **M12** | Cores: the shell split | The host layer (launcher, window, input, lights, scripts, oracle) talks to the machine through one core interface. Firebeat moves behind it unchanged (bit-identical digests). | **done 2026-10-12** (§17): `runtime/shell/core.h`; Firebeat and DJ Main are cores, each binary links one. Firebeat bit-identical. |
| **M13** | DJ Main core | beatmania 1st mix → THE FINAL, pop'n 1–3, pop'n stage: interpreted 68EC020 plus MAME's devices, checked against MAME. Analog turntable and lamps. | **in progress** (§17, docs/PLAY.md):<br>• `bm1stmix` and `popn1j` run in **cycle-exact lockstep with MAME**: work RAM, sprites and palette identical on every frame, with sound sample-identical. This holds through boot and a scripted game of each: beatmania (coin, turntable, song select, a song with keys and scratches, game over) and pop'n (all 9 buttons).<br>• In `tools/regress.py`.<br>• All 29 MAME sets have game files; the ATA password lock is ported from MAME (unit-tested).<br>• 2026-10-12: 14 more sets ingested and booted against MAME for 155 s: 2nd and 3rd MIX are cycle-exact like 1st mix. 4th MIX → THE FINAL, complete MIX (AA) and pop'n 2/3 reach attract, but their work RAM leaves MAME's between frames ~4380 and ~4730 (73–79 s in). Sprites and snapshots mostly stay identical: a few bytes at first, a timing offset later (5th, compm2, DCT, cmxaac). 1st–3rd MIX and pop'n 1 don't do this, so look for something the later games do at that time.<br>• **Left:** that divergence; hands-on test; faster drawing. |
| **M14** | System 573 core | The roadmap is §19. MAME's PS1 devices with an interpreted CPU, in cycle-exact lockstep with MAME on an analog-board game (CD audio). Then the digital board (MP3 + FPGA). Then a static MIPS recompiler if the CPU is what costs most on an N100. | **in progress** (§19):<br>• `ddr3mj` (digital board) runs **in lockstep with MAME at the pin**: main RAM identical on all 3,600 frames of a 60 s boot, all 60 snapshots pixel-identical. That covers the BIOS, the CD boot, the cassette and PC Card checks and ERROR 147 (no install yet). It is a regress case. Host cost ~3.5 ms a frame on the desktop, interpreted.<br>• Every device is MAME's own code on the framework shim, run by a copy of MAME's scheduler.<br>• **`ddr2m` (analog board, 2026-10-12): in lockstep with MAME at the pin for 400 s.** That covers the first boot, the flash install (two Test presses) and attract with its demo and video: main RAM identical on all 24,000 frames, all 400 snapshots pixel-identical. The sound (SPU + CD audio) matches MAME's WAV: lag 0, correlation ≥ 0.998, level 1.00. It is a regress case.<br>• **A whole game too (`oracle/scripts/ddr2m-play.his`, 350 s):** the same first boot, then two coins, Start, NORMAL, How To Play skipped, BOYS played by stepping on all four panels (GOOD/GREAT/BOO, DANGER, CLEARED), stage 2's song select and Ready. Main RAM identical on all 21,000 frames, 350/350 snapshots, the sound lag 0 with correlation ≥ 0.998. Regress case `ddr2m-play`. **This is 573-2's lockstep exit test, met; the hands-on test with a pad remains.**<br>• The board is chosen per game (`io_board`, `cassette`, `pccard2`, `memory_card_reader` in game.toml): the analog GX700-PWB(F) is ported from MAME. DDR controls and lamps are in `cabinet.cpp`. Coin, start, mode and song select, and a stage start work from the installed flash.<br>• Fixed: the shim's CD reader didn't byte-swap little-endian CD audio (noise); the shim's address map took over 12 GB per 573 run (per-byte tables for MAME's unmapped ranges), which got the desktop OOM-killed during a parallel regress run.<br>• **Owner's hands-on test (2026-10-12): "worked nicely", with slight crackles.** Those came from MAME's mix: the SPU and the CD audio summed at 1.0 each peak at 2.0 and clip at the output (~1 % of samples, a third on loud songs; MAME's WAV clips identically). The 573 core now plays the mix at 0.5, which never clips, in MAME's balance. `--mame-mix` restores MAME's level for oracle comparisons. A departure from MAME's output stage only: the machine is unchanged.<br>• **Next:** the install flow for `ddr3mj` (573-3). |
| **M15** | Twinkle core (beatmania IIDX 1st–8th) | The M14 PS1 core plus Twinkle's SPU (68000 + RF5C400, already in HIRAKU), SCSI CD, ATA hard disk. Then the background video MAME lacks: Video CD (MPEG-1) for early styles, DVD for 6th–8th. | **in progress** (§21):<br>• **TW-0 done:** oracle (pinned MAME with `twinkle.cpp`, Lua taps on both CPUs), `bmiidx3` and `bmiidx` ingested.<br>• **TW-1, boot and attract done (2026-10-13):** `hiraku-bmiidx3` (MAME's PS1 and MAME's 68000, all of MAME's devices on the shim) runs in lockstep with MAME: main RAM on all 9,000 frames, 150/150 snapshots, all 2.15 M traced device accesses, sound lag 0. Regress case `bmiidx3-boot`.<br>• `bmiidx3` (3rd style, MAME working) is the first set; `bmiidx` (1st style, MAME `NOT_WORKING`) arrived 2026-10-13.<br>• Phases TW-0 (oracle) → TW-1 (lockstep) → TW-2 (playable) → TW-3 (background video, beyond MAME) → TW-4 (N100 budget) → TW-5 (other styles, DVD).<br>• MAME ~570 % real time. |

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

**The MAME-device cores against MAME (2026-10-12 night).** The same method as above: MAME's `-bench` against HIRAKU headless, the same 150 s of game time, 3 runs each, on the same desktop (wall time, start-up included).

| Case | MAME (flatpak 0.289) | MAME at the pin | HIRAKU | HIRAKU per frame |
|---|---|---|---|---|
| `ddr2m`, from the installed flash (flash check, attract with 3D, demo, video) | 650 % real time | 595 % | 683 %, **1.05× MAME** | mean 2.46 ms, p99 4.0, worst 4.8 |
| `bm1stmix`, power-on to attract | 1,807 % | — | 1,762 %, **0.98× MAME** | mean 0.94 ms, p99 1.7, worst 2.2 |

- **These cores are level with MAME, by design.** They run MAME's own CPU and device code; only the scheduler and memory map are HIRAKU's. Firebeat's 3.4× comes from the static recompiler.
- **DJ Main needs nothing more.** Under 1 ms a frame is under 2 ms on an N100.
- **System 573 is at the edge of the budget on an N100.** Scaled by ~0.6, ddr2m is ~4 ms a frame on average (~25 % of a core) and ~8 ms at worst. Section 19's 573-4 has the profile.
- **With `tools/pgo.py` (2026-10-13):** ddr2m runs at ~950 % real time, ~1.45× MAME. Mean 1.76 ms a frame, worst 4.2 ms; ~2.9 / ~7 ms scaled to an N100.

**Profile-guided optimisation for every core (2026-10-13, `tools/pgo.py`).**
- It trains on the regress cases whose games are ingested and keeps one profile per core (build/pgo/<core>.profdata, never committed).
- Per core, because the cores build the same sources into separate copies: Musashi as a 68000 and as a 68EC020, MAME's shim for the XT446 and for the 573. A single shared profile blended them, and DJ Main and ddr2m got slower.
- CPU seconds per case, 2 runs each, every case bit-identical:

| Case | Before | With PGO |
|---|---|---|
| kbm attract | 23.35 | 18.8 (**−19 %**, mean 5.2 ms a frame) |
| popn7 attract | 3.10 | 3.00 (−3 %) |
| ppp attract | 1.83 | 1.80 (−2 %) |
| bm3final practice | 19.35 | 19.12 (−1 %) |
| ddr2m, 150 s from the flash | 21.9 (wall) | 15.9 (**−27 %**) |
| bm1stmix boot | 8.71 | 8.75 (no profile) |

- **Profiled:**
  - Firebeat's runtime, its SPU's 68000 and the XT446 (the synth card's SWP30 is where kbm gains).
  - The 573's MAME code.
- **Not profiled:**
  - Firebeat's recompiled game code: no gain, and only the trained games would get a profile.
  - DJ Main: 5 % slower with its profile.
- **The training mix matters:** ddr2m was 1.44× with a profile trained without the flash-install run, and is 1.38× with it.
- **A measurement note:** the regress cases write a snapshot every 60 frames to out/ on the DATA disk. Single 20–200 ms "frames" in `regress.py bench` are those writes, not the machine: with out/ on tmpfs, bm1stmix's worst frame is 2.2 ms.

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

**Keyboardmania's synth card (later on 2026-10-12):**
- The XT446 runs on a thread of its own (`runtime/firebeat/synth.cpp`), beside the game's CPU.
  - The game only sends it MIDI. The board queues "render n samples" and "MIDI byte" in the same order as before, so every byte lands on the same sample.
  - The board holds each frame's audio and adds the card's samples at vblank, with the same sums in the same order. The output is bit-identical (`kbm-attract`).
  - Latency is unchanged: the frame's audio is handed on at the same point.
  - `HK_XT446_INLINE=1` runs the card on the main thread, as before.
- The MEG's interpreter is about 8 % faster (`third_party/mame/README.md`), bit-identical with MAME's `step()`.
- Measured on the 5700X3D, kbm attract:
  - mean host work per frame 7.18 → 6.46 ms;
  - worst frame 12.8 → 10.6 ms.
- The card is now the critical path, at about 5.3 ms a frame:
  - the MEG is 64 % of it, about 12 ns a step, limited by the serial dependencies between its steps rather than instruction count;
  - the H8 firmware does real work (7.3 M instructions/s, no idle loop to skip).
- Tried and dropped, bit-identical but no faster:
  - a branchless MEG ALU (slower);
  - a fast path for arithmetic-only steps (1 %).

**Next:**
1. Measure on real N100 hardware, in `--window`, with `HK_LOG_STALLS=1`.
2. The SWP30 MEG (kbm) is still ~5 ms a frame on one core. What's left is structural: a compiled (JIT-style) MEG program, or a faster H8 core.
3. ~~Read-ahead for the large images.~~ **Done 2026-10-12** (`runtime/dev/ata.cpp`):
   - A READ's sectors are now fetched with one `pread` per stretch, not a stdio seek and read per sector.
   - Each READ also asks the kernel for the next 8 MB. In bm3 practice, 600 of the 615 READs on the sound hard disk continue the previous one.
   - Measured with the image evicted from the page cache (`posix_fadvise(DONTNEED)`, no root needed), on a DRAM-less SATA SSD:
     - bm3final practice, worst frame 251–335 ms → 9.8 ms;
     - pop'n 8's DVD reads, 4 ms → 0.06 ms in their worst frame, warm or cold.
   - Bit-identical (`tools/regress.py`).
   - What follows is the original note. CD images up to 1.5 GB are already warmed in the background, and every READ gets a `WILLNEED` hint (`ata.cpp`). The hard disk (bm3, 20 GB) and the DVDs (pop'n) only get the hint, issued just before the synchronous read, so it gives little lead time. One run had a single 125 ms frame at a song load that never reproduced, most likely a cold host file cache. Slower storage on an N100 makes that likelier.

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
| MAME | every set working, no flags | every BEMANI set has imperfect sound. The analog-board games (DDR 1st/2nd, GuitarFreaks 1st/2nd) work; almost every digital-board game is NOT_WORKING (§19) |
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
| `ddr3mj` DDR 3rd Mix | 573, digital I/O | `887jaa02` | ERROR 147: flash checksum. Needs the first install: set MAME's cassette slot to "install", reset, install, then back to "game" (ksys573.cpp, note 3). | 672 % |
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

**Lockstep with MAME (later the same day):**

4. **The vblank begins at multiples of the frame period** (MAME's screen: the beam is then at line 384). Its interrupt, the screen update and the frame notifier all happen there, not 384 lines into the frame. This was the constant 1.40 ms offset, from the moment the game enables the interrupt.
5. **A TRAP costs its own 4 cycles plus the whole exception** in MAME: 24, where Musashi charged 20. beatmania's kernel calls are TRAP #0.
6. **Musashi only looks at the interrupt lines when `m68k_execute()` starts.** MAME's scheduler aborts the CPU's timeslice whenever a device changes a line, so the interrupt is taken after the current instruction. The board now ends the timeslice when the IDE or vblank line changes. Single-stepped trace runs had hidden this.

**Result:**
- Both games run in lockstep with MAME through power-on, self-test, the disk loads and attract mode (155 s):
  - 150/150 snapshots pixel-identical with no frame offset;
  - the sprite list and palette identical on all 9,000 frames;
  - all 135 disk commands within 25 µs (the precision of the MAME taps);
  - 813 kernel calls compared one by one, identical to within half a microsecond.
- **The K054539 output is sample-identical to MAME's WAV** (`HK_DJ_RAW48`), one 48 kHz sample apart.
- Both games are cases in `tools/regress.py` (bm1stmix-boot, popn1j-boot).
- Host cost: ~1 ms a frame (5.7 % of a desktop core), most of it drawing.

**Gameplay and cycle-exact lockstep (later the same day).** `oracle/scripts/bm1stmix-play.his` plays a game:
- 2 coins and 1P start;
- the turntable picks "skip" at the practice-stage prompt;
- sound select;
- "u gotta groove", played with a fixed pattern of keys and scratches; it fails;
- continue countdown, game over, back to attract (217 s).

Comparing it with MAME, and tracing both CPUs instruction by instruction (MAME's debugger `trace` with `totalcycles` against a temporary Musashi instruction hook), found:
7. **MAME's Lua input lands a frame late.** MAME latches digital inputs in its frame update, which runs before the Lua frame callback. A button the script presses at frame F is seen from F+1; an analog value it sets is read live. The shell now delays a script's button changes by `Core::script_button_delay()` (1 for DJ Main, 0 for Firebeat, whose recorded runs predate it). Host input is not delayed: it matches MAME's own keyboard path. The turntable field is `TT1/Dial`, not `TT1/P1 Dial`.
8. **The K054539's key-on status and channel positions only move at MAME's stream updates:** on a write to that chip and 50 times a second. MAME doesn't update the stream on reads. beatmania's sound driver counts frames while a channel plays, so it saw samples end up to a frame early. The board still renders on every access (for latency); reads return the registers as of the last commit (`K054539::commit()`).
9. **Musashi's reset exception (4 cycles on a 68020) wasn't counted**: `m68k_execute()` took it from the first slice without reporting it, so the whole run sat 4 cycles early and frame-boundary interrupts landed an instruction apart.
10. **An idle loop (`bra` to itself) that ends exactly on a slice boundary ran once more** in Musashi (`remaining % cost` is 0 there).
11. **MAME's IDE drive sets the index-pulse bit (IDX)** on a status read once more than a rotation (1/90 s) has passed since it last did. The game copies the status into RAM.
12. **MAME's drive times are exact attotimes, and the CPU stops on the last whole clock at or before them.** HIRAKU rounded up: 400 ns is 6 clocks, not 7.
13. **MAME's 68k checks the slice before its first instruction (a `while` loop), Musashi after it (`do`-`while`).** An interrupt arriving during another's exception (the vblank 6 clocks after a disk interrupt) is taken before the first handler instruction.

Points 9, 10 and 13 are opt-in Musashi hooks (`third_party/musashi/HIRAKU.md`), so the Firebeat SPU is unchanged.

**Now:** cycle-for-cycle lockstep with MAME.
- **Boot, both games:** the whole work RAM, the sprite list and the palette are identical on every one of the 9,000 frames, and all snapshots match.
- **beatmania gameplay:** the same for all 12,600 frames, with 210/210 snapshots identical.
- **Disk:** every disk command is issued on the same CPU clock as in MAME (217 of 217 in gameplay).
- **Sound:** the chip output is sample-identical in all three runs (10.4 M samples in gameplay, keysounds included).
- `bm1stmix-play` is the third DJ Main case in `tools/regress.py`.

**pop'n music 1 gameplay** (`oracle/scripts/popn1j-play.his`, 233 s) matched MAME on the first run:
- the script: 2 coins, the red button starts, game mode select, a blue Sel button moves the genre twice, then "Quick Master" played with a fixed pattern over all 9 buttons, the continue countdown, ranking, game over and attract;
- work RAM, sprites and palette identical on all 13,500 frames, and 225/225 snapshots;
- the sound chips sample-identical (11.2 M samples);
- it's the case `popn1j-play` in `tools/regress.py`.

**Every MAME DJ Main set (2026-10-12).**
- `games/<set>/game.toml` for all 29 sets, generated from MAME's driver.
- `map = "u"` / `"a"` for MAME's `main_map_u` / `_a`: the IDE ports and tile RAM elsewhere.
- `djpstage` for pop'n stage EX: 10 buttons plus 3 selection buttons; lamps not decoded.
- CMake builds a binary for every `core = "djmain"` file.
- The **ATA password lock** is ported from MAME (`runtime/dev/ata.cpp`):
  - a disk with passwords is locked at power-on and soft reset;
  - reads fail with ERR/ABRT;
  - SECURITY UNLOCK clears a password when its 32 bytes match;
  - both must be cleared;
  - `tests/ata/test_ata_security.cpp` covers it.
- Run against MAME (2026-10-12, 155 s boot): bm1stmix, popn1j, bm2ndmix and bm3rdmix are cycle-exact. bm4thmix–bmfinal, bmcmxaac and popn2/3 diverge in work RAM at ~73–79 s (see M13). bm3rdmixe boots; MAME 0.289 lacks the set.

**Lamps.**
- beatmania's lamp table follows MAME's decoding bit for bit: buttons, start, effect, the four highlights, the neon and the board LEDs. The writes themselves are cycle-exact with MAME.
- MAME doesn't name pop'n's bits, so I worked them out from `popn1j-play`, registers logged against the scripted presses:
  - buttons 1–9 on reg 0 bits 0–4 and 6–9;
  - a 5-step top-lamp chase on reg 1 bits 6–10;
  - 4 side lamps on reg 1 bits 0–3;
  - reg 1 bit 15 is the coin counter and bit 14 the coin blocker.
- They take Firebeat pop'n's names (`runtime/shell/cabinet.cpp`, docs/LIGHTS.md), so one lights file serves both.

**Still open:**
- ~~The 48 → 44.1 kHz resampling.~~ Done 2026-10-12:
  - a core now says what rate it hands the shell (`Core::audio_rate()`), and DJ Main hands on the K054539s' 48 kHz as they are;
  - the SDL device and `--wav` follow that rate, so there's no resampling (most devices run at 48 kHz);
  - `--wav` now scales by 32768, as the chips and MAME do, so a DJ Main recording is sample-identical to MAME's `-wavwrite` (10.4 M samples in `bm1stmix-play`).
- **The ATA security unlock**, for complete MIX and later.
- **Faster drawing:** the tilemaps are drawn per pixel.

## 18. Firebeat music against MAME (2026-10-12)

Firebeat runs recompiled code, so it can't be in lockstep with MAME. What can be checked is whether the music keeps time. The tool is `oracle/audiosync.py HIRAKU.wav MAME.wav`:
- it finds the offset between the runs from the loudness envelopes, then refines it on the waveform (music repeating every couple of beats fooled the envelope alone);
- it reports the lag, waveform correlation and level in 2 s windows, with levels measured at each file's own rate.

**Setup.**
- MAME sets for `ppp` and `popn8` are in `~/bm3mame/roms-fb`: HIRAKU's ROM files plus the CHDs from ~/Downloads, hard-linked.
- Both machines start from the same saved state: HIRAKU's `--nvram` directory, and `HIRAKU_NVRAM=dir oracle/mame.sh ...` for MAME.
- From blank NVRAM, ppp stops at a flash-data error on both, wanting test mode.
- From blank NVRAM, pop'n 8 spends about a minute at its flash check in MAME. HIRAKU raced through it: its flash chip (`runtime/dev/flash29f016a.cpp`) erased instantly.
  - It now takes MAME's erase times: 1 s per 64 KiB sector, 16 s for the chip, close to the datasheet's typical values.
  - Until an erase ends, reads return the toggling status and writes are ignored.
  - pop'n 8's first boot now follows MAME's: the flash check, then the logos and attract.
  - It costs a minute or so on a first boot, as on the real board. The regress runs don't erase flash, so they're unchanged.

**Results, 2 minutes of attract:**
- **pop'n 8, DVD-streamed BGM** (RF5C400 streaming): in step to within 1 sample over 28 s (no drift), waveform correlation 0.998–1.000, the same level.
- **ParaParaParadise:** its attract music turned out to be YMZ280B sample playback, not CD audio: with the YMZ280B muted, HIRAKU is silent. The samples are the CD's music track, ADPCM-coded into flash.
  - Timing: from 78 s on, lag 0 samples, correlation 0.94–1.0, no drift. Checked against the disc's own track, both emulators jump between samples at the same moments by the same amounts.
  - The first sound comes 4.4 ms later in HIRAKU relative to the rest.
  - Level: the same below 4 kHz, within 0.3 %. Above 8 kHz HIRAKU has more: +5 % at 8–12 kHz, up to +35–46 % at 16–20 kHz. HIRAKU brings the chip's 88.2 kHz down to 44.1 kHz by averaging pairs, which filters less than MAME's resampler. Overall 1.00–1.03.
  - A sharper downsampling filter would match MAME better and alias less, but it changes Firebeat's sound digests, so it's left as a choice.
  - (The first version of this check reported "0.7 dB louder": `audiosync.py` resampled MAME linearly, which dulls its top octaves. Levels are now measured at each file's own rate.)
- HIRAKU starts 3.45 s (ppp) and 11.4 s (pop'n 8) ahead of MAME because it skips most of the BIOS boot.

**pop'n 4–7 and Animelo 1/2**, 2 minutes of attract each. pop'n 4, 6 and Animelo 2 started from saved state; the others blank.
- **Within each piece of music, every one is sample-exact with MAME:** correlation 0.99–1.000, level 1.00–1.01, no drift.
- **Between pieces, start times differ by 8–14 ms** (Animelo 2: 8–12 ms; pop'n 5 and 7: 14 ms; pop'n 6: 10.6 ms), earlier or later than MAME, about half a frame. The next likely cause is HIRAKU's DVD drive answering READs at once, where MAME models read times, as DJ Main needed for its disk.
- **pop'n 4 after 82 s and Animelo 1 at 88–100 s play different content** from MAME: the attract sequence goes a different way. Not looked into.
- The SPU's DVD drive gets ATA command E2 (STANDBY) and aborts it, as MAME's ATAPI device does.
- MAME sets: `/run/media/roganis/DATA/mame-roms/<set>/`. The CHDs were converted from the Firebeat-dumps images (`mame-roms/convert.sh`), so MAME warns about their checksums.
  - DVDs need `chdman createdvd -hs 2048`: MAME 0.289's DVD drive throws "unhandled exception" on chdman's default 4 KiB hunks.

**Not checked:** ppp's sensors, and gameplay rather than attract.

## 19. System 573 roadmap (M14, written 2026-10-12)

The approach is DJ Main's (§17): port MAME's devices, interpret the CPU, and match MAME cycle for cycle before anything else. Then make it cheap enough for an N100.

### What MAME has

Sources: MAME master `ksys573.cpp` (149 sets), `k573dio`, `k573fpga`, `mas3507d`, `k573cass`, and the PS1 devices in `devices/cpu/psx`, `video/psx`, `sound/spu`. Every file is BSD-3, which is fine for GPLv3.

**The board:**
- A CXD8530CQ (R3000A with GTE) at 33.8688 MHz with 4 MiB of RAM.
- The CXD8561Q GPU, and the SPU with 512 KiB of RAM.
- BIOS `700A01` (4 Mbit).
- 16 MiB of onboard flash: eight 29F016As, the chip HIRAKU already has (`runtime/dev/flash29f016a`).
- An M48T58 timekeeper, an ADC0834 for analog inputs, and two PCMCIA slots.
- A CR-589 ATAPI CD drive, whose CD audio goes into the board.
- The security cassette on the PS1's serial port 1: an X76F041, X76F100 or ZS01, some with a DS2401 serial number.
- JVS, for the memory-card reader.

**The I/O boards** (mapped at 0x1f640000):
- **Analog, GX700-PWB(F):** DDR 1st and 2nd, GuitarFreaks 1st and 2nd, DrumMania 1st, early Dancing Stage. Music is CD audio.
- **Digital, GX894-PWB(B):** DDR 3rd Mix and later, GuitarFreaks 3rd and later, DrumMania 2nd and later, Dance Maniax, Mambo a GoGo, Martial Beat. It has an FPGA that decrypts MP3 data, held in 64 MiB of EDO RAM, and feeds it to a MAS3507D. It also drives lamps.

**MAME's status:**
- Every BEMANI set is flagged `IMPERFECT_SOUND`.
- **The analog-board games mostly work in MAME:** DDR 1st (all but `ddrja`), 2nd Mix with its Link and Club versions, GuitarFreaks 1st and 2nd, and the analog Dancing Stage sets.
- **Almost every digital-board set is `NOT_WORKING`.** The exceptions are three Korean Percussion Freaks sets. DrumMania 1st (analog) is `NOT_WORKING` too. The driver doesn't say why.
- **Correction to §15 and §16:** `ddr3mj` is a digital-board game, not an analog one. Both 573 sets on hand, `ddr3mj` and `drmn10m`, are digital.

**MAME's timings, which decide how hard lockstep will be:**
- **CPU: one cycle per instruction**, including the load delay slots, with no wait states and no cache-miss cost (the execute loop in `psx.cpp`). That is easy to match exactly. It also makes a cycle-exact static recompiler straightforward: a block costs its instruction count.
- **CD: a fixed 30,000 CPU cycles per sector** (`ATAPI_CYCLES_PER_SECTOR`; the driver notes the BIOS needs at least 2,000).
- **The MP3 playback counter**, which the digital games read to place their notes:
  - MAME's FPGA counts machine time from the first MPEG frame sync, at 44.1 kHz, not decoded samples.
  - So the MP3 decoder can run on a worker thread, as the XT446 does, without changing what the game sees.

**Size of the port:** about 16,000 lines of MAME code.

| Part | Lines |
|---|---|
| CPU, GTE, DMA, IRQ, timers, SIO | ~5,200 |
| GPU | ~3,500 |
| SPU | ~3,200 |
| Driver parts | ~1,500 |
| Digital board (k573dio, FPGA, MAS3507D) | ~1,350 |
| Cassette chips | ~1,500 |

**Cost in MAME:** 650–670 % of real time, about 15 % of a desktop core (§16).

### Media needed

"Match MAME first" needs a game MAME runs. Neither set on hand qualifies.

Get one analog set:
- **`ddr2m`** (DDR 2ndMIX, CD `895jaa02`), recommended: DDR is the biggest family and its pads are plain switches.
- or **`gtrfrks`** (GuitarFreaks, CD `886__d02`).

Each is the analog board, an X76F041 cassette and one CD, nothing else. MAME's set holds the BIOS, the cassette data and the CHD.

### Phases

Each phase has an exit test. Sizes are assisted-effort guesses; DJ Main went faster than its estimate.

**573-0. Oracle and media** (a day or two)
- **`tools/ingest.py` for 573 sets:**
  - the BIOS and the cassette EEPROM;
  - the CD, via `chdman extractcd` (memory-capped);
  - a `game.toml` giving the I/O board, cassette type, PCMCIA cards and flash.
- **MAME reference runs** for each set: boot to attract, and a scripted song.
  - First-boot installs (CD to flash) are done once in MAME, and the flash, timekeeper and cassette are kept as seeded NVRAM (`HIRAKU_NVRAM`), as for kbm and pop'n 8.
  - `ddr3mj` installs with MAME's "install" cassette (ksys573.cpp, note 3).
- **Lua taps:** CPU state, main RAM, VRAM and SPU RAM at chosen frames. If Lua can't reach VRAM, use the debugger's `save` command.
- **Determinism:** two MAME runs from the same script must match. The M48T58's clock comes from the host, as Firebeat's RTC65271 did; pin it or mask it in comparisons.
- **Exit:** reference runs for `ddr2m` (or `gtrfrks`), `ddr3mj` and `drmn10m` that repeat exactly.

**573-1. The board, interpreted, in lockstep** (1–2 weeks)
- **What to build in `runtime/sys573/`:**
  - MAME's PS1 CPU (GTE, DMA, interrupts, root counters, the serial port for the cassette);
  - the GPU and the SPU;
  - the 573 memory map;
  - the flash bank, on the existing flash chip;
  - the timekeeper, ADC0834, analog I/O board, and X76F041/X76F100 cassettes;
  - the CR-589 on `runtime/dev/ata` (which already does ATAPI), with MAME's per-sector time;
  - CD audio, through the existing path.
- **Bring-up order:** the BIOS to its self-test, then its flash-against-CD check, then the game.
- **Measure the host cost by part** (CPU, GPU, SPU) with `HK_PROFILE`. This decides 573-4.
- **Exit:** the analog set boots and runs attract in lockstep with MAME: main RAM and VRAM identical at every snapshot, SPU output sample-identical. Regress cases are added.

**573-2. Playable analog games** (about a week)
- **Controls**, by MAME name in `cabinet.cpp`:
  - DDR: 4 panels per player and the menu buttons;
  - GuitarFreaks: 3 neck buttons, pick, wailing, and the effect knob (analog, through the ADC0834).
- **Lamps:** the analog board's outputs (`gx700pwbf_output`) mapped to the cabinet and pad lights (docs/LIGHTS.md).
- **Scripts:** a scripted game (coin, song select, a song with steps, results) in lockstep, like `bm1stmix-play`.
- **Launcher entries and PLAY.md.**
- **Exit:** a scripted song in cycle-exact lockstep with MAME, and a hands-on test by the owner, with real pads if available.

**573-3. Digital I/O, the MP3 games** (2–3 weeks; the main risk)
- **What to port:**
  - `k573dio` and `k573fpga`: MP3 decryption, the playback counter, lamp outputs;
  - the MAS3507D: MAME decodes with its `mp3_audio`; HIRAKU decodes on a worker thread;
  - the DS2401, and the ZS01 and X76F100 cassettes;
  - the multi-session and network units, stubbed as MAME does.
- **Games:** `ddr3mj` and `drmn10m` are already on hand.
- **Lockstep with MAME first.** Then find out why MAME flags these sets `NOT_WORKING`:
  - play them through in MAME, and read its bug tracker and git log;
  - depart from MAME only where a second reference shows it is wrong: hardware notes (psx-spx, community 573 research such as spicyjpeg's), or recordings from a real cabinet if the owner can get them;
  - each departure is a switch, so a MAME-exact mode stays available for the oracle.
- **Exit:** `ddr3mj` plays a full song with the MP3 in step with the arrows (`oracle/audiosync.py` against MAME's WAV), and `drmn10m` plays a song.
- **Progress (2026-10-13): `ddr3mj` installs, runs attract and plays a song, in lockstep with MAME.**
  - **What it took:**
    - MAME's install cassette: the slot's "install" option, from the set's `ge887ja.u1`/`u6`, with `--cassette install`. The core's own MAME devices already did the rest (digital board, FPGA, MAS3507D, YI cassette, PC Card).
    - The MP3 in the mix: the MAS3507D's stream as its 50 Hz updates produce it. It is never updated from the host side: it decodes and asks the FPGA for data as it updates, so that would change the machine. The mix waits for it, up to 20 ms. Other MP3 rates are resampled for the host only; this game's are 44.1 kHz.
    - `MAME_ARGS` in `oracle/mame.sh`, for slot options.
    - Regress cases that start from another case's NVRAM (the power cycle between install and game).
  - **The install** (`ddr3mj-install.his`, 16,200 frames, no inputs): main RAM identical to MAME on every frame, and all 27 NVRAM files (flash, PC Card, timekeeper, cassette) byte-identical after it.
  - **Attract** (`ddr3mj-attract.his`, 9,000 frames) and **a game** (`ddr3mj-play.his`, 16,000 frames: XANADU stepped through to its result, then stage 2):
    - main RAM identical to MAME on every frame;
    - the sound, MP3 included, matches MAME's WAV: lag 0, correlation ≥ 0.999, level 1.00.
  - **Speed** (recompiled, desktop): attract 0.48 ms a frame, the song 0.62 ms.
  - **Why MAME flags these sets NOT_WORKING is still open.** Its sources don't say, and so far the game behaves as MAME does. Next: play it through by hand, and compare the arrows with the music.
  - **Next:**
    - ~~the install cassette in the launcher~~: done (a first boot uses it; right-click "Boot once with the install cassette");
    - ~~`drmn10m`: ingest, the ZI cassette (ZS01 + DS2401), DrumMania's controls and lamps~~: done; the multi-session unit: below.
- **Progress (2026-10-13): `drmn10m` (DrumMania 10th Mix) runs as far as MAME does.**
  - **What it took:**
    - the ZI cassette (ZS01 + DS2401);
    - the digital board's outputs through MAME's `drmn_output_callback`;
    - DrumMania's inputs: the pads on IN2's stage bits, the cymbal and bass pedal on its buttons, select L/R;
    - the `drmn` family in `cabinet.cpp` (controls, lamps) and the launcher.
  - **The flow:**
    1. The first boot installs the runtime CD (`d40jaa02`) to the onboard flash by itself and shows INFO 508 (power-cycle).
    2. The next boot shows NOTE 605 (RTC-RAM cleared): Test goes on.
    3. The boot checks run, then **"Please call attendant": the multi-session unit can't be found.**
  - **MAME stops at the same screen.** Its `k573msu` is an empty stub, which is the likely reason behind its NOT_WORKING flag.
  - Regress cases `drmn10m-install` and `drmn10m-boot` (from what the install left), checked against MAME.
  - **The multi-session unit (GXA25)** is a box of its own:
    - a Toshiba TMPR3927 (MIPS R3900) running firmware from a 29F400 flash, `flash.20t`, which is in MAME's set;
    - an ATAPI CD drive with the multi-session disc (`d40jba02`, on hand);
    - four TC9446F audio decoders (MPEG-2 audio / AC-3);
    - two PC16552 UARTs, and its own M48T58.
  - It streams and mixes the music, so the songs' audio comes from it. The 573 talks to it over SIO1, the 8-pin mini-DIN cable. From DrumMania 4th Mix on the game won't run without it.
  - **There is an emulation to start from:** the branch `bemani_k573msu_wip` of 987123879113's (windyfairy's) MAME fork, last changed 2022-03, BSD-3. It has:
    - a TX3927 CPU in `mips1.cpp` (~1,500 lines);
    - `k573msu.cpp` (~600 lines);
    - `tc9446f.cpp` (~330 lines, decoding through pl_mpeg);
    - SIO1 changes in `psx/sio.cpp` (~500 lines).
  - It is a WIP, about 11,000 MAME commits behind, with TODOs (the CPU clock, an interrupt trigger, a buffer reset "hack").
  - **The plan:**
    1. Port it onto the shim as a departure from MAME: a switch, documented with its source. MAME-exact stays the default and the oracle's mode.
    2. Its SIO1 changes touch the PS1 device every 573 game uses: they go in only for games with the unit, or behind the switch, so DDR's lockstep holds.
    3. A second MIPS CPU then runs in the machine. MAME's `mips1` interpreter first; the recompiler later if it costs.
    4. Exit: DrumMania 10th Mix reaches attract and plays a song with its music.
- **Progress (2026-10-13): the multi-session unit runs, and DrumMania 10th Mix gets past it, further than MAME.** Attract (logo, title, hit chart, the How To Play demo) and a game (mode and music select, the first stage of 花火 with its music, the hit chart updated after) run.
  - **Ported** from windyfairy's fork (third_party/mame/README.md lists every change): the TX3927 in MAME's current `mips1.cpp`, plus SoftFloat 3; `k573msu`; `tc9446f`; and SIO1 as a UART (`psxsio1_uart_device`), which only games with the unit get.
  - **What it took, beyond the port:**
    - **The shim's `umask`** didn't repeat `umask16` across the 32-bit bus as MAME does, so the unit's 16-bit UART handlers were never called. The boot loader then failed its UART check: "DEVICE CHECK FAIL. HALT.", LED code 0xc0.
    - **MAME's ATAPI layer** rounds a chunk under the byte count limit down to whole sectors, so a REQUEST SENSE asked for 8 bytes at a time got none. Fixed as the fork does: such a chunk is all the data.
    - **MAME's 16550 lost a full TX FIFO:** 16 queued bytes looked like none. The unit's replies stopped after 16 bytes, and the 573 restarted the session every few seconds. Fixed with a count. This is also the bug behind Keyboardmania's silent XT446 in MAME.
    - **The TX3927 without cache emulation:** with MAME's caches and the fork's ignored `CACHE`, the unit ran stale code after its OS copied itself to RAM.
    - **SIO1's DSR stays the cassette's.** The fork also wires the unit's DTR to it; the firmware drops DTR while setting up its UARTs, and the game reported ERROR 120 (no cassette).
  - **What the unit does:**
    1. Its boot loader (`GXA25 BOOTLOADER`; `HK_MSU_CONSOLE=1` prints it) tests its RAM, configures the FPGA, checks its UARTs, initialises the four TC9446Fs, then loads and checks `/GXA25PWB.EBF` from its CD.
    2. The program talks to the 573 over SIO1 at 115200 baud: SLIP-framed packets with sequence numbers and CRCs, a keepalive every second, and the game's `GCD40JAA`.
    3. The 573 has it open `JA10.DAT`.
    - In a single-cabinet game the songs' music is the digital board's MP3, not the unit's. Its decoders (`NNNN__BK.DAT` / `___K.DAT` on its CD) are for session play with GuitarFreaks; their four streams are in the mix (at 0.65, the fork's routing), silent so far.
  - **Cost:** the unit is a second MIPS, interpreted by MAME's `mips1`, and costs more than the whole 573:
    - at first, at the fork's 25 MHz (the default), the run took ~6–10 ms a frame on the desktop;
    - at the real 132 MHz (8.25 MHz × 16, `HK_MSU_132=1`), ~31 ms. The game goes the same way at both: the same link timing, the same lamps.
    - The profile is `mips1`'s memory path (`translate`, `fetch`, `read_sized`, a `std::function` per access), around the firmware's DSP-status polling loop.
  - **Against MAME:** `drmn10m-install` is still MAME's: main RAM on every frame, and the NVRAM byte for byte. `drmn10m-boot` is MAME's until the game first touches SIO1 (frame 1688), where the link to the unit starts.
  - **Regress cases:** `drmn10m-install`, `drmn10m-boot` (now on to attract) and `drmn10m-play` (a game through the first stage). All the other cases were unchanged by the shim's umask fix.
  - **Next:** a faster TX3927 (below); session play needs a second cabinet.
- **Progress (2026-10-13): the unit, 3.7× faster.** The drmn10m run, the unit's steady state after its boot: 10.6 → 2.8 ms a frame on the desktop (p99 6.5 ms), the unit and the 573 together. The unit's boot: 7.1 → 3.0 ms.
  - **Exact** (the unit's RAM, PC and cycles every 60 frames unchanged, `HK_MSU_HASH=file`), 10.6 → 6.7 ms:
    - the shim's `space()` was a search on every access;
    - `mips1`'s fetch built a `std::function` per instruction;
    - the TX3927 maps kseg0/1 inline instead of through the virtual `translate()`, and reads memory pages directly.
  - **Timers on demand** (6.9 → 5.0 ms): the fork's three TX3927 timers fired ~1.5 M emu_timer events a second. They are now computed when read or written, with an emu_timer only for an enabled interrupt's tick. The unit's timing moves slightly, so the drmn10m cases were re-recorded after checking the same flow: link, mode and music select, the stage, the hit chart.
  - **Idle skipping** (5.0 → 3.2 ms, with the bench's digest): three quarters of the unit's instructions were its RTOS's idle loop, `j .` with a `nop`, which only an interrupt leaves. It now ends the timeslice at once. `HK_MSU_NOIDLE=1` turns it off.
  - **What's left:** the unit's share is the RTOS's decoder-control task, which polls through the scheduler (semaphores, task switches) as fast as the CPU runs. That's why the real 132 MHz still costs ~7 ms: 5× the polls for the same result.
  - drmn10m's R3000 recording now covers attract and a game, and PGO is trained on the drmn10m cases too (`pgo.py` builds with at most 8 jobs: 16 at once went past its 12 GB cap).
- **Started (2026-10-13): e-AMUSEMENT, step 1: the network PCB unit and its hard disk.** drmn10m's e-AMUSEMENT is the pop'n 9 protocol (the same RC4 secret, envelope and modules, plus GFDM's `gamedata`, shoprank, landdata and session), which the 573 game speaks itself (`httpac.c`). The unit only gives it sockets and files.
  - **The unit** (GUC07: a TX3927, an Ethernet chip and a 10 GB IDE disk, cabled to a PC card in slot 2; a stub in MAME) is high-level: `runtime/sys573/npu.{h,cpp}`, after windyfairy's HLE (github.com/987123879113/mame, branch `npu_hle`, 45e5eaa1, BSD-3). The 573 calls it through a 16-bit FIFO: start-up 0x80–0x82, sockets 0x22–0x2e, settings 0x3a, files 0x40–0x54. It needs no ROM.
  - **The disk is a folder,** the save folder's `npu_hdd/` (and a RAM disk, `npu_ram/`, emptied at power-on), since the game only asks for files. FORMAT empties it and writes `.format`, the partition figures that devctl 2 reads back. Without `.format` it doesn't mount, like an unformatted disk.
  - **Fitted with `--npu`.** `--cd hdd` boots the unit's data installer CD (`d40jca02`, game.toml's `hdd_chd`, ingested as `hdd_cd`). The game finds the disk unformatted, formats it, and installs 78 songs' data (407 MB, ~69,000 frames) to INSTALLATION COMPLETED. The next boot reads it all (DATA 3/4). Regress: `drmn10m-hdd` (the install's first 9000 frames).
  - **Beyond windyfairy's unit, from the game's driver (drmn10m's addresses):**
    - The 573 and the unit each publish their place in the two 64 KiB rings (the game writes 0x34/0x36 and reads the unit's back). windyfairy's counted words received instead, which drifts. The unit now follows the game's positions.
    - Message 0x39 is the unit's link event: its first word, "link up", is kept at 0x80139834. The network module (0x80058de8) fetches the settings (0x3a) only after it. The unit now sends it after call 0x37 (link events on); windyfairy's only answered call 0x38, which this game never makes.
    - Message 1 is a ping. The game answers each with call 0 and counts them (it waits for the first at start-up).
  - **e-AMUSEMENT ON** (test mode > NETWORK OPTIONS > e-AMUSEMENT OPTIONS, hold Start + Left/Right): the game fetches the seven settings, opens a socket (it fails: no network yet, step 2) and says センターサーバーと通信できません. Service goes on to the game in standby.
  - **Open: e-AMUSEMENT OFF with the installed disk ends at "Please call attendant" (the multi-session unit not recognised).** The link starts at 180 s, against 139.5 s from the same save without the unit. The exchange is byte for byte the same until one of the 573's packets carries 0x30 where it had 0x3e (`ff 20 3e 12` → `ff 20 30 12`, then `32 48` → `32 30`). From there the session goes another way and the game reconnects in a loop. Not a race: the timing is the same within milliseconds, and the unit at 132 MHz fails the same way. Likely the game tells the unit something about the disk's songs that the unit (or its CD) doesn't answer as the real one does.
  - **Next:** that difference; then step 2, the sockets through the Python core's VirtualNet (`runtime/python/eanet`), moved to shared code.
- **Progress (2026-10-13): GuitarFreaks 11th Mix runs, and DrumMania 10th Mix and GuitarFreaks 11th Mix play a session together.** Beyond MAME (both sets NOT_WORKING, its unit a stub).
  - **GuitarFreaks 11th Mix (`gtfrk11m`)** is MAME's set: the digital board, a ZI cassette and a 32 MiB flash card in slot 1 (`pccard1`, new). The first boot installs the runtime CD (`d39jaa02`) to the onboard flash, then to the card, until INFO 508 (~26,400 frames). After NOTE 605 (Test), attract and its demo play. About 1.8 ms a frame.
    - The `gtrf` family: per guitar, R/G/B, pick, wailing and the effect knob (MAME's dial, an analog axis), and Start. Service moves to IN3 as in MAME's `gtrfrks` ports. The lamps are the LAMPS port's (spots, starts).
    - Its network unit's installer is `d39jba02` (`--cd hdd`, as drmn10m's).
    - DrumMania 10th Mix sessions with GuitarFreaks **11th** Mix, not 10th (the 10th Mix pairs with DrumMania 9th).
  - **Session play is two processes on a session cable** (`runtime/sys573/link.{h,cpp}`). Each game is its own executable, with its own recompiled R3000 code, and windyfairy's fork also joins two MAMEs (`msu_port2` to the GuitarFreaks' `rs232_network`, null modem).
    - **The DrumMania side hosts** (`--session-host SOCKET`). Its unit's second port (`duart_com_0:chan0`) is on the cable, wired as SIO1 is to the first.
    - **GuitarFreaks joins** (`--session-join SOCKET`). Its SIO1 is a UART there, as on the DrumMania side, and the unit's DTR doesn't reach it (SIO1's DSR is the cassette's).
    - **The cable carries line levels** (TXD, DTR, RTS each way) stamped with the sender's emulated time. TXD goes once a bit, as the sending UART's bit clock calls it: the fork's SIO1 takes one bit per call, not by time (with changes only, every byte arrived as 0x55).
    - **Lockstep:** every quantum (500 µs; `--session-quantum`) each side sends what changed and waits for the other, and a change lands one quantum later, a cable 500 µs long. So a session replays the same on any host: two runs were identical on both sides, every snapshot.
    - **Transport:** a Unix socket or TCP (host:port).
    - **Cost:** the pair runs at the slower side's pace, about 4.1 ms a frame (the DrumMania side, its unit included).
  - **What happens:**
    - GuitarFreaks' SESSION NETWORK CHECK says PORT 2, OK; DrumMania's, PORT 1.
    - Coins and Start on the DrumMania side open WELCOME and its joining period (SESSION PLAY SETTINGS: 15 s), and the GuitarFreaks cabinet enters (PLAYER 1 ENTRY) with coins and Start there.
    - Both then show CAUTION, the mode and music select, and the first stage of 花火 together, SESSION on the guitar's screen.
    - The music is the unit's first MP3 decoder (peak 0.93, the first time it plays). It plays on both cabinets: the host sends its unit's mix (its decoders at 0.65) with each step, and the joining side mixes it in. That side waits for it from the start, as the host waits for its decoders, so the music lands with its own sounds of the same moment. The two recordings correlate 0.96 at 0 ms lag through the stage.
  - **Regress:** `gtfrk11m-install`, `gtfrk11m-boot`, and the pair `drmn10m-session` + `gtfrk11m-session` (a case may name a partner; the two run at once; `bench` skips them). The session sound on the GuitarFreaks side changed its digest's sound column only.
  - **Which decoder goes to which cabinet isn't known.** Each port has a sub board with its own DAC (GXA25-PWB(B)), but the fork routes all four decoders to one output, and only decoder 0 played with both cabinets in. So both cabinets get the unit's whole mix.
  - **Not yet:**
    - Hands-on play.
    - Two machines over TCP, not tried.
    - Sessions with the network unit fitted (its open issue above).

**573-4. Efficiency** (sized by the measurements in 573-1)
- **Measured (2026-10-12, `HK_PROFILE=1`, ddr2m 150 s from the installed flash, 21,600 samples):**

  | Where | Share |
  |---|---|
  | R3000 interpreter (`psxcpu_device::execute_run`) | ~45 % |
  | Memory accesses through the shim's address map, mostly instruction fetch (`read_sized`) | ~30 % |
  | MAME's per-instruction breakpoint and debugger hooks | ~5 % |
  | GPU drawing | ~5 % |
  | Copying the frame out of the screen bitmap | ~3 % |
  | MDEC (the intro video) | ~2–3 % |
  | SPU, CD, DMA, timers | ~3 % |

  - **The CPU dominates (~80 %), so the MIPS recompiler below is the way to Firebeat-like gains.**
  - **Done (2026-10-13): profile-guided optimisation, 1.44× for free.**
    - `tools/pgo.py` builds the 573 code instrumented, trains it on the ingested games (ddr2m-play, ddr3mj-boot), and rebuilds with the profile. The profile stays out of the repo.
    - ddr2m from the flash: 21.8 → 15.1 s for 150 s of game time (993 % real time, ~1.5× MAME's 650 %). Frames: mean 2.41 → 1.67 ms, p99 4.0 → 3.4, worst 4.8 → 4.1.
    - Every regress case is bit-identical.
    - `-O3` alone is ~4 % and is now the default. `-march=x86-64-v3` added nothing and could let FMA contraction change floating-point results.
  - **Tried and dropped: the "cheap steps" below, measured A/B.**
    - A cached instruction-fetch page: 6 % *slower*.
    - A row-wise frame copy: no change.
    - The breakpoint checks are the emulated CPU's own debug registers (CP0 DCIC); they must stay, and they exit on their first test.
    - The sampler's shares inside the interpreter loop are skewed: the switch's dispatch cost lands on the fetch line.
  - **Cheaper first steps considered** (each tried above): a cached instruction-fetch page, skipping the breakpoint hooks, a row-wise frame copy.
  - **Started (2026-10-13): the R3000 recompiler** (`runtime/sys573/mips.h`, `mips_ops.h`, `tools/mipsgen.py`, `tools/mipsrec.py`).
    - **Recording:** a run with `HK_573_RECORD=build/<game>/mips573.txt` interprets and notes every block it starts (up to a branch and its delay slot, max 64 instructions) with its instruction words. Counts add up over runs. `tools/mipsrec.py` records a game's regress cases.
    - **Generation:** `tools/mipsgen.py` turns the recording into one C++ function per block, built into the game's binary (CMake, when the recording exists).
      - Registers live in locals.
      - MAME's pipeline (`m_delayr`/`m_delayv`: the load and branch delay slots) is worked out at generation time. The one thing left to run time is a load still pending when the block starts.
      - RAM, ROM and the scratchpad are accessed through the shim's page table; anything else through MAME's handlers, at MAME's `m_icount` for that instruction.
      - An instruction a block doesn't do (an exception, a bus error, `mtc0`, `rfe`, COP1/3) goes to MAME's interpreter (`execute_one()`), and the block ends.
      - A device that cuts the timeslice short during an access ends the block after that instruction, as MAME's loop does.
    - **Running:** MAME's `execute_run()` offers each instruction outside a delay slot to the dispatcher.
      - A block runs when its words are at that address, the timeslice has room for all of it, and the CPU is in kernel mode with caches not isolated and no hardware breakpoints. Then the next block chains straight on.
      - Code is checked once: main RAM's 4 KiB host pages holding checked code are write-protected (`mprotect`). Any write to them (a store, DMA, a device) faults once and moves an epoch on, so every block is checked again. A page that keeps faulting stays unprotected, and its blocks are checked on every run.
      - Code no recording saw is interpreted.
    - **Exactness:** all 573 regress cases are bit-identical with blocks: ddr3mj-boot, ddr2m-boot, ddr2m-play (main RAM hashed on all 21,000 frames too). Blocks run 100.0 % of the instructions there.
    - **Speed, ddr2m attract from the installed flash, 9,000 frames, desktop:**

      | Build | Mean frame |
      |---|---|
      | PGO interpreter (6ea1cac) | 1.8–1.9 ms |
      | blocks | 0.65 ms (2.8×; MAME's 650 % is ~2.6 ms) |

      - Without that run in the recording, blocks covered 64 % and the frame was 1.57 ms. **Coverage depends on what was recorded.**
      - In the 150 s: 325 M block runs (7.8 instructions each), 135 code pages protected, 46 written to.
    - **Second pass (2026-10-13), the next bottlenecks.** Same ddr2m attract run (mean frame), and its demo play, frames 5,400–9,000 (closest to a song), apart. All bit-identical: every 573 regress case, main RAM hashed on every ddr2m-play frame.

      | Step | Mean frame | Demo play |
      |---|---|---|
      | after the first pass, PGO profile refreshed | 0.65 ms | ~0.67 ms |
      | the board's frame copy: rows instead of `pix()` per pixel | 0.57 ms | |
      | shim: one handler covering an access, called directly (the flash's 35 M byte reads per 150 s, the I/O registers) | 0.51 ms | |
      | MDEC: the IDCT over the nonzero coefficients only | 0.48 ms | 0.63 ms |
      | GPU: the level tables computed; textured pixels at shade 0x80 skip the shade tables | 0.48 ms | 0.61 ms |
      | **loop regions:** blocks that loop back to a block compile into one function with gotos | **0.36 ms** | **0.49 ms** |

      - **Loop regions** were the big one: the game waits for vblank by polling a RAM counter in a two-block loop (0x80020e18 ↔ 0x80020e64), which ran 450 M times in the recordings. Block runs in 150 s: 277 M → 24 M.
        - A jump to a block in the same function is a goto. Before it, the function checks that the timeslice has room for the target's first stretch and that no checked code was written (`hk_psx::epoch`).
        - The function's code is checked as a whole.
      - **Blocks run on past conditional branches that aren't taken** (superblocks). At each further stretch they check the timeslice has room. Alone this was no faster; loop regions build on it.
      - **Tools:** `tools/mipsgen.py` runs on every build and does nothing unless the recording changed, so `tools/mipsrec.py` then `ninja` is enough. `HK_FRAMETIMES=file` writes every frame's host time; `HK_PROFILE_AFTER=ms` starts the sampler later.
      - **Measured and dropped:**
        - transparent texels without a branch: no change;
        - the screen's pens computed instead of looked up: 3 % slower;
        - compiling MAME's GPU with strict aliasing: not tried, since MAME itself builds with `-fno-strict-aliasing`.
      - **Where the time goes now (demo play):** MAME's GPU ~45 % (textured polygons 27 %, the screen conversion 6 %), the recompiled code ~12 %, the vblank wait loop ~5 %.
    - **Next:**
      - **the GPU on a worker thread.** Commands are parsed in step with the CPU (status, VRAM reads and DMA stay synchronous); only the drawing is deferred, and every VRAM read waits for it. The N100 has 4 cores;
      - exact fast-forward of the vblank wait loop: within a timeslice nothing but the CPU touches RAM;
      - record more of each game (songs, menus);
      - measure on N100-class hardware;
      - `ddr3mj` and the digital board's MP3 path.
- **Budget**, as for M11: under 25 % of an N100 core on average, and no frame over ~8 ms of host work.
- **If the CPU dominates, statically recompile the MIPS.**
  - The code runs from RAM, loaded from flash or CD, so the recompiler works from the blocks the interpreter has executed in reference runs, keyed by a hash of their bytes.
  - The interpreter runs anything unseen or rewritten; pages holding recompiled code are watched for writes.
  - It stays cycle-exact under MAME's one-cycle-per-instruction model. A block costs its length, and a block that would cross the end of a timeslice runs in the interpreter.
  - Firebeat's shadow mode carries over.
- **If the GPU dominates:** faster span routines, still bit-identical (`tools/regress.py`).
- **The SPU and MP3 run on a worker thread**, handing over per frame as the XT446 does.
- **Exit:** every 573 regress case bit-identical, with timings measured on N100-class hardware.

**573-5. The rest of the catalogue** (ongoing, family by family)
- **In scope:**
  - DDR 3rd Mix to EXTREME;
  - the DDR Solo games: their own FPGA map and outputs;
  - GuitarFreaks 3rd–11th and DrumMania 2nd–10th, with the GF+DM session link stubbed as in MAME;
  - Dance Maniax (hand sensors), Mambo a GoGo, Martial Beat, Dancing Stage.
- **Out of scope unless asked:** karaoke (`k573kara`) and the non-BEMANI games (fishing, Bishi Bashi, GunMania).
- **Later, only if wanted:** PCMCIA edit-data cards and the memory-card reader.

**Then M15, Twinkle** (roadmap: §21). It reuses 573-1 to 573-4 for the PS1 core. It adds:
- the 68000 + RF5C400 sound board, already in HIRAKU;
- a SCSI CD and an ATA hard disk;
- the background video MAME lacks.

### Progress (2026-10-12): 573-0 and the start of 573-1

**The oracle:**
- MAME built at HIRAKU's pin, the 573 driver only: `/run/media/roganis/DATA/mame-pin/k573`, from `build573.sh` (memory-capped, `OSD=sdl3`).
- `oracle/mame.sh` takes `MAME_BIN=...` and caps every MAME run's memory (`MAME_MEM`, default 4G).
- `oracle/lua/hiraku.lua` knows the 573:
  - tap groups for its I/O, in all three PS1 address mirrors;
  - the PC under the PS1's register name;
  - the M48T58's power-on time in `log.txt` ("power-on time N"). HIRAKU starts at the same time with `--rtc N`.
- `oracle/lua/itrace573.lua` traces MAME's CPU with total cycles (`EXACT_PC=1`). `HK_573_ITRACE=file,from,to` writes HIRAKU's trace in the same terms.

**The core (`runtime/sys573/`, `core = "sys573"`):**
- A port of `ksys573.cpp`'s driver class for `ddr3m`: the memory map, flash bank, ports, ATAPI DMA, H8 response, JVS, the DDR stage multiplexor.
- MAME's own devices on the framework shim: the PS1 (CPU, GTE, DMA, IRQ, timers, SIO, MDEC, GPU, SPU), 29F016A flash, M48T58, ADC0834, the ATAPI stack with the CR-589, the YI cassette (X76F100 + DS2401), the PC Card slots with the 32 MiB flash card, the digital I/O board (FPGA, MAS3507D, minimp3), the JVS memory card reader with its PS1 controller ports.
- `dumps/ddr3mj/machine.txt` is written by hand for now; the CD comes from the CHD (`chdman extractcd`).

**The shim grew (third_party/mame/README.md):**
- an exact mode, a copy of MAME's `device_scheduler`:
  - the timer list in MAME's order;
  - the quantum, and MAME's cycle division (`divshift`);
  - aborted timeslices;
  - input lines through synchronize() timers;
  - local time through the device clock;
- MAME's screen timing, sound streams with their 50 Hz update, NVRAM files under MAME's names, slots, device interfaces, runtime map installs, a CD image class over `.cue`/`.iso`.
- The XT446 keeps the simple scheduler; every regress case is bit-identical.

**What lockstep took:**
- **Card detect** starts at 1 on both PC Card slots (active low), as in MAME's constructor.
- **The JVS memory card reader** is part of `ddr3m`: the game polls it at frame 187.
- **`nvram_default()` at start:** the cassette EEPROM is loaded from its region, and the flash is erased.
- **The M48T58** shows the time only after its first 1 s tick; the oracle reads it after that.

### Decisions (owner, 2026-10-12)
1. **The first set is `ddr2m`** (DDR 2ndMIX). MAME runs it, so 573-0 to 573-2 are checked against MAME.
2. **Where MAME doesn't support a game yet, go as far as the documentation allows, where that makes sense.**
   - Sources: psx-spx, the 573 hardware notes, the chips' datasheets (the MAS3507D's, the cassette chips').
   - Each departure from MAME is a switch, with MAME's behaviour kept for the oracle.
   - Write down which source each departure rests on. Where the documentation runs out, stop and record the gap.

## 20. Modding (discussed 2026-10-12; pilot: pop'n music 8)

Mods are layers over the MAME-exact game: off by default, with the regression cases run without
them. A mod set gets its own save, so the clean one is never touched.

The order of work:
1. **Unlocks and cheats.**
   - Started with pop'n 8's unlocks; cheats come later.
   - Cheats will need a writable memory path, `cheat.xml` import, and input scripts attached to a mod.
2. **Code patches.**
   - Byte patches to PowerPC code, with the patched functions run by the interpreter (it fetches
     from RAM; native code never sees the patch).
   - Native hooks on guest functions, through a per-function override check whose cost is
     measured first.
3. **Asset overlays** at the ATA/ATAPI sector level, with file maps from the discs' ISO9660;
   the hard-disk layouts are per game.
4. **Pilots on one game:** new songs; notes drawn at the display's rate from the audio clock,
   the logic staying at 60 Hz.

### Pilot: pop'n music 8 unlocks (done)

- **What the phase is:** the event phase is one byte of the RTC NVRAM, kept three times
  (`rtc.nv` 0xbf, 0x57b, 0xa37), with no checksum.
- **How it was found:** the four codes from remywiki were typed in EVENT OPTIONS > PASSWORD
  by `oracle/scripts/popn8-phases.his`, each from the save the previous one left, and the saves
  were compared. Each code changed only those bytes, 0 → 4.
- **Checked in play:** at MAX, the music select gets a "secret" category with the title-screen
  songs.
- **The tool:** `games/<id>/unlocks.toml` describes a game's unlocks, and `tools/unlock.py`
  reads and writes them in the save (with a backup). The launcher shows them under right-click >
  Unlocks.
- **Checked:** setting MAX with the tool gives a save byte-identical to the game's own.

### Sugoroku de 8, the event itself (step 1: the clock, 2026-10-12)

- **The event runs in HIRAKU.** Phase 1 turns it on, and attract mode shows the
  すごろく DE 8 絶賛実施中! board, with KRAFT on Tokyo, Shizuoka next, and a hidden-song rate of 0%.
- **The game never gives the event the date.** pop'n 8 reads the clock chip's time only through
  `0x8009ccdc`/`0x8009cd34`, and the callers of the readers above them are, exhaustively:
  - the test-mode clock setting (`0x8007f158`) and operator screens (`0x80083310`/`0x80083728`);
  - the boot check, with its battery and update-in-progress tests (`0x80071b3c`, `0x800715a4`);
  - the coin bookkeeping (`0x80079ec0`: a weekday via `0x8008a358`, and a 365-day history
    shifted at boot by `0x8007a014`);
  - error-log timestamps (`0x8006d1dc`, `0x800713c4`).

  The bookkeeping block (`*0x80100e64`, 0x10b4 bytes, saved to flash) is reached only through
  its accessors at `0x80079a70`–`0x8007a54c`. Outside bookkeeping and test mode they are called
  only for coins, play time and play counts, and none returns a date. No function pointer
  leads to any of them.
- **Booting phase 1 on five dates** (10 July, 26 July, 5 August, 30 September 2002, and
  12 October 2026) gives an identical attract mode, and none of the five boots writes the save.
- **Consequence:** HIRAKU's fixed clock doesn't hold the event back, and no event calendar is
  needed. RemyWiki's "each week" can't be a date the game checks. The rule will show in the
  event's own code and state.
- **`HK_RTC="YYYY-MM-DD[ HH:MM:SS]"`** sets the Firebeat clock for experiments; the default
  stays 2002-07-26 12:00.

### Sugoroku de 8, step 2: a round played to the board (done, 2026-10-12)

- **A round reaches the board.** Before each round, `FUN_80024c84` draws the requirement into
  `0x8052d7b0`: one of 13 for the mode, from the table at `0x80024ea0`. A round that meets it
  ends on the board: the roulette, then KRAFT moving (`oracle/scripts/popn8-sugoroku.his`).
- **The autoplay that met the requirement:**
  - Stage 1 is played from the chart in RAM: 8-byte records `{u32 time, type, 0x45, lane,
    sample}`, with type 1 a note in lane 0–8.
  - The times are in milliseconds, so a note is due at song frame t × 58.4866 / 1000: pop'n runs
    the GCU at its 16.6666 MHz pixel clock, 58.4866 frames/s (measured, steady after boot, the
    same in pop'n 4, 5, 7 and Animelo 2), not 60 or 59.94. 58.5 was used and is close enough
    (under 2 frames in two minutes); 60 drifted 2.6 %. The song's frame count is at `0x8021825c`.
  - Stage results are at `0x80218310`: combo, -, GREAT, GOOD, BAD.
  - The result was 176 GREAT, 2 GOOD, 0 BAD.
- **The event block** follows the phase in the RTC NVRAM, kept three times (+0, +0x4bc, +0x978).
  Each field is big-endian u32:

  | Offset | Field |
  |---|---|
  | `0xbc` | phase |
  | `0xc0` | stage, 0–2 |
  | `0xc4` | unlocked mask |
  | `0xc8` | missed mask |
  | `0xcc` | position, 0–1003 |
  | `0xd0` | lap |

  - `FUN_800bab40` loads the block (`FUN_8006fe8c`), moves KRAFT one space at a time, and saves
    it (`FUN_8006fee4`).
  - Two credits moved the position 0 → 1 → 6, and it survives a restart: the attract board's
    progress bar moved.
- **The rules** are in `FUN_800b8e64`:
  - **The board** is 1004 spaces (`0x800bcad0`, 12 bytes each: coordinates, flags, city). The
    city records (`0x800bb5d0`, 0x34 bytes) hold each stage's unlock bit.
  - **First time around a stage**, a city needs an exact landing; a city passed goes in the
    missed mask and is given on the next pass.
  - **Stage goals:** `0x3ffff` (18 cities), `0x7f8` (8 cities), `0x1fffff` (18 cities plus the
    three island bits). Completing the last gives `0xbb001e`, the HELL the 8 course.
  - **RemyWiki's "week"** is this stage, not a date.
- **The event runs at phases 1 and 2**; phase 3 replaces the board in attract mode.
- **In HIRAKU:** `games/popn8/unlocks.toml` `[[progress]]` describes the block, the board's
  cities, and the stage goals. `tools/unlock.py` decodes it, and the Unlocks dialog shows the
  stage, unlocks, KRAFT's space and lap, the next city, and the cities missed.
- **Not yet seen:** a city actually unlocked. 静岡 is 21 spaces on, an exact landing.

### Pilot 2: pop'n music 7 (done, 2026-10-12)

- **The operator codes** (RemyWiki) add switches to EVENT OPTIONS: BINGO de 7, SECRET DATA
  RELEASE, BINGO de 7 DATA RELEASE, EXPERT MODE DATA RELEASE. Each is NO until the operator
  sets it (hold red, press blue) and saves.
- **In the save:** one 32-bit word, kept three times (`rtc.nv` 0x84/0x88/0x8c).
  - The high byte holds two bits per switch: code entered, and on.
  - The low 24 bits are the bingo squares.
  - A 16-bit byte sum (0x5c-0xec0, at 0x56) covers it.
  - The boot check (`FUN_8005b940`) repairs the three copies by majority, and drops a switch
    whose code is missing.
- **The tool:** `tools/unlock.py` gained checksums; its MAX save is byte-identical to the
  game's own (`oracle/scripts/popn7-phases.his`).
- **BINGO de 7!!:**
  - **Opening squares:** after each stage, `FUN_800c4824` tests every closed square's condition
    (`FUN_800c2008`; the conditions' names are at `0x800c1134`, 16 bytes per bingo number) and
    opens those met.
  - **The card:** 24 numbers loaded from the DVD, plus the free centre. Squares map to bits
    0-23 column by column (the table at `0x800c18a0`). Lines A-L: the diagonals A and L, the
    columns B-F, the rows G-K. The numbers are shown doubled on screen.
  - **The hint** (`0x80352ae0`) is a random closed square each credit, not saved.
- **Verified:**
  - An autoplayed stage, ANTHEM / GLORIA at 154 GREAT (same chart format and timing as
    pop'n 8), opened square 26, `SCORE 100000 DEFSPEED` (`oracle/scripts/popn7-bingo.his`).
  - The event never reads the date: the clock readers are bookkeeping, error log, test and
    boot, and the event screen is identical on four dates, 2002 to 2026.
- **In the Unlocks dialog:** the level, the open squares, the lines done or one square away,
  and every closed square's condition.

### pop'n music 6 (done, 2026-10-12)

- **The codes:** `38281838` on test mode's main menu adds OPERATOR COMMAND OPTIONS. There, the
  last 12 presses of buttons 1-4 and 6-9 are compared with five codes in the program
  (`FUN_80071810`, table at 0x800721f8: 12 button masks per code, newest first). Each match
  adds an item, NO until switched on and saved on EXIT.
- **In the save:** one byte per flag in the RTC block (0x5c on).
  - `0x8c` is the menu.
  - Code bytes: `0x8d` SECRET MUSIC, `0x8e` ALL DATA, `0x8f` STEP UP, `0x90` SECRET COURSE,
    `0x91` STEP UP 2.
  - Switch bytes: `0x92`-`0x95`, in that order, without STEP UP 2.
  - The 16-bit byte sum covers 0x5c-0xec8, at 0x56.
- **The tool:** `games/popn6/unlocks.toml` kind = "switches"; `tools/unlock.py` and the
  Unlocks dialog (checkboxes) set them. Its everything-on save is byte-identical to the game's
  (`oracle/scripts/popn6-operator.his`).
- **Checked in play:** with SECRET MUSIC on, the music select has the SECRET songs.
- **Not checked:** the other switches' effects, which are RemyWiki's.

### beatmania III THE FINAL, Keyboardmania 3rd and 2nd (done, 2026-10-12)

Each unlock is one byte of the RTC NVRAM, kept three times, with no checksum. Each was done in
HIRAKU, the saves were compared, and `tools/unlock.py`'s save is byte-identical to the game's.

| Game | Offsets | How | Script |
|---|---|---|---|
| bm3final | `0x52f`/`0x9ff`/`0xecf` | COLOR CHECK code, 2P START held, keys 1-5 1P and 6-10 2P; written when test mode is left. Test mode is entered by holding Test through the boot check. | `bm3final-unlock.his` |
| kbm3rd | `0xf8`/`0x599`/`0xa3a` | 2P START + 1P F#2/G#2/A#2 pressed during the startup test | `kbm3rd-unlock.his` |
| kbm2nd | `0x95`/`0x4ab`/`0x8c1` | 2P side, wheel held down on SELECT PLAY MODE | `kbm2nd-pressure.his` |

- **kbm3rd:** the keyboards report changes, so the keys must go down during the test.
- **kbm2nd** (`FUN_80013600`): PRESSURE appears after more than 49 clicks past FREE, 3 once
  unlocked. A click is 48 of the wheel's offset from centre / 32 per frame: 16 frames at full
  deflection, so 50 clicks only just fit in the 15 s timer. The flag is saved when the selection
  ends.

### Code macros: per-credit and power-on codes (2026-10-12)

Codes that last a credit or until power-off can't be saved, so HIRAKU types them:
`games/<id>/macros.txt`, run by `runtime/host/macro.{h,cpp}` from the frame hook (before the
input script), with inputs through `core().set_input` like the scripts'.
- **Triggers:** Ctrl+1..9 in the window; `--macro NAME[@FRAME]` (several queue: one due while
  another types starts after it). The launcher's Unlocks dialog lists them, and a macro with an
  `at` frame gets an "every start" checkbox (`macros/<id>` in the settings → `--macro`).
- **Steps:** `keys` (digits), `press`, `hold`/`release`, `set` (analog), `wait`, `tap N gap M`.
- **Timing:** taps of 8 frames, 16 apart, by default. With 4/12, pop'n 4 lost a press (it never
  reached the ring) in the sixth code of a row; with 4/8, pop'n 5 lost one.
- **pop'n 5** (`FUN_8005b408`, called from the title screen `FUN_800385a0`):
  - 16 presses in a ring (`0x80264dc8`), compared newest first against the table at
    `0x8005b5e0` (0x28 bytes each: 16 u16 masks, length, name); 60 idle frames clear it.
  - A match sets `0x802649c8 + 4·index` until the title is set up again (`FUN_8005b2f0`),
    and `FUN_8003dbe0` makes the songs available: song 0 for LESSON, 0x3d for [SP]ENDING,
    5/0x1a for the remixes, 0x1d/0x1c for the characters.
  - Eight codes, one more than RemyWiki lists: `2179223718` [SP]ENDING.
  - All eight were typed back to back by the macros and set their flags; LESSON and
    スペシャルエンディング were seen in the music select.
  - With credits in, the title still cycles through the attract logos (about 1,600 frames after
    the coins); the new title clears the flags. All eight at 8/16 don't fit in one pass.
- **pop'n 4** (`FUN_80052e58`, called from `FUN_80034ae0`): the same checker, with a 10-press
  ring (`0x80268410`) and 0x1c-byte entries at `0x80053030` (10 masks, length, name); flags at
  `0x80268010`. Eight codes: BRITPOP `33997733`, HORROR `333111333`, GROOVEROCK `6861379`,
  [RX]POPS `44322366`, [RX]J-TEKNO `864291`, [RX]DANCE `68686824`, [RX]BNSTRK `444322211`,
  [SP]ENDING `2179223718`. All eight set their flags typed back to back (frames 5100–6142).
- **Animelo 2** (`FUN_800369a0`): the last 16 presses (`0x80298858`, newest first) against three
  tables at `0x80037a00`; the attract screens show the codes. DOUBLE EXCITE `19283746` sets
  `0x80102374`, TRIPLE `1919282837374646` `0x80102370` (both checked); `FUN_8002862c` reads them
  in modes 2 and 6. NON STOP `1496149614961496` calls a region check (`FUN_8002ebd8`) and drops
  the result: in JAC nothing sets its flag `0x80102384`.

### Display pacing: smooth frames on fixed-refresh displays (2026-10-12)

The games run at 59.94 (beatmania III), 58.4866 (pop'n) and 58.51 Hz (Keyboardmania). Shown as
they come, a fixed-refresh display judders. `runtime/host/pacing.{h,cpp}` runs the machine on the
display's refresh grid when the display is within 3 % of a whole multiple (the game 2.6 % fast at
60/120/240 Hz, the sound resampled with `SDL_SetAudioStreamFrequencyRatio`, steered to keep the
queue at its target). The emulated machine is unchanged.
- **Per frame:** burst (the machine), draw, wait until the commit time (a lead before a grid
  vblank), present, wait until the next burst (a budget, p99 of burst + draw + 1 ms, before the
  next commit). The audio queue is topped up at once when it's under half its target.
- **Wayland presentation-time** gives the vblank times, the refresh period and each frame's
  commit-to-screen time. KWin 6.7 at 240 Hz: about 8 ms at best, and the deadline wanders by up
  to a refresh with its render-time estimate. Frames committed near it came out irregular, and
  0.5-1 % were irregular at any phase.
- **fifo-v1** (each frame's commit plus k-1 empty commits, each waiting for the previous barrier)
  stops early display. Irregular frames, 2.5 min runs: 144 Hz at 2 refreshes per frame 838 without
  fifo, 3 with (0.03 %); 240 Hz at 4, about 2,500 (the first designs) against 15 (0.17 %).
- **Finding the deadline:** the commit moves later, a 32nd of a refresh every 20 frames, until the
  median of 10 commit-to-screen times is a refresh slower than the best (a miss: with fifo the
  queue moved). Then the grid catches up a refresh and the commit goes a 32nd plus 1.5 ms earlier;
  later misses add 0.75 ms. Learning a fixed "pipeline" offset, or reacting to single irregular
  frames, failed (both chase the compositor's own jank).
- **Without Wayland** (X11): the grid runs on the display mode's rate with an unknown phase.
- Tested with the window on DP-1 (240 Hz) and DP-2 (144 Hz, with `HK_DISPLAY_SYNC_SKEW` to force a
  fit); 60 Hz still to test on a real 60 Hz display (the N100 cabinet).

## 21. Twinkle roadmap (M15, written 2026-10-13)

The approach is the 573's (§19): MAME's devices on the shim, an interpreted CPU, cycle-exact lockstep with MAME first. Then the one thing MAME doesn't do at all, the background video, as a documented departure. Then the N100 budget.

### Media on hand

| Set | Style | MAME config | Program CD | Video CD | HDD | Dongle | MAME status |
|---|---|---|---|---|---|---|---|
| `bmiidx3` | 3rd style (GC992 JAC) | `twinklei` | `gc992-jac01` | `gc992-jaa04` | `992hdda01` | `992a02`, M24C02 EEPROM (plain) | **works** (`IMPERFECT_SOUND`); first boot stops at E150, 1P Start clears it (§16) |
| `bmiidx` | 1st style (863 JAB), downloaded 2026-10-13 | `twinklex` | `gq863-jab01` | `gq863a04` (4 tracks) | `863hdda01` (3.2 GB) | `863a02`, X76F041, `BAD_DUMP` (write and config passwords unrecoverable; the read side is complete) | **`NOT_WORKING`**: the DVD player check needs RTS→CTS on the PS1's SIO1, which MAME's SIO lacks |

- Both carry the BIOS `863a03.7b` (512 KiB) and the SPU program `863a05.2x` (512 KiB).
- `bmiidx3` is in the rompath and verified (§16). `bmiidx` is only in `~/Downloads` so far: hard-link it into `~/bm3mame/roms-cores/bmiidx/` and run `-verifyroms` first. Its three CHDs have non-zero data SHA-1s.
- **Disk:** 63 GB free on DATA (2026-10-13). The raw HDD images are ~1.5 and ~3.2 GB before `tools/chd.py` punches holes.

### What MAME has

Sources: MAME master `twinkle.cpp` (1,700 lines, 18 sets plus the BIOS), `bus/rs232/xvd701.cpp`, and the devices it uses. All BSD-3.

**The main board (TWINKLE/MAIN):**
- The same PS1 chipset as the 573: CXD8530CQ at 33.8688 MHz with 4 MiB of RAM, CXD8561Q GPU with 2 MiB of VRAM, the SPU with 512 KiB.
- **SCSI, not ATAPI,** for the program CD: an NCR 53CF96 with a Toshiba XM-5401 drive, on the PS1's DMA channel 5.
- An RTC65271 (as on Firebeat), an LTC1232 watchdog (1.2 s), and an SMC FDC37C665GT super I/O whose second 16550 talks to the DVD player from Substream on.
- The security dongle, bit-banged through 0x1f270000/0x1f280000: an X76F041 (1st style, Substream, Club Version 2) or an M24C02 (2nd style and later).
- The I/O mux at 0x1f220000 (an index register, then data), the key LEDs, and the Bt812 video decoder's registers at 0x1f2b0000.

**The SPU board (TWINKLE/SPU) is Firebeat's SPU board.** MAME's two memory maps are the same address for address:
- a 68000 at 16 MHz running `863a05`, RF5C400, 24 MiB of wave RAM in three 8 MiB banks, a CY7C131 mailbox to the main CPU;
- an ATA hard disk whose DMA fills the wave RAM.
- The differences are small: IRQ acknowledges through a control register (bits 7–10), IRQ1 at 60 Hz, and the DMA loop.

**The sub board (TWINKLE/SUB2)** decodes the external player's video (Bt812), buffers it in field memories and overlays the PS1 picture on it. **MAME ignores it:** the register writes go nowhere and nothing is mixed.

**The DVD player:** a JVC XV-D701 on a serial line, modelled by MAME's `xvd701` as far as the game can see (JLIP protocol at 9600 baud, 8O1: power, play, pause, stop, seek chapter, next/previous, status, ID; replies 100 ms later). It produces no picture. 1st style uses the PS1's SIO1 over the mini-DIN; Substream and later use the super I/O's UART on the DSUB.

**Controls and lamps:** 7 keys and a turntable per player (the turntable is an 8-bit position counter), Start ×2, VEFX, Effect, Test, Service, Coin, 5 effector sliders (4 bits each). Lamps: 1P/2P Start, VEFX, Effect, credit, 8 spotlights, neon, 14 key LEDs, a 9-character 16-segment ticker, board LEDs.

**Speed:** `bmiidx3` at 567 % in MAME (§16), about 18 % of a desktop core.

### What HIRAKU already has, and what is new

**Reused:**
- the 573 core's PS1 on the shim, its exact scheduler, the recompiler (573-4) and its tools;
- MAME's `x76f041`, `ins8250` (with the TX FIFO fix), `diserial`, `sio_uart`, `cdda`, `ataintf`/`atadev` on the shim;
- the Firebeat SPU's board logic (`runtime/spu`) and the RF5C400 port;
- the oracle (`oracle/mame.sh`, `hiraku.lua`, snapcmp, audiosync), regress and PGO;
- the analog turntable input and the lamps path (docs/LIGHTS.md).

**To port:**

| Part | Lines | Note |
|---|---|---|
| `twinkle.cpp` driver class | ~1,700 | memory map, I/O mux, SPU board glue, SCSI DMA |
| SCSI: `ncr53c90`, `nscsi_bus`, `nscsi_hle`, `nscsi/cd` | ~4,300 | new bus type for the shim |
| `fdc37c665gt` | ~530 | needs the 16550s; the floppy controller and parallel port can be stubs if the games never touch them (check with a tap) |
| `i2cmem` (M24C02) | ~620 | 2nd style on |
| `xvd701` + `rs232` port | ~500 | |
| `rtc65271` | ~720 | HIRAKU's Firebeat copy runs on its own clock; the shim needs MAME's |
| `watchdog`, `cy7c131` | ~300 | |
| **the 68000** | one generated file (~4 MB) | see the decision below |

**Decision for the owner: which 68000.**
- MAME's SPU 68000 is its new cycle-exact core (`m68000.cpp`, generated by `m68000gen.py`), not Musashi. Firebeat's SPU runs Musashi and only had to match MAME's sound, not its cycles.
- On Twinkle, the 68000 and the PS1 share the mailbox, so the 68000's timing is visible to the PS1. Lockstep needs MAME's core.
- **Recommendation:** port MAME's generated 68000 (one variant) onto the shim. It costs one big compile unit (like a `gen573` block file: build it at `-j2`). Firebeat could switch to it later if it ever wants cycle lockstep.
- **Decided (owner, 2026-10-13): MAME's 68000.**

### Phases

Each phase has an exit test. Sizes are assisted-effort guesses; the 573 went faster than its estimates.

**TW-0. Oracle and media** (a day)
- `tools/ingest.py` for Twinkle sets: BIOS, SPU ROM, dongle, the program CD and Video CD via `chdman extractcd`, the HDD via `extracthd` (memory-capped, holes punched), and a `game.toml` with `core = "twinkle"`, the dongle type and the DVD port (SIO1 or super I/O).
- A MAME build at the pin with `twinkle.cpp` (as `build573.sh`).
- `hiraku.lua`: Twinkle tap groups (I/O mux, SCSI, mailbox, dongle, mixer), the 68000 as a second tapped CPU, the RTC's power-on time.
- `bmiidx3-boot.his`: first boot, E150, 1P Start, attract. Seeded NVRAM afterwards for the later runs.
- **Exit:** two MAME runs of `bmiidx3-boot` match, and the reference run is in `out/oracle/bmiidx3/`.

**TW-1. The board, interpreted, in lockstep** (1–2 weeks)
- `runtime/twinkle/`. First split the PS1 machine out of `runtime/sys573/` into something both cores link, so the recompiler, GPU and scheduler work stay shared.
- Bring-up order: the BIOS to its SCSI boot, the program CD loading, the SPU board's boot (68000, HDD to wave RAM, mailbox), the device check, E150, attract.
- The DVD player is MAME's `xvd701`, unchanged.
- **Exit:** `bmiidx3` boot and attract in lockstep with MAME: main RAM identical every frame, the 68000's RAM and the wave RAM at every snapshot, pixel-identical snapshots, the WAV matching (lag 0, correlation ≥ 0.998). Regress cases `bmiidx3-boot`, `bmiidx3-attract`. Peak RSS (VmHWM) checked before adding them, as for the 573.

**TW-2. Playable** (about a week)
- An `iidx` family in `cabinet.cpp`: keys, turntables (analog axis, as bm3 and DJ Main), Start, VEFX, Effect, the effector sliders.
- Lamps: spotlights, neon, key LEDs, Start/VEFX/Effect, and the 16-segment ticker as text (HID, socket, and an on-screen strip).
- `bmiidx3-play.his`: coins, mode and song select, a song with keys and scratches, the result. In lockstep.
- Launcher entry, PLAY.md.
- **Exit:** the scripted song in cycle-exact lockstep with MAME, then the owner's hands-on test with an IIDX controller.

**TW-3. The background video: further than MAME** (2–3 weeks; the main unknown)

The game already tells the player what to play. HIRAKU adds the picture. The machine doesn't change: the player's serial replies stay MAME's, so lockstep holds. `--video off` gives MAME's picture.

1. **The disc:** parse the Video CD (`ENTRIES.VCD`, the MPEG tracks as Mode 2 Form 2 sectors) to map a chapter to a sector. Check that the shim's CD image keeps raw Mode 2 sectors.
2. **Decode:** MPEG-1 with pl_mpeg (MIT, single header, MPEG-1 only, which is exactly a Video CD), on a worker thread. 352×240 at 29.97 fps costs little.
3. **Timing:** playback starts at the machine time of the PLAY reply, so it is reproducible and can be checked. Measure the real player's seek and start delays only if footage shows they matter.
4. **The mix, the open question.** The sub board keys the PS1 picture over the video. Nobody has documented how. Find out from:
   - the game's writes to the Bt812 and the mixer (0x1f2b0000; MAME's notes: reg 0x08 bit 1 "perform overlay mixing"; HCLOCK 853, ACTIVE_PIXELS 706);
   - the PS1 frame during a song: what fills the video window (a key colour, black, the mask bit?);
   - real-cabinet footage of 3rd style, for placement, scaling and colour.

   Write down which source each choice rests on (decision 2 of §19). Where the sources run out, stop and record the gap.
5. **Exit:** 3rd style's attract and a song show the right clip, in the right place, at the right time, against real-cabinet footage. The audio and the machine stay MAME-exact.

**TW-4. Efficiency** (a week)
- Record `bmiidx3` runs for the MIPS recompiler (`tools/mipsrec.py`). Its blocks come from code the program CD loads into RAM, as on the 573.
- Measure the parts with `HK_PROFILE`: the PS1 GPU (already ~45 % of 573 gameplay; IIDX draws more notes than DDR), the RF5C400, the 68000, SCSI.
- The video decoder and the mix run off the emulation thread.
- PGO profile `twinkle.profdata`.
- **Exit:** the N100 budget (§14): average under 25 % of one core, no frame over ~8 ms of host work, bit-identical to the interpreter.

**TW-5. The rest of the catalogue** (weeks, mostly media and per-set config)
- **1st style (`bmiidx`, on hand), a departure:** add CTS to the PS1's SIO1 (psx-spx documents SIO1's CTS bit), behind a switch, for the DVD check MAME fails. The X76F041 dump lacks only the passwords the game never uses.
- **Substream, Club Version 2 (`twinklex2`):** X76F041, player on the super I/O UART.
- **2nd, 4th, 5th style:** `twinklei`, Video CD, like 3rd.
- **6th, 7th, 8th style: DVD.** An MPEG-2 decoder (libmpeg2, GPL-2+, or FFmpeg's libavcodec, LGPL) and DVD-Video chapter navigation (the IFO tables). 8th style may use a different player (XV-S1100; MAME's code only mentions it).
- **Club kits (`bmiidxc`, `bmiidxca`):** two program CDs; find out what the second one does.
- *Ugoke! Purimucho* (touch panel, camera, printer) is out of scope.
- **Media needed:** every set except `bmiidx3` and `bmiidx`. Recommended next: `bmiidx2` or `bmiidx4` (Video CD, MAME working), then `bmiidx6` for the DVD path.

### Progress (2026-10-13): TW-0

**The oracle:**
- MAME built at HIRAKU's pin, the Twinkle driver only: `/run/media/roganis/DATA/mame-pin/twinkle`, from `buildtw.sh` (memory-capped, `-j3`). Both sets pass `-verifyroms` (`bmiidx` is "best available": MAME marks its dongle as a bad dump).
- `oracle/mame.sh` takes snapshots at the screen's own size for Twinkle (`-snapview native`): the set's layout draws the 16-segment display around the screen. Twinkle's screen is 640×240.
- `oracle/lua/hiraku.lua` knows Twinkle (it tells it from the 573 by its SCSI controller):
  - tap groups for the main board (mailbox, SCSI, super I/O, I/O, RTC, mixer, and the PS1's own) in all three address mirrors;
  - tap groups on the SPU board's 68000 (control, mailbox, ATA, RF5C400), recorded with the 68000's PC;
  - the RTC65271's power-on time ("power-on time N" in `log.txt`), read from MAME's save items: the registers sit behind an index register that a bus read would change.

**The media:** `tools/ingest.py` takes Twinkle sets (`ingest_twinkle`); `games/bmiidx3` and `games/bmiidx` have `core = "twinkle"`, which the launcher hides until the core exists. Both are in `dumps/` (3.9 and 1.7 GB, with holes punched). **The Video CDs have one track per clip:** 38 for 3rd style, 28 for 1st style.

**The reference run** (`oracle/scripts/bmiidx3-boot.his`, 9,000 frames, out/oracle/bmiidx3/boot):
- First power-on: BIOS, SCSI boot, device check by frame ~1,380 with E150 (backup RAM), "1P START = BACKUP CLEAR"; 1P Start at frame 2,400; "Clear completed", warning, Konami, BEMANI, the title, the How To Play demo.
- **Two runs started in the same second match exactly:** the RAM hash on all 9,000 frames, all 2,151,643 device accesses, the 150 snapshots, and the NVRAM. Runs started in different seconds differ by the RTC; HIRAKU will take `--rtc` as the 573 does.
- The cleared NVRAM (`rtc`, `security`) is kept in `out/oracle/bmiidx3/seed/` for later runs (`HIRAKU_NVRAM`).
- **MAME's speed at the pin:** 480–489 % of real time (`-bench 90` from the cleared NVRAM: boot, device check, attract with the demo). The 567 % of §16 was MAME 0.289 idling at E150.

**1st style** (`oracle/scripts/bmiidx-boot.his`, 3,600 frames, no inputs): MAME stops at the device check with **E600 DVD INIT ERROR**: DVD and backup RAM BAD, SCSI, CD-ROM, security and sound OK. The missing CTS, as the driver says. This is TW-5's starting point.

**Found on the way, for TW-1 and TW-3:**
- The super I/O is used only for its second UART, at the PC's COM2 address (0x2f8): no floppy controller, parallel port or configuration-mode access during boot and attract. Those parts can be stubs.
- The mixer gets ~200 writes during boot (Bt812 index/data at 0x00/0x08, a bit-banged serial line at 0x20).
- The How To Play demo draws the middle of the play field dark: where the video goes.

### Progress (2026-10-13): TW-1, the board in lockstep with MAME

**`runtime/twinkle/`**, `core = "twinkle"`, `hiraku-bmiidx3`: a port of `twinkle.cpp`'s driver class (memory maps, the I/O mux, the SPU board's control register and hard-disk DMA into wave RAM, SCSI DMA on the PS1's channel 5, the dongle's port bits, the DVD player on SIO1 or the super I/O's UART). Every device is MAME's own code on the shim, run by the exact scheduler:
- the PS1 as on the 573 (shared library `hiraku_mame573`);
- **MAME's 68000** (the generated cycle-exact core, as decided), the 53CF96 and the XM-5401 on MAME's SCSI bus, the FDC37C665GT, the M24C02 or X76F041 dongle, the RTC65271, the LTC1232 watchdog, the CY7C131 mailbox, the RF5C400, the RS-232 port with MAME's XV-D701, MAME's ATA hard disk on the raw image (`hdd_chs` from the CHD) — library `hiraku_mametw`;
- the shim's additions for them are listed in third_party/mame/README.md (interruptible accesses and wait states, masked submaps, device ROM regions, shares, the periodic interrupt...).

**Result:** `bmiidx3-boot` (first power-on, device check, E150, 1P Start, attract, the How To Play demo; 9,000 frames, `--rtc` from MAME's log):
- main RAM identical to MAME on **all 9,000 frames**;
- **all 150 snapshots pixel-identical**;
- **all 2,151,644 traced device accesses identical**, PS1 and 68000 sides, at the same emulated times (to the nanosecond the doubles keep).
- **the sound matches MAME's WAV** (`--wav` against `WAV=1`, same `--rtc`): lag 0 in every window with sound, correlation ≥ 0.997, level 1.00 (the music starts ~61 s in);
- regress case `bmiidx3-boot`; peak memory ~75 MB early in the run (the 573's runs are ~250 MB).
- Host cost, interpreted, desktop: ~3.2 ms a frame (MAME at the pin: ~485 %).
- With the shim's additions, every existing regress case stays identical (the 573's, kbm's and the rest).

**What it took, beyond the port:**
- The 68000's program needs no byte swap here: the file's little-endian words are already how the shim's big-endian space holds them on this host. With a swap, the 68000 started at 0x8600 and took a Line-F exception at once.
- **Masked submaps** were the first divergence (frame 1,079): the shim put the 53CF96's 8-bit registers at consecutive bytes, while MAME spreads them over the lanes `umask16(0x00ff)` selects (two to a 32-bit word: register 8 at 0x10). The game's write-and-read-back test of the configuration register then failed.
- **The effector's sliders start centred in MAME** (frame 1,273): an `IPT_AD_STICK` field whose default is its minimum reads (65536 · (15 ≪ 24) / 131072 + 2²³) ≫ 24 = 8, so the I/O mux gives 0x88, not 0.
- `--trace` writes the oracle's MMIO format with the same groups and folding; comparing two traces record by record found both divergences in minutes.

### Risks
- **The mix is undocumented** (TW-3). Fallback: a plain "video behind the PS1 picture where it is black" mode, labelled as a guess.
- **The 68000 port** is a big generated file; if the shim can't host it cleanly, Musashi with tolerance-based checks on the SPU side, accepting that lockstep stops at the mailbox.
- **SCSI is a new bus on the shim.** MAME's nscsi uses its own handshake timers and line callbacks; the exact scheduler must reproduce their ordering.
- **DVD styles** add an MPEG-2 decoder: heavier on an N100, but on its own core.

## 22. ARM hosts (2026-10-13)

Step 1 towards an Android/ARM version: the cores on 64-bit ARM Linux. The recompilers emit C and
C++, not x86 code, and neither guest CPU has an FPU, so the work was in the host glue:
- **Fibers:** `hk_fiber_switch` in AArch64 assembly too (x19-x30, d8-d15, FPCR). The ucontext
  fallback costs ~14 % (a system call per switch) and Android's libc has no ucontext at all.
- **Same results everywhere:** `-ffp-contract=off` (AArch64 compilers fuse `a*b+c` by default;
  no change on x86-64, which has no FMA without `-march`) and `-fsigned-char` (plain char is
  unsigned on AArch64). minimp3's NEON path rounded its 16-bit output differently from the SSE
  path; it now rounds as SSE does (`hiraku-edits.py`).
- **Page size from the host:** the 573's RAM and its write-protect guard used 4 KiB; ARM hosts
  can have 16 KiB pages (Android 15+, Asahi).
- The profiler's ticks (`cntvct_el0`) and sampled PC; a NEON copy of the K057714 blit.
- `cmake/aarch64-linux-gnu.cmake` cross-builds with clang on Arch's `aarch64-linux-gnu-gcc`,
  statically linked, so qemu-aarch64 runs the binaries (and the build's own `hiraku-recomp`).

**Result:** the recompiler's output on ARM is byte-identical to x86's, and all 17 regress cases
give the same digests on the ARM build (under qemu) as on x86. Not measured yet: speed on real
ARM hardware (qemu says nothing about it). Android itself (GLES renderer, frame pacing,
launcher, packaging of the per-dump game code) is still to decide.

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
