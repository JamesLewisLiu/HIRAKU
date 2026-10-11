# How the recompiler works

## Pipeline

```
dumps/<game>/program.bin ──analyze/ghidra.sh──▶ build/<game>/manifest.json
                         ──analyze/prep.py───▶ build/<game>/recomp.txt
                         ──hiraku-recomp─────▶ <cmake>/gen/<game>/*.c ──▶ hiraku-<game>
```

1. **`analyze/ghidra.sh`** runs Ghidra headless and `ExportManifest.java`. Before exporting, it also creates functions for:
   - every data word that points at code Ghidra decoded but did not assign to a function (callbacks, thread entries);
   - every address listed in `[recomp] extra_entries` in `game.toml`.

   The export records functions, body ranges, calls, tail branches, jump tables and indirect sites.
2. **`analyze/prep.py`** checks the control flow and writes the plain-text spec.
   - Every call or tail-branch target must be a function entry. A target in the *middle* of a function becomes a **secondary entry** of that function.
   - Every `bctr` should have a resolved jump table.
3. **`hiraku-recomp`** emits one C function per guest function, split over a fixed number of files so CMake knows the outputs in advance. Generated code is never committed: it is derived from the game.

## What the generated code looks like

```c
void f_800c7f30(hk_ctx *c) {
    HK_POLL(c, 0x800c7f30u);
    op_stwu(c, 1, 1, -64);                     /* 800c7f30 */
    op_mfspr(c, 0, 8);                         /* 800c7f34 */
    op_stw(c, 0, 1, 72);                       /* 800c7f38 */
    HK_CALL(c, f_800c597c, 0x800c7f40u);       /* 800c7f3c */
    op_cmpi(c, 0, 3, 0);                       /* 800c7f40 */
    if (hk_bc_ok(c, 12, 2)) { goto L_800c7f4c; }
    ...
```

- **Instructions** are calls to the `op_*` functions in [include/hiraku/ppc.h](../include/hiraku/ppc.h), with constant operands. That header is the *only* definition of instruction semantics. The interpreter and the recompiler are both generated from one operand list (`HK_OPLIST` in `src/ppc/decode.h`), so they cannot disagree about operand order.
- **Guest state** lives in `hk_ctx`: GPRs, CR as a single 32-bit word, XER, LR, CTR, MSR, SRRs and SPRGs. Privileged registers that are not in `hk_ctx` are in `hk::g_cpu`.
- **Memory** is kept big-endian. `hk_r*`/`hk_w*` mask the 4xx mirror bit; RAM (32 MiB) takes a fast path, and everything else goes to the MMIO bus.
- **Control flow:**

  | Guest instruction | Generated code |
  |---|---|
  | branch inside the function | `goto` |
  | backward branch | `HK_POLL` first, so interrupts are delivered at loop back-edges |
  | `bl` | `HK_CALL`: set LR, direct C call, then check that the callee returned to the expected address |
  | branch into another function | tail call (`f(c); return;`) |
  | `bctr` | `switch` over the jump table, with a dynamic fallback |
  | `blrl` / `bctrl` | `hk_call_dyn` |
  | `blr` | `HK_RET` |
  | `sc` | `hk_syscall` |

- **Return-up helpers.** GCC's out-of-line epilogue helpers (`_restgpr_*_x`) restore the caller's registers and LR and return straight past the caller. They are detected by following the fall-through chain and checking whether it writes LR (`mtlr`) without ever reading it (`mflr`). Calls to them are emitted as "call, then return".
- **Unknown targets** (a dynamic call or jump to an address that was not recompiled) run in the interpreter until they return. Each address is logged once, to be added to the manifest.

## Verification

| Check | What it compares | Status |
|---|---|---|
| `ctest` / `test_ppc` | 21,600 random cases over 108 instruction forms. Inputs run on QEMU's PowerPC (`tests/ppc/gen.py`, the reference) and through `ppc.h`; every register and the 64-byte memory window are compared. | all pass |
| shadow mode (`--shadow K`) | During a real run, the first K calls of each function execute natively *and* in the interpreter from the same state; registers and all 32 MiB of RAM are compared. Calls that enter the kernel, or spin on hardware that does not exist yet, are rolled back and counted as "impure". | bm3final 153/153, pop'n 4–7 143–151 each: 0 mismatches |
| MAME oracle | Full-system MMIO/frame comparison. | from M2, once devices exist |

Undefined behaviour is deliberately not taken from QEMU. `divw`/`divwu` by zero and `INT_MIN / -1` use a placeholder (`ppc.h`), still to be checked against MAME's PowerPC core.

## Running

```bash
cmake -S . -B build/cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build/cmake                    # recompiler, runtime, tests, every game with a recomp.txt
ctest --test-dir build/cmake            # instruction semantics vs QEMU
build/cmake/hiraku-bm3final --interp-only --shadow 3 --max 4000000000
```

Runner options:
- **`--interp-only`** runs the whole machine in the interpreter. It is a full CPU emulator (exceptions, `rfi`, thread switches), so the multithreaded boot runs; shadow checks still exercise the native code.
- **Without `--interp-only`**, the boot assembly is interpreted and `main` onwards runs natively, until the first thread switch. Running native code across thread switches needs the M2 fiber glue (`docs/KERNEL.md`).

## State at the end of M1 (2026-10-11)

- All five programs translate with no unresolved static control flow except:
  - each program's fall-through after `bl main`, which never returns;
  - one jump table each in pop'n 4 and 5, which uses dynamic dispatch.
- Each program compiles in about 4 s on 16 cores (bm3final: 6.6 MB of C).
- In interpreter mode every program boots through the kernel into its threads (bm3final: 4.0 billion instructions, 3.5M system calls). The game then waits on hardware that does not exist until M2. The only device behaviour stubbed so far is the GCU status register.
