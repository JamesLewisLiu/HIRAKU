// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: stand-in for MAME's machine/idectrl.h. The FDC37C665GT can route an IDE
// controller, which no machine here gives it; its ata_interface_device is enough.
#pragma once
#include "bus/ata/ataintf.h"
