// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: stand-in for MAME's bus/psx/analogue.h: a controller-port option no 573 game
// selects by default; creating one is a fatal error.
#pragma once
#include "emu.h"
DECLARE_DEVICE_TYPE(PSX_DUALSHOCK, psx_dualshock_device)
DECLARE_DEVICE_TYPE(PSX_ANALOG_JOYSTICK, psx_analog_joystick_device)
