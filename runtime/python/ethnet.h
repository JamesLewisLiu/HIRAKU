// SPDX-License-Identifier: GPL-3.0-or-later
// The network a Python 2 cabinet sees, inside HIRAKU: Ethernet frames from the PS2's own
// network adapter (DEV9 SMAP), answered on the emulator's CPU thread, so runs repeat exactly.
// No host sockets. The game's boot program waits for a network before anything else (PLAN.md
// section 23, PY-6), then asks a server for updates and goes on without one. This answers as
// a small LAN with nobody on it would:
//   - DHCP: the cabinet is 10.50.0.2/24, gateway and DNS 10.50.0.1. Not 192.168.x.x as the
//     Python 1 cabinets' network (eanet.h): the e-AMUSEMENT library takes a cabinet whose own
//     address is 192.168.x.x (or none) to have no router, "ルーターと通信できません" 5-2000-0000
//     (eamusement's check, 0x947a64: (address & 0xffff) == 0xa8c0);
//   - ARP: every other address on the LAN answers, with the LAN's one MAC address;
//   - ICMP echo: answered;
//   - SNTP (UDP 123 to 10.50.0.1): the cabinet's clock (`now`, as the e-AMUSEMENT server's);
//   - DNS: "services..." (e-AMUSEMENT's first server, on when the game's NETWORK OPTIONS say
//     so) is 10.50.0.1; any other name, none (so the boot program finds no update);
//   - TCP to 10.50.0.1:80: HTTP, each request handed to `http` (the e-AMUSEMENT server in the
//     core) and its answer sent back, the connection closed after it (HTTP/1.0). The link
//     never loses or reorders a frame, so no retransmission: only the game's window is kept;
//   - AVS services (avs-services.c, the library's own start-up, without which the game reports
//     "ルーターと通信できません" 5-2000-0000): its broadcast to UDP 42197 ("SgetRQ" + MAC + the two
//     XORed) gets 10.50.0.1:42197 back (6 bytes the library skips, the address, the port);
//     its "GET <MAC>\r\n" there, an empty list (a line, the list's length, then lines
//     "name://a.b.c.d:port", none);
//   - any other TCP connection refused (a reset), UDP to anything else dropped.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
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
    // an HTTP request to 10.50.0.1:80, whole (head and Content-Length body) -> the answer
    std::function<std::string(const std::string &)> http;
    // SNTP's answer: the time, Unix seconds (the machine's clock: repeatable)
    std::function<int64_t()> now;

    static constexpr uint32_t CABINET = 0x0a320002, SERVER = 0x0a320001, MASK = 0xffffff00;

private:
    void arp(const uint8_t *f, uint32_t n);
    void ip(const uint8_t *f, uint32_t n);
    void dhcp(const uint8_t *f, uint32_t n, uint32_t udp);
    void dns(const uint8_t *f, uint32_t n, uint32_t udp);
    void sntp(const uint8_t *f, uint32_t n, uint32_t udp);
    void tcp(const uint8_t *f, uint32_t n, uint32_t at, uint32_t len, uint32_t src, uint32_t dst);
    void refuse(const uint8_t *f, uint32_t at, uint32_t len, uint32_t src, uint32_t dst);

    // a connection to the server's port 80, by the game's port
    static constexpr uint16_t AVS_SERVICES = 42197;
    struct Conn {
        uint16_t local = 80;         // our port: 80 (HTTP) or AVS_SERVICES
        uint32_t rcv_nxt = 0;        // the next byte expected from the game
        uint32_t snd_una = 0, snd_nxt = 0, window = 0;   // ours: first unacknowledged, next, the game's window
        std::string request, answer;   // what came in; what goes out (from snd_una's place on)
        size_t answer_at = 0;        // answer bytes sent so far (snd_nxt's place)
        bool answered = false, fin_sent = false, fin_got = false;
    };
    void segment(uint16_t port, Conn &c, uint8_t flags, const std::string &data);
    void push(uint16_t port, Conn &c);
    std::map<uint32_t, Conn> conns_;   // by our port << 16 | the game's port
    // a frame to the game: Ethernet header from the LAN's MAC to the game's, then the IP
    // packet with its header (and the UDP/TCP/ICMP checksums) filled in
    void reply_ip(uint8_t proto, uint32_t src, uint32_t dst, std::vector<uint8_t> payload);

    uint8_t game_mac_[6] = {};
    bool have_mac_ = false;
    uint16_t ip_id_ = 1;
    std::deque<std::vector<uint8_t>> out_;
};

}  // namespace hk::py
