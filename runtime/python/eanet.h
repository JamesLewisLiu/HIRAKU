// SPDX-License-Identifier: GPL-3.0-or-later
// The network a Python 1 cabinet's e-AMUSEMENT sees, inside HIRAKU: no host sockets. The
// interface board has the network port, and the game (through the IOP) sends it BSD-style
// socket commands on channels 0-7 (PLAN.md section 21, "e-AMUSEMENT"). This answers them
// as a small LAN would:
//   - the board's addresses: 192.168.50.2/24, gateway and DNS 192.168.50.1;
//   - DNS (UDP 53): any name starting "services" is 192.168.50.1;
//   - TCP to 192.168.50.1: an HTTP/1.0 server in the process (a handler per request).
// Board commands (the game's RPC number + 1): 0b socket, 0c bind, 0d listen, 0e accept,
// 0f connect, 13 setsockopt, 14 send, 15 sendto, 16 recv, 17 recvfrom, 1c shutdown,
// 1d close, 1e a board property. The rest (start-up, DHCP, the machine id) stay the
// emulator's.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hk::py {

class VirtualNet {
public:
    // How it reaches the emulator: data into IOP memory, the channel's status block, and
    // reading what the game sends.
    struct Io {
        std::function<void(uint32_t addr, const uint8_t *data, uint32_t size)> write;
        std::function<void(uint32_t channel, const uint32_t words[8])> respond;
        std::function<bool(uint32_t addr, uint8_t *data, uint32_t size)> read;
    };
    // One HTTP request in (bytes as sent), the whole response out.
    using Http = std::function<std::string(const std::string &request)>;

    VirtualNet(Io io, Http http) : io_(std::move(io)), http_(std::move(http)) {}

    // A board socket command; false = not ours.
    bool command(uint32_t channel, const uint32_t *p, uint32_t quads);

    // Each command and its answer, for HK_PY_NET_LOG.
    std::function<void(const std::string &)> log;

private:
    struct Socket {
        int type = 0;              // 1 stream, 2 datagram
        bool connected = false;    // to the server
        std::string request;       // stream: bytes the game sent, not answered yet
        std::string pending;       // stream: answer not read yet
        bool answered = false;     // the server has answered (and will close: HTTP/1.0)
        std::vector<std::vector<uint8_t>> datagrams;   // datagram: answers not read yet
    };
    void respond(uint32_t channel, int32_t ret, uint32_t w2 = 0, uint32_t w3 = 0);
    void http_progress(Socket &s);

    Io io_;
    Http http_;
    std::map<int, Socket> sockets_;
};

}  // namespace hk::py
