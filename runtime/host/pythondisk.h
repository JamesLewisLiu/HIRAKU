// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The file system of Konami Python 1 game disks (pop'n music 14 and its family; tools/popnfs.py
// tells it in full), for mods' [[file]] tables (docs/MODS.md): a file replaced or added by its
// name, its bytes and the directory laid over the disk through shell/overlay.h.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hk::pythondisk {

class Files {
public:
    // The disk at image (read as it is, without mods' ranges), when it holds this file system: its
    // directory at sector 0x4800 adds up. nullptr otherwise.
    static std::unique_ptr<Files> open(const std::string &image);

    // From now on the file `name` (image/kc/kc_001.gzz, or #hash) is `content`: the file as the
    // game uses it (as tools/popnfs.py extract writes it), packed here the way the game reads that
    // file. A name not on the disk is added. Throws with a message when it can't be.
    void put(const std::string &name, const std::vector<uint8_t> &content, const std::string &label);

    // Lay the directory, with the files put, over the disk (once, after the puts)
    void finish();

private:
    struct Entry { uint32_t hash, sector, size, flags, sum, last; };
    std::string image_;
    std::vector<Entry> entries_;
    uint32_t free_ = 0;     // the first sector of the room after the files, for files that grew
    bool changed_ = false;
    std::string labels_;
};

// The hash the loader looks names up by
uint32_t name_hash(const std::string &name);

// LZSS as the game unpacks it (4 KB window from 0xfee, flags LSB first): n bytes in, the stream out
std::vector<uint8_t> lzss_pack(const uint8_t *in, size_t n);

}  // namespace hk::pythondisk
