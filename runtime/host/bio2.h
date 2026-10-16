// SPDX-License-Identifier: GPL-3.0-or-later
// beatmania IIDX's BIO2 I/O board (IIDX 25 and later cabinets) as the cabinet's controls and lamps
// for the beatmania games: --bio2 PATH, the board's serial port (on Linux, USB CDC: /dev/ttyACM0,
// or better its /dev/serial/by-id/ name).
//
// The board speaks ACIO (acio.h) with its BI2A firmware, as the IIDX 25-26 games drive it and
// spice2x's games/iidx/bi2a.cpp answers them: node 1, version "BI2A"; command 0152 carries the
// lamps out (0x30 bytes) and brings the controls back (0x2e bytes). Out: 03-06 the panel's lamps
// (1P start, 2P start, VEFX, EFFECT), 07-14 the 14 keys' lamps, 17-1f the 9-character ticker
// (ASCII), 20-23 spotlights 1-4, 24 the neon, 25-28 spotlights 5-8; a lamp is lit when its byte
// isn't 0. Back, by bit (bit n = byte n/8, 1 << n%8): 10 Service, 11 Test, 76 EFFECT, 77 VEFX,
// 78 2P start, 79 1P start, keys 151 + 16 k (1P) and 263 + 16 k (2P); bytes 16 and 17 the
// turntables (256 a turn), the high nibbles of 0, 2, 4, 6 and 7 the five effector sliders, byte 8
// a coin count. A board with the BI2X firmware (IIDX 27 and later) speaks ACIO2 instead: not yet.
//
// Not checked on a board yet: the start-up (0001, 0002, 0003, 0100 as spice2x answers them; IIDX
// itself may send more) and the turntables' direction. HK_ACIO_LOG=1 shows the packets.
#pragma once

#include <functional>
#include <string>

namespace hk {
class Core;
}

namespace hk::host {

// --bio2 PATH: true if `name` was it
bool bio2_option(const std::string &name, const std::function<std::string()> &val);
std::string bio2_help();

void bio2_start();               // the board's thread, if --bio2 was given
void bio2_frame(hk::Core &core); // once a frame on the core's thread: the controls in, the lamps out
void bio2_stop();

}  // namespace hk::host
