# The BIO2 with BI2X firmware: ACIO2 and IIDX's TDJ I/O

How beatmania IIDX 28's I/O library drives a BIO2 board (USB `1ccf:8050`, product "BI2X") as the
Lightning Model (TDJ) cabinet's controls. Worked out statically from IIDX 28's DLLs
(LDJ-003-2020092900): `libaio-iob.dll` (the node manager `AIO_NMGR_IOB2`, the packet layer
`AIO_NODE_IOB2_COMMPKT`, the base class `AIO_IOB2_BI2X`, the firmware writers), `libaio-iob2_video.dll`
(`AIO_IOB2_BI2X_TDJ`), `libaio.dll` (serial ports) and `bm2dx.dll` (the game's calls). None of them
was run. Addresses are virtual addresses at the DLLs' image base 0x180000000; `FUN_` names are
Ghidra's. Where this disagrees with spice2x's `acio2emu/`, libaio is the authority.

Status: the framing, CRCs and obfuscation are settled (they decode the board's own answer of
2026-10-18 with both CRCs right); the start-up, the firmware-write paths, the TDJ commands and their
byte layouts are read from the code. Not settled: whether the board needs Konami's TDJ module
uploaded before it does any I/O (it looks so, see "Modules"), what several bits mean, and whether
the board insists on obfuscated host packets. Read-only tests that settle the first and the last are
at the end.

**Never send** is its own section below; read it before writing any code that talks to the board.

## The link

| | | evidence |
|---|---|---|
| Device | USB CDC ACM, VID 1CCF PID 8050, opened by a `"%04X%04X_"` id match | `AIO_IOB2_BI2X::OpenSciUsbCdc` 0x1800175c0 |
| Line | 115200 baud (0x1c200), 8 data bits, no parity, 1 stop bit | same |
| Reset | at start the node manager holds a line BREAK for 2750 ms, releases it, waits 5500 ms, then reopens the port (the board re-enumerates, presumably) | `FUN_1800117b0` (state 0 of the manager loop `FUN_180016890`) |

`AC_TIME::GetTime()` is taken to count milliseconds (0x14 = 20 between sync pings, 500 for a quiet
line, 1000 and 5000 for long commands all read sensibly as ms). The BREAK is libaio's habit, not a
requirement: the board answered our 2026-10-18 probe without one. A BREAK resets the board, so it
also unloads any module (below).

## Packets

The same layout both ways. Packet memory layout in libaio: `+0x18` 0xAA, `+0x19` node byte, `+0x1a`
tag, `+0x1b` number of size bytes, `+0x1c` size (u32), `+0x20` flags byte, `+0x21` payload.

| byte | contents | evidence |
|---|---|---|
| 0 | `AA`, start of packet. Never appears anywhere else in a packet | `Send` 0x180012190 |
| 1 | node byte: host to board `index*2`; board to host `index*2+1` (the master, index 0, answers `00`) | `PacketCreate` 0x180011fc0; `CheckRecvPkt` 0x1800114f0 rejects an even node byte other than 0; index = `byte>>1 & 0x3f` (`FUN_180016cb0`) |
| 2 | tag. The host takes a fresh one per request: the last tag + 1, skipping 00, AA, FF and tags still in flight. The answer carries the request's tag; requests and answers pair by (node index, tag) | `AIO_NMGR_IOB::GetTag` 0x180002120, `FUN_180016cd0` |
| 3.. | payload size, 1 to 5 bytes: for each of bits 25, 19, 13, 7 whose 6-bit group `(size>>n)&0x3f` isn't zero, a byte `0xC0 \| group`; then `size & 0x7f`. The reader shifts in 6 bits per `11xxxxxx` byte (at most 4) and 7 bits from the last (`0xxxxxxx`) byte | `Send`; `PacketRecv` 0x180012550 state 3 |
| next | flags: `mode<<5 \| obfuscated<<4 \| crc4` | `Send` |
| next | payload, encoded by mode (below), obfuscated if the flag says so | |
| last | `crc7(payload) ^ 0x7f`, over the plain (decoded) payload; obfuscated with it. A size-0 packet has no payload and no CRC7 byte | `QueryDataSum` 0x1800112d0; `PacketRecv` |

Note a group of zero in the middle is skipped (libaio's encoder), so sizes of 8192 and up are
ambiguous; nothing sent here comes near that (the largest is 263 bytes: `C2 07`).

**CRC4** (`AC_CRC::Crc4_MakeLGP_C`, init 0x0F, result `^ 0x0F`) runs over the node byte, the tag,
each size byte as sent (`QueryHeadSum` rebuilds them as `(size>>n)|0xC0`) and `flags & 0xF0`
(`QueryHeadSum` 0x180011180). **CRC7** is `Crc7_MakeLGP_48`, init 0x7F, result `^ 0x7F`. Both are
the tables in spice2x's `acio2emu/internal/crc.h` and `tools/bio2/bi2x_version.py`; they check out
on the board's packets.

### Payload modes (flags bits 7-5)

The sender picks the mode per packet (`FUN_180011300`):

| mode | name | encoding | host sends it | host accepts it |
|---|---|---|---|---|
| 0 | stuffed | each payload `AA` or `FF` goes as `FF, ~b` | when every byte value 0..255 is used (never in practice) | yes |
| 1 | (discarded) | raw | never | the packet is dropped after it is read (`PacketRecv`: `(flags & 0xE0) == 0x20`) |
| 2 | raw | payload as is | when the payload has no `AA` | yes |
| 3 | replace | first byte after the flags is a substitute value (one not in the payload); each `AA` goes as it | when the payload has an `AA` | yes |
| 4 | LZ | compressed (`MC_LZ_DEFLATE`; spice2x's `internal/lz.h` is the inflater) | for payloads of 7 bytes and up, when it comes out shorter than mode 2/3 | no: `PacketRecv` rejects modes above 3 |

The board accepts mode 0 (our probe packets were mode 0). HIRAKU should send mode 0 or 2 only and
must accept 0, 2 and 3 (the board's version answer was mode 2) and ignore mode 1.

### Obfuscation (flags bit 4)

One function scrambles and unscrambles (`FUN_18000f8e0`), with a 32-bit LCG state:

```
xf(b):  if (b & 0xAA) == 0xAA: return b            # bits 1,3,5,7 all set: untouched, state not stepped
        state = state * 1103515245 + 12345         # mod 2^32
        return b ^ (state & (0x55 if b & 0x80 else 0x7F))
```

A byte with bit 7 clear gets bits 0-6 changed; a byte with bit 7 set gets only bits 0, 2, 4 and
6 changed. So bit 7 survives, bits 1, 3, 5 of a bit-7 byte survive, the "untouched" test gives the
same answer on both sides, `AA` and `FF` pass through, and the function is its own inverse. It runs over every byte after the flags byte: the
substitute byte (mode 3), the encoded payload (escape bytes included: the escape `FF` passes
unchanged, the escaped `~b` is scrambled) and the CRC7 byte. The receiver frames on raw `AA` first,
then unscrambles (`PacketRecv`, `LAB_1800127b4`).

| direction | seed | evidence |
|---|---|---|
| host to board | `tag ^ 0x55` | `Send`: `*(state) = tag ^ 0x55` |
| board to host | `tag ^ 0xAA` | `PacketRecv` state 4: `*(state) = byte[0x9de] ^ 0xAA`, 0x9de being the tag |

That explains 2026-10-18's "seed 43": the version answer had tag 01, seed `01 ^ AA = AB`, and only
the low 7 bits of the state reach a byte with bit 7 clear (171 ≡ 43 mod 128). With the 0x55 mask
for bytes with bit 7 set, the answer's one such byte decodes too; both CRCs then pass.

**When the host obfuscates:** libaio keeps a per-node flag (a vector of 8-byte entries at
`AIO_NMGR_IOB2+0x570`, one per node, `+0` enabled, `+4` LCG state). It starts clear; the first
obfuscated, non-empty answer from a node that isn't a `00 7F` message sets it (`PacketRecv`,
`**(this+0x580) = 1`), and from then on every non-empty packet the host sends that node is
obfuscated (`Send`, `this[0x20] |= 0x10`). So libaio's own first packets to the BI2X node (the
version request) go plain, and everything after the version answer goes obfuscated. Whether the
board refuses plain packets after that is **unknown** (test B below).

### Unsolicited packets

The host drops mode 1 packets and packets whose payload starts `00 7F` (`PacketRecv`, the
`FUN_180016dc0` branch). A reader must skip them rather than take them as an answer.

### The board's version answer, decoded

The 2026-10-18 answer `AA 03 01 23 5C` + 35 bytes + CRC7: node 1, tag 01, size 0x23, flags 0x5C =
mode 2, obfuscated, CRC4 0xC. Unscrambled with seed 0xAB:

```
00 02 00 | 0D 06 00 01  00 01 03 03  42 49 32 58  01 A9 99 A4  00 00 00 33  00 00 00 00
           00 00 00 00  00 00 00 00  00 00 F0 2E
```

`00 02 00` is the echoed command and status 0; the 32 bytes after it are the node's **FIRMINFO**
(`AIO_NMGR_IOB::FIRMINFO::SetFirmInfo`, `FUN_180013ed0`). What libaio reads in it:

| FIRMINFO bytes | this board | IIDX 28's image | used by |
|---|---|---|---|
| 0-3 | `0D 06 00 01` | `0D 06 00 01` | the node type, BI2X (`AIO_IOB2_BI2X` constructor passes 0x0D060001, 0x180017310) |
| 5-7 | `01 03 03` | `01 02 03` | the version, shown as `%u.%u.%u` by the game (`FUN_1806289b0` reads the node controller's `+0xa0..a2`, copied from here by `FUN_180001ba0`) |
| 8-11 | `BI2X` | `BI2X` | |
| 0x10-0x1d | `00 00 00 33` + ten `00` | twelve `00` + `27 7D` | `CheckFirmware` compares these 14 bytes with the class's expected ones |
| 0x1e-0x1f | `F0 2E` | `8E E2` | the firmware writer's key (below) |

So this board runs BI2X 1.3.3, newer than the 1.2.3 that IIDX 28 carries, and IIDX 28's TDJ class
would refuse it (`CheckFirmware` wants bytes 5-6 = `01 02`, error -0x613 in `FUN_180001ba0`). It was
flashed by a later game.

## Commands

A payload is one or more commands. Each starts with two bytes: a target (0 = the board's system,
otherwise a loaded module's id) and a command number. The answer echoes the two bytes, adds a status
byte (0 = done), then any data (the master's `00 01` answer alone has no status byte). Several
commands may share a packet and the answer then holds the answers in the same order (TDJ's poll
packet, `FUN_18000b990`). spice2x's `acio2emu/firmware/bi2x.cpp` numbers them as one 16-bit value
(2 = `00 02`, 784 = `03 10`).

Every request is queued with a retry count (3), an expected answer size (used for the receive
timeout, `AIO_NODE_IOB2_COMMPKT::QueryRecvTimeout` 0x180011590: the time to send the bytes at the
line rate plus a per-command slack, 2 ms for I/O, 1000 ms for a module start, 5000 ms for a firmware
erase) and flags (TDJ's tape LED packets pass 3: no retry, as far as `mgrPacketSend` shows).

| command | sent by | payload after the 2 bytes | answer after `cc cc 00` | spice2x |
|---|---|---|---|---|
| `00 01` | master (node 0): assign nodes | (none) | no status byte: `00 01` then one byte per node | 1 |
| `00 02` | each node: FIRMINFO | `81` (libaio; our probe sent nothing and was answered) | 32 bytes FIRMINFO | 2 |
| `00 10` | module: allocate | size, u32 big-endian | handle (1 byte) | 16 |
| `00 13` | module: write | handle, offset (u32 BE), up to 64 bytes | (none) | 19 |
| `00 78` | module: start | handle, `00 00 00 00` | module id (1 byte) | 120 |
| `00 70` | **firmware: begin (erase?)** | (none) | (none) | not in spice2x |
| `00 71` | **firmware: write** | offset (u32 BE), up to 256 bytes | (none) | not in spice2x |
| `mm 10` | TDJ: poll inputs | (none) | 74 bytes | 784 |
| `mm 11` | TDJ: outputs | 17 bytes | (none; status 0 or 1) | 785 |
| `mm 12` | TDJ: I/O reset | 4 bytes | (none) | 786 |
| `mm 20` | TDJ: tape LED gamma | 256 bytes | (none) | 800 |
| `mm 21` | TDJ: tape LED data | channel, offset (u16 BE), count (u16 BE), count × 2 bytes | (none) | 801 |
| `mm 22` | TDJ: tape LED commit | (none) | (none) | 802 |

`mm` is the TDJ module's id, the answer to its `00 78` (spice2x answers 3; TDJ keeps it at
`this+0x121` and puts it in front of every I/O command, `FUN_18000b990`, `FUN_18000c120`).

## The start-up, in order

The manager thread (`AIO_NMGR_IOB::ThreadProc` 0x180003e80 → `FUN_180016890`):

| step | what | detail | evidence |
|---|---|---|---|
| 0 | line reset | BREAK 2750 ms, release, wait 5500 ms, reopen | `FUN_1800117b0` |
| 1 | open | reopen the port, up to 10 tries | `FUN_180011b90` |
| 2 | sync | an empty packet to the master, `AA 00 tag 00 crc4`, every 20 ms, up to 450 times, until the same empty packet comes back (same tag, size 0); then read until the line has been quiet 500 ms | `FUN_180013210` |
| 3 | assign | `00 01` to node 0, 3 tries. Answer `00 01` then one byte per node, no status byte (this board: `00 01 20`). A node byte with low nibble above 1 is an error (-0x482); TDJ refuses low nibble 1 (-0x616) | `FUN_180013780`, `FUN_18000b070` |
| 4 | identify | `00 02 81` to each node, answer at least 0x23 bytes, 3 tries; FIRMINFO kept per node | `FUN_180013ed0` |
| 5 | wait | until the game calls `BeginManage` (vtable +0x50; IIDX 28 does right after `aioIob2Bi2xTDJ_Create`) | `FUN_180016890` state 5, `FUN_1806264b0` |
| 6 | firmware | the in-band firmware check and write, see Never send | `FUN_180014730` |
| 7 | run | each node controller's step, every cycle | `mgrProcNodeCtlPktSendRecv` |

The TDJ node controller (`aioIob2Bi2xTDJ_Create(nmgr, 1)`, node index 1; vtable 0x180023d58):

| step | what | evidence |
|---|---|---|
| attach | finds its node record (type 0x0D060001, its index), copies it, calls `CheckFirmware` (FIRMINFO 5-6 = `01 02` and 0x10-0x1d = the 14 expected bytes) or fails with -0x613 | `FUN_180001ba0`, `AIO_IOB2_BI2X::CheckFirmware` 0x1800172b0 |
| modules | loads two modules, the TDJ one from `libaio-iob2_video.dll` (descriptor 0x18002b270: 0x1dae bytes packed, 0x2421 unpacked; its id goes to `this+0x121`), then the "SCI" one from `libaio-iob.dll` (`GetModuleDataSci` 0x180016f90: 0x3f4 packed, 0x455 unpacked; id to `this+0x123`) | `FUN_180009c50`, `AIO_NCTL_IOB2::LoadModule` 0x18000fc90 |
| I/O | the poll loop below, plus the gamma table and the tape LEDs | `FUN_18000cb30` |

A module load is: `00 10 size` → handle; `00 13 handle offset data` in 64-byte chunks; `00 78 handle
00 00 00 00` (1000 ms) → module id. Each step 3 tries. The module blob in the DLL is a 4-byte
big-endian unpacked size, then bytes XORed with a running key (`k = 0`; `plain = b ^ k`;
`k = ~ror8(plain, 1)`) and packed with a 4 KiB LZSS (window 0x1000 filled with 0, write position
0xFEE, flag byte LSB first, 1 = literal, 0 = two bytes `v = b0<<8 | b1`, position `v & 0xfff`,
length `(v >> 12) + 3`): `LoadModule`, `AC_LZ_INFLATE::PutChar` in `libacc.dll` 0x180049ff0. The
board receives the unpacked bytes. The TDJ module's first bytes include the tape LED channel lengths
(38, 45, 45, 21, 130, 34, 197, 197), so it is the I/O handler.

**What this means for HIRAKU:** the TDJ I/O commands go to the module's id, so the board most likely
does no TDJ I/O until a host uploads the module, after every reset. The module is Konami's code
(about 9 KiB); HIRAKU can't ship it, only read it from the user's own `libaio-iob2_video.dll` and
unpack it as above. And the one module at hand, IIDX 28's, was made for firmware 1.2, while this board
runs 1.3.3, a combination even IIDX 28 never tries (it stops at `CheckFirmware`). Module memory is
RAM as far as anyone can tell (every game reloads it at every start, after a reset), so a crash
should end at a power cycle, but that is a guess. Test C below asks the board first whether it
already answers `03 10`.

## Never send

These are the paths that write a board's firmware. HIRAKU must never send any of them, nor open a
device that a firmware writer would open.

**ACIO2, in band (node 0 commands to a BI2X node):**
- `00 70` (begin; 5 s timeout) and `00 71 offset data` (256-byte blocks), sent by the node manager's
  firmware step `FUN_180014730` (states 4-7). Treat every `00 7x` command as off limits: `00 78`
  is the module start (it runs code, see Modules), `00 7F` is a message from the board, and the rest
  are unknown neighbours of the firmware commands.
- `00 10`, `00 13`, `00 78` write and start code in the board. Not firmware as far as the code shows,
  but only ever with a module known to match the board's firmware.

**When IIDX 28 writes firmware in band** (`FUN_180014730`), for each node and each firmware image
registered with `AIO_NMGR_IOB2::FIRMDATA` (libaio-iob registers one BI2X image, version 1.2.3, header
at 0x18002d580), it writes when all of these hold:
1. node FIRMINFO 0-3 = the image's (0x0D060001);
2. node FIRMINFO 0x1e-0x1f = the image's (`8E E2`), **or** the node looks like a blank or old board
   (`FUN_180012460`: FIRMINFO 0-3 = 0x0D060001, 5-6 below 0x0102, 0x1c-0x1d = 0, 0x1e-0x1f = `F0 2E`);
3. a BI2X node controller (TDJ, TCS, SA1, S36) was created for that node;
4. node FIRMINFO ≠ the image's 32 bytes, **or** FIRMINFO 0x10-0x1d ≠ the controller's expected 14
   bytes (`plVar6 + 0x1e`, i.e. `this+0xf0`);
5. the controller's `FirmPrgSetExFlags` agrees (the base class's, 0x1800173b0, always does).

So a game whose libaio carries a different BI2X image rewrites any board with the same key, older or
newer (a downgrade is as likely as an upgrade). Asked without `81` this board's key read `F0 2E`;
asked as libaio asks (`00 02 81`, test A below) it reads `8E E2`, IIDX 28's own key, with version
1.3.3 against the image's 1.2.3, so **IIDX 28 would most likely rewrite (downgrade) it**. Running any
game's I/O library against the board is how it gets rewritten: don't.

**USB bootloader** (outside ACIO2): at every boot IIDX 28 calls
`aioIob2Bi2x_CreateWriteFirmContext(0, 1)` (`FUN_180629770`, from `FUN_180627b30`). It looks for a
CDC device by VID/PID from a table at 0x180048e40: value 1 in the second argument selects PID
0x8040 (`BIO2WRFIRM`, the board's loader, which names itself "bio2", "bio2wrfirm" or "bio2boot2nd"),
value 2 PID 0x804C (`BI2AWRFIRM`, a board running BI2A). Found, it writes over that port; not
found, the state goes straight to 7, "completed" (`AIO_IOB2_BI2X::CreateWriteFirmContext`
0x180018580). This board is 0x8050, so nothing happens. Never open a `1ccf:8040` or `1ccf:804C` device as a BI2X board.
- The 0x8040 loader protocol (`FUN_180019200`, `FUN_1800199c0`): frames `7E ... 7F` with `7D, ~b`
  escaping and a CRC16 (poly 0x1021, init 0xFFFF); commands 0x01 (identify), 0x0F, 0x0C (erase),
  0x0D (1 KiB writes), 0x41 (verify/commit). Never send any of it.
- The BI2A path (`FUN_18001ace0`): ACIO `0001`, `0002`, then **`0004`** to node 1, then raw loader
  packets starting `A1` and `A8` (`FUN_18001a440`). See docs/BIO2-BI2A.md.

## The TDJ I/O loop

`FUN_18000cb30`, every manager cycle once the modules are in:
- **Poll** (`FUN_18000b990`): one packet to node 1, in this order, each part only when pending:
  `mm 12` + 4 bytes (an I/O reset is pending), `mm 11` + 17 bytes (the outputs changed), `mm 22`
  (tape LEDs to commit), and always `mm 10`. Expected answer 77 bytes plus 3 per extra part. The
  `mm 10` answer's 74 bytes go to `this+0x18c`; `mm 11` status 0 marks the outputs sent, status 1 leaves them
  pending (sent again next time). 3 tries, then error -0x641.
- A new poll only goes out after the game has taken the last one (`aioNodeCtl_UpdateDevicesStatus`,
  the vtable's slot 1, `FUN_18000a140`, sets the pending bits; `FUN_18000b220` consumes them). IIDX 28
  calls it each time it reads its I/O (`FUN_180626640`), so the poll rate follows the game's.
- **Gamma** (once at start): `mm 20` + 256 bytes, `table[i] = (int)((1 - powf(1 - i/255, 0.5)) * 255)`
  (`AIO_IOB2_BI2X::GenerateTapeLedGammaTable` 0x180016fa0, gamma 0.5 from 0x18001e758, constants at
  0x180048c80).
- **Tape LEDs** (`FUN_18000c120`): when tapes changed, packets of `mm 21` parts up to 263 bytes each
  (so at most 128 LEDs a part), separate from the poll and only between polls; then a `mm 22` rides
  on the next poll. A tape packet answered while a poll was pending pauses the tape data for the next
  two updates (`this+0x13ed`).

At start every tape is marked changed and the output flags are 0x37 (poll, outputs, gamma, tapes),
all outputs zero (`AIO_IOB2_BI2X_TDJ` constructor 0x180009e00).

### Inputs: the 74-byte poll answer

The game reads a 0xCA-byte `DEVSTATUS` (`GetDeviceStatus` 0x18000a0d0 copies `this+0xd80`), built by
`FUN_18000a140` from the last answer (`raw`). Names in the last column are spice2x's
(`games/iidx/bi2x_hook.cpp`, which writes `buffer[n]`); IIDX 28's own reader (`FUN_180628c60`) uses
DS 4-15 as 12 buttons, 20-21 as the turntables, 22 as the coin count and 27-40 as the 14 keys.

| DEVSTATUS | from | meaning |
|---|---|---|
| 0 | | count of polls answered (`this+0x189`) |
| 1 | | count of outputs acknowledged (`this+0x18a`) |
| 2 | | count of tape LED commits (`this+0x18b`) |
| 3 | raw 0 | unknown |
| 4, 5, 6, 7 | raw 1 bits 0, 1, 2, 3 | Test, Service, coin mech, unknown |
| 8-15 | raw 2 bits 0-7 | 1P Start, 2P Start, VEFX, EFFECT, 1P headphone, 2P headphone, unknown, unknown |
| 16-19 | raw 3 bits 0-3 | unknown |
| 20, 21 | raw 4, raw 5 | turntable 1P, 2P: an 8-bit position. IIDX 28 takes the change since the last frame, folded into -128..128 |
| 22 | raw 6 | coin count, 8-bit running; taken only when two answers in a row agree (`this+0x13ec`). IIDX 28 adds the change to the coin counter (`AddCounter`) |
| 23, 24, 25, 26 | raw 7 bits 0, 1, 6, 7 | unknown |
| 27-33 | raw 8 bits 0-6 | 1P keys 1-7 |
| 34-40 | raw 9 bits 0-6 | 2P keys 1-7 |
| 0x29-0x72 | raw 0-73 | the whole answer |
| 0x73-0x83 | | the 17 output bytes last acknowledged |
| 0x84-0x87 | | the 4 I/O reset bytes last acknowledged |
| 0x88-0xc9 | raw 8-73 | raw 8 onwards again |

raw 10-73 (64 bytes) aren't decoded by libaio or the game code read here. That raw 8-73 is copied
whole suggests key-state history (33 pairs of 1P/2P key bytes, a sample a millisecond?); a guess.

### Outputs: the 17 bytes of `mm 11`

Staged at `this+0xe4a` by the setters (`AIO_IOB2_BI2X_TDJ::...`), sent when they differ from the
last acknowledged copy. RGB values are the game's `0xRRGGBB` cut to RGB555,
`(r>>3)<<10 | (g>>3)<<5 | (b>>3)`, **big-endian** here.

| byte | contents | setter |
|---|---|---|
| 0 | watchdog (u8). 0 at start; IIDX 28 sets 0xFF once the I/O is up (`FUN_1806289b0`). Units and effect unknown | `SetWatchDogTimer` 0x18000a6e0 |
| 1 | bit 0 coin blocker (`ControlCoinBlocker(0, on)`; spice2x: 0 = blocked), bit 1 1P Start lamp, bit 2 2P Start lamp, bit 3 VEFX lamp, bit 4 EFFECT lamp | 0x18000a740, `SetStartLamp` 0x18000a820, `SetVefxButtonLamp` 0x18000a8b0, `SetEffectButtonLamp` 0x18000a910 |
| 2 | coin counter: a running u8 total, `AddCounter(0, n)` adds n (presumably the board pulses the meter for each step) | `AddCounter` 0x18000a7b0 |
| 3-4 | woofer LED RGB555 | `SetWooferLed` 0x18000ab70 |
| 5 | 1P key lamps, bit k = key k+1 | `SetPlayerButtonLamp(0, k, on)` 0x18000a970 |
| 6-7 | 1P card reader LED RGB555 | `SetIccrLed(0, rgb)` 0x18000ac10 |
| 8-9 | 1P turntable LED RGB555 | `SetTurnTableLed(0, rgb)` 0x18000ad20 |
| 10 | 1P turntable resistance (u8) | `SetTurnTableResist(0, r)` 0x18000ae30 |
| 11 | 2P key lamps | `SetPlayerButtonLamp(1, ...)` |
| 12-13 | 2P card reader LED RGB555 | `SetIccrLed(1, ...)` |
| 14-15 | 2P turntable LED RGB555 | `SetTurnTableLed(1, ...)` |
| 16 | 2P turntable resistance | `SetTurnTableResist(1, ...)` |

**`mm 12`, I/O reset:** 4 bytes, a big-endian u32 of reset bits: `IoReset(bits)` ORs them in and
the next poll sends them once, then they clear (`IoReset` 0x18000a640, `FUN_18000a140`). IIDX 28 sends
`IoReset(0x11)` when it resets its turntable and coin state (`FUN_180628af0`). What the bits reset is
unknown.

### Tape LEDs

`SetTapeLedData(tape, rgb)`: 3 bytes per LED, R, G, B (IIDX 28 unpacks `0xRRGGBB`, `FUN_1806288b0`).
libaio turns each LED into RGB555, **little-endian** (`AIO_IOB2_BI2X::SetTapeLedData` 0x180017520),
reversing the order for tapes 1, 6, 8, 9, 11, 13, 14 and 16 (`AIO_IOB2_BI2X_TDJ::SetTapeLedData`
0x18000af10). The 17 tapes sit in a 707-LED array (table 0x18002b280) on 8 board channels (starts at
0x180023ba0):

| tape | LEDs | channel, first LED | spice2x's name |
|---|---|---|---|
| 0, 1 | 19, 19 | 0: 0, 19 | stage left, right |
| 2 | 45 | 1: 0 | cabinet left |
| 3 | 45 | 2: 0 | cabinet right |
| 4 | 21 | 3: 0 | control panel under |
| 5, 6, 7, 8 | 54, 11, 11, 54 | 4: 0, 54, 65, 76 | ceiling left, title left, title right, ceiling right |
| 9, 10 | 17, 17 | 5: 0, 17 | touch panel left, right |
| 11, 12, 13 | 68, 68, 61 | 6: 0, 68, 136 | side panel left inner, left outer, left |
| 14, 15, 16 | 68, 68, 61 | 7: 0, 68, 136 | side panel right outer, right inner, right |

Before sending, the whole array is dimmed when it is too bright (`SetTapeLedDataLimited`
0x180017060, with `SetTapeLedDataLimit(scale, limit)` defaults 0xFF and 0x55): with
`m = Σ(r5+g5+b5) * scale / (707 * 93)` (the mean brightness on 0..255), every component is multiplied
by `f/256` where `f = scale + 1`, or `limit * f / m` when `m > limit`. This limits the current the
tapes draw; HIRAKU should keep it. A `mm 21` part carries `channel, offset (BE16), count (BE16)` and
the LEDs' 2-byte values; only changed tapes are sent; then `mm 22` shows them.

## A HIRAKU driver, in short

1. Open the `1ccf:8050` CDC port at 115200 8N1 (raw). A BREAK reset is optional (then wait about
   5.5 s and reopen).
2. Sync (empty packets to node 0 until echoed), `00 01` to node 0, `00 02 81` to node 1; accept only
   type `0D 06 00 01` and "BI2X", and log the version.
3. Obfuscate every non-empty packet to node 1 once it has answered obfuscated (unless test B shows
   plain is fine); accept modes 0, 2, 3; skip mode 1 and `00 7F` packets; pair answers by tag.
4. Only if test C says so, and with the owner's go-ahead: upload the TDJ module, unpacked from the
   user's own `libaio-iob2_video.dll` (never shipped), and take its id from the `00 78` answer.
5. Send the gamma table once, then each frame one packet: `mm 11` + 17 bytes when the outputs
   changed, `mm 10` always; turntables as 8-bit positions (deltas folded into ±128); tape LEDs as
   above with the brightness limit kept.
6. Never anything from "Never send".

## Unknown or unsure

- Whether the board does any TDJ I/O without the module upload, and whether IIDX 28's module (made
  for firmware 1.2) works on 1.3.3. Test C.
- Whether the board insists on obfuscated packets once it has sent one. Test B.
- The meaning of `81` in `00 02 81`, of the master's node byte `20` (high nibble), of raw 0, raw 1
  bit 3, raw 2 bits 6-7, raw 3, raw 7, raw 10-73, the watchdog byte's units, the I/O reset bits,
  the coin counter byte's semantics, the `mm 11` status 1.
- The turntables' direction and counts per turn.
- That a BREAK resets the board (libaio's reason for it is assumed).
- `FUN_18001d390` taken to be `powf` (it is called with the base and 0.5 and the result used as such).
- FIRMINFO 0x10-0x1f's meaning; the BI2X 1.3 layout may differ from 1.2's.
- What the SCI module is for. `AIO_IOB2_BI2X::SciOpen` / `aioIob2Bi2x_SciOpenB8PNS1` open a serial
  port tunnelled through it (commands `nn 12` etc. to its id, `FUN_180017bc0`), presumably for card
  readers wired to the board; IIDX 28 doesn't use it (its readers are on a COM port of their own,
  `aioSciComm_OpenB8PNS1` + `aioIobIcca_Create`), so HIRAKU would need only the TDJ module.

## Results on the owner's board (2026-10-18, tests A and B, `tools/bio2/bi2x_tests.py`)

All five packets answered, every head and body CRC good; the board unchanged afterwards (same USB
device, no reset).

- **A.** The sync `AA 00 01 00 09` comes back as is. Assign nodes answers `00 01 21` (the first
  probe got `00 01 20`: the low bit changes from one session to the next; meaning unknown). FIRMINFO
  asked libaio's way (`00 02 81`) answers 35 bytes, mode 2, obfuscated with seed tag ^ 0xAA:
  `00 02 00 | 0D 06 00 01 | 00 01 03 03 | "BI2X" | 01 A9 99 A4 | 00 00 00 33 | 00 x 8 | EF 73 8E E2`.
- **B.** The same request plain (tag 04) and obfuscated (tag 05) both get the same answer: the board
  takes plain packets even after it has answered obfuscated, and always answers obfuscated. HIRAKU
  can send plain packets.
- **The `81` changes the last four bytes:** without it (the first probe, `00 02`) they were
  `00 00 F0 2E`; with it, `EF 73 8E E2`. libaio asks with `81`, so IIDX 28's firmware check sees
  `8E E2`, its own image's key, and firmware info that differs from its image (1.3.3 against
  1.2.3). By the rule in "Never send" above, **IIDX 28 would most likely rewrite this board with its
  older firmware**: don't run IIDX 28 (or any game whose I/O library carries a BI2X image) with this
  board connected, unless that downgrade is wanted. (The earlier note that IIDX 28 would leave it
  alone was drawn from the `F0 2E` answer.)
- **C** (the owner's OK, after sync and assign): the plain TDJ poll to module 3, `AA 02 06 02 02 03
  10 3C`, is answered `AA 03 06 03 53 76 0A 91 70`: payload `03 00 C0` (mode 2, obfuscated, CRCs
  good), not `03 10 00` + 74 bytes. Read as target 3, command zeroed, status `C0`: no module 3 on
  the board. So no I/O module is resident after a power-up: a driver has to upload one (16/19/120)
  before polling, from the owner's game data, as libaio does. The obfuscated variant wasn't sent
  (the plain one was answered). Board unchanged afterwards.

## Cross-check with Feiryn's bio2-tools (2026-10-18)

[github.com/Feiryn/bio2-tools](https://github.com/Feiryn/bio2-tools) (MIT, Rust; the BIO2's
memory map, both protocols, firmware extraction and flashing tools) agrees with this file wherever
both cover the same ground, independently of it:
- **Packets:** the same layout, size bytes (their "VLQ"), flags, modes 0/2/3, CRC4 and CRC7, and
  the tag ("sequence number") skipping 00, AA and FF. Mode 4 is MC_LZ compression, which they
  decode (`lib/compression/src/mc_lz.rs`); libaio's host side rejects it.
- **Obfuscation:** the same LCG, the same untouched bytes, masks and seeds (`tag ^ 0x55` to the
  board, `tag ^ 0xAA` from it). They keep only the low 8 bits of the state between bytes; the
  masks only use bits the low byte decides, so it's the same stream.
- **Module upload:** `00 10` (size, big-endian) → handle, `00 13` (handle, offset, ≤ 64 bytes),
  `00 78` (handle, descriptor offset, init arguments) → the handle for the module's own commands.
  They call the module "game firmware"; test C above showed none is resident after power-up.
- **Firmware images in libaio-iob.dll:** their `aio_iob_dumper` finds three encrypted images by
  start and end bytes, undoes the XOR chain (the same one as the modules, above) and AC_LZ (the same
  LZSS). `tools/bio2/aio_fw.py` does the same in Python (our own code; output only into `out/`,
  as the images are Konami's). From IIDX 28's DLL:

| file | size | what it is |
|---|---|---|
| `bio2wrfirm.bin` | 60 481 | the BIO2 loader, `bio2wrfirm-2019040100` |
| `bi2a_804c.bin` | 65 536 | an `RXU1` image, "BI2X Apr 23 2019", FIRMINFO type `0D 06 01 00`: BI2X as written to a board still running BI2A (USB 0x804C) through the BI2A flasher (`0004`, then `A1`/`A2`/`A8`, docs/BIO2-BI2A.md) |
| `bi2x.bin` | 131 072 | **the BI2X firmware, "MC-KERNEL"**: its first 32 bytes are FIRMINFO, `0D 06 00 01 \| 00 01 02 03 \| "BI2X" \| 01 8E B4 66 \| 0 × 12 \| 27 7D 8E E2`, version 1.2.3, key `8E E2`: the image libaio registers (its plain 32-byte header at 0x18002d580, then the size `00 02 00 00`, then this blob at 0x18002d5a4) and writes in band with `00 70`/`00 71` when the check above says so. bio2-tools calls it "the bi2x kernel (usage still to be found)" |

  The owner's board answers the same layout with version 1.3.3 and `EF 73 8E E2`. The kernel's
  strings include `BootMod` and `ExIoTCA6416A` (a TI TCA6416A 16-bit I/O expander).
- **Disagreement:** their README gives USB PID 0x8040 as BI2A and 0x804C as BI2X. libaio's table
  (above) has 0x8040 for the `BIO2WRFIRM` loader, 0x804C for a board running BI2A, and 0x8050 for
  BI2X, which is what this board reports. libaio is the authority here; the never-send rules stand.
- **Recovery:** the BIO2's MCU is a Renesas RX621. Its boot mode (pins J2 shorted at power-up) is in
  mask ROM and takes a full flash from Renesas Flash Programmer, so a board can be rewritten from
  scratch with a valid image (their `generate_mot`). That makes a bad in-band write recoverable,
  and a BI2A downgrade possible (docs/BIO2-BI2A.md). It's still the owner's job, never HIRAKU's.

## The kernel and its modules, read statically (1.2.3, 2026-10-18)

Disassembled with binutils 2.45's `rx-elf-objdump` (built locally; Ghidra 12.1.2 has no RX module)
from IIDX 28's image and the two modules libaio uploads, unpacked from the owner's DLLs into
`out/bio2/mod/` (TDJ 0x2421 bytes, SCI 0x455, as expected). Nothing was run; no device opened. The
working files (disassemblies, helper scripts, a partial RAM map) are in `out/bio2/`.

| range | what |
|---|---|
| 0xFFFE0000-1F | FIRMINFO (what `00 02` sends), then `MC-KERNEL.HEAD` at 0xFFFE0020 |
| 0xFFFE0030-244F | the BI2X layer: the ACIO2 links and command dispatcher, lamps and PWM, the TCA6416A glue, the USB descriptors (VID 1CCF PID 8050) |
| 0xFFFE2450-2754 | a built-in module, in the uploaded modules' format: the TCA6416A I/O expander's driver, started at boot |
| 0xFFFF0000-92AF | the OS ("MC"): scheduler, heap, handles, drivers (timers, DMA, serial, I2C, CRC), C library |
| 0xFFFFDE00-DFDF | **the export table**: 120 entry points |
| 0xFFFFDFF0 | `MC-KERNEL.TAIL`, then a CRC-16 (Kermit) that makes the 0x1E000-byte kernel image check to 0 |
| 0xFFFFE000-FFFF | the boot block: reset, flash routines, bank handling, the fixed vectors; not part of the kernel image |

**Modules are position-independent and reach the kernel only through the export table** (certain
for the three modules at hand): every call loads a fixed slot address, 0xFFFFDE00 + 4n, and calls
through it; everything else is relative to the module's own position. TDJ uses 11 slots (63, 64,
80, 81, 83, 85, 86, 98-100, 117), SCI 6 (88, 89, 92, 94, 103, 104), the built-in TCA6416A module 2
(63, 64). So whether IIDX 28's TDJ module runs on the owner's 1.3.3 board comes down to whether
1.3.3 keeps those slots with the same meaning. That's what an export table is for, and the boot
block is the same (below), but it can't be proven without a 1.3.3 image. Whether the kernel checks
a module before starting it (a CRC, a version) wasn't settled.

**FIRMINFO's last two bytes are the boot block's checksum, not a version key:** `8E E2` is the
CRC-16/X-25 of 0xFFFFE000-0xFFFFFFFF, as stored in the image's header; asked without `81` the
kernel computes it live instead and answers `F0 2E` (the low 16 bits of a CRC-32 of the same
block, inverted). The 1.2.3 image's boot block gives exactly `8E E2` / `F0 2E`, the owner's 1.3.3
board both of those: **its boot block is IIDX 28's**, and only the kernel above it differs. Bytes
0x1C-0x1D (`27 7D` in 1.2.3, `EF 73` on the board) differ with the kernel. (`out/bio2/tools/firminfo_crc.py`.)

## Proposed read-only tests (for the owner's approval)

The board on 12 V, opened at 115200 8N1 raw, no BREAK. Each packet once, then 2 s of listening;
log every byte. The bytes are computed with libaio's rules (they reproduce the two packets
`tools/bio2/bi2x_version.py` sent on 2026-10-18 byte for byte).

**A. libaio's own opening, still read-only.**

| send | what | expected |
|---|---|---|
| `AA 00 01 00 09` | sync: an empty packet to the master, tag 01 | the same five bytes back |
| `AA 00 02 02 00 00 01 4A` | assign nodes, tag 02 | payload `00 01 20` again, tag 02 |
| `AA 02 03 03 0F 00 02 81 34` | node 1 FIRMINFO the way libaio asks, tag 03 | 35 bytes, obfuscated with seed `03 ^ AA = A9`; compare with 2026-10-18's |

**B. Plain or obfuscated after that.** Only if A answered. The same FIRMINFO request twice more,
to see whether plain packets still get answers after an obfuscated answer:

| send | what |
|---|---|
| `AA 02 04 03 09 00 02 81 34` | plain, tag 04 |
| `AA 02 05 03 1B 49 4C C4 48` | obfuscated (seed `05 ^ 55 = 50`), tag 05: payload `00 02 81` and CRC7 `34` scrambled |

Both read-only; whichever is answered is the form HIRAKU uses from then on.

**C. Is there an I/O module already?** Only with the owner's approval, as it is a module command:

| send | what |
|---|---|
| `AA 02 06 02 02 03 10 3C` | a TDJ poll to module 3, plain, tag 06 |
| `AA 02 07 02 10 20 30 65` | the same obfuscated (seed `07 ^ 55`), tag 07, if the plain one gets nothing |

A poll changes no output. An answer of `03 10 00` + 74 bytes means module 3 is resident (built in,
or left by the last game); `03 10` with a non-zero status, or no answer, means it must be uploaded,
and whether to upload IIDX 28's module (made for firmware 1.2) to a 1.3.3 board is the owner's call.

Nothing in A-C touches `00 10`, `00 13`, `00 70`-`00 7F` or any `mm 11`/`12`/`2x` output command.
