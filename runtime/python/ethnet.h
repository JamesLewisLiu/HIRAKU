// SPDX-License-Identifier: GPL-3.0-or-later
// The network a Python 2 cabinet sees, inside HIRAKU: Ethernet frames from the PS2's own
// network adapter (DEV9 SMAP), answered on the emulator's CPU thread, so runs repeat exactly.
// No host sockets. The game's boot program waits for a network before anything else (PLAN.md
// section 23, PY-6), then asks a server for updates and goes on without one. This answers as
// a small LAN with nobody on it would:
//   - DHCP: the cabinet is 192.168.50.2/24, gateway and DNS 192.168.50.1 (as the Python 1
//     cabinets' network, eanet.h);
//   - ARP: every other address on the LAN answers, with the LAN's one MAC address;
//   - ICMP echo: answered;
//   - DNS: no such name;
//   - TCP: every connection refused (a reset), UDP to anything else dropped.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace hk::py {

class EtherNet {
public:
    // A frame the game sent; the answers are queued for take()
    void send(const uint8_t *frame, uint32_t size);
    // The next frame for the game (its size), 0 when there is none
    uint32_t take(uint8_t *frame, uint32_t capacity);

    std::function<void(const std::string &)> log;   // HK_PY_NET_LOG

    static constexpr uint32_t CABINET = 0xc0a83202, SERVER = 0xc0a83201, MASK = 0xffffff00;

private:
    void arp(const uint8_t *f, uint32_t n);
    void ip(const uint8_t *f, uint32_t n);
    void dhcp(const uint8_t *f, uint32_t n, uint32_t udp);
    void dns(const uint8_t *f, uint32_t n, uint32_t udp);
    // a frame to the game: Ethernet header from the LAN's MAC to the game's, then the IP
    // packet with its header (and the UDP/TCP/ICMP checksums) filled in
    void reply_ip(uint8_t proto, uint32_t src, uint32_t dst, std::vector<uint8_t> payload);

    uint8_t game_mac_[6] = {};
    bool have_mac_ = false;
    uint16_t ip_id_ = 1;
    std::deque<std::vector<uint8_t>> out_;
};

}  // namespace hk::py
