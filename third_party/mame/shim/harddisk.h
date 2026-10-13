// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's util/harddisk.h, the geometry a hard disk image reports.
#pragma once
#include "emu.h"
class hard_disk_file {
public:
    struct info {
        u32 cylinders = 0, heads = 0, sectors = 0, sectorbytes = 512;
    };
};
