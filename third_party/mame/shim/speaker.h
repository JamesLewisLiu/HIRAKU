// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's speaker device, configuration only (the host mixes the streams).
#pragma once
#include "emu.h"
class speaker_device : public device_t {
public:
    speaker_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 channels = 1);
    speaker_device &front() { return *this; }
    speaker_device &front_left() { return *this; }
    speaker_device &front_right() { return *this; }
    speaker_device &front_center() { return *this; }
    speaker_device &headrest_left() { return *this; }
    speaker_device &headrest_right() { return *this; }
    template <typename... A> speaker_device &set_position(A &&...) { return *this; }
};
DECLARE_DEVICE_TYPE(SPEAKER, speaker_device)
