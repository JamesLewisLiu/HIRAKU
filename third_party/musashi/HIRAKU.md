# Musashi in HIRAKU

Musashi (MIT licence, `LICENSE`) at upstream commit `COMMIT`. It is built twice:
- as a plain 68000 for the Firebeat SPU (`runtime/spu/hk_m68kconf.h`);
- as a 68EC020 for the DJ Main board (`runtime/djmain/hk_m68kconf_dj.h`).

## Local changes

Cycle-cost hooks, so a configuration can use MAME's 68020 timing. All default to Musashi's own behaviour, so the 68000 build is unchanged:
- `HK_SHIFT_CYCLES(shift)` replaces `shift<<CYC_SHIFT` in the shift and rotate handlers: 36 sites in `m68kops.c` and `m68k_in.c`.
- `HK_MOVEM_STORE_CYCLES(count, cyc)` replaces `count<<CYC_MOVEM_W/L` in the MOVEM register-to-memory handlers: 12 sites in `m68kops.c`, 2 in `m68k_in.c`.
- `HK_TRAP_EXCEPTION_CYCLES(vector)` replaces `CYC_EXCEPTION[vector] - CYC_INSTRUCTION[REG_IR]` in `m68ki_exception_trap` and `m68ki_exception_trapN`: 2 sites in `m68kcpu.h`.
- `HK_COUNT_RESET_CYCLES`: when 1, `m68k_execute()` counts the reset exception's cycles in what it returns and in `m68k_cycles_run()`. Upstream takes them from the first slice without reporting them, so the caller's clock runs behind by that much: 1 site in `m68kcpu.c`.
- `HK_USE_ALL_CYCLES()` replaces `USE_ALL_CYCLES()` (a branch to itself skipping to its last iteration in the slice). Upstream's `remaining % cost` is 0 when the iterations fit exactly, and the loop then runs once more than fits: `cost` cycles past the end of the slice. 1 site in `m68kcpu.h`.
- `HK_LOOP_CHECKS_FIRST`: when 1, `m68k_execute()` runs no instruction if the interrupt exception it took on entry used up the slice. Upstream's main loop is a do-while, MAME's a while: 1 site in `m68kcpu.c`.
- The defaults are in `m68kcpu.h`.

The DJ Main configuration sets them to MAME's 68020 values:
- shifts cost nothing per bit before the 68030;
- MOVEM stores cost 3 cycles a register.
- a TRAP costs its own 4 cycles plus the whole exception (24 cycles in all, not 20).
- the reset exception's 4 cycles are counted: MAME's first instruction runs at cycle 4.
- an idle loop that ends exactly on a slice boundary stops there, so the vblank interrupt is taken on the same cycle as in MAME.
- an interrupt that arrives while another one's exception is being taken (the vblank just after a disk interrupt) is taken before the first instruction of the first handler.

With Musashi's costs, beatmania ran about 1 % slower than in MAME, and missed a polling window after its disk load.

## Configuration notes

- **A 68EC020 needs `M68K_EMULATE_010` on.** Without it, `m68kcpu.h` defines `CPU_TYPE_IS_010_LESS()` as `CPU_TYPE_IS_EC020_LESS()`, which is true for the EC020 itself. 68020 addressing modes are then decoded as on a 68000: no index scaling, no memory indirect.
- **`m68k_set_irq()` doesn't check for interrupts; only `m68k_execute()` does when it starts.** An interrupt raised by a device in the middle of a timeslice therefore waits for the slice to end. MAME takes it after the current instruction, so the DJ Main board calls `m68k_end_timeslice()` whenever a device changes an interrupt line (`runtime/djmain/board.cpp`, `irq_changed()`).
