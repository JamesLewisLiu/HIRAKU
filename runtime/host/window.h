// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 front end: shows the GCU output, paces virtual time to the wall clock,
// and maps the keyboard onto the cabinet's inputs.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hk::host {

// Before window_open: the picture (runtime/host/picture.h): mode off, light or full, the
// presets in shaders_dir, settings layered from config_dirs. F10 cycles the modes.
void window_set_picture(const std::string &mode, const std::string &shaders_dir, const std::vector<std::string> &config_dirs);
bool window_open(const char *title, int scale, const std::string &bindings, int audio_target_ms, bool fullscreen = false,
                 int audio_rate = 44100);
// Present one frame; then sleep so that `virtual_seconds` does not run ahead
// of real time. Returns false when the user closed the window.
bool window_frame(const uint32_t *rgb, int w, int h, double virtual_seconds);
void window_close();
// Queue stereo float audio for playback (at window_open()'s audio_rate) and pace the machine on it:
// the emulation waits while more than the target is queued, and controls are
// polled meanwhile. Audio is then the master clock (low, steady latency);
// without an audio device, video frames pace time instead.
void audio_push(const float *lr, int frames);
std::string audio_describe();   // device buffer / target, for the latency report

}  // namespace hk::host
