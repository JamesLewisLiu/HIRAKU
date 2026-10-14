// SPDX-License-Identifier: GPL-3.0-or-later
// pop'n music 14's e-AMUSEMENT PASS reader: Konami's slotted IC card reader ("ICCA") and its
// 10-key, on the interface board's serial port at 19200 baud, as the game's driver uses it (EE
// program, in RAM after the second stage: 0x17f7f30 the start-up, 0x17f8540-0x17f9520 the
// commands; docs/EAMUSE3.md, "pop'n music 14").
//
//   game:   aa                                   sync; the reader answers aa
//           aa 00 HI LO 00 LEN [payload] SUM     command HI LO; SUM = 8-bit sum of HI..payload
//   reader: aa 81 HI LO 00 LEN [payload] SUM     SUM = 8-bit sum of everything after the aa
//
// The bus is full duplex (no echo). After a packet's aa, an aa or ff byte goes as ff and the byte
// inverted (ACIO's escape; the game undoes it, so a card id holding ff must be sent so). Commands: 00 03, 01 00, 01 30 (start-up:
// one byte 00 answers), 01 34 state, 01 35 slot (18 then open 11, eject 12, close 00), 01 39 read the
// card (the state, with the card's id), 01 10 the 10-key. The state is 0x18 bytes: 0 status (2
// ready; 4 after a read with no card), 1 sensors (0x10 front: a card at the slot; 0x20 rear: a
// card inside), 2-9 the card's id (8 bytes, the 16 hex digits e-AMUSEMENT uses).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "python/cardreader.h"

namespace hk::py {

class IcCardUnit final : public CardReader {
public:
    // card_id: the card's 16 hex digits (E004...)
    explicit IcCardUnit(const std::string &card_id);

    bool write(const uint8_t *data, uint32_t size, std::vector<uint8_t> &reply) override;
    bool echoes() const override { return false; }
    void card_button() override;
    void key(uint8_t scan, bool down) override;
    bool card_offered() const override { return offered_ || inside_; }
    std::string state() const override;

private:
    void answer(const uint8_t *request, const std::vector<uint8_t> &payload, std::vector<uint8_t> &reply) const;
    std::vector<uint8_t> status(bool read) const;
    void slot(uint8_t mode);

    std::array<uint8_t, 8> id_{};
    bool offered_ = false;    // the player holds the card at the slot
    bool inside_ = false;     // the reader has taken it in
    bool given_back_ = false; // ejected: out at the slot, for the player to take
    bool open_ = false;       // the game has opened the slot
    uint16_t keys_ = 0;       // the 10-key held: bits 0-3 blank 3 6 9, bits 8-15 0 1 4 7 00 2 5 8
};

}  // namespace hk::py
