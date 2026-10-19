// SPDX-License-Identifier: GPL-3.0-or-later
// The game's lights over its picture (F9, --lights-overlay): every lamp the core decodes
// (cab::lamps(family)), grouped as there, with its output number ([bit], or [register.bit]), lit or
// not as the game last drove it, and how many times the lamps have changed. For seeing what a game
// does with its lights without the cabinet's hardware (or a lights file and board): which output
// is which, when each comes on.
#pragma once

#include <cstdint>
#include <vector>

namespace hk::host::lampview {

void set_shown(bool on);
void toggle();
bool shown();

// The overlay's picture as the lamps are now: false when it's hidden. pixels: ARGB, w x h (SDL's
// 8x8 font); id changes with the picture.
struct Panel {
    const std::vector<uint32_t> *pixels;
    int w, h;
    unsigned id;
};
bool panel(Panel &p);

}  // namespace hk::host::lampview
