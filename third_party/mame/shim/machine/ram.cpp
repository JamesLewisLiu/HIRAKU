// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/ram.h"

DEFINE_DEVICE_TYPE(RAM, ram_device, "ram", "RAM")

ram_device::ram_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
    : device_t(mconfig, RAM, tag, owner, clock) {}

#include "speaker.h"
DEFINE_DEVICE_TYPE(SPEAKER, speaker_device, "speaker", "Speaker")
speaker_device::speaker_device(const machine_config &mconfig, const char *tag, device_t *owner, u32)
    : device_t(mconfig, SPEAKER, tag, owner, 0) {}
