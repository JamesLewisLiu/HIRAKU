# Musashi in HIRAKU

Musashi (MIT licence, `LICENSE`) at upstream commit `COMMIT`. It is built twice:
- as a plain 68000 for the Firebeat SPU (`runtime/spu/hk_m68kconf.h`);
- as a 68EC020 for the DJ Main board (`runtime/djmain/hk_m68kconf_dj.h`).

## Local changes

Two cycle-cost hooks, so a configuration can use MAME's 68020 timing. Both default to Musashi's own costs, so the 68000 build is unchanged:
- `HK_SHIFT_CYCLES(shift)` replaces `shift<<CYC_SHIFT` in the shift and rotate handlers: 36 sites in `m68kops.c` and `m68k_in.c`.
- `HK_MOVEM_STORE_CYCLES(count, cyc)` replaces `count<<CYC_MOVEM_W/L` in the MOVEM register-to-memory handlers: 12 sites in `m68kops.c`, 2 in `m68k_in.c`.
- The defaults are in `m68kcpu.h`.

The DJ Main configuration sets them to MAME's 68020 values:
- shifts cost nothing per bit before the 68030;
- MOVEM stores cost 3 cycles a register.

With Musashi's costs, beatmania ran about 1 % slower than in MAME, and missed a polling window after its disk load.

## Configuration notes

- **A 68EC020 needs `M68K_EMULATE_010` on.** Without it, `m68kcpu.h` defines `CPU_TYPE_IS_010_LESS()` as `CPU_TYPE_IS_EC020_LESS()`, which is true for the EC020 itself. 68020 addressing modes are then decoded as on a 68000: no index scaling, no memory indirect.
