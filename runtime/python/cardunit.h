// SPDX-License-Identifier: GPL-3.0-or-later
// The e-AMUSEMENT card reader/writer and 10-key of a Konami Python 1 pop'n cabinet, on the
// interface board's serial port (9600 baud), as pop'n music 9's driver uses them
// (EE program 0x1a1050-0x1a6c00; PLAN.md section 21, "e-AMUSEMENT"). The bus is half-duplex:
// the game reads its own packet back before each answer.
//
//   game:   aa aa aa 55                         sync, echoed
//           aa T ADDR CMD LEN [payload] SUM     T = aa (bus) or 00 (card unit); SUM = 8-bit
//                                               sum of everything after the first byte
//   device: the packet, then  aa T' ADDR CMD LEN [payload] SUM   (T' = a5 bus, 01 card unit)
//
// Bus commands: 01 assign the address, 02 revision, 03 start. Card unit: 10/11 reset,
// 12 status, 14/15 control (1 open, 2 eject, 4 read, 5 write), 16 write buffer (128 bytes),
// 18 read buffer, 1e/1f format, 20 10-key init, 24 10-key status, 26 10-key events
// (PS/2 set 2 scan codes). Odd commands (11, 15, 1f) poll an even one still busy; this
// device is never busy.
//
// The card is a file of 128 bytes: what the game writes is kept, and a new file is a fresh
// pop'n 9 card ("C00 NEW CARD"). A card is taken in when the game opens the slot and the
// player has put one in, and given back when the game ejects it.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace hk::py {

class CardUnit {
public:
    explicit CardUnit(std::string card_file);

    // A write from the game to the serial port: false if it isn't this bus's (left to the
    // emulator). The answer, if any, is appended to `reply`.
    bool write(const uint8_t *data, uint32_t size, std::vector<uint8_t> &reply);

    // The player: the card button (puts the card in the slot, or takes it back when the
    // reader has given it out), and the 10-key (scan code of the key, pressed or not).
    void card_button();
    void key(uint8_t scan, bool down);

    bool card_inside() const { return inside_; }
    // A line for the window title ("card in", "card ready", ...), "" when nothing to say.
    std::string state() const;

    // A fresh pop'n 9 card: the header the game's "C00 NEW CARD" test wants
    // (0x1a2728 type 2), the rest blank.
    static std::vector<uint8_t> new_card();

private:
    void load();
    void save();
    void answer(uint8_t type, uint8_t cmd, const std::vector<uint8_t> &payload, uint8_t len,
                std::vector<uint8_t> &reply) const;
    uint8_t status() const;

    std::string file_;
    std::vector<uint8_t> card_, buffer_;
    uint8_t addr_ = 1;
    bool offered_ = false;   // the player has put the card in the slot
    bool inside_ = false;    // the reader has it
    bool accepting_ = false; // the game has opened the slot
    bool read_ready_ = false, buffer_ready_ = false;
    std::deque<uint8_t> keys_;   // 10-key events not yet sent
};

}  // namespace hk::py
