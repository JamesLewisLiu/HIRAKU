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
// card of the game running ("C00 NEW CARD" for pop'n 9, "C20 NEW CARD" for pop'n 10, ...). A card is taken in when the game opens the slot and the
// player has put one in, and given back when the game ejects it.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace hk::py {

class CardUnit {
public:
    // fresh: a new card's first four bytes, for the game running (fresh_header)
    using Header = std::array<uint8_t, 4>;
    explicit CardUnit(std::string card_file, Header fresh = fresh_header("popn9"));
    // A game's new-card header ({} if not known): pop'n 9 "C00 NEW CARD" 28 23 41 01 (0x1a2728
    // type 2), pop'n 10 "C20 NEW CARD" 28 23 49 01 (0x1ad3c8 type 7), pop'n 11 "D11 NEW CARD"
    // 28 24 45 11 (0x1a6200), pop'n 12 28 25 45 21 (0x1ad4e8). A used card has 0x40 more in the
    // first byte.
    static Header fresh_header(const std::string &game);

    // A write from the game to the serial port: false if it isn't this bus's (left to the
    // emulator). The answer, if any, is appended to `reply`.
    bool write(const uint8_t *data, uint32_t size, std::vector<uint8_t> &reply);

    // The player: the card button (puts the card in the slot, or takes it back when the
    // reader has given it out), and the 10-key (scan code of the key, pressed or not).
    void card_button();
    void key(uint8_t scan, bool down);

    bool card_inside() const { return inside_; }
    bool card_offered() const { return offered_; }   // in the slot, or in the reader
    // A line for the window title ("card in", "card ready", ...), "" when nothing to say.
    std::string state() const;

    // A fresh card: the header and a CRC-8 of it, the rest blank.
    static std::vector<uint8_t> new_card(const Header &h);

private:
    void load();
    void save();
    void answer(uint8_t type, uint8_t cmd, const std::vector<uint8_t> &payload, uint8_t len,
                std::vector<uint8_t> &reply) const;
    uint8_t status() const;

    std::string file_;
    Header fresh_;
    std::vector<uint8_t> card_, buffer_;
    uint8_t addr_ = 1;
    bool offered_ = false;   // the player has put the card in the slot
    bool inside_ = false;    // the reader has it
    bool accepting_ = false; // the game has opened the slot
    bool read_ready_ = false, buffer_ready_ = false;
    std::deque<uint8_t> keys_;   // 10-key events not yet sent
};

}  // namespace hk::py
