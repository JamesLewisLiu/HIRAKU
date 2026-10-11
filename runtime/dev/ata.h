// SPDX-License-Identifier: GPL-3.0-or-later
// ATA bus with ATAPI CD/DVD-ROM drives (PIO), as the Firebeat's main and SPU
// buses use them. Register-level behaviour was matched against MAME's ATA/ATAPI
// emulation through the oracle trace (oracle/), not ported line by line.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hk {

// A read-only disc image: .iso (2048-byte sectors), or .bin/.cue / raw track
// (2352-byte MODE1 sectors).
class DiscImage {
public:
    bool open(const std::string &path);
    bool read_sector(uint32_t lba, uint8_t *out2048) const;
    uint32_t sectors() const { return nsectors_; }
    ~DiscImage();

private:
    FILE *f_ = nullptr;
    uint32_t stride_ = 2048, skip_ = 0, nsectors_ = 0;
};

class AtapiDrive {
public:
    explicit AtapiDrive(std::string name) : name_(std::move(name)) {}
    std::string name_;
    std::unique_ptr<DiscImage> disc;   // null: drive present, no medium
};

class AtaBus {
public:
    AtaBus();
    void attach(int unit, std::unique_ptr<AtapiDrive> d);

    // Register access as the ATA bus sees it: cs 0/1, register 0..7, 16-bit
    // value in ATA (little-endian) order. The board does the Firebeat byte swap.
    uint16_t read(int cs, int reg);
    void write(int cs, int reg, uint16_t v);

    std::function<void(bool)> irq;
    bool log = false;

private:
    void set_irq(bool on);
    void command(uint8_t cmd);
    void packet_done();
    void start_data_in(std::vector<uint8_t> data);
    void finish(bool error, uint8_t sense_key = 0, uint8_t asc = 0);
    AtapiDrive *cur();

    std::unique_ptr<AtapiDrive> drives_[2];
    uint8_t feature_ = 0, error_ = 0, nsector_ = 0, sector_ = 0, cyl_lo_ = 0, cyl_hi_ = 0, device_ = 0;
    uint8_t status_ = 0, devctl_ = 0;
    bool irq_ = false;

    enum class Phase { idle, packet_in, data_in };
    Phase phase_ = Phase::idle;
    uint8_t packet_[12]{};
    int packet_pos_ = 0;
    std::vector<uint8_t> data_;
    size_t data_pos_ = 0, chunk_end_ = 0;
    uint16_t byte_limit_ = 0;
    uint8_t sense_key_[2]{}, asc_[2]{};
};

}  // namespace hk
