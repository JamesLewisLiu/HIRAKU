// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagedev/harddriv.h"

#include "dev/image.h"   // HIRAKU's image reader: a hard disk CHD read in place
#include "shell/sys.h"

#include <fcntl.h>
#include <unistd.h>

#include <map>

DEFINE_DEVICE_TYPE(HARDDISK, harddisk_image_device, "harddisk_image", "Harddisk")

namespace {
struct disk_cfg { std::string path; u32 c, h, s; };
std::map<std::string, disk_cfg> &disks() { static std::map<std::string, disk_cfg> m; return m; }
}

harddisk_image_device::harddisk_image_device(const machine_config &mconfig, const char *tag, device_t *owner, const char *)
    : harddisk_image_device(mconfig, tag, owner, u32(0)) {}

harddisk_image_device::harddisk_image_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
    : device_t(mconfig, HARDDISK, tag, owner, clock), device_image_interface(mconfig, *this) {}

harddisk_image_device::~harddisk_image_device() {
    hk::sys::unmap_file(m_map, m_map_size);
    if (m_fd >= 0) close(m_fd);
    hk::image::close(m_chd);
}

void harddisk_image_device::hk_set_disk(const std::string &tag, const std::string &path, u32 cylinders, u32 heads, u32 sectors) {
    disks()[tag] = {path, cylinders, heads, sectors};
}

void harddisk_image_device::device_start() {
    auto it = disks().find(tag());
    if (it == disks().end() || it->second.path.empty()) return;
    m_info.cylinders = it->second.c;
    m_info.heads = it->second.h;
    m_info.sectors = it->second.s;
    m_info.sectorbytes = 512;
    if (hk::image::is_chd(it->second.path)) {   // the CHD itself, through a cache of its hunks
        m_chd = hk::image::fopen_image(it->second.path);
        if (!m_chd) fatalerror("%s: cannot open the disk image %s", tag(), it->second.path.c_str());
        m_sectors = (u64)hk::image::chd_size(m_chd) / 512;
        return;
    }
    m_fd = open(it->second.path.c_str(), O_RDONLY | O_BINARY);
    if (m_fd < 0) fatalerror("%s: cannot open the disk image %s", tag(), it->second.path.c_str());
    m_sectors = (u64)lseek(m_fd, 0, SEEK_END) / 512;
    m_map_size = m_sectors * 512;
    m_map = (const u8 *)hk::sys::map_file(m_fd, m_map_size);   // if not, pread(), a sector at a time
}

bool harddisk_image_device::read(u32 lba, void *buffer) {
    if (auto w = m_written.find(lba); w != m_written.end()) {
        memcpy(buffer, w->second.data(), 512);
        return true;
    }
    if (lba >= m_sectors) return false;
    if (m_chd) return hk::image::pread(m_chd, buffer, 512, (u64)lba * 512) == 512;
    // read-ahead: entering a new 8 MiB stretch, ask the kernel for the next one too, so a cold page
    // cache doesn't stall the machine a sector at a time (the image is read the same either way)
    const uint64_t stretch = (uint64_t)lba * 512 >> 23;
    if (stretch != m_readahead) {
        m_readahead = stretch;
        hk::sys::will_need(m_fd, stretch << 23, 2 << 23);
    }
    if (m_map) {
        memcpy(buffer, m_map + (size_t)lba * 512, 512);
        return true;
    }
    return hk::sys::pread(m_fd, buffer, 512, (u64)lba * 512) == 512;
}

bool harddisk_image_device::write(u32 lba, const void *buffer) {
    if (lba >= m_sectors) return false;
    auto &w = m_written[lba];
    w.assign((const u8 *)buffer, (const u8 *)buffer + 512);
    return true;
}
