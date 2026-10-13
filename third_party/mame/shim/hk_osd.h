// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: inert stand-ins for MAME's OSD files and directories. MAME's SCSI CD drive
// shares a host folder with Apple's CD toolbox through them; nothing here uses that:
// no folder opens and no file opens.
#pragma once
#include <memory>
#include <string>
#include <system_error>

#define PATH_SEPARATOR "/"
constexpr u32 OPEN_FLAG_READ = 1, OPEN_FLAG_WRITE = 2, OPEN_FLAG_CREATE = 4;

class osd_file {
public:
    using ptr = std::unique_ptr<osd_file>;
    static std::error_condition open(const std::string &, u32, ptr &, u64 &size) {
        size = 0;
        return std::make_error_condition(std::errc::no_such_file_or_directory);
    }
    std::error_condition read(void *, u64, u32, u32 &actual) { actual = 0; return std::make_error_condition(std::errc::io_error); }
    std::error_condition write(const void *, u64, u32, u32 &actual) { actual = 0; return std::make_error_condition(std::errc::io_error); }
};

namespace osd {
class directory {
public:
    using ptr = std::unique_ptr<directory>;
    struct entry {
        enum class entry_type { NONE, FILE, DIR, OTHER };
        const char *name = "";
        entry_type type = entry_type::NONE;
        u64 size = 0;
    };
    static ptr open(const std::string &) { return nullptr; }
    const entry *read() { return nullptr; }
};
}  // namespace osd
