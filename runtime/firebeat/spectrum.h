// SPDX-License-Identifier: GPL-3.0-or-later
// beatmania III extend board spectrum analyser (2x NJU7507 per channel), as
// MAME's firebeat_extend_spectrum_analyzer_device models it (BSD-3-Clause):
// 512-sample blocks, the magnitude of the first FFT bin inside each of six
// bands, reported as 20*log10(mag*4096) clamped to 0..255.
#pragma once

#include <cstdint>

namespace hk::spectrum {

void push(const float *lr, int frames, int sample_rate);   // stereo audio as it is produced
uint8_t read(uint32_t offset);                             // 0x70008000 + offset

}  // namespace hk::spectrum
