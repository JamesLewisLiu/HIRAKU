# pop'n music's USB I/O board

The I/O board of pop'n music cabinets from pop'n music 15 on, as the games drive it through their `ezusb.dll`, and as HIRAKU drives it with `--popn-io` (`runtime/host/popnio.h`): on Linux over USB itself, as below; on Windows through the `ezusb.dll` (last section). This page is read from the `ezusb.dll` pop'n music ships (its build for pop'n music 18, "J39"). The packet layout agrees with bemanitools' `ezusb2-popn` emulation and CrazyRedMachine's PopnForwarder. **Not yet checked on a board.**

## The chip and its firmware

- A Cypress EZ-USB FX2LP (CY7C68013A), **04b4:8613**, with no boot EEPROM. Windows calls it "Cypress EZ-USB FX2LP - EEPROM missing", the description `ezusb.dll` looks for. It talks to it through Cypress's CyUSB driver (interface GUID `{AE18AA60-7F6A-11D4-97DD-00010229B959}`).
- `ezusb.dll` has one firmware image (`usbStart(0)`): 253 of Cypress's 70-byte records (length, 0, address little-endian, type, 65 bytes of data), type 0 data, ended by a record of another type (3). They load about 6.5 KB into the chip's RAM, between 0x0000 and 0x196e. HIRAKU finds these records by their shape in the file given to `--popn-io-dll`. It carries no copy.
- **The load** is the FX2LP's own: vendor request `0xa0` (`bmRequestType 0x40`) to CPUCS `0xe600` with one byte, 1, which holds the 8051. Then the records go out as `0xa0` writes to their address, records that follow on joined into chunks of up to about 0x3e0 bytes, 16 ms apart. Then CPUCS gets 0, and the 8051 starts. That is RAM only: a power cycle leaves a bare chip again. `ezusb.dll` never sends `0xa2`, `0xa3` or `0xa9` (the requests that reach a boot EEPROM or external RAM) and loads no second-stage loader.
- After the load the board leaves the bus and comes back. `ezusb.dll` waits 15 s, and tries up to four times. It keeps **04b4:8613**, and its string descriptor 1 now says **"KONAMI"**, which is how `ezusb.dll` knows its firmware runs. A bare chip has no string descriptors at all. HIRAKU loads the firmware only into a bare 04b4:8613. A board with strings (Konami's firmware already running, or a board with firmware of its own) is used as it is.
- Then `ezusb.dll` does one handshake: an interrupt in, an empty (all-zero) interrupt out, two 512-byte bulk reads on EP 0x86 and two empty 512-byte bulk writes on EP 0x02. HIRAKU does the same, and carries on even if the bulk reads go unanswered.

## Endpoints and packets

| endpoint | type | size | use |
|---|---|---|---|
| 0x81 | interrupt in | 64 | the controls, coins, acknowledgements |
| 0x01 | interrupt out | 64 | the lamps, commands |
| 0x86 | bulk in | 512 | EEPROM and security replies |
| 0x02 | bulk out | 512 | EEPROM and security data |

**Out** (one each time the game calls `usbLamp`, or another call sets something). Normal polling sends node 0, command 0:

| byte | meaning |
|---|---|
| 0, 1, 5 | 0 |
| 2, 3 | a command, node and command (0 0 when there's none). pop'n music sends `09 03` once (the coin mode, `usbCoinMode(3)`) |
| 4 | a command's parameter (security) |
| 6 | the external output and coin meter bits (`usbSetExtIo`, `usbCoinMeterUp/Down`) |
| 7 | the watchdog's kick counter (`usbWdtReset`; the watchdog is off unless `usbWdtStart` arms it) |
| 8–11 | the lamps, `usbLamp`'s word little-endian: bits 0–4 the top lamps, 8–11 the side lamps (PopnForwarder lights 8 and 10 for blue, 9 and 11 for red; HIRAKU takes them as left 1, left 2, right 1, right 2, not checked), 16–19 the coin blocker (`usbCoinBlocker`: 0xf on), 23–31 buttons 1–9 (the game writes button 1 as the whole of bits 20–23). `ezusb.dll` drops bits 12–15 |

**In:**

| byte | meaning |
|---|---|
| 0–2 | 03 1d 85 |
| 3 | a sequence number (`ezusb.dll` takes the board as gone when it stops changing) |
| 4 | a command's acknowledgement |
| 5–6 | the coin count, big-endian |
| 8–11 | the controls, little-endian, **active low**: bit 6 SERVICE, 7 TEST, 8–16 buttons 1–9, 22 the coin switch, 24–27 the DIP switches (DIP 4: the monitor's 15 or 31 kHz) |
| 16–35 | ten 16-bit samples of the buttons (`usbPadReadLast`; newest first, as bemanitools has it) |

`usbPadRead` gives the controls of the newest in packet, inverted, and asks for the next one.

## What HIRAKU never sends

The board has an EEPROM and a security chip that the games write through these commands. A bad write could break the board, or the game's checks of it, so HIRAKU refuses to send any out packet but polling (`00 00`) and the coin mode (`09 03`), any bulk out that isn't all zero, and any vendor request but `0xa0` to RAM below 0x4000 or one byte, 0 or 1, to CPUCS (`popnio::allowed_*`; `tests/popnio` checks them).

| node, command | what | |
|---|---|---|
| 02 03 (+ bulk `02 00` and 128 bytes) | write the board's EEPROM | persistent |
| 01 03 (+ bulk `01 00` and 32 bytes) | write the security chip | persistent |
| fe 00, 01 01, 01 02, 01 06, 01 07–0b | the security chip's session key, search, read, ID, select | |
| 02 02 | read the EEPROM | |
| 0c n | arm (n s, up to 60) or stop (0) the watchdog | |
| 40 … | write the board's SRAM (not used by pop'n music) | |

## Windows: through the ezusb.dll

On Windows HIRAKU calls the DLL as the games do. With Konami's board, and with a board that copies it such as PopnIO in its REALIO mode, that's the real `ezusb.dll` (with Cypress's CyUSB driver installed for the board). A controller that comes with an `ezusb.dll` of its own, such as UltimatePopnController, is driven through that one. The DLL is 32-bit and HIRAKU's games are 64-bit, so `hiraku-ezusb.exe` (`runtime/host/ezusb/helper.c`, built 32-bit) loads it. HIRAKU starts it with a page of shared memory (`runtime/host/ezusb/link.h`):

1. `usbStart(0)` until it gives 0, `usbFirmResult()` until 0 (0x60 means still busy; the real board takes about 17 s per firmware load), `usbCoinMode(3)`.
2. About 1000 times a second: `usbPadRead` (the controls, 1 = pressed), `usbCoinGet(0)` if the DLL has it, and `usbLamp` when the lamps change, or twice a second.
3. At the end: `usbLamp(0)`, `usbEnd()`.

The names are looked up both ways: MSVC's C++ ones (`?usbStart@@YAHH@Z`), as the games' DLL has them, and plain ones (`usbStart`), as pop'n music 15's and replacements built as C have them. The helper calls nothing else: none of the DLL's EEPROM, security or watchdog functions. `tests/popnio/test_ezusb_helper.cpp` runs it against a pretend 32-bit DLL (under Wine on Linux).

Coins: Konami's board counts them (`usbCoinGet`). UltimatePopnController's DLL counts nothing and shows the coin switch as pad bit 22. HIRAKU takes the count when it moves, and otherwise a rise of bit 22 (unless the count moves within 15 frames of it).
