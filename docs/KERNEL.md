# The Firebeat in-image kernel (as seen in beatmania III The Final, GCC01)

Read 2026-10-11 from the unpacked program (sha1 `1b5bd916…`, base `0x80000000`).
Addresses are BM3 Final's. Other games are expected to carry the same kernel at
different addresses. Confirm that with signatures before reusing anything here.

## Summary

This is a small **preemptive multithreading kernel** written mostly in C. Its
thin assembly layer sits in the first 16 KiB of the image.
- **Threads** run in **problem state** (`MSR.PR = 1`).
- **The kernel** runs in supervisor state, with external interrupts disabled,
  on its own stack.
- **A context switch** happens only on the way out of an exception. The kernel
  points **SPRG3** at another thread's saved register frame, and the common exit
  path restores registers from whatever SPRG3 points to, then executes `rfi`.

For the recomp this means:
- Keep the C kernel as translated code: scheduler, priorities, thread and
  semaphore objects, timers.
- Replace only the assembly glue with host code.
- Each guest thread runs on a **host fiber**. When the glue's exit path finds
  SPRG3 pointing at a frame owned by another fiber, it switches fibers.
- Interrupts can only be taken in user code. Kernel code never needs safe-point
  checks.

## Boot (`0x80003000`, assembly, runs supervisor)

1. Clear TSR. Program the bank registers BR0–7 (memory controller) and IOCR =
   `0xE401` (DCR `0xA0`). Set EVPR = `0x80000000`, so the vectors are at the
   image base.
2. Zero the debug SPRs, ESR, TCR, cache control registers, TB, XER and SPRG0–3.
   Set EXIER/EXISR-related DCRs to `0xFFFFFFFF`.
3. Set the stacks:
   - SPRG2 = `0x81FE0000`: kernel/exception stack.
   - SPRG3 = `0x81FF0000`: initial "current frame".
4. Invalidate the caches (`dccci`/`iccci` loops), then enable them
   (ICCR/DCCR = `0xFFFF`).
5. Set MSR = `0x0002920C`.
6. Zero three RAM ranges: `0x80100000–0x81FE0000`, `0x81FE0000–0x81FF0000` and
   `0x81FF0000–0x82000000`. The loops write the status-LED byte `0x7D000807`
   with `0xFB`/`0xF7`/`0xEF`/`0xAA` and keep resetting TSR (the watchdog).
7. Clear the GPRs. Set up the first stack and the SPRGs.
8. Enable the decrementing timer (TCR = `0x80000000`). Set `MSR.PR` (`ori 0x4000`):
   **from here on the program runs in user mode.**
9. Set r13 = `0x80100678` (`_SDA_BASE_`) and r2 = `0x800E9EE4` (`_SDA2_BASE_`),
   which is the EABI small-data layout. Call `0x800C923C`, the C `main`.

## Exception vectors (EVPR = `0x80000000`)

| Vector | Exception | Handler |
|---|---|---|
| `0x0100` | critical input | LED code `0x7E`, spin forever |
| `0x0200` | machine check | LED `0x7D`, hang |
| `0x0300` | data storage | LED `0x7C`, hang |
| `0x0400` | instruction storage | LED `0x7B`, hang |
| `0x0500` | **external** | → `0x800034AC` |
| `0x0600` | alignment | LED `0x79`, hang |
| `0x0700` | program | LED `0x78`, hang |
| `0x0C00` | **system call** | inline at `0x80000C00`–`0x80000DD4` |
| `0x1000` | **PIT** | → `0x80003634` (`rfi` at `0x800037B4`) |
| `0x1010` | **FIT** | → `0x800037B8` (`rfi` at `0x8000393C`) |
| `0x1020` | watchdog | → `0x8000346C`: clear TSR, `rfci` |
| `0x1100`, `0x1200`, `0x2000` | others / debug | LED `0x73`/`0x72`/`0x71`, hang |

The fatal vectors only report a code on the status LEDs. The runtime turns
them into a host abort with a guest register dump.

### Common entry and exit (identical in the `0x500`, `0xC00`, PIT and FIT paths)

**Entry:**
1. Check `SRR1 & 0x4000` to see whether the exception interrupted user code.
2. Push a **384-byte frame** on the current stack:
   - r0–r31 via `stmw` at +0;
   - CTR at +260, LR at +264, CR at +268, XER at +272;
   - SRR0 at +276, SRR1 at +280.
3. If the exception came from user mode:
   - save the user r1, which now points at the frame, into **SPRG3**;
   - switch r1 to the kernel stack from **SPRG2**.
4. Push a 12-byte scratch area holding SRR0, SRR1 and r0.
5. Call the C handler:
   - external: `0x800C5B54` (EXISR dispatch), then `0x800C7F30` (reschedule if needed);
   - PIT: `0x800C6030`;
   - FIT: `0x800C5E08`, then `0x800C7F30`;
   - system call: `0x800C52F4`, then `0x800C7E08`.

**Exit:**
1. If returning to user mode, store the kernel r1 back into SPRG2 and **load r1
   from SPRG3**. This is the context switch.
2. Restore SRR0/SRR1/GPRs/CTR/LR/CR/XER from the frame.
3. Execute `rfi`.

For a system call, the handler's r3 result is written into the frame's r3 slot
(+12 via SPRG3 when the call came from user mode, otherwise +32), so it reaches
the caller after the switch.

## System calls: one generic "call this in the kernel"

- **User stub** `0x800C5278(fn, a1..a5)` (a second copy is at `0x800C9658`):
  passes `fn` in r3 and the arguments in r4–r8, executes `sc`, and returns r3.
- **Kernel dispatcher** `0x800C52F4`: `mtlr r3; r3..r7 = a1..a5; blrl`. In other
  words, it **calls an arbitrary kernel function pointer** in supervisor mode.
  The set of kernel services is therefore the set of function pointers passed
  to the stub. Collecting them is a manifest task, needed for indirect-call
  coverage.
- After the call, `0x800C7E08` runs the scheduler and may switch threads.

## Context-switch primitives (kernel C)

| Address | Role |
|---|---|
| `0x800C5354` | `current_frame()`: SPRG3 if the saved SRR1 has PR set, else 0 |
| `0x800C53A8` | `set_frame(f)`: SPRG3 = f |
| `0x800C53D4` | `switch_to(f)`: SPRG3 = f, and copy f's SRR0/SRR1 into the kernel scratch at SPRG2+0/+4, which the exit path restores from |
| `0x800C7F30` | `if (0x800C597C()) 0x800C7E08()`: reschedule-if-needed after an IRQ or FIT |
| `0x800C7E08` | scheduler: picks the next thread (74 instructions, 12 callees) |
| `0x800C923C` | C `main` (called once from boot) |

## Recomp design consequences

1. **The glue becomes host code** in `runtime/kernel/`:
   - the boot sequence, done as host setup plus the guest's own C init;
   - the frame save and restore;
   - the SPRG2/3 juggling;
   - the vector dispatch.

   It builds the **same 384-byte frames in guest RAM**, so the kernel's data
   structures stay authentic and comparable with MAME.
2. **One fiber per guest frame chain.** A thread is identified by the frame
   pointer SPRG3 holds when it is not running. When the exit path finds SPRG3
   pointing at a frame that belongs to another fiber, the host suspends the
   current fiber and resumes that one.
   - A frame no fiber has seen yet is a new thread. Its fiber starts by loading
     registers from the frame and calling the recompiled function at the frame's
     SRR0 (via the indirect-call table).
3. **Interrupt delivery.** An exception can be taken only while `MSR.EE` is set,
   which is only in user code:
   - Recompiled user code polls `irq_pending` at back-edges and calls. When it
     is set, it enters the glue exactly as the hardware would, with SRR0 = the
     guest address of the safe point.
   - Kernel functions (`0x800C5xxx`–`0x800C9xxx`, to be bounded precisely)
     are emitted **without** polls.
4. **The `sc` instruction** compiles to a call into the glue's system-call path.
   `rfi`, `rfci`, `mtmsr` and `mfmsr` appear only in the glue and in boot, so the
   translator should never meet them inside a function. If it does, that is a
   manifest error.
5. **The watchdog** (TSR resets everywhere) becomes a no-op unless we want hang
   detection.

## Across games (measured 2026-10-11 with `analyze/sigmatch.py`)

**The assembly glue is the same design in every program checked**: bm3final and pop'n 4, 5, 6 and 7.
- The vector layout matches.
- `0xC00` starts with the same instruction.
- Each image has eight 384-byte frame pushes, 5 `mtspr SPRG3`, 4 `rfi` and 2 `sc`.
- Only branch targets move. pop'n 6/7 are linked 0x1000 lower: entry `0x80002018`, external vector → `0x80001F94+0x500`.

So **one host glue implementation serves all of them.**

**The C kernel is shared too.** Of the 118 BM3 kernel functions with a unique signature (`0x800C5000`–`0x800CA000`), 115 are found in each of pop'n 5, 6 and 7 (83% of kernel instructions). Every primitive in the table above maps one-to-one; in pop'n 7:

| BM3 Final | pop'n 7 |
|---|---|
| `current_frame` `0x800C5354` | `0x8001E470` |
| `switch_to` `0x800C53D4` | `0x8001E4F0` |
| IRQ dispatch `0x800C5B54` | `0x8001EC70` |
| scheduler `0x800C7E08` | `0x8002034C` |
| `main` `0x800C923C` | `0x80021320` |

pop'n 4 (2000) matches only 13. It carries an older or differently compiled build of the same kernel: same glue, different code generation. Its labels need another reference, probably beatmania III 1st mix (GQ972, same year), whose CHD is in `~/bm3mame/roms/bm3`.

## Open items

- Pin down the exact extent of the kernel C (all functions reachable only from
  the dispatcher, handlers and scheduler) so no-poll emission is applied
  precisely.
- Identify the thread, semaphore and event API: the function pointers passed to
  `0x800C5278`/`0x800C9658`, plus the "failed to create … thread" call sites.
- Map EXISR bits to their handlers inside `0x800C5B54`, and check against MAME's
  EXT0–4 assignment.
- PIT reload value and tick rate (`0x800C6030`); FIT period (TCR.FP).
- Confirm the same kernel in pop'n, ParaPara and KBM once their programs are
  ingested.
