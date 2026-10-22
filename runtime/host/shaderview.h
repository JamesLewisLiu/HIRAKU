// SPDX-License-Identifier: GPL-3.0-or-later
// The picture's shader settings in the game (F8), as the lights are (F9): the CRT shader in use (its
// parameters, the launcher's Picture settings), changed with the keyboard and seen at once on the
// picture, then saved for all games or for this one. A small panel in SDL's 8x8 font (316 x 400 at
// 1x), so it fits a 640x480 screen; the window draws it at whole times its size, on the right or the
// left of the picture.
//
//   F8                  show it on the right, then on the left, then hide it
//   Up, Down            pick a setting (Page Up, Page Down, Home, End)
//   Left, Right         change it a step (Shift: ten)
//   Backspace           back to the value saved;  Delete: to the default (the shader's, HIRAKU's)
//   Enter               save for all games;  Shift+Enter: for this game
//
// While it's shown it takes those keys (not the game); Esc still quits the game.
#pragma once

#include <SDL3/SDL_events.h>

#include <cstdint>
#include <vector>

#include "picture.h"

namespace hk::host::shaderview {

void toggle();        // F8
bool shown();
bool on_left();
// A key (down or up) while shown: true if the panel used it (the game doesn't get it)
bool key(const SDL_KeyboardEvent &k, picture::Mode mode);

// The panel's picture for the mode drawn: false when it's hidden. ARGB, w x h; id changes with it.
struct Panel {
    const std::vector<uint32_t> *pixels;
    int w, h;
    unsigned id;
};
bool panel(Panel &p, picture::Mode mode);

}  // namespace hk::host::shaderview
