// SPDX-License-Identifier: GPL-3.0-or-later
// GuitarFreaks and DrumMania V's e-AMUSEMENT card units on Konami Python 2: the same magnetic card
// reader/writer and 10-key per player as the 573 games' (runtime/sys573/cardunit.h, the game's
// EXTIO: ExtIOBoardInit, MagCardAcs, KeyboardAcs), here on the P2IO board's serial port 0 (the
// game's SCI writes and reads), so byte-wise instead of bit-wise. The protocol is the 573 unit's
// (after windyfairy's k573acio/k573cardunit): a sync aa aa aa 55 and then aa aa 00 00, both
// echoed; then ACIO packets aa CMD NODE SUB LEN data SUM (CMD aa for the bus: node count, version,
// start; 00 for a reader: status 12, slot 14/15, write 16, read 18, the 10-key 26), each echoed
// before its answer (the line is half duplex), the answer's CMD 01.
//
// A card is a file of 128 bytes (a blank card when there is none): the save folder's card1.bin.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "python/cardreader.h"

namespace hk::py {

class GfdmCardUnit final : public CardReader {
public:
    // file: the card's file ("" keeps no card)
    explicit GfdmCardUnit(std::string file);

    bool write(const uint8_t *data, uint32_t size, std::vector<uint8_t> &reply) override;
    bool echoes() const override { return true; }
    void card_button() override;
    void key(uint8_t scan, bool down) override;
    bool card_offered() const override { return inserted_; }
    std::string state() const override { return inserted_ ? "card in the reader" : ""; }

private:
    void packet(std::vector<uint8_t> &reply);
    void save() const;

    std::string file_;
    uint8_t data_[128] = {};
    bool inserted_ = false;
    std::deque<uint8_t> in_;
    int sync_ = 0;
    uint32_t keys_ = 0, last_keys_ = 0;   // the 10-key held, by KEYS' order
};

}  // namespace hk::py
