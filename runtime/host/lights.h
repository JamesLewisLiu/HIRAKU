// SPDX-License-Identifier: GPL-3.0-or-later
// Cabinet lamps out to the real world: HID controller LEDs, and a local socket
// for anything else (LED strips, microcontrollers, the launcher's preview).
//
// Lights file (one per line, '#' comments), lamp names from runtime/firebeat/cabinet.cpp:
//     <lamp> = <output>[, <output>...]
//   output:   hid:<vid>:<pid>[:<interface>]/<n>   (see hidout.h)
//
// Socket (UNIX stream, text): on connect the server sends
//     hiraku-lights 1 <game> <family>
// then one "<lamp>=<0|1>" line per lamp, then a line per change as it happens.
// The full protocol is in docs/LIGHTS.md.
#pragma once

#include <string>

namespace hk::host {

// file / socket_path "" = not used. Returns false if nothing could be set up.
bool lights_init(const std::string &game, const std::string &family, const std::string &file, const std::string &socket_path);
void lights_frame();   // once per video frame: passes on lamp changes
void lights_close();   // turns the outputs off, removes the socket

}  // namespace hk::host
