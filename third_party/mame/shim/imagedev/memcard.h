// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's imagedev/memcard.h (memory card images): see image.h.
#pragma once
#include "imagedev/image.h"
class device_memcard_image_interface : public device_image_interface {
public:
    device_memcard_image_interface(const machine_config &mconfig, device_t &device) : device_image_interface(mconfig, device) {}
    bool is_readable() const noexcept override { return true; }
    bool is_writeable() const noexcept override { return true; }
    bool is_creatable() const noexcept override { return true; }
    bool support_command_line_image_creation() const noexcept override { return true; }
    const char *image_type_name() const noexcept override { return "memcard"; }
    const char *image_brief_type_name() const noexcept override { return "memc"; }
};
