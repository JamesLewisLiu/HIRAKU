// SPDX-License-Identifier: GPL-3.0-or-later
// NET TAISEN online through hiraku-eamuse's relay (netrelay.h).
#include "python/netrelay.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "python/lobby.h"

namespace hk::py {

namespace {

constexpr uint32_t MAGIC = 0x48'4b'50'52;   // "HKPR"
constexpr uint32_t VERSION = 1;
constexpr uint32_t FIRST = 0x0a390001;      // 10.57.0.1, the first cabinet's address

struct Hello {
    uint32_t magic, version, address;   // address: from the relay, the cabinet's
};

struct PacketHeader {
    uint32_t src, dst;
    uint16_t sport, dport;
    uint32_t size;
};

std::string dotted(uint32_t a) {
    char b[20];
    snprintf(b, sizeof b, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
    return b;
}

bool send_all(int fd, const void *p, size_t n) {
    const char *c = (const char *)p;
    while (n) {
        ssize_t k = send(fd, c, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        c += k;
        n -= (size_t)k;
    }
    return true;
}

bool recv_all(int fd, void *p, size_t n) {
    char *c = (char *)p;
    while (n) {
        ssize_t k = recv(fd, c, n, 0);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        c += k;
        n -= (size_t)k;
    }
    return true;
}

bool send_packet(int fd, const LanCable::Packet &k) {
    PacketHeader h{k.src, k.dst, k.sport, k.dport, (uint32_t)k.data.size()};
    return send_all(fd, &h, sizeof h) && (k.data.empty() || send_all(fd, k.data.data(), k.data.size()));
}

}  // namespace

// --- the cabinet's side ---

RelayClient::~RelayClient() {
    if (fd_ >= 0) shutdown(fd_, SHUT_RDWR);
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) close(fd_);
}

bool RelayClient::open(const std::string &host, uint16_t port, int seconds, std::string *error) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (int e = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res); e != 0) {
        *error = host + ": " + gai_strerror(e);
        return false;
    }
    int fd = -1;
    for (addrinfo *a = res; a && fd < 0; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        // connect within `seconds`
        fcntl(fd, F_SETFL, O_NONBLOCK);
        int r = connect(fd, a->ai_addr, a->ai_addrlen);
        if (r < 0 && errno == EINPROGRESS) {
            pollfd p{fd, POLLOUT, 0};
            int err = 0;
            socklen_t len = sizeof err;
            r = poll(&p, 1, seconds * 1000) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && !err ? 0 : -1;
            if (err) errno = err;
        }
        if (r != 0) {
            *error = host + ":" + std::to_string(port) + ": " + (errno ? strerror(errno) : "no answer");
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);
    if (fd < 0) return false;
    fcntl(fd, F_SETFL, 0);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    timeval tv{seconds, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    Hello mine{MAGIC, VERSION, 0}, theirs{};
    if (!send_all(fd, &mine, sizeof mine) || !recv_all(fd, &theirs, sizeof theirs) || theirs.magic != MAGIC ||
        theirs.version != VERSION) {
        *error = host + ":" + std::to_string(port) + ": not a HIRAKU relay (or another version)";
        close(fd);
        return false;
    }
    tv = {0, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    fd_ = fd;
    address_ = theirs.address;
    thread_ = std::thread([this] { reader(); });
    return true;
}

void RelayClient::reader() {
    for (;;) {
        PacketHeader h{};
        if (!recv_all(fd_, &h, sizeof h) || h.size > 65536) break;
        LanCable::Packet k{h.src, h.dst, h.sport, h.dport, std::vector<uint8_t>(h.size)};
        if (h.size && !recv_all(fd_, k.data.data(), h.size)) break;
        std::lock_guard<std::mutex> l(mutex_);
        came_.push_back(std::move(k));
    }
    std::lock_guard<std::mutex> l(mutex_);
    lost_ = true;
}

void RelayClient::exchange(std::vector<LanCable::Packet> &out, std::vector<LanCable::Packet> &in) {
    if (fd_ < 0) return;
    bool lost;
    {
        std::lock_guard<std::mutex> l(mutex_);
        in.insert(in.end(), std::make_move_iterator(came_.begin()), std::make_move_iterator(came_.end()));
        came_.clear();
        lost = lost_;
    }
    if (!lost)
        for (const LanCable::Packet &k : out)
            if (!send_packet(fd_, k)) { lost = true; break; }
    if (lost) {
        fprintf(stderr, "relay: the connection is gone; NET TAISEN online is off now\n");
        shutdown(fd_, SHUT_RDWR);
        if (thread_.joinable()) thread_.join();
        close(fd_);
        fd_ = -1;
    }
}

// --- the server's side ---

bool RelayServer::start(const std::string &bind_addr, uint16_t port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    const int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_addr.c_str(), &sa.sin_addr) != 1 || bind(listen_fd_, (sockaddr *)&sa, sizeof sa) != 0 ||
        listen(listen_fd_, 16) != 0) {
        fprintf(stderr, "relay: %s:%u: %s\n", bind_addr.c_str(), port, strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    thread_ = std::thread([this] { run(); });
    thread_.detach();
    return true;
}

void RelayServer::run() {
    struct Cabinet {
        uint32_t address = 0;
        bool greeted = false;
        std::string in;   // bytes not yet a whole hello or datagram
    };
    std::map<int, Cabinet> cabinets;
    uint32_t next = FIRST;
    Lobby lobby;
    const auto t0 = std::chrono::steady_clock::now();
    auto say = [&](const std::string &s) { if (log) log("relay: " + s); };
    for (;;) {
        std::vector<pollfd> fds{{listen_fd_, POLLIN, 0}};
        for (auto &[fd, c] : cabinets) fds.push_back({fd, POLLIN, 0});
        if (poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (fds[0].revents & POLLIN) {
            sockaddr_in peer{};
            socklen_t pl = sizeof peer;
            const int fd = accept(listen_fd_, (sockaddr *)&peer, &pl);
            if (fd >= 0) {
                int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                cabinets[fd];
            }
        }
        std::vector<int> gone;
        for (size_t i = 1; i < fds.size(); i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const int fd = fds[i].fd;
            Cabinet &c = cabinets[fd];
            char buf[8192];
            const ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n <= 0) { gone.push_back(fd); continue; }
            c.in.append(buf, (size_t)n);
            if (!c.greeted) {
                if (c.in.size() < sizeof(Hello)) continue;
                Hello h;
                memcpy(&h, c.in.data(), sizeof h);
                c.in.erase(0, sizeof h);
                if (h.magic != MAGIC || h.version != VERSION) { gone.push_back(fd); continue; }
                c.address = next++;
                Hello mine{MAGIC, VERSION, c.address};
                if (!send_all(fd, &mine, sizeof mine)) { gone.push_back(fd); continue; }
                c.greeted = true;
                size_t here = 0;
                for (auto &[ofd, o] : cabinets) here += o.greeted;
                say("a cabinet came, " + dotted(c.address) + " (" + std::to_string(here) + " now)");
            }
            while (c.in.size() >= sizeof(PacketHeader)) {
                PacketHeader h;
                memcpy(&h, c.in.data(), sizeof h);
                if (h.size > 65536) { gone.push_back(fd); break; }
                if (c.in.size() < sizeof h + h.size) break;
                LanCable::Packet k{c.address, h.dst, h.sport, h.dport,
                                   std::vector<uint8_t>(c.in.begin() + sizeof h, c.in.begin() + (ptrdiff_t)(sizeof h + h.size))};
                c.in.erase(0, sizeof h + h.size);
                // to a cabinet: passed on, from the sender's address
                int to = -1;
                for (auto &[ofd, o] : cabinets)
                    if (o.greeted && o.address == k.dst) to = ofd;
                if (to >= 0) {
                    if (!send_packet(to, k)) gone.push_back(to);
                } else if (k.dport == Lobby::PORT) {   // the lobby, at the address the game sent to
                    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    std::vector<uint8_t> r = lobby.handle(k.data.data(), k.data.size(), now);
                    if (!r.empty() && !send_packet(fd, {k.dst, k.src, Lobby::PORT, k.sport, std::move(r)})) gone.push_back(fd);
                    if (log) {
                        static const char *kinds[] = {"entry", "list", "leave"};
                        const int kind = k.data.size() > 10 ? k.data[10] : -1;
                        say(dotted(k.src) + " lobby " + (kind >= 0 && kind < 3 ? kinds[kind] : "?"));
                    }
                }
                // anywhere else (a broadcast, a shop's LAN): nowhere
            }
        }
        for (int fd : gone)
            if (auto it = cabinets.find(fd); it != cabinets.end()) {
                if (it->second.greeted) say(dotted(it->second.address) + " left");
                close(fd);
                cabinets.erase(it);
            }
    }
}

}  // namespace hk::py
