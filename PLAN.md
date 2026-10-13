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

**Then M15, Twinkle.** It reuses 573-1 to 573-4 for the PS1 core. It adds:
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

## 21. Konami Python 1/2 (M16, started 2026-10-13)

The owner asked for the groundwork for a Python core. The facts behind this section are in docs/PYTHON.md. The work files (sources, the Reliquary build, oracle outputs, survey notes, decoded boot records) live in `/run/media/roganis/DATA/Python-work/`, outside the repo; the oracle build and runner are in `oracle/pcsx2/`.

### What is different from every core so far

- **Python is a PlayStation 2.** EE (R5900 + VU0/VU1), GS, IOP (R3000A), SPU2, IPU, DMAC/VIF/GIF. PCSX2's core is ~365,000 lines; MAME's whole 573 port was ~16,000. Porting a PS2 into HIRAKU device by device, as was done for DJ Main and the 573, is not a few-week job.
- **There is no MAME oracle.** Every Python 1 and Python 2 set in MAME is a NOT_WORKING skeleton. The only emulator that runs these games is **PCSX2 Reliquary**, a GPL-3.0+ PCSX2 fork. "Match MAME first" becomes "match Reliquary first", with two caveats:
  - PCSX2 is not cycle-exact hardware; its timing model is its own. Lockstep against it means matching *its* model, as HIRAKU matched MAME's.
  - Reliquary emulates the Python 1 interface board (a TX39 MIPS with its own firmware) at the FireWire command level; the board's CPU never runs. So below that layer the reference is an HLE, not hardware truth.
- **The game program is easy to get at.** Each disk carries the EE program as plain MIPS, only LZ-compressed (docs/PYTHON.md §4). The offline decode of pop'n 9's equals Reliquary's EE RAM byte for byte in its code. So a static recompiler has its input without any decryption; only the memory-card boot loader is a KELF.

### Measured (2026-10-13, Ryzen 5700X3D, pop'n 9 attract, Reliquary built here)

- **Reliquary builds on Linux** with system packages plus six small local dependencies: 83 s, 1.3 GB peak (`oracle/pcsx2/build-reliquary.sh`).
- **pop'n 9 boots to attract in it:** device checks, warning, title, How To Play, looping every ~100 s.
- **Host cost at real-time speed**, whole process, from 30 s to 150 s of attract:

  | CPU mode | GS renderer | Host CPU |
  |---|---|---|
  | JITs (default) | software | 44 % of one core |
  | JITs | Vulkan | 34 % of one core (plus the GPU) |
  | interpreters | software | 78 %, and still **below real time**: ~0.75× (3,000 frames in ~66 s unlimited) |

  - Unlimited, the JITs run 3,000 frames in ~7 s including start-up: 7× real time or more.
  - For comparison, HIRAKU's bm3final takes 10–13 % of a core and ddr2m ~10 %. pop'n 9 in Reliquary is 3–4× that, before any N100 discount (×2 or worse per core).
- **Runs repeat exactly.** Same settings and CPU mode: EE and IOP RAM identical at all 50 checkpoints over 3,000 frames, frame-limited or not (the `HK_FRAMEHASH` hook). JIT and interpreter runs differ from frame 60 on, so the oracle pins its CPU mode.
- **PCSX2's EE timing is a static table:** each opcode has a cost in 1/8 cycles (`R5900OpcodeTables.cpp`; 9 by default, 2×8 for MULT, 14×8 for DIV), summed per block and scaled at the block's end (`Interpreter.cpp:31-52, 214`). A recompiled block can carry its cost as a constant, as on the 573.
- **pop'n 9's EE program** decoded offline equals Reliquary's EE RAM in all its code; 1.4 MB, ~13,000 `jal`.

### Options for the core

1. **Host Reliquary's emulation core inside HIRAKU** (`runtime/python/`, `core = "python"`). Link PCSX2's `pcsx2` core library, which doesn't need Qt (checked: `pcsx2-gsrunner` builds against it with no Qt library linked), and implement its `Host::` interface on HIRAKU's shell: window, audio, input scripts, cabinet controls, lamps, NVRAM.
   - Playable soonest. Identical to the oracle by construction.
   - HIRAKU then owns a fork of a ~365,000-line emulator that its upstream re-syncs monthly; efficiency is PCSX2's, not better.
2. **Port the PS2 into HIRAKU piece by piece**, the 573 way.
   - Matches the codebase's style. Months of work before anything boots, against a reference that isn't cycle-exact anyway.
3. **Both, in order (recommended).** Start from (1), then replace parts behind the same seams only where a measurement says it pays:
   - **the EE game program by static recompilation**, the Firebeat pipeline on MIPS: Ghidra manifest (Ghidra has no R5900 language; it needs an extension or HIRAKU's own decoder), one C function per guest function, PCSX2's EE interpreter as the fallback and as the shadow-mode reference in the same process;
   - the interface board's I/O replaced by HIRAKU's cabinet layer (pop'n buttons, lamps, latency);
   - the GS: Reliquary's software renderer for the oracle, its Vulkan renderer for play, unless the N100 numbers say otherwise.
   - Each replacement is checked in lockstep against the unmodified Reliquary path, as the 573 is against MAME.

The efficiency case is weaker than on the 573. There the baseline was an interpreter; here it is PCSX2's mature x86-64 JITs. HIRAKU's gains on Python would come mostly from latency, I/O and the shell, plus whatever the EE recompile and an idle-skip give over the JIT. PY-0 measures this before anything is decided.

### Roadmap (revised 2026-10-13, after PY-1, from a measured profile)

PY-0 (the oracle) and PY-1 (Reliquary hosted, pop'n 9 playable) are done; see Progress below. The phase list written before them had the EE recompile as the main work. A profile of the running core says otherwise.

**Where the host time goes** (pop'n 9, `popn9-play.his`, Vulkan GS, 5700X3D, the core's `HK_PY_PROFILE=1` sampler; ~45,000 samples):

| Thread | Share of all samples | Inside it |
|---|---|---|
| GS | ~63 % | CPU-side texture decoding and swizzling (GSVector ops, `ReadAndExpandBlock16`, ...) ~35 %; texture-cache hashing (XXH3) ~15 %; HIRAKU's picture conversion (`on_gs_frame`) ~9 %; the Vulkan driver ~6 % |
| CPU (EE, IOP, VU, SPU2, board) | ~37 % | SPU2 mixing ~31 %; EE JIT code ~17 %; libc copies ~14 % (part of it the core's 0.9 MB picture copy per frame); IOP JIT code ~7 %; event and counter scheduling ~5 %; CHD decompression ~2 % |

- **The EE recompile is no longer the main work.** EE code is ~6 % of all samples, about 1 % of a desktop core. A static recompile can't win more than that on pop'n 9.
- **The cost is in the GS, the picture handoff and SPU2.**
- **Per 100 s of game** (`--no-picture` A/B): Vulkan ~18 % of a core (CPU thread ~9 %, GS ~7 %, picture readback ~2–3 %); software GS ~26 %.

Each phase ends with `tools/regress.py check popn9-play` bit-identical, unless it says otherwise.

**PY-1b. Finish the playable base** (days)
1. **The owner's hands-on test**: picture, sound (crackles?), feel. Its findings come first.
2. **Lamps.** Decode the interface board's JAMMA output command (0x0e0/0xf80 in `KonamiPython1.cpp`) into a Python pop'n lamp family in `cabinet.cpp`. The ground truth is the game's own test mode, I/O CHECK > LAMP CHECK, lamp by lamp. Then the lights socket and HID lamps (docs/LIGHTS.md).
3. **Launcher.** The game shows from `games/popn9/game.toml`; its "not built" hint must name `oracle/pcsx2/build-reliquary.sh`. Windowed runs get `--renderer vk`.
4. **NVRAM.** Check that settings, bookkeeping and the disk overlay survive restarts, and that ALL FACTORY SETTINGS in test mode works.
5. **Packaging** (`tools/package.sh`, the owner's laptop test):
   - ship `hiraku-popn9`, Reliquary's `resources/` and the three local libraries (ryml, plutosvg, plutovg), with an `$ORIGIN` rpath;
   - drop the hard FFmpeg dependency (`USE_LINKED_FFMPEG=OFF`, loaded at run time) if the build allows.
6. **Reliquary updates.** Pin the version (1.9.4) and write down how to re-apply the patch to a new release. Keep the patch small: taps and hooks, no logic.

**PY-2. Measure properly** (1–2 days)
- An A/B harness (alternating runs, medians), as for the 573; `HK_PY_PROFILE` for the breakdown.
- **Low-end hardware:** the owner's Haswell laptop (Mesa's `hasvk` Vulkan, or `--renderer gl`), the MacBook pack, an N100 when there is one. Judge by frame-time spikes, not means.
- **Exit:** a cost table per part, desktop and low-end. It decides how far PY-3 goes.

**PY-3. Efficiency, cheapest first**
1. **The picture handoff** (~9 % of GS samples, plus copies on the CPU thread):
   - hand RGBA over as it comes and let the shell's GL upload swizzle it;
   - swap buffers instead of copying;
   - keep one download texture instead of creating a render target and a download texture every frame (a patch to `SaveSnapshotToMemory`'s path).
2. **PGO for Reliquary's core with HIRAKU's**, trained on `popn9-play`, as `tools/pgo.py` does for the other cores (the 573 got 1.44×).
3. **GS settings that leave the picture unchanged:** texture preloading and hashing levels. Check them with picture digests on a Vulkan regress case, which this needs.
4. **SPU2:** profile-driven speed-ups that keep the output bit-identical, small and upstreamable. It stays on the CPU thread, because the game reads its state back.
5. **Only if the readback still shows:** present the GS's picture straight into the shell's window, sharing the texture. That interacts with HIRAKU's picture modes (CRT shaders), so the readback stays the default.
- **Exit:** a budget set from PY-2, like M11's: a share of an N100 core on average, and no frame over a set host time.

**PY-4. Latency and I/O** (HIRAKU's reasons to exist here)
- **Input:** find how often the game's IOP driver polls the interface board over FireWire (the board's command trace, `FW_VERBOSE_LOGS`). Then measure key-to-picture with a latency script, as for pop'n 4 and Animelo 2.
- **Picture:** the picture of vsync N reaches the shell at vsync N+1. Add an option to wait for vsync N itself, trading GS/EE overlap for a frame less latency, and measure both.
- **Sound:** delivered once per vsync (16.7 ms batches). Deliver per SPU2 chunk for less audio latency.

**PY-5. The rest of Python 1, as media arrive**
- **pop'n music 10–14:** a `games/<id>/game.toml` each (the ingester prints the EE program's SHA-1), the IoMode, and a play script as a regress case.
- **Dancing Stage Fusion** needs a clean dump, or a documented repair (the boot ROM's missing 32 bytes, record A's first sector).
- The non-BEMANI titles only if wanted.

**PY-6. Python 2** (needs media: DDR SuperNOVA 1/2, GuitarFreaks/DrumMania V–V3)
- `.py2` ingest; P2IO over USB; the DEV9 hard disk bound by DNAS to the HDD ID and the i.LINK ID; the e-amusement card reader stubbed or emulated as Reliquary does.
- Cabinet families: DDR exists; GuitarFreaks (neck, pick, wailing, effect knob) and DrumMania (pads, pedal) are new.

**PY-7. Optional, only where PY-2/PY-3 measurements show the CPUs matter**
- **The EE recompiled statically.** The groundwork stands: plain-MIPS program on the disk, a static cost table, PS2Recomp's tables as a cross-check. Upper bound ~6 % of samples on pop'n 9; a heavier 3D Python 2 game could change that.
- **The IOP on the 573's MIPS recompiler** (the same R3000A): ~7 % of the CPU thread.
- **The interface board's TX39 firmware run for real**, instead of Reliquary's command-level emulation: only if a game needs behaviour the emulation lacks. It would be a departure from the reference, so it needs a hardware source.

### Progress (2026-10-13): PY-0 and PY-1

**The oracle, `oracle/pcsx2/`:**
- **The patch** (`reliquary-oracle.patch`, six files):
  - the RAM-hash hook;
  - `pcsx2/HirakuHooks.h`: taps for the vsync, the audio output after the interface board's ADPCM, and the displayed picture;
  - the keystore printout made opt-in (`PCSX2_PRINT_KEYSTORE`);
  - a CMake entry point (`-DHIRAKU_DIR`) that builds HIRAKU's Python core inside Reliquary's tree. Reliquary's CMake assumes it is top-level in ~50 places, so it can't go inside HIRAKU's.
- **Memory-card slot 2 is off** in the oracle as in the core: a Python cabinet has only the dongle, in slot 1. With slot 2 on, the BIOS probes it and the runs part at frame ~180.
- **Dropped from PY-0:** scripted P1IO input in `pcsx2-qt`. The core runs Reliquary's own code, and HIRAKU's script runner drives its inputs, so the oracle only has to confirm that the core matches it from power-on. It does, below.

**The core, `runtime/python/` (`core = "python"`, `hiraku-popn9`):**
- **Built by** `oracle/pcsx2/build-reliquary.sh` against Reliquary's `PCSX2` library. The binary sits next to Reliquary's resources and is linked into `build/cmake/`.
  - HIRAKU's shell sources are compiled there too, with `-fexceptions`: PCSX2 builds without exceptions.
- **`Host::`:** implemented after `pcsx2-gsrunner`. The GS runs surfaceless.
- **Pictures:** the GS thread hands each presented frame over (native size: 512×448 on pop'n 9). At vsync N the CPU thread waits for frame N−1, so pictures are deterministic and the GS still overlaps the EE.
- **Sound:** 48 kHz, gathered per vsync.
- **The run's files:** the BBSRAM, dongles, boot ROM and memory card are copied into the NVRAM folder (or a fresh folder per scripted run); the CHD gets Reliquary's write overlay there.
- **Exit:** the shell's `exit()` ends through glibc `on_exit` → `_exit`. PCSX2's static destructors assert while the VM is still mapped.
- **Settings stay out of `~/.config/PCSX2`.** `EmuFolders::SetDataDirectory()` would create it, so the core sets the folders itself.
- **`tools/ingest.py <set>.py1`:**
  - identifies the game by the SHA-1 of its decoded EE program (`games/popn9/game.toml [python] ee_sha1`);
  - copies the board's files, the BIOS and the keys into `dumps/popn9/`, and links the CHD.

**Results (pop'n 9):**
- **Matches the oracle:** EE and IOP RAM hashes identical to `pcsx2-qt`'s at all 50 checkpoints up to frame 3,000 (JITs, software GS).
- **Repeatable:** picture and sound digests identical over 6,000 frames.
- **Plays:** `oracle/scripts/popn9-play.his` goes from power-on through coins, NORMAL and song select to "Requiem", scored, and CLEARED. It is the regress case `popn9-play` (14,000 frames, ~850 MB peak).
- **Buttons checked in test mode:** I/O CHECK > INPUT CHECK lights SW1–SW9 for buttons 1–9; Service lights SERVICE. Coins count.
- **The window works:** the shell's OpenGL picture beside PCSX2's surfaceless Vulkan device. The Vulkan GS (`--renderer vk`) gives the same game, with the same scores.
- **Host cost** (headless, 100 s of game, this desktop, noisy):
  - software GS ~30 % of a core; Vulkan ~18–27 %;
  - the CPU thread's own work 1.9–2.3 ms a frame on average;
  - one 175 ms frame at boot (frame 326);
  - to be measured properly (A/B, median) and on N100-class hardware.

**PY-1b (2026-10-13), done but for the owner's test:**
- **Lamps:** the patch taps the interface board's JAMMA output command (`Hiraku::p1io_output`). Word 8 holds the lamps.
  - The bits were read off the game's LAMP CHECK, item by item: SW-LAMP 1–9 = bits 0–8, HI-LAMP 1–5 = bits 14–10, LEFT-LAMP 1–2 = 15–16, RIGHT-LAMP 1–2 = 17–18.
  - They form the cabinet family `pypopn`: pop'n's controls, Firebeat pop'n's lamp names. All 18 lamps light through the lights socket.
  - `HK_PY_P1IO_LOG=<file>` logs the command's payload changes.
- **The input poll:** the game sends that command, and gets the input report back, **exactly once per frame**. This is the fact PY-4 starts from.
- **Launcher:**
  - accepts a `.py1`;
  - the ingester also takes a set's folder (the `.py1` beside it);
  - Python games get `--renderer vk`, a `pypopn` family name and icon, and Python-specific install and build hints.
- **NVRAM:**
  - Reliquary rewrites the BBSRAM file in the NVRAM folder on every write; the core copies the dump's files there only when they're missing.
  - pop'n 9 writes nothing in normal play: bookkeeping is OFF by default, and a factory reset rewrites identical bytes.
  - A scripted change of a saved setting is still to do. Test-mode submenus miss 6-frame presses (10 work), and the hold-red-and-press-blue change didn't take in a script. The owner's hands-on test covers it meanwhile.
- **Packaging:**
  - Reliquary is built with `$ORIGIN/lib` in the run path and FFmpeg loaded at run time (the binaries no longer need it).
  - `tools/package.sh` adds `resources/`, the libraries built for Reliquary and the CHD reader. The packaged binary runs with the build's `deps/` moved away.
- **Reliquary pinned:** 1.9.4, the zip's SHA-256 and the update procedure are in `oracle/pcsx2/README.md`. The patch: 7 files, 190 lines.

**PY-3 and PY-4, first steps (2026-10-13).** Measured with `tools/ab.py`: alternating runs, 5 each, medians of total process CPU. Vulkan, 6,000 frames unless noted. Every change is bit-identical (`popn9-play`, and the new `popn9-play-vk` case) unless noted.

| Change | CPU | Notes |
|---|---|---|
| **Picture handoff:** `bswap32(p) >> 8` instead of per-pixel masks; buffers swapped, not copied | **−7.3 %** | `on_gs_frame`: 8.9 → 1.0 % of GS samples |
| **PGO** (`build-reliquary.sh pgo`, trained on `popn9-play`, both renderers) | **−3.2 %** (software GS −1.9 %) | far from the 573's 1.44×: the hot paths are JIT code and vectorised texture code; kept as an optional step |
| Texture preloading Partial instead of Full | **+11.4 %**: dropped | the picture is identical; None differs in 17 frames |
| **Each vsync's own picture** (`vsync_posted` hook; `--picture-overlap` for the old way) | +1.4 % (wall +9 % unthrottled) | **one frame less latency**; EE/IOP hashes identical in both modes; now the default |

- The Vulkan GS is deterministic on this GPU and driver: identical digests over 14,000 frames. That makes `popn9-play-vk` possible; its baseline must be recorded per PC.
- **Latency** (`oracle/scripts/popn9-latency.his`, `tools/pylatency.py`: test mode's INPUT CHECK, button 1 every 60 frames):
  - 4 frames from a press or release to the changed picture, every time; 5 before the picture change;
  - the game polls once per frame, and in windowed play the frame runs within ~2 ms of the shell reading the controls, so later input reading would gain little;
  - sound goes to the shell once per vsync, but a frame's emulation is a ~2 ms burst, so finer delivery would gain ~2 ms at most;
  - the 4 frames are pop'n 9's own pipeline.
- **Gameplay latency.** Runs are deterministic, so the method is two runs of the same song, identical but for one press (frame 6003); the first differing audio sample and picture are the press's effects.
  - **Picture:** 5 frames after the press.
  - **Keysound (button 5): ~2 ms after the vsync at which the press is applied.** The game polls just after the vsync and starts the voice at once. Button 1 made no sound difference at that point of the song.
  - What the host adds on top: up to a frame until the press is read (the game polls once a frame, as on the cabinet), the audio queue (8 ms by default), the display.
- **Against stock Reliquary (`pcsx2-qt`, same build, same settings, same pop'n 9 attract from power-on), 2026-10-13:**

  | | pcsx2-qt | hiraku-popn9 | |
  |---|---|---|---|
  | Playing at real time, windowed, CPU from 30 s to 150 s, Vulkan | 34 % of a core | 20 % | **−40 %** |
  | The same, software GS | 44 % | 29 % | **−34 %** |
  | Flat out, CPU for 6,000 frames, Vulkan (pcsx2-qt still presenting; the core headless) | 16.8 s | 16.3 s | −3 % |
  | The same, software GS | 22.9 s | 23.0 s | 0 % |

  - Medians of 2 real-time and 3 flat-out runs, alternated. Audio was null in both, and the display ran at 144 Hz, so the core's display sync was off.
  - **The emulation costs the same:** it is the same code. The saving appears only at real-time speed, so it is in how each paces and presents a frame. The split between pcsx2-qt's frame limiter, its presentation and its Qt UI is **not measured**.
- **Unlocks (§20's system), 2026-10-13:** pop'n 9's event phase, set from the launcher's Unlocks dialog or `tools/unlock.py popn9 phase=0..4`.
  - **How it was found:** the four RemyWiki codes typed in test mode > EVENT OPTIONS > PASSWORD (`oracle/scripts/popn9-phases.his`), the saves compared, then the program read.
  - **Where it lives:** the interface board's BBSRAM (`m48t58y.u48`), a small file system (`bbsram0:/settings`, `/book`) written by Konami's `mirror.c`:
    - each file is kept twice (settings0 at 0x0, settings1 at 0x110);
    - each copy is a 12-byte header {sum, size, data sum} with the data at +0x10. The sums are of 32-bit LE words, each XORed with its index (`0x1ab6f8`);
    - the 0x60-byte settings record (RAM `0x1212180`) ends with its own sum at +0x5c (`0x1a9b28`): five segments, each byte XORed with its index in the segment, plus 0x50. A record with a wrong sum is ignored at boot (the phase reads `--`);
    - the phase is record +0x14, 0–4 (`0x1aa4f0` reads it).
  - **Checked:**
    - every phase written by the tool, from phase 0 or from MAX, is byte-identical to the game's own save at that phase;
    - a tool-made MAX save boots at MAX (EVENT OPTIONS), and the mode select then has HITASURA after EXPERT.
  - **Not checked:** what phases 1–3 do without e-AMUSEMENT (the Gambler Z labyrinth needed a card).
  - `tools/unlock.py` gained checksum kind `mirror` (with an optional `inner` record sum) for it. Code macros (`macros.txt`) already run in the Python core, which shares `runtime/main.cpp`; pop'n 9 has no per-credit codes known yet that would need one.
- **e-AMUSEMENT, 2026-10-13 (owner: "start work on eamusement"):** pop'n 9 is the first pop'n on it. Read with Ghidra 12.1.2 headless (`MIPS:LE:64:64-32addr`, raw binary at 0x100000, `$gp` = 0x25da70, functions seeded at every `addiu sp,sp,-n`; scripts in `$HK_PYTHON_WORK/ghidra/`).
  - **What the game has:**
    - an e-AMUSE3 client: DHCP and sockets on the interface board (the board has the Ethernet port, and the game sends it socket commands);
    - HTTP/1.0 to a host named `services` (User-Agent `EAMUSE.Httpac/1.0`, an `X-Eamuse-Info` header, XML bodies), with services `services.get`, `numbering.assign`, `package.list`/`intend`, user data, rankings, KONAMI ID, p-friend, the coin count, NTP and ee'MALL;
    - a motorized magnetic card reader/writer and a 10-key, on the board's serial port.
  - **The card and the server:** the card holds only an ID and a type. Its first 4 bytes plus a CRC-8 give the type (`0x1a28c0`): `28 23 41 01` a new pop'n 9 card, `68 23 41 01` a used one, and others for Konami common, location-test and "death" cards. The ID is 8 bytes, written as 16 hex digits. A new card gets its ID from the server (`numbering`), and is then formatted, written and read back. A used card's player data is read from the server and written back to it. So a card does nothing without a server.
  - **Step 1, the card unit (done):** `runtime/python/cardunit.{h,cpp}`, behind a new hook in the Reliquary patch (`Hiraku::uart_write`/`uart_reply`, installed for family `pypopn` only).
    - The bus (driver `0x1a1050`–`0x1a6c00`): 9600 baud, half-duplex. The game reads its own packet back before each answer, which is what Reliquary's stock answer lacked (CARD UNIT ERROR).
    - Packets are `aa T ADDR CMD LEN [payload] SUM`. Bus commands: 01 address, 02 revision, 03 start. Card commands: 10 reset, 12 status (0x80 card in, 0x20 read done, 0x10 buffer loaded), 14 control (1 open, 2 eject, 4 read, 5 write), 16/18 write/read the 128-byte buffer, 1e format. 10-key: 20 init, 24 status, 26 events (PS/2 set-2 scan codes).
    - Reliquary delivers 38 received bytes per transfer, so with the hook it now drains longer answers (the card read is 141 bytes).
    - **Checked:** with e-AMUSEMENT ON, the boot passes CARD R/W CHECK (test mode shows the unit as VER 1.0.0) and 10 KEY CHECK. Test mode's CARD CHECK reads the card: "CHECK IS COMPLETED. NEW CARD" (`oracle/scripts/popn9-eamuse-on.his`, then `popn9-cardcheck.his`). popn9-play and popn9-play-vk are unchanged. With e-AMUSEMENT OFF the game sends the unit no commands (4,000 frames from power-on, `HK_PY_CARD_LOG`).
    - **Not checked yet:** writing a card (only a new card's creation writes, after the server's numbering), and the 10-key in play.
  - **Step 2, the network (done):** `runtime/python/eanet.{h,cpp}`, a virtual LAN with no host sockets, behind a second hook (`Hiraku::net_command`, with `net_respond`/`net_write`/`iop_read`).
    - **The commands:** the board command is the game's RPC number + 1 (EE wrappers `0x1b3648`–`0x1b6ae8`): 0b socket, 0c bind, 0d listen, 0e accept, 0f connect, 13 setsockopt, 14 send, 15 sendto, 16 recv, 17 recvfrom, 1c shutdown, 1d close, 1e a property (0x101 IP, 1 mask, 3 gateway, 6 DNS, 0xf the local domain `PYTHON1-00000001`).
    - Each answer is the channel's 8-word status block: word 1 the result (byte-swapped), words 2 and 3 the sender for recvfrom.
    - Start-up and DHCP (00–07, "bound") stay Reliquary's.
    - **The LAN:** the board at 192.168.50.2/24; gateway, DNS and `services` at 192.168.50.1. DNS answers `services.*`, and TCP port 80 there reaches an HTTP/1.0 handler in the core.
    - **The game's start-up** (state machine `0x182c58`): net init, DHCP, the addresses, then e-AMUSE3 starts (`0x18ec30`: DNS, domain, port 0x7e90). It resolves `services.PYTHON1-00000001`, connects to port 80 and POSTs `/` with `X-Eamuse-Info: 1-xxxxxxxx-xxxx` and an RC4-encrypted body.
    - **The crypto is the modern e-AMUSEMENT one** (as in bemaniutils): key = MD5(the header's 6 bytes + the 26-byte shared secret), RC4, plain XML with no compression. The first request decrypts to `<call srcid="01401401000100045FEF" model="C00:J:A:A"><services method="get"/></call>`.
    - **Checked:** popn9-play(-vk) unchanged. With e-AMUSEMENT OFF the game sends the board no socket commands (4,000 frames, `HK_PY_NET_LOG`).
  - **Step 3, the server (in progress):** `runtime/python/easerver.{h,cpp}`: the protocol's MD5/RC4, a small XML reader, and answers built from the game's response parsers. State is kept in the save folder's `eamuse/`.
    - **Modules and methods** (the method entries at `0x1e81e8`…, each {name, `services` item, -, handlers: init, build, parse, end}):
      - services get;
      - posevent income/sales/sale (sale and sales need `approve`);
      - pcbtracker alive;
      - numbering assign (`number`);
      - package list/intend;
      - userdata read (109 = not registered, 110 = expired; `time`, `<b>`) and write;
      - userid ctou (`user`, `active`), adduser (`alter`), addcard;
      - rank get/register (+ `_h` HITASURA, `_gz` Gambler Z);
      - p_friend get/state/ask;
      - eemall i_get;
      - ntp get (`<time year month day hour min sec>`).
      - `services` must list the items `posevent`, `pcbtracker`, `numbering`, `package`, `userdata`, `userid` and **`local`** (rank, p_friend, eemall and ntp look up `local`).
    - **The envelope:** `<response><MODULE status="0" …>…</MODULE>…</response>`, one element per call, in order. `fault` or a non-zero `status` fails the call.
    - **Encryption follows the call's packet type.** Some calls go out unencrypted (numbering, ntp), and their answer must also be unencrypted, **without** `X-Eamuse-Info` (`0x190290` compares the header's crypto version with the packet type's). Answering them encrypted failed them before any XML was read. That was found with a temporary EE-interpreter PC trace, kept outside the patch (`$HK_PYTHON_WORK/scratch/Interpreter.cpp.eetrace`, `HK_EE_TRACE=pc,…`, run with `--interp`).
    - **The start-up** (`0x182c58`): services.get → pcbtracker.alive → posevent → ntp.get → status 0. The title then says "e-AMUSEMENT 接続中!" and "カードが使用できます!!". A ping check (raw ICMP, `0x1885b8`) runs every so often; the LAN answers the echo with an IP header.
    - **Checked, a new card** (`oracle/scripts`-style runs from the e-AMUSEMENT-on save):
      - credits and Start, then the INFORMATION screen asks for the pop'n card;
      - Insert: the reader takes it and reads "NEW CARD"; then name entry, then the PIN twice on the 10-key;
      - `numbering.assign` gets `2003010000000010` (16 digits, Luhn check);
      - the game formats, writes and reads back the card: `68 23 41 01` + CRC-8, the ID at 6–13 (byte-reversed), CRC-16 at 0x3f;
      - "カードセーブ完了", then `userid.ctou` (no KONAMI ID), and mode select with the card.
    - **Checked, a whole credit and the card again** (`oracle/scripts/popn9-card-new.his`, then `popn9-card-again.his`, same save):
      - at the credit's end the game sends `p_friend.state` and `userdata.write card=… time="2003-06-02 06:42:41+9"`, with the player data base64 in `<b>` (chunks of 0x200 bytes, `0x197fc0`); the server keeps the time and the `<b>` chunks;
      - next credit: `userdata.read` gets them back, the game skips the name and asks the PIN;
      - 1234 shows the player's name ("あああ"), character and options, then `p_friend.get` and `userid.ctou`, and mode select with the card;
      - a wrong PIN gets "パスワードが違います" and, after the retries, play without the card;
      - the PIN is checked by the game, from the player data.
    - **Rankings, KONAMI ID, ee'MALL** (2026-10-13), from the parsers (`0x19a080`…, `0x1992e8`…, `0x198858`…, `0x19b558`) and the status mappings (`0x184f68`, `0x1863e8`, `0x186620`, `0x186818`):
      - **Boards:** score per course, HITASURA per course, and Gambler Z per shop. Each entrant's best is kept; the board is sorted; `register` answers with the place, the number of entries and `update`.
      - **userid:** ctou 112 means no KONAMI ID; adduser 113/114; addcard 115/116/114.
      - **ee'MALL:** items per card, from `eemall-CARD`.
      - **Checked in play** (`oracle/scripts/popn9-card-expert.his`): an EXPERT course with the card, failed in its first song, still sends `rank.register`. The name is hex Shift-JIS, `area_num` 255. The game then shows INTERNET RANKING, "あああ さんの New ハイスコア 918 は 4位 4人中です", from the server's answer against three seeded entries.
    - **Online** (`--eamuse http://host[:port][/path]`, `eaclient.cpp`): the core forwards each request to the server. Answers arrive on a worker thread, and the game's recv waits for them (the board's status block is written when the answer is there). DNS gives every name an address of its own on the virtual LAN, so the URLs the server hands out reach their hosts.
      - **`hiraku-eamuse`** (`tools/eamuse/server.cpp`): the same server behind an HTTP/1.0 listener.
      - **Checked:** a card registered through it on 127.0.0.1; offline unchanged.
    - **For server owners:**
      - `docs/EAMUSE3.md`: transport, the crypto and its in-kind rule, the envelope, every module with its statuses, the card, a card's life.
      - `tools/eamuse/corpus.json`: the game's calls, captured or built from its formats, flagged.
      - `tools/eamuse/check.py URL`: the corpus checked against the parsers' rules, then a stateful scenario. hiraku-eamuse passes all of it.
    - **Launcher:** Settings > e-AMUSEMENT > Server (empty = offline) passes `--eamuse`.
    - **Phases MAX and 1–3 with the card:**
      - start-up sends `rank.get_h` (MAX) and `rank.get_gz` (1–3);
      - a Gambler Z credit (phase 1) ends with `register_gz prog="0" parts="00000000"`, and attract then shows "Gambler Z in progress" pages;
      - the labyrinth is the cabinet's (settings record 0x18–0x23; `0x1aa7f0`…), and the credit set +0x1c to 0xffff;
      - a HITASURA run with no song cleared sends no `register_h`.
    - **`--autoplay`** (the owner's request, for testing): the game's own demo play in a real credit. `0x257084` is pop'n 9's autoplay flag: each song's start sets it to "mode (`0x25a0d8`) == 2, the attract demo" (`0x10e570`); while it is set, each note reaching the line is judged GREAT (`0x10cbe8`). The play screen's button reader (`0x10dd80`) gives nothing in the demo by its own mode test. The mod patches both tests (`0x10e644` bnel → nop, `0x10dd9c` beq → b) through PCSX2's TLB at a vsync, as its own patches are written, so the recompiler drops the blocks.
      - **Checked:** "Requiem" 201 GREAT, combo 200, 100000, CLEARED; a whole NORMAL credit, ALL STAGE CLEARED. Without the flag, `popn9-play` and `popn9-play-vk` are identical.
      - **HITASURA** (`oracle/scripts/popn9-hitasura.his`, phase MAX): the first course's three songs cleared, life ×50 left; `rank.register_h course="0" music="3" combo="365"` (now captured in the corpus).
      - **Gambler Z** (`oracle/scripts/popn9-gamblerz.his`, phase 1): after the results and the staff roll, the labyrinth (`0x19e188`, phases 1 and 2 by `0x143c98`): "1階 Aフロア", 3 steps, one per cleared stage (`0x1088c8`, two for a stage whose song counts double). Settings +0x1c went -1 → -4. `register_gz` still sent `prog="0" parts="00000000"`.
      - **The labyrinth, read from the program** (docs/EAMUSE3.md): cells hold direction bits; a junction is the player's choice (buttons 4/6, red, a 180-frame timer); treasure cells give items (flags +0x18, low 24 bits); stairs change floor (+0x20); from floor index 10, door cells start a boss song whose score deals 1–5 damage (70k/80k/90k/95k) to 25 HP, 50 on floor index 15 (+0x22). The record also holds the shop's area (+0x35) and name (+0x37) that every `rank.register*` sends.
    - **The clock:** `ntp.get` is set into the cabinet's clock as given (`0x18a138` → `0x1a9598`), and the game stamps its times "+9". `hiraku-eamuse` sends Japan time (`--tz`); the core sends its emulated clock.
    - **KONAMI ID on the cabinet** (done): at the end of a credit with a card that has none (`ctou` 112), INFORMATION "KONAMI IDのご案内" (`0x1a8678`): 新規発行 (the ID on a character wheel, 8–32 of `a-z0-9.-_`; a 4-digit password twice on the 10-key) sends `userid.adduser`; カードへ登録 (ID and password once) sends `userid.addcard`. Checked with `oracle/scripts/popn9-konamiid-new.his` (`hiraku09` made) and `popn9-konamiid-addcard.his` (a second, fresh card added): the server's `users` lists both cards and `ctou` gives the ID for each. The game sends no `aflag` (only when nonzero); both calls are now captured in the corpus (with test card numbers, so replaying it doesn't take numbers a server hands out).
    - **Gambler Z on the server** (done): `register_gz` sends `parts` = the labyrinth's items word (`%08x`) and `prog` = its bits set (`0x188248`); a save with two songs and the first boss sent `prog="4" parts="00084003"`. The bits, read from the item list of a save with all of them: 0–13 a song per floor's treasure, 14+k/19+k a boss's EX chart and EXPERT course, 24 cleared. `tools/unlock.py popn9` decodes them (`[[progress]] kind = "gamblerz"`), so the launcher's Unlocks dialog shows them.
    - **Ranking pages** (done): attract mode shows "popper's ranking" per course after each demo song, and in phases 1–2 the Gambler Z shop ranking after the labyrinth, both from the server's boards, while the network check passed (`0x2571f8`). `area` is 0-based JIS order (0 北海道, 12 東京都, 46 沖縄県, 255 none); `shop_name` hex Shift-JIS, from test mode's SHOP NAME SETTING (`oracle/scripts/popn9-shopname.his`: "ひらく" went out as `82D082E782AD`).
    - **pop'n friends** (done): `get` while the player data's friend flag is 0, `ask` once it's set, `state` at the credit's end; all three seen. 600 (get) and 601 (ask) are final noes. **ee'MALL:** `eemall.i_get` is unreachable in pop'n 9 (its wrapper `0x186ef8` has no caller).
    - **A boss fought in play** (done): saves edited to floor index 10 with a fight under way (HP at settings +0x22), then the autoplayed Gambler Z credit: the boss's EX STAGE (テクノポップ "321 STARS" EX) at 100,000 deals 5; HP 25 -> 20 saved, and from 5 the boss falls: "隠しコース 出現！ すわコース", bits 0x84000, back to 1階 Aフロア, and the same credit's `register_gz` sends `prog="2" parts="00084000"`. `register_gz` comes after the labyrinth (the docs had it before).
    - **Phase 3** (done): the event's end. `get_gz` at start-up; after a credit no labyrinth and no `register_gz`; attract shows the shop ranking after its demos in place of the course pages, and no labyrinth (an autoplayed credit and 12,000 frames of attract from a save with items).
    - **package: the title message** (done): after `package.list`, the game looks for an item named `message`; if offered it sends `package.intend url=..` and GETs the url (plain HTTP), keeping `size`-1 bytes, and scrolls the text across its title screen (`0x184410`, `0x1237c0`). The server offers the owner's `message` file (≤255 bytes, one line, ASCII or Shift-JIS) and answers the GET; checked offline and online through hiraku-eamuse, ASCII and kanji. All 23 calls the game makes are now captured in the corpus (eemall.i_get is never sent).
    - **bemaniutils** (done, out of tree): `tools/eamuse/bemaniutils/popn9-python.patch` (base 1e66fcd) adds pop'n 9 to a bemaniutils server: a backend for all its calls (`bemani/backend/popn/popn9.py`, game code `C00`), several requests per call in the dispatcher, and answers in kind in the XML layer (no declaration when the request has none: pop'n 9 rejects `encoding=`; untyped element text kept for `userdata.write`). Written with an AI model, so not for upstream: bemaniutils has refused AI contributions since 1e66fcd (README); the patch and its README say so. Checked with MariaDB 11 in Docker and HIRAKU online: check.py all pass; a new card numbered, written, saved and read back with its PIN; a KONAMI ID made on the cabinet (113 for one taken); course, HITASURA and Gambler Z registrations and boards. bemaniutils' own tests pass (71; 5 need packages not installed). Not supported: the title message (its GET is the health check), frontend pages.
    - **Found on the way:** with a KONAMI ID on the card, INTERNET RANKING shows "? 位 ? 人中" (both servers; cause not found). The XML declaration with `encoding=` is refused by the game (without one, or `<?xml version="1.0"?>`, is fine).
    - **Not done yet:** nothing known on the server side; the meanings of a few statuses (docs/EAMUSE3.md, "Not known yet").
    - **Seen once:** one `popn9-play` check differed right after a rebuild, then was identical three times running. Where it differed was not captured; watch for it.
  - **Debugging aids:** `HK_PY_WATCH=addr,…` prints EE RAM words when they change. Ghidra headless decompiles (`$HK_PYTHON_WORK/ghidra`: `Decomp.java OUT addr|range:A-B…`, `Refs.java addr…`).
- **Tools:**
  - `tools/ab.py`: A/B of two binaries, or of two settings with `--a-args`/`--b-args`;
  - `--pcsx2-set Section/Key=value`: any emulator setting, for experiments;
  - `--no-picture`, `HK_PY_PROFILE`.

**Next:**
- the owner's test;
- PY-2 on low-end hardware, which decides whether PY-3 goes into PCSX2's GS texture path or SPU2 (both mean patching emulator logic, not just taps);
- hands-on latency with real controls and audio (the owner). The profile behind it was measured with the core's `HK_PY_PROFILE=1` sampler and its `--no-picture` option (both added after PY-1).

### Decisions (owner, 2026-10-13)

1. **Option 3:** host Reliquary's core in HIRAKU first (PY-1), then replace parts (the EE by static recompilation, the I/O by HIRAKU's cabinet layer) where a measurement says it pays.
2. **Reliquary is the reference** for Python, in place of MAME, whose Python drivers are skeletons. The oracle pins its CPU mode.
3. **The first set is `popn9`.** The owner gets the other games later for testing.
- Still open: Python 2 media; a clean `dstagef`.
- Licence: linking Reliquary makes the Python binaries GPL-3.0 as a whole (its Python 2 files carry only the repo's GPL-3). HIRAKU's own code stays GPL-3.0-or-later.

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
