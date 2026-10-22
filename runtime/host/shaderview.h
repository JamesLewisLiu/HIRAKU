// SPDX-License-Identifier: GPL-3.0-or-later
// The picture's shader settings in the game (F8), as the lights are (F9): the CRT shader in use (its
// parameters, the launcher's Picture settings), changed with the keyboard and seen at once on the
// picture, then saved for all games or for this one. A small panel in SDL's 8x8 font (316 x 370 at
// 1x), so it fits a 640x480 screen; the window draws it at whole times its size, on the right or the
// left of the picture.
//
//   F8                  show it on the right, then on the left, then hide it
//   Up, Down            pick a setting (and Page Up, Page Down, Home, End)
//   Left, Right         change it a step (Shift: ten)
//   Backspace           back to the value saved;  Delete: to the default (the shader's, HIRAKU's)
//   Enter               save for all games;  Shift+Enter: for this game
//
// Those are the defaults; each can be another key or a controller's button, hat or axis instead
// (--shader-key ACTION=BINDING, the launcher's Settings > Display). While it's shown it takes its
// controls (not the game); Esc still quits the game.
#pragma once

#include <SDL3/SDL_events.h>

#include <cstdint>
#include <string>
#include <vector>

#include "picture.h"

namespace hk::host::shaderview {

// The controls, by action: show (F8), up, down, less, more, saved, default, save (for all games),
// save_game (for this game; on the keyboard also Shift+save); each a binding as in a bindings file
// ("key:F8", "joy[DAO FPS]:button3"), "" for none. An action not given keeps its default.
const std::vector<std::string> &actions();
bool set_control(const std::string &action, const std::string &binding);   // false: no such action

void toggle();        // the show control
bool shown();
bool on_left();
// An event: true if it's one of the panel's controls and the panel used it (the game doesn't get it).
// can_show: the window draws with OpenGL (the shaders run)
bool event(const SDL_Event &e, picture::Mode mode, bool can_show);

// The panel's picture for the mode drawn: false when it's hidden. ARGB, w x h; id changes with it.
struct Panel {
    const std::vector<uint32_t> *pixels;
    int w, h;
    unsigned id;
};
bool panel(Panel &p, picture::Mode mode);

}  // namespace hk::host::shaderview
