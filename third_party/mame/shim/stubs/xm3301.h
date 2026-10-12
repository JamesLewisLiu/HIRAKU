// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: stand-in for MAME's bus/ata/xm3301.h. The drive is a slot option of every ATA
// slot (ata_devices) but no machine here selects it: creating one is a fatal error.
#pragma once
#include "emu.h"
DECLARE_DEVICE_TYPE(XM3301, xm3301_device)
