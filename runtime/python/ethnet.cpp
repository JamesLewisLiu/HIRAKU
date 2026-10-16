// SPDX-License-Identifier: GPL-3.0-or-later
// A Python 2 cabinet's network inside HIRAKU (ethnet.h).
#include "python/ethnet.h"

#include <cstdio>
#include <algorithm>
#include <cstring>

namespace hk::py {

namespace {

// the LAN's one MAC address: the gateway, the DNS server and every other address answer with it
constexpr uint8_t LAN_MAC[6] = {0x02, 0x48, 0x52, 0x4b, 0x00, 0x01};

uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)), put16(p + 2, (uint16_t)v); }

uint32_t sum16(const uint8_t *p, size_t n, uint32_t s = 0) {
    for (size_t i = 0; i + 1 < n; i += 2) s += be16(p + i);
    if (n & 1) s += (uint32_t)p[n - 1] << 8;
    return s;
}
uint16_t fold(uint32_t s) {
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (uint16_t)~s;
}

std::string ip_text(uint32_t a) {
    char b[16];
    snprintf(b, sizeof b, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
    return b;
}

}  // namespace

void EtherNet::send(const uint8_t *f, uint32_t n) {
    if (n < 14) return;
    if (!have_mac_ || memcmp(game_mac_, f + 6, 6)) memcpy(game_mac_, f + 6, 6), have_mac_ = true;
    const uint16_t type = be16(f + 12);
    if (type == 0x0806) arp(f, n);
    else if (type == 0x0800) ip(f, n);
}

uint32_t EtherNet::take(uint8_t *frame, uint32_t capacity) {
    if (out_.empty()) return 0;
    std::vector<uint8_t> f = std::move(out_.front());
    out_.pop_front();
    const uint32_t n = (uint32_t)std::min<size_t>(f.size(), capacity);
    memcpy(frame, f.data(), n);
    return n;
}

void EtherNet::arp(const uint8_t *f, uint32_t n) {
    if (n < 42 || be16(f + 20) != 1) return;   // a request
    const uint32_t asker = be32(f + 28), wanted = be32(f + 38);
    // the cabinet's own address (its conflict check before using it) and other subnets: nobody
    if (wanted == CABINET || (wanted & MASK) != (CABINET & MASK) || wanted == 0) return;
    std::vector<uint8_t> r(60, 0);
    memcpy(r.data(), f + 6, 6);
    memcpy(r.data() + 6, LAN_MAC, 6);
    put16(&r[12], 0x0806);
    put16(&r[14], 1), put16(&r[16], 0x0800), r[18] = 6, r[19] = 4, put16(&r[20], 2);
    memcpy(&r[22], LAN_MAC, 6), put32(&r[28], wanted);
    memcpy(&r[32], f + 22, 6), put32(&r[38], asker);
    out_.push_back(std::move(r));
}

void EtherNet::ip(const uint8_t *f, uint32_t n) {
    if (n < 34 || (f[14] >> 4) != 4) return;
    const uint32_t ihl = (f[14] & 15) * 4u, total = be16(f + 16);
    if (14 + total > n || ihl < 20 || total < ihl) return;
    const uint8_t proto = f[23];
    const uint32_t src = be32(f + 26), dst = be32(f + 30), at = 14 + ihl, len = total - ihl;
    if (proto == 17 && len >= 8) {
        const uint16_t dport = be16(f + at + 2);
        if (dport == 67) dhcp(f, n, at);
        else if (dport == 53 && dst == SERVER) dns(f, n, at);
        else if (log) log("eth: UDP to " + ip_text(dst) + ":" + std::to_string(dport) + " dropped");
    } else if (proto == 1 && len >= 8 && f[at] == 8 && dst != CABINET) {   // echo request
        std::vector<uint8_t> icmp(f + at, f + at + len);
        icmp[0] = 0, icmp[2] = icmp[3] = 0;
        reply_ip(1, dst, src, std::move(icmp));
    } else if (proto == 6 && len >= 20 && !(f[at + 13] & 0x04)) {   // TCP, not a reset: refused
        const uint8_t flags = f[at + 13];
        const uint32_t seq = be32(f + at + 4), ack = be32(f + at + 8);
        const uint32_t seglen = len - (f[at + 12] >> 4) * 4u + ((flags & 0x02) ? 1 : 0) + ((flags & 0x01) ? 1 : 0);
        std::vector<uint8_t> tcp(20, 0);
        put16(&tcp[0], be16(f + at + 2)), put16(&tcp[2], be16(f + at));
        if (flags & 0x10) {
            put32(&tcp[4], ack), tcp[13] = 0x04;   // RST
        } else {
            put32(&tcp[8], seq + seglen), tcp[13] = 0x14;   // RST, ACK
        }
        tcp[12] = 5 << 4;
        reply_ip(6, dst, src, std::move(tcp));
        if (log && (flags & 0x02)) log("eth: TCP to " + ip_text(dst) + ":" + std::to_string(be16(f + at + 2)) + " refused");
    }
}

void EtherNet::dhcp(const uint8_t *f, uint32_t n, uint32_t udp) {
    const uint8_t *b = f + udp + 8;
    const uint32_t blen = n - udp - 8;
    if (blen < 240 || b[0] != 1 || be32(b + 236) != 0x63825363) return;
    uint8_t type = 0;
    for (uint32_t i = 240; i < blen && b[i] != 255;) {
        if (b[i] == 0) { i++; continue; }
        if (i + 1 >= blen) break;
        if (b[i] == 53 && b[i + 1] >= 1 && i + 2 < blen) type = b[i + 2];
        i += 2 + b[i + 1];
    }
    uint8_t answer = type == 1 ? 2 : type == 3 ? 5 : type == 8 ? 5 : 0;   // offer, ack (a request, an inform)
    if (!answer) return;
    std::vector<uint8_t> r(8 + 240, 0);
    uint8_t *o = r.data() + 8;
    o[0] = 2, o[1] = 1, o[2] = 6;
    memcpy(o + 4, b + 4, 4);   // xid
    memcpy(o + 10, b + 10, 2);   // flags
    if (type != 8) put32(o + 16, CABINET);   // yiaddr
    put32(o + 20, SERVER);
    memcpy(o + 28, b + 28, 16);   // chaddr
    put32(o + 236, 0x63825363);
    const uint8_t opts[] = {53, 1, answer, 54, 4, 192, 168, 50, 1, 51, 4, 0x00, 0x09, 0x3a, 0x80,   // a week
                            1, 4, 255, 255, 255, 0, 3, 4, 192, 168, 50, 1, 6, 4, 192, 168, 50, 1, 255};
    r.insert(r.end(), opts, opts + sizeof opts);
    put16(&r[0], 67), put16(&r[2], 68), put16(&r[4], (uint16_t)r.size());
    reply_ip(17, SERVER, 0xffffffff, std::move(r));
    if (log) log(std::string("eth: DHCP ") + (answer == 2 ? "offer" : "ack") + " " + ip_text(CABINET));
}

void EtherNet::dns(const uint8_t *f, uint32_t n, uint32_t udp) {
    const uint32_t len = be16(f + udp + 4);
    if (len < 8 + 12 || udp + len > n) return;
    std::vector<uint8_t> r(f + udp, f + udp + len);
    put16(&r[0], be16(f + udp + 2)), put16(&r[2], 53), r[6] = r[7] = 0;
    uint8_t *h = r.data() + 8;
    h[2] = (uint8_t)(0x80 | (h[2] & 0x79));   // a response, its opcode and RD
    h[3] = 0x80 | 3;                          // recursion available, NXDOMAIN
    put16(h + 6, 0), put16(h + 8, 0), put16(h + 10, 0);
    std::string name;
    for (uint32_t i = 12; i < len - 8 && h[i];) {
        const uint32_t l = h[i];
        if (i + 1 + l > len - 8) break;
        if (!name.empty()) name += '.';
        name.append((const char *)h + i + 1, l);
        i += 1 + l;
    }
    reply_ip(17, SERVER, be32(f + 26), std::move(r));
    if (log) log("eth: DNS " + name + ": no such name");
}

void EtherNet::reply_ip(uint8_t proto, uint32_t src, uint32_t dst, std::vector<uint8_t> payload) {
    // the transport checksum, over the pseudo-header (ICMP: over the message only)
    const uint32_t len = (uint32_t)payload.size();
    const size_t ck = proto == 6 ? 16 : proto == 17 ? 6 : 2;
    payload[ck] = payload[ck + 1] = 0;
    uint32_t s = 0;
    if (proto != 1) {
        uint8_t ph[12];
        put32(ph, src), put32(ph + 4, dst), ph[8] = 0, ph[9] = proto, put16(ph + 10, (uint16_t)len);
        s = sum16(ph, 12);
    }
    uint16_t c = fold(sum16(payload.data(), len, s));
    if (proto == 17 && c == 0) c = 0xffff;
    put16(&payload[ck], c);

    std::vector<uint8_t> f(14 + 20, 0);
    memcpy(f.data(), game_mac_, 6);
    memcpy(f.data() + 6, LAN_MAC, 6);
    put16(&f[12], 0x0800);
    uint8_t *h = f.data() + 14;
    h[0] = 0x45, put16(h + 2, (uint16_t)(20 + len)), put16(h + 4, ip_id_++), h[8] = 64, h[9] = proto;
    put32(h + 12, src), put32(h + 16, dst);
    put16(h + 10, fold(sum16(h, 20)));
    f.insert(f.end(), payload.begin(), payload.end());
    if (f.size() < 60) f.resize(60, 0);
    out_.push_back(std::move(f));
}

}  // namespace hk::py
