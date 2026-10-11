// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 front end: shows the GCU output, paces virtual time to the wall clock,
// and maps the keyboard onto the cabinet's inputs.
#pragma once

#include <cstdint>
#include <string>

namespace hk::host {

bool window_open(const char *title, int scale, const std::string &bindings, int audio_target_ms, bool fullscreen = false);
// Present one frame; then sleep so that `virtual_seconds` does not run ahead
// of real time. Returns false when the user closed the window.
bool window_frame(const uint32_t *rgb, int w, int h, double virtual_seconds);
void window_close();
// Queue stereo float audio for playback (44.1 kHz) and pace the machine on it:
// the emulation waits while more than the target is queued, and controls are
// polled meanwhile. Audio is then the master clock (low, steady latency);
// without an audio device, video frames pace time instead.
void audio_push(const float *lr, int frames);
std::string audio_describe();   // device buffer / target, for the latency report

}  // namespace hk::host
