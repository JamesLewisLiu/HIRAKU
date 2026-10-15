// SPDX-License-Identifier: GPL-3.0-or-later
// The lobby that pop'n music 12-14's NET TAISEN finds its opponents through (docs/EAMUSE3.md,
// "NET TAISEN"). The services list names it as globby://HOST:PORT; the game sends it UDP datagrams
// from its port 3200 and waits for one answer each (pop'n 12 0x199860):
//   - a 12-byte header: "lob0", the length (big-endian, the header included), a CRC-16/X-25 of the
//     whole datagram taken with its own field 0, a sequence number the answer repeats, the kind
//     (0 entry, 1 list, 2 leave) and a 0;
//   - entry (0x28 bytes): the room mask, the cabinet's address and port as the world sees them
//     (portfw get's globalip and port), how many seconds to keep it, and 16 bytes of the
//     cabinet's own (its address on the shop's LAN first);
//   - list (0x10): the room mask. The answer holds the cabinets in a room of the mask, 0x1c bytes
//     each: the room mask, the address, the port, the 16 bytes; then zeros (0x199aa8: a port of 0
//     ends it; 0x19d380);
//   - leave (0x14): the address and port.
// The answer's header has the request's sequence number and kind 0 (any other is refused,
// 0x199d98: "Replay type mismatched").
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hk::py {

class Lobby {
public:
    // The port HIRAKU's lobby listens on (the services list's globby URL names it)
    static constexpr uint16_t PORT = 5740;

    // One datagram in, at `now` seconds; the answer (empty: none, the request was malformed)
    std::vector<uint8_t> handle(const uint8_t *d, size_t n, double now);

private:
    struct Entry {
        uint32_t rooms = 0;
        uint8_t addr[4] = {};
        uint16_t port = 0;
        uint8_t own[16] = {};
        double until = 0;
    };
    std::vector<Entry> entries_;
};

}  // namespace hk::py
