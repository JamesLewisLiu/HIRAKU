// SPDX-License-Identifier: GPL-3.0-or-later
// The e-AMUSEMENT GuitarFreaks and DrumMania's card units: a magnetic card reader/writer and
// 10-key per player ("ICCA", the game's EXTIO), on the 573's SIO1 at 38,400 baud. The security
// cassette's line d7 switches SIO1 between them and the multi-session unit (the game's
// swuart_get_device: SWUART_DEVICE_EXTIO or _MSU).
//
// After windyfairy's fork (github.com/987123879113/mame, branch npu_hle, 45e5eaa1, BSD-3-Clause:
// k573acio.cpp, the bus; k573cardunit.cpp, the reader). The bus is Konami's ACIO, as on later
// e-AMUSEMENT readers: packets of 0xaa, then a command (0xaa for the bus, 0 for a node), the
// node, a sub command, a length and data, and a sum. Here it is high-level and byte-wise: the
// 573's bits are framed back into bytes, and the answers go out a byte at a time at the line's
// rate (SIO1 takes one bit per call).
//
// A card is the save folder's card<N>.bin (128 bytes; a blank card when there is none).
// HK_573_CARD_LOG=1 prints the packets.
#pragma once

#include "emu.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace hk::k573 {

class CardUnit {
public:
    // readers: 1 (DrumMania) or 2 (GuitarFreaks); dir: the save folder ("" keeps no card)
    void configure(int readers, std::string dir);
    bool fitted() const { return !readers_.empty(); }

    // once the machine exists; rxd drives SIO1's receive line, a bit a call
    void attach(running_machine &m, std::function<void(int)> rxd);
    void reset();

    // SIO1's transmit line, a bit a call (as its bit clock calls it)
    void txd(int bit);
    // the cassette's switch: true connects the card units, false the multi-session unit
    void select(bool extio);
    bool selected() const { return selected_; }
    // called when the switch changes (the board gives SIO1 the switched-in side's handshake)
    std::function<void(bool extio)> on_select;

    // "Card" / "P2 Card" (insert, or take back), "Keypad 0".."Keypad 9", "Keypad Clear", ...
    // ("P2 Keypad ..." for the second reader)
    bool set_input(const std::string &name, int value);
    void save();   // the cards, as written

private:
    struct Reader {
        std::string file;
        uint8_t data[128] = {};
        bool inserted = false;
        uint32_t keys = 0, last_keys = 0;   // held 10-key keys (bits of KEYS)
    };
    void packet();
    void reply_byte(uint8_t b) { out_.push_back(b); }
    void send_tick(s32);
    void log(const char *what, const std::deque<uint8_t> &bytes, size_t n);

    std::vector<Reader> readers_;
    std::string dir_;
    running_machine *machine_ = nullptr;
    std::function<void(int)> rxd_;
    emu_timer *send_timer_ = nullptr;
    bool sending_ = false;   // the send timer is running
    bool selected_ = false;
    bool log_ = false;

    // receiving SIO1's frames: bits since a start bit (-1: waiting for one), the byte so far
    int rx_bits_ = -1;
    uint8_t rx_byte_ = 0;
    std::deque<uint8_t> in_;    // bytes from the 573, not yet a packet
    std::deque<uint8_t> out_;   // bytes to the 573
    int sync_ = 0;              // the bus's start-up handshake (two sync packets)
};

}  // namespace hk::k573
