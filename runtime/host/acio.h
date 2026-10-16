// SPDX-License-Identifier: GPL-3.0-or-later
// Konami's ACIO bus on a serial port, from the PC's side, as spice2x speaks it (reader/, acioemu/):
// the e-AMUSEMENT card readers (readers.h) and beatmania IIDX's BIO2 I/O board (bio2.h).
//
//   aa NODE CMD_HI CMD_LO ID LEN [payload] SUM   SUM: the 8-bit sum of NODE..payload. After the aa,
//                                                an aa or ff byte goes as ff and its inverse.
//
// Nodes are numbered from 1 (command 0001 to node 0); a node answers with a packet of its own (its
// node | 0x80, the command, the same ID). A run of zeros and aa wakes the bus, and a node answers aa.
// HK_ACIO_LOG=1 prints the packets.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hk::host {

class AcioPort {
public:
    struct Message {
        uint8_t node = 0, id = 0;
        uint16_t cmd = 0;
        std::vector<uint8_t> payload;
    };

    explicit AcioPort(std::string path);
    ~AcioPort();
    AcioPort(const AcioPort &) = delete;
    AcioPort &operator=(const AcioPort &) = delete;

    const std::string &path() const { return path_; }
    bool open();           // false with errno set
    void close();
    bool is_open() const { return fd_ >= 0; }

    // The handshake at each rate (baud) in turn, ten tries each, while keep_going(): the rate a
    // node answered at, or 0
    int handshake(const std::vector<int> &rates, const std::function<bool()> &keep_going);
    bool send(uint8_t node, uint16_t cmd, const std::vector<uint8_t> &payload);
    // The last whole packet with a good sum among what arrives: waiting up to wait_ms for it to
    // start, then until it pauses 30 ms (spice2x's port timeouts)
    bool receive(Message &m, int wait_ms = 500);
    bool ask(uint8_t node, uint16_t cmd, const std::vector<uint8_t> &payload, Message &m, int wait_ms = 500) {
        return send(node, cmd, payload) && receive(m, wait_ms);
    }

private:
    bool set_rate(int baud);
    bool write_all(const uint8_t *d, size_t n);
    std::vector<uint8_t> read_some(int first_ms, size_t most);
    void dump(const char *dir, const std::vector<uint8_t> &bytes) const;

    std::string path_;
    intptr_t fd_ = -1;   // a file descriptor; on Windows, the port's HANDLE
    uint8_t id_ = 0;
    bool log_ = false;
};

}  // namespace hk::host
