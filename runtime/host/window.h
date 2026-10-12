// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 front end: shows the GCU output, paces virtual time to the wall clock,
// and maps the keyboard onto the cabinet's inputs.
#pragma once

#include <cstdint>
#include <string>

namespace hk::host {

// Before window_open: show the game through the CRT shader (runtime/host/crt.h), whose
// .glsl files are in shader_dir; params is "" or a parameters file. F10 switches between
// it and sharp pixels, Shift+F10 reads the parameters file again.
void window_set_crt(const std::string &shader_dir, const std::string &params);
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
