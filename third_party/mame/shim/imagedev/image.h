// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: the part of MAME's device_image_interface (emu/diimage.h) its users here
// need. Images are given by the board before start (the CD) or not at all (memory
// cards): nothing is loaded or unloaded at run time, and no image file is open.
#pragma once

#include "emu.h"
#include "notifier.h"

namespace util { class option_resolution; }
enum class image_error : int { NONE = 0, INTERNAL, UNSUPPORTED, NOSOFTWARE, INVALIDIMAGE, INVALIDLENGTH, ALREADYOPEN, UNSPECIFIED };
inline std::error_condition make_error_condition(image_error e) { return std::error_condition((int)e, std::generic_category()); }
namespace std { template <> struct is_error_condition_enum<image_error> : public std::true_type {}; }

class device_image_interface : public device_interface {
public:
    enum class media_change_event { LOADED, UNLOADED };
    device_image_interface(const machine_config &, device_t &device) : device_interface(device, "image") {}
    virtual ~device_image_interface() = default;

    virtual std::pair<std::error_condition, std::string> call_load() { return {}; }
    virtual std::pair<std::error_condition, std::string> call_create(int, util::option_resolution *) { return {}; }
    virtual void call_unload() {}
    virtual bool is_readable() const noexcept { return true; }
    virtual bool is_writeable() const noexcept { return false; }
    virtual bool is_creatable() const noexcept { return false; }
    virtual bool is_reset_on_load() const noexcept { return false; }
    virtual bool support_command_line_image_creation() const noexcept { return false; }
    virtual const char *image_interface() const noexcept { return nullptr; }
    virtual const char *file_extensions() const noexcept { return ""; }
    virtual const char *image_type_name() const noexcept { return "image"; }
    virtual const char *image_brief_type_name() const noexcept { return "img"; }

    void add_region(std::string, bool = false) {}
    bool exists() const noexcept { return false; }
    bool is_loaded() const noexcept { return false; }
    bool is_open() const noexcept { return false; }
    bool is_readonly() const noexcept { return true; }
    const char *filename() const noexcept { return nullptr; }
    const char *basename() const noexcept { return nullptr; }
    const char *basename_noext() const noexcept { return nullptr; }
    const char *brief_instance_name() const noexcept { return "card"; }
    void unload() {}
    u64 length() { return 0; }
    u32 fread(void *, u32) { return 0; }
    u32 fwrite(const void *, u32) { return 0; }
    std::error_condition fseek(s64, int) { return {}; }
    u64 ftell() { return 0; }
    template <typename F> util::notifier_subscription add_media_change_notifier(F &&) { return {}; }
};
