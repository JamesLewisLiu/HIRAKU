// SPDX-License-Identifier: GPL-3.0-or-later
// Windows: the desktop compositor's (DWM's) vblank, for display pacing where there's no Wayland
// (pacing.cpp): when the display last refreshed, and how often it does. Windows only.
#pragma once

#include <cstdint>

namespace hk::host::dwm {

// The last vblank, on prof::now_ns()'s clock, and the refresh period, both ns; false where DWM
// doesn't say (DwmGetCompositionTimingInfo, loaded at run time)
bool vblank(int64_t &last_ns, int64_t &period_ns);

}  // namespace hk::host::dwm
