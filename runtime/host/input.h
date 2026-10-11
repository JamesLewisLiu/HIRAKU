// SPDX-License-Identifier: GPL-3.0-or-later
// Host controls: keyboard and joysticks (IIDX-style controllers present as
// plain joysticks) bound to the cabinet's inputs by name.
//
// Bindings file (one per line, '#' comments):
//     <input> = <binding>[, <binding>...]
//   input:    a MAME input name ("P1 Button 1", "1 Player Start", "Coin 1",
//             "Test", "Service", "P1 Foot", "Effect 3"), or a turntable:
//             "P1 Turntable" (an analog axis), "P1 Turntable -" / "+" (buttons)
//   binding:  key:<SDL key name>        e.g. key:Z, key:Left Shift, key:F2
//             joy<N>:button<K>          joystick N (0-based), button K
//             joy<N>:axis<K>            analog axis (turntables)
//             joy<N>:axis<K>-, +        an axis direction used as a button
//             joy<N>:hat<K>:<up|down|left|right>
// Defaults for beatmania III are built in; a file only needs the changes.
#pragma once

#include <string>

union SDL_Event;

namespace hk::host {

void input_init(const std::string &bindings_file);   // "" = defaults only
void input_event(const SDL_Event &e);
void input_joystick_added(unsigned int instance_id);   // hot-plugged controller
void input_update(double elapsed_ms);                 // turntable keys, analog axes
std::string input_describe();                         // the active bindings, for --help

}  // namespace hk::host
