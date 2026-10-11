// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 front end: shows the GCU output, paces virtual time to the wall clock,
// and maps the keyboard onto the cabinet's inputs.
#pragma once

#include <cstdint>

namespace hk::host {

bool window_open(const char *title, int scale);
// Present one frame; then sleep so that `virtual_seconds` does not run ahead
// of real time. Returns false when the user closed the window.
bool window_frame(const uint32_t *rgb, int w, int h, double virtual_seconds);
void window_close();
// Queue stereo float audio for playback (44.1 kHz). Drops audio if more than
// ~150 ms is already queued, so latency stays bounded while video paces time.
void audio_push(const float *lr, int frames);

}  // namespace hk::host
