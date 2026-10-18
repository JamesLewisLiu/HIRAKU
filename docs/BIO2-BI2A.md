# The BIO2 with BI2A firmware, as IIDX 27 drives it

How beatmania IIDX 27 (LDJ-003-2019090200) talks to a BIO2 board running BI2A firmware, the IIDX
cabinet board HIRAKU's `--bio2` (`runtime/host/bio2.cpp`) drives. IIDX 27 has no libaio: its
`bm2dx.dll` carries its own driver, namespace `BI2A` (`CIOManager`, `CPacketBase` and ten packet
classes, vtables from 0x1805f8070). Worked out statically from that DLL with Ghidra; nothing was
run. Addresses are virtual at image base 0x180000000; `FUN_` names are Ghidra's. Checked against
bemanitools' capture of a real IIDX BIO2's start-up (`doc/dev/journal/2020-12-16-acio-bio2-iidx-package-dump.md`),
which it matches byte for byte. The last section lists where HIRAKU's driver differs.

## The port

| | | evidence |
|---|---|---|
| Device | the COM port whose friendly name contains `BIO2(VIDEO)` (Ports class GUID 4d36e978-e325-11ce-bfc1-08002be10318), its `PortName` | `FUN_180417550` ("[ CheckDevice ]") |
| Line | 115200, 8N1, no flow control, DTR and RTS on; 16 KiB buffers; read interval timeout 4 ms | `FUN_1804177d0` ("[ OpenSerialPort ]") |
| Sync | `SetCommBreak`, 250 ms, `ClearCommBreak`, 200 ms; then rounds of: write `AA` ten times (one byte per write), read bytes; done when ten `AA` in a row come back; at most 100 rounds | `FUN_1804173a0` ("[ BaudRateSetting ]") |

Only 115200 is used. bemanitools' `iidxio-bio2.conf` says the same ("real devices expect 115200").

## Packets

Classic ACIO, `FUN_18041a0c0` (build) and `FUN_1804180a0` (parse):

| byte | host to board | board to host |
|---|---|---|
| 0 | `AA` | `AA` |
| 1 | address: 0 for the first command, then the address the board gave (1) | the address plus 0x80 (`81`) |
| 2, 3 | command, big-endian | the same command |
| 4 | tag: always 0 (`+0x220`, never set) | the same |
| 5 | data length | data length |
| 6.. | data | data |
| last | 8-bit sum of bytes 1 to the end of the data | same |

After the first `AA`, an `AA` or `FF` byte goes as `FF, ~b` (both ways). The reader starts a packet
at each `AA` and has one when it holds `len + 7` bytes with a good sum. An answer is matched to its
request by the command's low byte (`FUN_1804185f0`); an answer with command `0000` and length 1 is
an error answer and fails the request. A request not answered within 20 ms is sent again, and a
fourth miss is an error (`FUN_180418830`).

## The packets IIDX 27 has

Built at start-up (`FUN_180417f30`), index, class, constructor:

| # | class | command | data out | data back | constructor |
|---|---|---|---|---|---|
| 0 | `CPacketIDSettings` | `0001`, address 0 | 1 byte, `00` | 1 byte: the address to use (1) | `FUN_180419c20`, data `FUN_18041a310` |
| 1 | `CPacketGetFirmVersion` | `0002` | none | 0x2C bytes (version 5-7, "BI2A" at 8-11) | `FUN_180419bb0` |
| 2 | `CPacketIoExecEnable` | `0003` | none | 1 byte status | `FUN_180419d00` |
| 3 | `CPacketIoInit` | `0100` | 1 byte, **`2D`** | 1 byte status | `FUN_180419f50`, data `FUN_18041a360` |
| 4 | `CPacketSetWd` | `0120` | 2 bytes, big-endian: watchdog × 100 | 1 byte status | `FUN_180419fc0` |
| 5 | `CPacketIoGetSet` | `0152` | 0x30 bytes, the outputs | 0x2E bytes, the inputs | `FUN_180419de0` |
| 6 | `CPacketIoGetSetWd` | `0153` | 0x30 bytes | 0x2E bytes | `FUN_180419e60` |
| 7 | `CPacketIoGet` | `0110` | none | inputs | `FUN_180419d70` |
| 8 | `CPacketIoGetWd` | `0111` | none | inputs | `FUN_180419ee0` |
| 9 | `CPacketIoCN7Check` | `0154` | none | 1 byte | `FUN_180419c90` |

## Start-up

`FUN_180416710`, a state machine on its own thread (`FUN_180416b10`), one step per state, logging
each:

| state | log | sends | on success |
|---|---|---|---|
| 0 | `[ CheckDevice ]` | (finds the port) | 1 |
| 1 | `[ OpenSerialPort ]` | | 2 |
| 2 | `[ BaudRateSetting ]` | BREAK, then `AA` × 10 until echoed | 3 |
| 3 | `[ IDSetting ]` | `AA 00 00 01 00 01 00 02` | the answer's data byte becomes every later packet's address (`FUN_180418560`); 4 |
| 4 | `[ CheckFirmwareVersion ]` | `AA 01 00 02 00 00 03` | 5 |
| 5 | `[ ChangeCommandMode ]` | `AA 01 00 03 00 00 04` | 6 |
| 6 | `[ SetInitCommand ]` | `AA 01 01 00 00 01 2D 30` | 7 |
| 7 | `[ SetWDCommand ]` | `AA 01 01 20 00 02 hi lo sum`, hi lo = watchdog × 100 | 8 |
| 8 | `[ Boot ] end.` | starts the I/O thread | |
| 9 | `[ Boot ] error end.` | | |

Any failure goes to state 9 and the game shows `ERROR(FM ...)` with the step's name (`FUN_1800dce10`:
SERIALPORT OPEN, DEVICE NOT FOUND, BANDRATE SETTING, COM WRITE, COM READ, ID SETTING, CHECK FIRMWARE,
CHANGE COMMANDMODE, INIT COMMAND, WD SETTING, GETSET IO COMMAND, GETSET WD IO COMMAND, GET IO
COMMAND, GET WD IO COMMAND, CN7 CHECK, TIMEOUT).

**The watchdog.** IIDX 27 starts the driver as `FUN_180415d50(0, 0)` (from `FUN_1800dce10`): mode 0,
watchdog 0. So state 7 sends `0120` with `00 00`, exactly as in bemanitools' capture. A non-zero
watchdog would also switch the poll to `0153`.

**The version check** only asks: `CheckFirmwareVersion` succeeds when `0002` is answered. The 0x2C
bytes are kept (`FUN_180417060`, into `+0x1bc`) and bytes 5-7 are shown as `BI2A:%d.%d.%d` on a debug
screen (`FUN_180415f80`, `FUN_1803a2970`). Nothing compares them, and IIDX 27's `bm2dx.dll` has no
BIO2 firmware writer at all: no libaio import, no write command, no image. A wrong firmware just
fails later. (IIDX 27's `libacio.dll`, which the game uses only for the card readers on their own
port, `ac_io_begin` and the `ac_io_icca_*` calls, does carry an ACIO node firmware updater:
"UpdateFirmwareEnd Node %d", "WriteFlashRAMStart". It never talks to the BIO2.)

## The I/O loop

`FUN_180416e40`, its own thread, as fast as the board answers (each round waits for its answer, then
`Sleep(0)`). The packets per round come from small tables (`{count, index...}`):

| mode, watchdog | table | sends |
|---|---|---|
| mode 0, watchdog 0 (IIDX 27) | 0x1805f80ac | `0152` |
| mode 0, watchdog > 0 | 0x1805f80d8 | `0153` |
| mode 1 | 0x1805f8104 | `0154`, then `0111` |

The game reads a snapshot of the last answer (`FUN_1804166a0`, `FUN_180416d70`) and writes the
outputs into a buffer the next `0152` carries (`FUN_180416de0`, `FUN_180417000`).

### Inputs: the 0x2E bytes back

From IIDX 27's readers: `FUN_180415e00` (buttons, as a 32-bit word), `FUN_180410590` (that word into
the game's), `FUN_180416150` (sliders), `FUN_1804161e0` (turntables), `FUN_180416330` (coins). "Bit"
is spice2x's numbering (byte n/8, bit n%8), which HIRAKU uses.

| byte | bits | meaning | bit | evidence |
|---|---|---|---|---|
| 0, 2, 4, 6, 7 | 7-4 | effector sliders 1-5 (0-15) | | `FUN_180416150` |
| 1 | 1 | coin switch: held for 120 frames is a coin jam error | 9 | `FUN_180415e00` raw bit 22, `FUN_180410450` |
| 1 | 2 | SERVICE (game bit 15) | 10 | raw bit 29 |
| 1 | 3 | TEST (game bit 14) | 11 | raw bit 28 |
| 8 | 5-0 | coin count, 6 bits, wraps at 64 | | `FUN_180416330`: `in[8] & 0x3f` |
| 9 | 4 | EFFECT (game bit 18) | 76 | raw bit 27 |
| 9 | 5 | VEFX (game bit 19) | 77 | raw bit 26 |
| 9 | 6 | 2P START (game bit 17) | 78 | raw bit 25 |
| 9 | 7 | 1P START (game bit 16) | 79 | raw bit 24 |
| 0x10, 0x11 | | turntable 1P, 2P: 8-bit position | | `FUN_1804161e0` |
| 0x12, 0x14, ..., 0x1e | 7 | 1P keys 1-7 (game bits 0-6) | 151 + 16k | raw bits 8-14 |
| 0x20, 0x22, ..., 0x2c | 7 | 2P keys 1-7 (game bits 7-13) | 263 + 16k | raw bits 15-21 |

Which of game bits 18 and 19 is EFFECT and which VEFX isn't stated in the code read; the table
follows spice2x, and the lamps below use the same two bits the same way round, so the pair is at
least consistent.

### Outputs: the 0x30 bytes out

From `FUN_180416220` (lamps, from the game's 32-bit lamp word masked with 0xf3fff by `FUN_180410560`),
`FUN_180415fe0`, `FUN_180416450` (spotlights), `FUN_1804164e0` (neon), `FUN_180416040` (ticker),
`FUN_180416330` (coin meter). A lamp is on when its byte's **bit 7** is set; IIDX writes `0x80`.

| byte | contents | evidence |
|---|---|---|
| 0 bits 5-0 | coin meter: a running 6-bit count; IIDX adds at most 1 a frame per coin still to count, when metering is on (`FUN_180416030`) | `FUN_180416330` |
| 2 bit 7 | game lamp-word bit 14, set and cleared as the game enters and leaves its modes (`FUN_180410410`, `FUN_1800dd360`); probably the coin blocker (set = accept), **unsure** | `FUN_180415fe0` |
| 3 | 1P START lamp (game bit 16) | `FUN_180416220` |
| 4 | 2P START lamp (bit 17) | |
| 5 | VEFX lamp (bit 19) | |
| 6 | EFFECT lamp (bit 18) | |
| 7-0x0d | 1P key lamps 1-7 (bits 0-6) | |
| 0x0e-0x14 | 2P key lamps 1-7 (bits 7-13) | |
| 0x17-0x1f | the ticker, 9 bytes as the game gives them | `FUN_180416040` |
| 0x20-0x23 | spotlights 1-4 (argument bits 0-3) | `FUN_180416450` |
| 0x24 | neon | `FUN_1804164e0` |
| 0x25-0x28 | spotlights 5-8 (argument bits 4-7) | `FUN_180416450` |

Bytes 1, 0x15, 0x16 and 0x29-0x2f are never written by IIDX 27 (zero).

## Never send (BI2A)

IIDX 27 can't rewrite the board, but IIDX 28's `libaio-iob.dll` can (its `BI2AWRFIRM`, for a board
that enumerates as `1ccf:804C`, `FUN_18001ace0`): it sends `0001`, `0002`, then **`0004`** to node 1,
and goes on with raw loader packets starting `A1` and `A8` (`FUN_18001a440`, which escapes bytes
`Ax` and `FF`). So:
- never send `0004` (most likely "enter the loader"), nor any command IIDX 27 doesn't send;
- never send a packet starting `A1`..`AF`;
- never talk to a `1ccf:8040` device (the BIO2 loader; HDLC-style frames, see docs/BIO2-BI2X.md).

The commands above (`0001`, `0002`, `0003`, `0100`, `0120`, `0152`, and `0110`, `0111`, `0153`,
`0154`) are the whole of what IIDX 27 sends.

## HIRAKU's bio2.cpp against this

Discrepancies, by file and line, with the change proposed (not applied):

1. **`0100` without its data byte.** `runtime/host/bio2.cpp:67` sends `0x0100` with `{}`; IIDX 27
   sends `{0x2D}` (`FUN_18041a360`), and bemanitools found the sub-I/O (panel, lamps) stays dead
   without it. Change to `port.ask(1, 0x0100, {0x2d}, m)`.
2. **No watchdog command.** IIDX 27 sends `0120` with `{0x00, 0x00}` after `0100` and before the
   first poll (state 7). Add `port.ask(1, 0x0120, {0x00, 0x00}, m)` to the same line's chain.
3. **The sync.** `runtime/host/acio.cpp:252-265` (`handshake`) writes 525 zeros and 40 `AA` and
   accepts any reply ending `AA`, at each of four rates (`bio2.cpp:50` passes 115200, 57600, 38400,
   19200). IIDX 27: a 250 ms BREAK, 200 ms, then `AA` × 10 until ten `AA` come back, 115200 only. For
   the BIO2, add a BIO2 handshake: the BREAK (`tcsendbreak(fd, 0)`, 250 ms on Linux, or
   `ioctl(fd, TIOCSBRK)`, 250 ms, `ioctl(fd, TIOCCBRK)`; Windows `SetCommBreak`/`ClearCommBreak`),
   200 ms, then up to 100 rounds of ten `AA` and read until ten `AA` come back; and pass only
   `{115200}`. Keep the zeros-and-`AA`
   form for the card readers.
4. **Each poll costs at least 30 ms.** `acio.cpp:228-241` (`read_some`) keeps reading until the line
   has been quiet 30 ms, and `receive` (`acio.cpp:286`) parses only after that, so `bio2.cpp:74`'s
   loop runs at about 30 Hz with 30 ms of added input latency. IIDX 27 parses as bytes arrive and
   takes the answer at once (20 ms timeout, three resends). Make `receive` stop as soon as a whole
   packet with a good sum is in (it knows the length from byte 5), and give the poll a 20 ms wait
   with three tries before calling the board lost.
5. **Lamps written as `0xFF`.** `bio2.cpp:159` sets a lit lamp's byte to `0xff`; IIDX 27 sets bit 7
   only (`0x80`). The board may well read only bit 7, but the other bits are unaccounted for: write
   `0x80`.
6. **The coin count is 6 bits.** `bio2.cpp:190` takes `in[8] - last[8]` over the whole byte (and
   ignores jumps of 16 or more); IIDX 27 reads `in[8] & 0x3f`, which wraps at 64, so the wrap from 63
   to 0 reads as -63 and drops a coin, and bits 6-7 (unknown) would count as coins. Use
   `uint8_t d = (in[8] - last[8]) & 0x3f`.
7. **The address.** `bio2.cpp:51` and on address node 1; IIDX 27 uses the data byte of the `0001`
   answer. With one BIO2 it is 1 (bemanitools' capture), so this only matters if it isn't; reading
   it costs nothing: `const uint8_t node = m.payload.empty() ? 1 : m.payload[0];` after the `0001`.
8. **Tags.** `acio.cpp:268` numbers packets `++id_`; IIDX 27 always sends 0. Harmless as far as is
   known (the board echoes the tag); sending 0 would match exactly.
9. **Byte 2 bit 7** (probably the coin blocker) is always 0 from HIRAKU; if it is the blocker and 0
   means blocked, coins are refused. To check on the board: insert a coin with it 0, then `0x80`.
10. **The coin meter** (byte 0) is never advanced; optional, for operators who want the counter.
11. **The comment** at `runtime/host/bio2.h:16-17` and PLAN.md section 25's bring-up list ("the 0120
    watchdog the game sends; its payload is unknown"): the start-up is now known (`0001 [00]`, `0002`,
    `0003`, `0100 [2D]`, `0120 [00 00]`, then `0152`).

What matches: the `0152` lengths (0x30 out, 0x2E back), the input layout (keys, START, VEFX, EFFECT,
TEST, SERVICE, sliders, turntables at 0x10/0x11), the output layout (panel 3-6, keys 7-0x14, ticker
0x17-0x1f, spotlights 0x20-0x23 and 0x25-0x28, neon 0x24), the version parsing (bytes 5-7, "BI2A" at
8-11), 115200 as the first rate, 8N1, the 0001-0002-0003-0100 order. Still only the board can tell:
the turntables' direction and counts per turn, and byte 2 bit 7.
