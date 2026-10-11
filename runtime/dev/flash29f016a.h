// SPDX-License-Identifier: GPL-3.0-or-later
// Fujitsu MBM29F016A 2 MiB byte-wide flash (AMD command set). Command handling
// follows MAME's intelfsh.cpp for this part; erases complete immediately.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hk {

class Flash29F016A {
public:
    Flash29F016A() : data_(0x200000, 0xff) {}
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t v);
    std::vector<uint8_t> &data() { return data_; }
    bool dirty = false;

private:
    enum class Mode { normal, id1, id2, readid, program, erase1, erase2, erase3, erase4 };
    std::vector<uint8_t> data_;
    Mode mode_ = Mode::normal;
};

}  // namespace hk
