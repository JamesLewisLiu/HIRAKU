// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: the ATA drive types the System 573 never selects (see stubs/*.h). The hard disk is
// MAME's own (bus/ata/hdd.cpp), for Twinkle.
#include "emu.h"
#include "imagedev/cdromimg.h"
#include "cp2024.h"
#include "px320a.h"
#include "xm3301.h"
#include "zip100.h"
#include "gdrom.h"
#include "analogue.h"
#include "multitap.h"
class cp2024_device : public device_t { public: cp2024_device(const machine_config &m, const char *t, device_t *o, u32 c) : device_t(m, CP2024, t, o, c) { fatalerror("%s: not available in HIRAKU", t); } };
DEFINE_DEVICE_TYPE(CP2024, cp2024_device, "cp2024", "CP2024 (unavailable)")
class px320a_device : public device_t { public: px320a_device(const machine_config &m, const char *t, device_t *o, u32 c) : device_t(m, PX320A, t, o, c) { fatalerror("%s: not available in HIRAKU", t); } };
DEFINE_DEVICE_TYPE(PX320A, px320a_device, "px320a", "PX320A (unavailable)")
class xm3301_device : public device_t { public: xm3301_device(const machine_config &m, const char *t, device_t *o, u32 c) : device_t(m, XM3301, t, o, c) { fatalerror("%s: not available in HIRAKU", t); } };
DEFINE_DEVICE_TYPE(XM3301, xm3301_device, "xm3301", "XM3301 (unavailable)")
class zip100_ide_device : public device_t { public: zip100_ide_device(const machine_config &m, const char *t, device_t *o, u32 c) : device_t(m, ZIP100_IDE, t, o, c) { fatalerror("%s: not available in HIRAKU", t); } };
DEFINE_DEVICE_TYPE(ZIP100_IDE, zip100_ide_device, "zip100_ide", "ZIP100_IDE (unavailable)")
class atapi_gdrom_device : public device_t { public: atapi_gdrom_device(const machine_config &m, const char *t, device_t *o, u32 c) : device_t(m, ATAPI_GDROM, t, o, c) { fatalerror("%s: not available in HIRAKU", t); } };
DEFINE_DEVICE_TYPE(ATAPI_GDROM, atapi_gdrom_device, "atapi_gdrom", "ATAPI_GDROM (unavailable)")

class psx_dualshock_device : public device_t { public: psx_dualshock_device(const machine_config &m, const char *t, device_t *o, u32 c) : device_t(m, PSX_DUALSHOCK, t, o, c) { fatalerror("%s: not available in HIRAKU", t); } };
DEFINE_DEVICE_TYPE(PSX_DUALSHOCK, psx_dualshock_device, "psx_dualshock", "PSX_DUALSHOCK (unavailable)")
class psx_analog_joystick_device : public device_t { public: psx_analog_joystick_device(const machine_config &m, const char *t, device_t *o, u32 c) : device_t(m, PSX_ANALOG_JOYSTICK, t, o, c) { fatalerror("%s: not available in HIRAKU", t); } };
DEFINE_DEVICE_TYPE(PSX_ANALOG_JOYSTICK, psx_analog_joystick_device, "psx_analog_joystick", "PSX_ANALOG_JOYSTICK (unavailable)")
class psx_multitap_device : public device_t { public: psx_multitap_device(const machine_config &m, const char *t, device_t *o, u32 c) : device_t(m, PSX_MULTITAP, t, o, c) { fatalerror("%s: not available in HIRAKU", t); } };
DEFINE_DEVICE_TYPE(PSX_MULTITAP, psx_multitap_device, "psx_multitap", "PSX_MULTITAP (unavailable)")
