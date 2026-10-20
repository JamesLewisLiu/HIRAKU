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

    // The file `name` as the game uses it (as tools/popnfs.py extract writes it), from the disk as
    // it is (no mod's): unpacked, or as stored. Empty when there's no such file; throws for one the
    // game decrypts (its keys are the loader's: tools/popnfs.py extract gets those).
    std::vector<uint8_t> get(const std::string &name) const;
    bool has(const std::string &name) const { return find(name) != nullptr; }

private:
    struct Entry { uint32_t hash, sector, size, flags, sum, last; };
    const Entry *find(const std::string &name) const;   // in the disk's own directory
    // the loader's keys (boot record C): tag -> key and IV, and Blowfish's starting tables
    struct Keys {
        std::vector<std::pair<std::string, std::pair<std::string, std::string>>> tags;
        std::vector<uint32_t> p, s;
    };
    const Keys &keys() const;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t> &file) const;   // all its blocks
    mutable std::unique_ptr<Keys> keys_;
    std::vector<Entry> disk_;   // the directory as the disk has it
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
// ... and unpacked: n bytes out (a stream that ends early is padded with zeros, as some of the
// disk's largest pictures need)
std::vector<uint8_t> lzss_unpack(const uint8_t *in, size_t n_in, size_t n);

// A picture (.gzz: "GC", the size, width and height, then 16-bit A1B5G5R5 or 8-bit pixels into
// its folder's palette, the last 1 KB of its system.idx) of the same size and kind as `like`,
// from RGBA pixels (width x height x 4): 16-bit colours rounded to 5 bits, alpha on at 128 and up;
// 8-bit ones the nearest of the folder's palette, which stays as it is (tools/popnfs.py frompng).
// Throws when the size isn't like's, or an 8-bit picture has no palette.
std::vector<uint8_t> picture(const std::vector<uint8_t> &like, const uint8_t *rgba, int width, int height,
                             const std::vector<uint8_t> &system_idx);

}  // namespace hk::pythondisk
