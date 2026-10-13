// SPDX-License-Identifier: GPL-3.0-or-later
// The network a Python 1 cabinet's e-AMUSEMENT sees, inside HIRAKU (eanet.h).
#include "python/eanet.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

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

// The A record for a "services..." name, NXDOMAIN for anything else; empty if not a query.
std::vector<uint8_t> dns_answer(const uint8_t *q, size_t n, std::string *name) {
    if (n < 12 || q[4] != 0 || q[5] == 0) return {};
    size_t at = 12;
    std::string first;
    while (at < n && q[at] != 0) {
        const size_t len = q[at];
        if ((len & 0xc0) || at + 1 + len >= n) return {};
        if (!name->empty()) *name += '.';
        name->append(reinterpret_cast<const char *>(q + at + 1), len);
        if (first.empty()) first = name->substr(0, len);
        at += 1 + len;
    }
    if (at + 5 > n) return {};
    at += 5;   // the terminating 0, type, class
    std::vector<uint8_t> r(q, q + at);
    const bool ours = first == "services";
    r[2] = 0x81; r[3] = ours ? 0x80 : 0x83;
    r[4] = 0; r[5] = 1; r[6] = 0; r[7] = ours ? 1 : 0;
    r[8] = r[9] = r[10] = r[11] = 0;
    if (ours)
        r.insert(r.end(), {0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 60, 0x00, 0x04,
                           SERVER_IP[0], SERVER_IP[1], SERVER_IP[2], SERVER_IP[3]});
    return r;
}

}  // namespace

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
    s.pending += http_(req);
    s.answered = true;
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
        const bool ok = it != sockets_.end() && arg(3) == SERVER_IP_BE;
        if (ok) it->second.connected = true;
        respond(ch, ok ? 0 : -1);
        note(fmt("connect(%d, %u.%u.%u.%u:%u) = %d", fd, arg(3) >> 24, (arg(3) >> 16) & 255, (arg(3) >> 8) & 255,
                 arg(3) & 255, arg(2) & 0xffff, ok ? 0 : -1));
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
        if (s.type == 2) {
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
        if (s.type == 2) {
            if (s.datagrams.empty()) { respond(ch, 0); note(fmt("recvfrom(%d) nothing", fd)); return true; }
            std::vector<uint8_t> d = std::move(s.datagrams.front());
            s.datagrams.erase(s.datagrams.begin());
            d.resize(std::min<size_t>(d.size(), arg(3)));
            io_.write(arg(2), d.data(), (uint32_t)d.size());
            uint8_t from[4] = {};
            io_.read(arg(5), from, 4);
            uint32_t from_word;
            std::memcpy(&from_word, from, 4);
            respond(ch, (int32_t)d.size(), SERVER_IP_RAW, (from_word & 0xffff0000) | 53);
            note(fmt("recvfrom(%d) = %zu", fd, d.size()));
            return true;
        }
        if (s.pending.empty()) {
            const int32_t r = s.answered ? 0 : -0xe;
            respond(ch, r);
            note(fmt("recv(%d) = %d", fd, r));
            return true;
        }
        const uint32_t n = (uint32_t)std::min<size_t>(s.pending.size(), arg(3));
        io_.write(arg(2), reinterpret_cast<const uint8_t *>(s.pending.data()), n);
        s.pending.erase(0, n);
        respond(ch, (int32_t)n);
        note(fmt("recv(%d) = %u", fd, n));
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
