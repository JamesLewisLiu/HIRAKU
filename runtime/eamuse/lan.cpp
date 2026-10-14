// SPDX-License-Identifier: GPL-3.0-or-later
// The network an e-AMUSEMENT cabinet sees, inside HIRAKU (lan.h).
#include "eamuse/lan.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hk::ea {

namespace {

uint16_t checksum(const uint8_t *p, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) sum += (uint32_t)p[i] << 8 | p[i + 1];
    if (n & 1) sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

void put_ip(uint8_t *p, uint32_t ip) {
    p[0] = (uint8_t)(ip >> 24); p[1] = (uint8_t)(ip >> 16); p[2] = (uint8_t)(ip >> 8); p[3] = (uint8_t)ip;
}

// An ICMP echo request from the cabinet to `to`, answered: IPv4 header, then the echo as a reply
// (a raw socket reads the whole packet)
std::string echo_reply(const std::string &icmp, uint32_t to) {
    std::vector<uint8_t> r(20, 0);
    const size_t total = 20 + icmp.size();
    r[0] = 0x45; r[2] = (uint8_t)(total >> 8); r[3] = (uint8_t)total;
    r[8] = 64; r[9] = 1;   // TTL, ICMP
    put_ip(&r[12], to);
    put_ip(&r[16], Lan::CABINET);
    const uint16_t hc = checksum(r.data(), 20);
    r[10] = (uint8_t)(hc >> 8); r[11] = (uint8_t)hc;
    std::vector<uint8_t> e(icmp.begin(), icmp.end());
    e[0] = 0;   // echo reply
    e[2] = e[3] = 0;
    const uint16_t ec = checksum(e.data(), e.size());
    e[2] = (uint8_t)(ec >> 8); e[3] = (uint8_t)ec;
    r.insert(r.end(), e.begin(), e.end());
    return std::string(r.begin(), r.end());
}

std::string dotted(uint32_t ip) {
    char b[16];
    snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return b;
}

}  // namespace

void Lan::note(const char *fmt, ...) {
    if (!log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    log(b);
}

// The A record for a name: "services..." is the server; any other name an address of its own
std::string Lan::dns_answer(const std::string &s, std::string *name) {
    const uint8_t *q = reinterpret_cast<const uint8_t *>(s.data());
    const size_t n = s.size();
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
    uint32_t ip = ROUTER;
    std::string lower = *name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (lower.compare(0, 8, "services") != 0) {
        auto it = name_ip_.find(*name);
        if (it == name_ip_.end()) {
            const uint32_t next = 0xc0a8320a + (uint32_t)name_ip_.size();   // 192.168.50.10 on
            if ((next & 0xff) == 0xff) return {};
            it = name_ip_.emplace(*name, next).first;
            ip_name_[next] = *name;
        }
        ip = it->second;
    }
    std::string r(s, 0, at);
    r[2] = (char)0x81; r[3] = (char)0x80;
    r[4] = 0; r[5] = 1; r[6] = 0; r[7] = 1;
    r[8] = r[9] = r[10] = r[11] = 0;
    const uint8_t rr[16] = {0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 60, 0x00, 0x04,
                            (uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip};
    r.append(reinterpret_cast<const char *>(rr), sizeof rr);
    return r;
}

Lan::Target Lan::target_of(uint32_t ip, uint16_t port) const {
    Target t;
    t.port = port;
    if (ip == ROUTER) {
        t.host = "services";
        t.services = true;
    } else if (auto it = ip_name_.find(ip); it != ip_name_.end()) {
        t.host = it->second;
    } else {
        t.host = dotted(ip);
    }
    return t;
}

Lan::Socket *Lan::find(int fd) {
    auto it = sockets_.find(fd);
    return it == sockets_.end() ? nullptr : &it->second;
}

void Lan::http_progress(Socket &s) {
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

bool Lan::poll() {
    std::vector<std::pair<uint64_t, std::string>> done;
    {
        std::lock_guard<std::mutex> l(done_mutex_);
        if (done_.empty()) return false;
        done.swap(done_);
    }
    for (auto &[id, answer] : done)
        for (auto &[fd, s] : sockets_) {
            if (s.id != id) continue;
            s.pending += answer;
            s.in_flight = false;
            s.answered = true;
        }
    return true;
}

int Lan::socket(Type type) {
    int fd = 0;
    while (sockets_.count(fd)) fd++;
    Socket &s = sockets_[fd];
    s.type = type;
    s.id = next_id_++;
    note("socket(%d) = %d", type, fd);
    return fd;
}

int Lan::connect(int fd, uint32_t ip, uint16_t port) {
    Socket *s = find(fd);
    if (s) {
        s->connected = true;
        s->target = target_of(ip, port);
    }
    note("connect(%d, %s:%u) = %d%s%s", fd, dotted(ip).c_str(), port, s ? 0 : -1, s ? " " : "", s ? s->target.host.c_str() : "");
    return s ? 0 : -1;
}

int Lan::send(int fd, const uint8_t *data, size_t n) {
    Socket *s = find(fd);
    if (!s || s->type != STREAM) {
        note("send(%d): no such stream", fd);
        return -1;
    }
    s->request.append(reinterpret_cast<const char *>(data), n);
    note("send(%d, %zu bytes)", fd, n);
    http_progress(*s);
    return (int)n;
}

int Lan::sendto(int fd, const uint8_t *data, size_t n, uint32_t ip, uint16_t port) {
    Socket *s = find(fd);
    if (!s) {
        note("sendto(%d): no such socket", fd);
        return -1;
    }
    const std::string d(reinterpret_cast<const char *>(data), n);
    if (s->type == RAW) {
        // the router check: the echo back, with its IP header. Every raw socket gets a copy, as
        // from a real stack (pop'n 12 sends each ping on a socket it closes at once and reads the
        // replies on another).
        if (n >= 8 && data[0] == 8) {
            const std::string reply = echo_reply(d, ip);
            for (auto &[other_fd, other] : sockets_)
                if (other.type == RAW) other.datagrams.push_back({reply, ip, 0});
        }
        note("ping(%d, %zu bytes) %s", fd, n, dotted(ip).c_str());
    } else {
        std::string name;
        if (port == 53) {
            std::string r = dns_answer(d, &name);
            if (!r.empty()) s->datagrams.push_back({std::move(r), ip, 53});
        } else if (port == 123 && n >= 48) {   // NTP: a server's answer (mode 4, stratum 1), all
            // four times now; the client's transmit time back as the originate time
            const uint64_t t = (uint64_t)((now ? now() : 0) + 2208988800LL) << 32;   // from 1900
            std::string r(48, '\0');
            r[0] = 0x24;   // no warning, version 4, server
            r[1] = 1;
            r.replace(24, 8, d, 40, 8);
            for (int at : {16, 32, 40})
                for (int i = 0; i < 8; i++) r[at + i] = (char)(t >> (56 - 8 * i));
            s->datagrams.push_back({std::move(r), ip, 123});
            name = "ntp";
        }
        note("sendto(%d, %zu bytes, %s:%u) %s", fd, n, dotted(ip).c_str(), port, name.c_str());
    }
    return (int)n;
}

Lan::Status Lan::recv(int fd, size_t max, std::string *out) {
    Socket *s = find(fd);
    if (!s || s->type != STREAM) {
        note("recv(%d): no such stream", fd);
        return BAD;
    }
    if (s->pending.empty()) {
        if (s->in_flight) return WAIT;   // the answer is on its way (not logged: a device asks again)
        note("recv(%d) = %s", fd, s->answered ? "0 (closed)" : "nothing sent");
        return s->answered ? END : NOTHING;
    }
    const size_t n = std::min(s->pending.size(), max);
    out->assign(s->pending, 0, n);
    s->pending.erase(0, n);
    note("recv(%d) = %zu", fd, n);
    return DATA;
}

Lan::Status Lan::recvfrom(int fd, size_t max, std::string *out, uint32_t *ip, uint16_t *port) {
    Socket *s = find(fd);
    if (!s || s->type == STREAM) {
        note("recvfrom(%d): no such socket", fd);
        return BAD;
    }
    if (s->datagrams.empty()) return NOTHING;
    Socket::Datagram d = std::move(s->datagrams.front());
    s->datagrams.erase(s->datagrams.begin());
    if (d.data.size() > max) d.data.resize(max);
    *out = std::move(d.data);
    *ip = d.ip;
    *port = d.port;
    note("recvfrom(%d) = %zu from %s:%u", fd, out->size(), dotted(d.ip).c_str(), d.port);
    return DATA;
}

int Lan::close(int fd) {
    const bool ok = sockets_.erase(fd) != 0;
    note("close(%d) = %d", fd, ok ? 0 : -1);
    return ok ? 0 : -1;
}

}  // namespace hk::ea
