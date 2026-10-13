// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's util/coreutil.h, the BCD conversions.
#pragma once
#include "emu.h"
inline constexpr u32 bcd_2_dec(u32 a) {
    u32 result = 0, scale = 1;
    while (a != 0) { result += (a & 0x0f) * scale; a >>= 4; scale *= 10; }
    return result;
}
inline constexpr u32 dec_2_bcd(u32 a) {
    u32 result = 0;
    int shift = 0;
    while (a != 0) { result |= (a % 10) << shift; a /= 10; shift += 4; }
    return result;
}
