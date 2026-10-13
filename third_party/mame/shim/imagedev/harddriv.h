// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: stand-in for MAME's harddisk_image_device (imagedev/harddriv.h), reading a raw
// disk image instead of a CHD. The board gives the image and its geometry (from the CHD's
// GDDD metadata) before start. MAME mounts the sets' disks read-only with a fresh diff file
// per run (the oracle's -diff_directory); here writes go to an overlay in memory, so reads
// after them see what was written and the image on disk never changes.
#pragma once

#include "emu.h"
#include "harddisk.h"
#include "imagedev/image.h"

#include <unordered_map>

class harddisk_image_device : public device_t, public device_image_interface {
public:
    harddisk_image_device(const machine_config &mconfig, const char *tag, device_t *owner, const char *intf);
    harddisk_image_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
    ~harddisk_image_device();
    // HIRAKU: the image and its geometry, before the machine starts; none = no disk
    static void hk_set_disk(const std::string &tag, const std::string &path, u32 cylinders, u32 heads, u32 sectors);

    bool exists() const { return m_fd >= 0; }
    const hard_disk_file::info &get_info() const { return m_info; }
    void get_inquiry_data(std::vector<u8> &data) const { data.clear(); }   // a CHD's IDNT: none
    bool read(u32 lba, void *buffer);
    bool write(u32 lba, const void *buffer);

protected:
    void device_start() override;

private:
    int m_fd = -1;
    u64 m_sectors = 0;
    hard_disk_file::info m_info;
    std::unordered_map<u32, std::vector<u8>> m_written;
};
DECLARE_DEVICE_TYPE(HARDDISK, harddisk_image_device)
