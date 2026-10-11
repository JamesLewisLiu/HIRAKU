// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-in for Keyboardmania's XT446 synth card (an embedded Yamaha MU100B): the
// game's MIDI stream played through FluidSynth with a General MIDI SoundFont.
// Not the cabinet's sound - the instruments are whatever the SoundFont has - but
// the notes, timing and controllers are the game's own. Built only when
// FluidSynth is found; otherwise every call is a no-op.
#pragma once

#include <cstdint>
#include <string>

namespace hk::synth {

// path: a .sf2 file, or "" to search $HK_SOUNDFONT, /usr/share/soundfonts and
// ~/.local/share/hiraku. Returns false (and stays silent) without one.
bool init(const std::string &path);
bool active();
void midi(uint8_t byte);                       // one byte of the MIDI stream, in time order
void render(float *lr, int frames, float gain);   // add 44.1 kHz stereo
std::string describe();

}  // namespace hk::synth
