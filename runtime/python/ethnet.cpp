// SPDX-License-Identifier: GPL-3.0-or-later
// A Python 2 cabinet's network inside HIRAKU (ethnet.h).
#include "python/ethnet.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
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
    else if (log) log("eth: frame type " + std::to_string(type) + " dropped");
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
    if (log) log("eth: ARP who has " + ip_text(wanted));
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
    if (proto != 1 && proto != 6 && proto != 17 && log) log("eth: IP protocol " + std::to_string(proto) + " to " + ip_text(dst) + " dropped");
    if (proto == 17 && len >= 8) {
        const uint16_t dport = be16(f + at + 2);
        if (dport == 67) dhcp(f, n, at);
        else if (dport == 53 && dst == SERVER) dns(f, n, at);
        else if (dport == 123 && dst == SERVER && now) sntp(f, n, at);
        else if (dport == AVS_SERVICES && len >= 8 + 12 && !memcmp(f + at + 8, "SgetRQ", 6)) {
            // AVS services' search (ethnet.h): here, at our port
            std::vector<uint8_t> r(8 + 12, 0);
            put16(&r[0], AVS_SERVICES), put16(&r[2], be16(f + at)), put16(&r[4], (uint16_t)r.size());
            memcpy(&r[8], "SgetRP", 6);
            put32(&r[14], SERVER), put16(&r[18], AVS_SERVICES);
            reply_ip(17, SERVER, src, std::move(r));
            if (log) log("eth: AVS services search: " + ip_text(SERVER) + ":" + std::to_string(AVS_SERVICES));
        }
        else if (log) {
            std::string hex;
            char b[4];
            for (uint32_t i = 8; i < len && i < 8 + 64; i++) snprintf(b, sizeof b, "%02x", f[at + i]), hex += b;
            log("eth: UDP " + std::to_string(be16(f + at)) + " to " + ip_text(dst) + ":" + std::to_string(dport) + " dropped: " + hex);
        }
    } else if (proto == 1 && len >= 8 && f[at] == 8 && dst != CABINET) {   // echo request
        if (log) log("eth: ping " + ip_text(dst) + " ttl " + std::to_string(f[22]) + " id " + std::to_string(be16(f + at + 4)) + " seq " + std::to_string(be16(f + at + 6)) + " len " + std::to_string(len));
        std::vector<uint8_t> icmp(f + at, f + at + len);
        icmp[0] = 0, icmp[2] = icmp[3] = 0;
        reply_ip(1, dst, src, std::move(icmp));
    } else if (proto == 6 && len >= 20) {
        const uint16_t to = be16(f + at + 2);
        if (dst == SERVER && ((to == 80 && http) || to == AVS_SERVICES)) tcp(f, n, at, len, src, dst);
        else if (!(f[at + 13] & 0x04)) refuse(f, at, len, src, dst);
    }
}

void EtherNet::refuse(const uint8_t *f, uint32_t at, uint32_t len, uint32_t src, uint32_t dst) {
    {   // TCP, not a reset: refused
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
    uint8_t opts[] = {53, 1, answer, 54, 4, 0, 0, 0, 0, 51, 4, 0x00, 0x09, 0x3a, 0x80,   // a week
                      1, 4, 0, 0, 0, 0, 3, 4, 0, 0, 0, 0, 6, 4, 0, 0, 0, 0, 255};
    put32(opts + 5, SERVER), put32(opts + 17, MASK), put32(opts + 23, SERVER), put32(opts + 29, SERVER);
    r.insert(r.end(), opts, opts + sizeof opts);
    put16(&r[0], 67), put16(&r[2], 68), put16(&r[4], (uint16_t)r.size());
    reply_ip(17, SERVER, 0xffffffff, std::move(r));
    if (log) log(std::string("eth: DHCP ") + (answer == 2 ? "offer" : "ack") + " " + ip_text(CABINET));
}

void EtherNet::dns(const uint8_t *f, uint32_t n, uint32_t udp) {
    const uint32_t len = be16(f + udp + 4);
    if (len < 8 + 12 || udp + len > n) return;
    std::vector<uint8_t> r(f + udp, f + udp + len);
    put16(&r[0], 53), put16(&r[2], be16(f + udp)), r[6] = r[7] = 0;   // from port 53 to the asker's
    uint8_t *h = r.data() + 8;
    h[2] = (uint8_t)(0x80 | (h[2] & 0x79));   // a response, its opcode and RD
    std::string name;
    uint32_t qend = 12;
    for (; qend < len - 8 && h[qend];) {
        const uint32_t l = h[qend];
        if (qend + 1 + l > len - 8) break;
        if (!name.empty()) name += '.';
        name.append((const char *)h + qend + 1, l);
        qend += 1 + l;
    }
    const bool known = name.compare(0, 9, "services.") == 0 && be16(h + 4) == 1;
    put16(h + 8, 0), put16(h + 10, 0);
    if (known) {   // one A record, the server, after the question (its name, type, class)
        r.resize(8 + qend + 1 + 4);
        h = r.data() + 8;
        h[3] = 0x80;   // recursion available, no error
        put16(h + 6, 1);
        uint8_t a[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0x0e, 0x10, 0, 4, 0, 0, 0, 0};
        put32(a + 12, SERVER);
        r.insert(r.end(), a, a + sizeof a);
        put16(&r[4], (uint16_t)r.size());
    } else {
        h[3] = 0x80 | 3;   // recursion available, NXDOMAIN
        put16(h + 6, 0);
    }
    reply_ip(17, SERVER, be32(f + 26), std::move(r));
    if (log) log("eth: DNS " + name + (known ? ": " + ip_text(SERVER) : std::string(": no such name")));
}

// an NTP server's answer (RFC 4330): mode 4, stratum 1, the client's transmit time as the origin,
// the cabinet's clock as the receive and transmit times
void EtherNet::sntp(const uint8_t *f, uint32_t n, uint32_t udp) {
    if (be16(f + udp + 4) < 8 + 48 || udp + 8 + 48 > n) return;
    const uint8_t *q = f + udp + 8;
    std::vector<uint8_t> r(8 + 48, 0);
    put16(&r[0], 123), put16(&r[2], be16(f + udp)), put16(&r[4], (uint16_t)r.size());
    uint8_t *a = r.data() + 8;
    a[0] = (uint8_t)((q[0] & 0x38) | 4);   // no leap warning, the client's version, server
    a[1] = 1, a[2] = q[2], a[3] = 0xec;    // stratum 1, its poll, precision ~2^-20
    memcpy(a + 12, "HRKU", 4);             // reference id
    const uint32_t t = (uint32_t)(now() + 2208988800LL);   // seconds since 1900
    put32(a + 16, t);                      // reference time
    memcpy(a + 24, q + 40, 8);             // origin: the client's transmit time
    put32(a + 32, t), put32(a + 40, t);    // receive, transmit
    reply_ip(17, SERVER, be32(f + 26), std::move(r));
    if (log) log("eth: SNTP " + std::to_string(t));
}

// TCP to the server's port 80 (ethnet.h)
void EtherNet::tcp(const uint8_t *f, uint32_t n, uint32_t at, uint32_t len, uint32_t src, uint32_t dst) {
    const uint16_t port = be16(f + at), local = be16(f + at + 2);
    const uint32_t key = (uint32_t)local << 16 | port;
    const uint8_t flags = f[at + 13];
    const uint32_t seq = be32(f + at + 4), ack = be32(f + at + 8), off = (f[at + 12] >> 4) * 4u;
    const uint16_t window = be16(f + at + 14);
    if (off < 20 || off > len) return;
    if (flags & 0x04) { conns_.erase(key); return; }   // a reset
    if (flags & 0x02) {   // SYN: a new connection (the game's port reused: the old one is gone)
        Conn c;
        c.local = local;
        c.rcv_nxt = seq + 1;
        c.snd_una = c.snd_nxt = 0x48524b00 + ((uint32_t)port << 8);   // our sequence: from the port, repeatable
        c.window = window;
        conns_[key] = c;
        Conn &k = conns_[key];
        segment(port, k, 0x12, "");   // SYN, ACK
        k.snd_nxt++;
        return;
    }
    auto it = conns_.find(key);
    if (it == conns_.end()) { refuse(f, at, len, src, dst); return; }
    Conn &c = it->second;
    if (flags & 0x10) {   // what the game has of ours
        if ((int32_t)(ack - c.snd_una) > 0 && (int32_t)(ack - c.snd_nxt) <= 0) c.snd_una = ack;
        c.window = window;
    }
    const uint32_t dlen = len - off;
    bool ack_now = false;
    if (dlen) {
        if (seq == c.rcv_nxt) {
            c.request.append((const char *)f + at + off, dlen);
            c.rcv_nxt += dlen;
        }
        ack_now = true;   // in order or not, say where we are
    }
    if ((flags & 0x01) && seq + dlen == c.rcv_nxt) {   // the game's FIN
        c.rcv_nxt++;
        c.fin_got = true;
        ack_now = true;
    }
    // AVS services: one line, "GET <the MAC>", answered with an empty list
    if (!c.answered && c.local == AVS_SERVICES && c.request.find('\n') != std::string::npos) {
        c.answered = true;
        c.answer = "200\n0\n";
        if (log) log("eth: AVS services " + c.request.substr(0, c.request.find_first_of("\r\n")) + ": no services");
    }
    // a whole request: its head, and the body its Content-Length gives
    if (!c.answered) {
        const size_t head = c.request.find("\r\n\r\n");
        if (head != std::string::npos) {
            size_t body = 0;
            std::string lower(c.request, 0, head);
            for (char &ch : lower) ch = (char)tolower((unsigned char)ch);
            const size_t cl = lower.find("content-length:");
            if (cl != std::string::npos) body = (size_t)strtoul(lower.c_str() + cl + 15, nullptr, 10);
            if (c.request.size() >= head + 4 + body) {
                c.answered = true;
                c.answer = http(c.request.substr(0, head + 4 + body));
                if (log) log("eth: HTTP from port " + std::to_string(port) + ", " + std::to_string(head + 4 + body) +
                             " bytes -> " + std::to_string(c.answer.size()));
            }
        }
    }
    const uint32_t before = c.snd_nxt;
    push(port, c);
    if (ack_now && c.snd_nxt == before) segment(port, c, 0x10, "");
    if (c.fin_got && c.fin_sent && c.snd_una == c.snd_nxt) conns_.erase(key);
}

// our answer, as far as the game's window allows, then our FIN
void EtherNet::push(uint16_t port, Conn &c) {
    if (!c.answered) return;
    while (c.answer_at < c.answer.size()) {
        const uint32_t in_flight = c.snd_nxt - c.snd_una;
        if (in_flight >= c.window) return;
        const size_t room = std::min<size_t>({c.window - in_flight, 1460, c.answer.size() - c.answer_at});
        segment(port, c, 0x18, c.answer.substr(c.answer_at, room));   // PSH, ACK
        c.answer_at += room;
        c.snd_nxt += (uint32_t)room;
    }
    if (!c.fin_sent) {
        segment(port, c, 0x11, "");   // FIN, ACK
        c.fin_sent = true;
        c.snd_nxt++;
    }
}

void EtherNet::segment(uint16_t port, Conn &c, uint8_t flags, const std::string &data) {
    std::vector<uint8_t> t(20, 0);
    put16(&t[0], c.local), put16(&t[2], port);
    put32(&t[4], c.snd_nxt), put32(&t[8], c.rcv_nxt);
    t[12] = 5 << 4, t[13] = flags;
    put16(&t[14], 0xffff);
    t.insert(t.end(), data.begin(), data.end());
    reply_ip(6, SERVER, CABINET, std::move(t));
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
