// SPDX-License-Identifier: GPL-3.0-or-later
// The network a Python 1 cabinet's e-AMUSEMENT sees, inside HIRAKU (eanet.h).
#include "python/eanet.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace hk::py {

namespace {

constexpr uint8_t BOARD_IP[4] = {192, 168, 50, 2};
constexpr uint8_t NETMASK[4] = {255, 255, 255, 0};
constexpr uint8_t GATEWAY[4] = {192, 168, 50, 1};
constexpr uint8_t SERVER_IP[4] = {192, 168, 50, 1};   // DNS and "services"
constexpr uint32_t SERVER_IP_BE = 0xc0a83201;
constexpr uint32_t SERVER_IP_RAW = 0x0132a8c0;       // as the board's status block carries it

std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...) {
    char b[512];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

uint16_t checksum(const uint8_t *p, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) sum += (uint32_t)p[i] << 8 | p[i + 1];
    if (n & 1) sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

// An ICMP echo request to `to` (big-endian address) answered: IPv4 header, then the echo as a reply
std::vector<uint8_t> echo_reply(const std::vector<uint8_t> &icmp, uint32_t to) {
    std::vector<uint8_t> r(20, 0);
    const size_t total = 20 + icmp.size();
    r[0] = 0x45; r[2] = (uint8_t)(total >> 8); r[3] = (uint8_t)total;
    r[8] = 64; r[9] = 1;   // TTL, ICMP
    r[12] = (uint8_t)(to >> 24); r[13] = (uint8_t)(to >> 16); r[14] = (uint8_t)(to >> 8); r[15] = (uint8_t)to;
    for (int i = 0; i < 4; i++) r[16 + i] = BOARD_IP[i];
    const uint16_t hc = checksum(r.data(), 20);
    r[10] = (uint8_t)(hc >> 8); r[11] = (uint8_t)hc;
    std::vector<uint8_t> e = icmp;
    e[0] = 0;   // echo reply
    e[2] = e[3] = 0;
    const uint16_t ec = checksum(e.data(), e.size());
    e[2] = (uint8_t)(ec >> 8); e[3] = (uint8_t)ec;
    r.insert(r.end(), e.begin(), e.end());
    return r;
}

}  // namespace

// The A record for a name: "services..." is the server; any other name an address of its own
std::vector<uint8_t> VirtualNet::dns_answer(const uint8_t *q, size_t n, std::string *name) {
    if (n < 12 || q[4] != 0 || q[5] == 0) return {};
    size_t at = 12;
    while (at < n && q[at] != 0) {
        const size_t len = q[at];
        if ((len & 0xc0) || at + 1 + len >= n) return {};
        if (!name->empty()) *name += '.';
        name->append(reinterpret_cast<const char *>(q + at + 1), len);
        at += 1 + len;
    }
    if (at + 5 > n || name->empty()) return {};
    at += 5;   // the terminating 0, type, class
    uint32_t ip = SERVER_IP_BE;
    if (name->compare(0, 8, "services") != 0) {
        auto it = name_ip_.find(*name);
        if (it == name_ip_.end()) {
            const uint32_t next = 0xc0a8320a + (uint32_t)name_ip_.size();   // 192.168.50.10 on
            if ((next & 0xff) == 0xff) return {};
            it = name_ip_.emplace(*name, next).first;
            ip_name_[next] = *name;
        }
        ip = it->second;
    }
    std::vector<uint8_t> r(q, q + at);
    r[2] = 0x81; r[3] = 0x80;
    r[4] = 0; r[5] = 1; r[6] = 0; r[7] = 1;
    r[8] = r[9] = r[10] = r[11] = 0;
    r.insert(r.end(), {0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 60, 0x00, 0x04,
                       (uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip});
    return r;
}

VirtualNet::Target VirtualNet::target_of(uint32_t ip, uint16_t port) const {
    Target t;
    t.port = port;
    if (ip == SERVER_IP_BE) {
        t.host = "services";
        t.services = true;
    } else if (auto it = ip_name_.find(ip); it != ip_name_.end()) {
        t.host = it->second;
    } else {
        t.host = fmt("%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    }
    return t;
}

void VirtualNet::respond(uint32_t channel, int32_t ret, uint32_t w2, uint32_t w3) {
    uint32_t w[8] = {};
    w[1] = __builtin_bswap32((uint32_t)ret);
    w[2] = w2;
    w[3] = w3;
    io_.respond(channel, w);
}

void VirtualNet::http_progress(Socket &s) {
    const size_t head = s.request.find("\r\n\r\n");
    if (head == std::string::npos) return;
    size_t body = 0;
    std::string lower = s.request.substr(0, head);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (size_t at = lower.find("\r\ncontent-length:"); at != std::string::npos) body = std::strtoul(lower.c_str() + at + 17, nullptr, 10);
    if (s.request.size() < head + 4 + body) return;
    const std::string req = s.request.substr(0, head + 4 + body);
    s.request.erase(0, head + 4 + body);
    s.in_flight = true;
    const uint64_t id = s.id;
    http_(s.target, req, [this, id](std::string answer) {
        std::lock_guard<std::mutex> l(done_mutex_);
        done_.emplace_back(id, std::move(answer));
    });
    poll();   // an answer given at once is there at once
}

void VirtualNet::deliver(Socket &s, uint32_t ch, uint32_t buf, uint32_t len) {
    if (s.pending.empty()) {
        respond(ch, 0);   // the server has closed (HTTP/1.0)
        if (log) log(fmt("ch%u 16 recv = 0", ch));
        return;
    }
    const uint32_t n = (uint32_t)std::min<size_t>(s.pending.size(), len);
    io_.write(buf, reinterpret_cast<const uint8_t *>(s.pending.data()), n);
    s.pending.erase(0, n);
    respond(ch, (int32_t)n);
    if (log) log(fmt("ch%u 16 recv = %u", ch, n));
}

void VirtualNet::poll() {
    std::vector<std::pair<uint64_t, std::string>> done;
    {
        std::lock_guard<std::mutex> l(done_mutex_);
        done.swap(done_);
    }
    for (auto &[id, answer] : done)
        for (auto &[fd, s] : sockets_) {
            if (s.id != id) continue;
            s.pending += answer;
            s.in_flight = false;
            s.answered = true;
            if (s.waiting) {
                s.waiting = false;
                deliver(s, s.wait_ch, s.wait_buf, s.wait_len);
            }
        }
}

bool VirtualNet::command(uint32_t ch, const uint32_t *p, uint32_t quads) {
    if (quads < 1) return false;
    const uint32_t cmd = p[0];
    auto arg = [&](uint32_t i) { return i < quads ? p[i] : 0u; };
    auto note = [&](const std::string &s) { if (log) log(fmt("ch%u %02x ", ch, cmd) + s); };
    switch (cmd) {
    case 0x1e: {   // a board property: the addresses; the machine id and the rest stay the emulator's
        const uint8_t *v = nullptr;
        switch (arg(1)) {
        case 0x101: v = BOARD_IP; break;
        case 0x001: v = NETMASK; break;
        case 0x003: v = GATEWAY; break;
        case 0x006: v = SERVER_IP; break;
        default: return false;
        }
        const uint32_t n = std::min<uint32_t>(arg(3), 4);
        io_.write(arg(2), v, n);
        respond(ch, (int32_t)n);
        note(fmt("property %x: %u.%u.%u.%u", arg(1), v[0], v[1], v[2], v[3]));
        return true;
    }
    case 0x0b: {   // socket(domain, type, protocol)
        int fd = 0;
        while (sockets_.count(fd)) fd++;
        sockets_[fd].type = (int)arg(2);
        sockets_[fd].id = next_id_++;
        respond(ch, fd);
        note(fmt("socket(%u, %u) = %d", arg(1), arg(2), fd));
        return true;
    }
    case 0x0c:   // bind
    case 0x13:   // setsockopt
    case 0x1c:   // shutdown
        respond(ch, 0);
        note(fmt("%08x %08x %08x = 0", arg(1), arg(2), arg(3)));
        return true;
    case 0x0d: case 0x0e:   // listen, accept: nobody calls in
        respond(ch, -1);
        note("refused");
        return true;
    case 0x0f: {   // connect(fd, family/port, address)
        const int fd = (int)arg(1);
        auto it = sockets_.find(fd);
        const bool ok = it != sockets_.end();
        if (ok) {
            it->second.connected = true;
            it->second.target = target_of(arg(3), (uint16_t)arg(2));
        }
        respond(ch, ok ? 0 : -1);
        note(fmt("connect(%d, %u.%u.%u.%u:%u) = %d", fd, arg(3) >> 24, (arg(3) >> 16) & 255, (arg(3) >> 8) & 255,
                 arg(3) & 255, arg(2) & 0xffff, ok ? 0 : -1) + (ok ? " " + it->second.target.host : ""));
        return true;
    }
    case 0x14: case 0x15: {   // send / sendto(fd, buffer, length, flags[, family/port, address])
        const int fd = (int)(arg(1) >> 16);
        const uint32_t n = std::min<uint32_t>(arg(3), 0x2000);
        std::vector<uint8_t> d(n);
        if (n && !io_.read(arg(2), d.data(), n)) return false;
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) { respond(ch, -1); note(fmt("fd %d: no such socket", fd)); return true; }
        Socket &s = it->second;
        if (s.type == 3) {   // raw ICMP: the ping check (0x1885b8) wants the echo back with its IP header
            if (n >= 8 && d[0] == 8) s.datagrams.push_back(echo_reply(d, arg(5)));
            note(fmt("ping(%d, %u bytes) %u.%u.%u.%u", fd, n, arg(5) >> 24, (arg(5) >> 16) & 255, (arg(5) >> 8) & 255, arg(5) & 255));
        } else if (s.type == 2) {
            std::string name;
            const uint16_t port = (uint16_t)arg(4);
            if (port == 53) {
                auto r = dns_answer(d.data(), d.size(), &name);
                if (!r.empty()) s.datagrams.push_back(std::move(r));
            }
            note(fmt("sendto(%d, %u bytes, port %u) %s", fd, n, port, name.c_str()));
        } else {
            s.request.append(reinterpret_cast<const char *>(d.data()), d.size());
            note(fmt("send(%d, %u bytes)", fd, n));
            http_progress(s);
        }
        respond(ch, (int32_t)n);
        return true;
    }
    case 0x16: case 0x17: {   // recv / recvfrom(fd, buffer, length, ...)
        const int fd = (int)(arg(1) >> 16);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) { respond(ch, -1); note(fmt("fd %d: no such socket", fd)); return true; }
        Socket &s = it->second;
        if (s.type == 2 || s.type == 3) {
            if (s.datagrams.empty()) { respond(ch, 0); note(fmt("recvfrom(%d) nothing", fd)); return true; }
            std::vector<uint8_t> d = std::move(s.datagrams.front());
            s.datagrams.erase(s.datagrams.begin());
            d.resize(std::min<size_t>(d.size(), arg(3)));
            io_.write(arg(2), d.data(), (uint32_t)d.size());
            uint8_t from[4] = {};
            io_.read(arg(5), from, 4);
            uint32_t from_word;
            std::memcpy(&from_word, from, 4);
            const uint32_t src = s.type == 3 && d.size() >= 16 ? (uint32_t)d[12] | d[13] << 8 | d[14] << 16 | (uint32_t)d[15] << 24 : SERVER_IP_RAW;
            respond(ch, (int32_t)d.size(), src, (from_word & 0xffff0000) | (s.type == 3 ? 0 : 53));
            note(fmt("recvfrom(%d) = %zu", fd, d.size()));
            return true;
        }
        if (s.pending.empty() && s.in_flight) {   // the answer is on its way: the recv waits for it
            s.waiting = true;
            s.wait_ch = ch;
            s.wait_buf = arg(2);
            s.wait_len = arg(3);
            note(fmt("recv(%d) waits", fd));
            return true;
        }
        if (s.pending.empty() && !s.answered) {
            respond(ch, -0xe);
            note(fmt("recv(%d) = -14 (nothing sent)", fd));
            return true;
        }
        deliver(s, ch, arg(2), arg(3));
        return true;
    }
    case 0x1d: {   // close
        const int fd = (int)arg(1);
        sockets_.erase(fd);
        respond(ch, 0);
        note(fmt("close(%d)", fd));
        return true;
    }
    default:
        return false;
    }
}

}  // namespace hk::py
