// SPDX-License-Identifier: GPL-3.0-or-later
// pop'n music 14's IC card reader (icca.h).
#include "python/icca.h"

#include <cstdlib>

namespace hk::py {

namespace {

// The 10-key's bits (icca.h), by the PS/2 scan code of the key HIRAKU binds (core.cpp's
// KEYPAD_SCAN): Enter is the blank key, '.' the 00 key.
int key_bit(uint8_t scan) {
    switch (scan) {
    case 0x5a: return 0;    // blank
    case 0x7a: return 1;    // 3
    case 0x74: return 2;    // 6
    case 0x7d: return 3;    // 9
    case 0x70: return 8;    // 0
    case 0x69: return 9;    // 1
    case 0x6b: return 10;   // 4
    case 0x6c: return 11;   // 7
    case 0x71: return 12;   // 00
    case 0x72: return 13;   // 2
    case 0x73: return 14;   // 5
    case 0x75: return 15;   // 8
    default: return -1;
    }
}

}  // namespace

IcCardUnit::IcCardUnit(const std::string &card_id) {
    for (size_t i = 0; i < id_.size() && 2 * i + 1 < card_id.size(); i++)
        id_[i] = (uint8_t)std::strtoul(card_id.substr(2 * i, 2).c_str(), nullptr, 16);
}

std::vector<uint8_t> IcCardUnit::status(bool read) const {
    std::vector<uint8_t> s(0x18, 0);
    s[0] = read && !inside_ ? 4 : 2;
    s[1] = (uint8_t)((offered_ || given_back_ ? 0x10 : 0) | (inside_ ? 0x30 : 0));
    if (inside_)
        for (size_t i = 0; i < id_.size(); i++) s[2 + i] = id_[i];
    return s;
}

void IcCardUnit::slot(uint8_t mode) {
    open_ = mode == 0x11;
    if (mode == 0x12 && inside_) {   // eject: out at the slot until the player takes it
        inside_ = false;
        offered_ = false;
        given_back_ = true;
    }
    if (open_ && offered_) inside_ = true;
}

void IcCardUnit::answer(const uint8_t *request, const std::vector<uint8_t> &payload, std::vector<uint8_t> &reply) const {
    std::vector<uint8_t> body = {0x81, request[2], request[3], request[4], (uint8_t)payload.size()};
    body.insert(body.end(), payload.begin(), payload.end());
    uint8_t sum = 0;
    for (uint8_t b : body) sum = (uint8_t)(sum + b);
    body.push_back(sum);
    // after the aa, an aa or ff goes as ff and the byte inverted (a card id can hold either)
    reply.push_back(0xaa);
    for (uint8_t b : body) {
        if (b == 0xaa || b == 0xff) {
            reply.push_back(0xff);
            reply.push_back((uint8_t)~b);
        } else {
            reply.push_back(b);
        }
    }
}

bool IcCardUnit::write(const uint8_t *data, uint32_t size, std::vector<uint8_t> &reply) {
    if (size == 0 || data[0] != 0xaa) return false;
    if (size == 1) {   // the sync byte
        reply.push_back(0xaa);
        return true;
    }
    std::vector<uint8_t> in;   // the packets unescaped (icca.h)
    for (uint32_t i = 0; i < size; i++) {
        if (data[i] == 0xff && i + 1 < size) in.push_back((uint8_t)~data[++i]);
        else in.push_back(data[i]);
    }
    data = in.data();
    size = (uint32_t)in.size();
    for (uint32_t at = 0; at + 6 <= size && data[at] == 0xaa;) {
        const uint8_t *p = data + at;
        const uint32_t len = p[5];
        if (at + 7 + len > size) break;
        const uint16_t cmd = (uint16_t)(p[2] << 8 | p[3]);
        switch (cmd) {
        case 0x0134: answer(p, status(false), reply); break;              // state
        case 0x0135: slot(len >= 2 ? p[7] : 0); answer(p, status(false), reply); break;   // 18, the mode
        case 0x0139: answer(p, status(true), reply); break;               // read the card
        case 0x0110: answer(p, {0, 0, (uint8_t)(keys_ & 0xff), (uint8_t)(keys_ >> 8)}, reply); break;
        default: answer(p, {0}, reply); break;                            // 00 03, 01 00, 01 30
        }
        at += 7 + len;
    }
    return true;
}

void IcCardUnit::card_button() {
    if (inside_) return;   // the reader holds it until the game gives it back
    if (given_back_) { given_back_ = false; return; }
    offered_ = !offered_;
    if (offered_ && open_) inside_ = true;
}

void IcCardUnit::key(uint8_t scan, bool down) {
    const int bit = key_bit(scan);
    if (bit < 0) return;
    if (down) keys_ |= (uint16_t)(1u << bit);
    else keys_ &= (uint16_t)~(1u << bit);
}

std::string IcCardUnit::state() const {
    if (inside_) return "card in the reader";
    if (given_back_) return "card given back at the slot (Insert takes it)";
    if (offered_) return "card at the slot";
    return "";
}

}  // namespace hk::py
