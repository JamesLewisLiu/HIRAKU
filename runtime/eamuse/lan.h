// SPDX-License-Identifier: GPL-3.0-or-later
// The network an e-AMUSEMENT cabinet sees, inside HIRAKU: no host sockets of its own. The
// cabinet's network hardware (the Python 1 interface board, the System 573's network PCB unit)
// takes BSD-style socket calls from the game; its device turns them into calls here, which
// answer as a small LAN would (from the Python core's VirtualNet, python-groundwork branch):
//   - the cabinet's addresses: 192.168.50.2/24, gateway and DNS 192.168.50.1;
//   - DNS (UDP 53): "services..." is 192.168.50.1; any other name gets an address of its own
//     on the LAN (192.168.50.10 on), remembered, so a connection to it names its host again;
//   - ICMP echo (the games' router check) answered, to every raw socket;
//   - NTP (UDP 123): the time of `now`, from any address (the GuitarFreaks and DrumMania server
//     names its NTP server);
//   - TCP: each HTTP request the game sends goes to a handler, with the host and port it
//     connected to. The handler answers at once (the server in the core: reproducible) or later
//     (a real server on the network); until then a recv has nothing, and the device waits.
// Addresses and ports are numbers here (192.168.50.1 = 0xc0a83201); each device converts its
// game's layout.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace hk::ea {

class Lan {
public:
    static constexpr uint32_t CABINET = 0xc0a83202;   // 192.168.50.2
    static constexpr uint32_t NETMASK = 0xffffff00;
    static constexpr uint32_t ROUTER = 0xc0a83201;    // gateway, DNS, the game's own server

    // Where a connection goes: the name the game looked up ("services..." for its e-AMUSEMENT
    // server), or a dotted address if it connected to one directly; and the port.
    struct Target {
        std::string host;
        uint16_t port = 0;
        bool services = false;   // the game's own server ("services...")
    };
    // One HTTP request in (bytes as sent); call `done` with the whole response, now or later
    // and from any thread ("" = the connection failed).
    using Http = std::function<void(const Target &, const std::string &request, std::function<void(std::string)> done)>;

    explicit Lan(Http http) : http_(std::move(http)) {}

    enum Type { STREAM = 1, DATAGRAM = 2, RAW = 3 };   // raw: ICMP
    int socket(Type type);   // a descriptor (the lowest free)
    int connect(int fd, uint32_t ip, uint16_t port);   // 0, or -1: no such socket
    int send(int fd, const uint8_t *data, size_t n);   // n, or -1
    int sendto(int fd, const uint8_t *data, size_t n, uint32_t ip, uint16_t port);
    int close(int fd);

    // What a recv finds: bytes, nothing yet (the answer is on its way: a device's recv waits for
    // it, as the real one blocks), the end (the server has closed: HTTP/1.0), or an error.
    enum Status { DATA, WAIT, END, NOTHING, BAD };
    Status recv(int fd, size_t max, std::string *out);
    // a datagram, and where it came from; NOTHING if none is waiting
    Status recvfrom(int fd, size_t max, std::string *out, uint32_t *ip, uint16_t *port);

    // Answers that arrived from another thread, taken in (call it from the emulator's thread);
    // true if any did.
    bool poll();

    // The time NTP gives: seconds since 1970 (as the cabinet's clock reads them)
    std::function<int64_t()> now;

    // Each call and its result, for a device's network log.
    std::function<void(const std::string &)> log;

private:
    struct Socket {
        uint64_t id = 0;           // unique, unlike the descriptor
        Type type = STREAM;
        Target target;
        bool connected = false;
        std::string request;       // stream: bytes the game sent, not complete yet
        std::string pending;       // stream: answer not read yet
        bool in_flight = false;    // a request is with the handler
        bool answered = false;     // the handler has answered (and will close: HTTP/1.0)
        struct Datagram {
            std::string data;
            uint32_t ip;
            uint16_t port;
        };
        std::vector<Datagram> datagrams;   // datagram, raw: not read yet
    };
    void note(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void http_progress(Socket &s);
    std::string dns_answer(const std::string &q, std::string *name);
    Target target_of(uint32_t ip, uint16_t port) const;
    Socket *find(int fd);

    Http http_;
    std::map<int, Socket> sockets_;
    uint64_t next_id_ = 1;
    std::map<std::string, uint32_t> name_ip_;   // DNS names given out, and back
    std::map<uint32_t, std::string> ip_name_;
    std::mutex done_mutex_;                       // answers from the handler, any thread
    std::vector<std::pair<uint64_t, std::string>> done_;
};

}  // namespace hk::ea
