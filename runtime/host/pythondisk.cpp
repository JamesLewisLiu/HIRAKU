// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/pythondisk.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>

#include "shell/overlay.h"

namespace hk::pythondisk {

namespace {

constexpr uint32_t kDirSector = 0x4800, kDirSectors = 0x1f8;
constexpr uint32_t kMaxUnpacked = 0x80018;   // the most the game's file loader takes (FUN_01730398)

uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void put32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i)));
}

// an encrypted file's tag: 13G0 .. 13A9 (the loader's table); 13P0 is the program
bool tagged(const uint8_t *p) {
    return p[0] == '1' && p[1] == '3' && strchr("GSPA", p[2]) && p[2] && p[3] >= '0' && p[3] <= '9';
}

}  // namespace

uint32_t name_hash(const std::string &name) {
    std::string n;
    for (char c : name) n += (char)std::tolower((unsigned char)c);
    for (size_t at; (at = n.find("./")) != std::string::npos;) n.erase(at, 2);
    while (!n.empty() && n[0] == '/') n.erase(0, 1);
    uint32_t v = 0;
    for (unsigned char c : n)
        for (int i = 0; i < 6; i++) v = ((v << 1) | ((c >> i) & 1)) ^ ((v & 0x80000000u) ? 0x04c11db7u : 0);
    return v;
}

std::vector<uint8_t> lzss_pack(const uint8_t *in_, size_t n_) {
    // The decoder's window starts as zeros, so the input is matched as if 4096 zeros came before
    // it (a picture's runs of zeros at its start cost nothing). Longest match among the last
    // 4095 bytes, through chains of 3-byte prefixes, with one step of lazy matching.
    constexpr size_t kPre = 4096;
    constexpr int kWin = 4096, kMax = 18, kTries = 512;
    std::vector<uint8_t> buf(kPre + n_, 0);
    std::copy(in_, in_ + n_, buf.begin() + kPre);
    const uint8_t *in = buf.data();
    const size_t n = buf.size();
    std::vector<int> head(1 << 16, -1), prev(n, -1);
    auto key = [&](size_t i) { return (in[i] << 8 ^ in[i + 1] << 4 ^ in[i + 2]) & 0xffff; };
    size_t inserted = 0;
    auto insert_to = [&](size_t end) {   // chain every position before end
        for (; inserted < end && inserted + 2 < n; inserted++) {
            const int k = key(inserted);
            prev[inserted] = head[k];
            head[k] = (int)inserted;
        }
    };
    auto longest = [&](size_t i, size_t &at) {
        int best = 0;
        if (i + 2 >= n) return 0;
        insert_to(i);
        const size_t room = std::min<size_t>(kMax, n - i);
        int tries = kTries;
        for (int p = head[key(i)]; p >= 0 && i - (size_t)p < kWin && tries--; p = prev[p]) {
            size_t l = 0;
            while (l < room && in[p + l] == in[i + l]) l++;
            if ((int)l > best) {
                best = (int)l;
                at = (size_t)p;
                if (l == room) break;
            }
        }
        return best;
    };
    std::vector<uint8_t> out;
    out.reserve(n_ + n_ / 8 + 16);
    size_t i = kPre, flag_at = 0;
    int bit = 8;
    while (i < n) {
        if (bit == 8) {
            flag_at = out.size();
            out.push_back(0);
            bit = 0;
        }
        size_t at = 0, at2 = 0;
        int best = longest(i, at);
        if (best >= 3 && best < kMax && longest(i + 1, at2) > best) best = 0;   // a literal, then the longer match
        if (best >= 3) {
            const uint32_t pos = (uint32_t)(0xfee + at - kPre) & 0xfff;   // where the decoder's ring holds it
            out.push_back((uint8_t)pos);
            out.push_back((uint8_t)((pos >> 4 & 0xf0) | (best - 3)));
            i += best;
        } else {
            out[flag_at] |= (uint8_t)(1 << bit);
            out.push_back(in[i]);
            i++;
        }
        bit++;
    }
    return out;
}

std::vector<uint8_t> lzss_unpack(const uint8_t *in, size_t n_in, size_t n) {
    std::vector<uint8_t> out;
    out.reserve(n);
    uint8_t win[4096] = {};
    size_t r = 0xfee, i = 0;
    unsigned flags = 0;
    while (out.size() < n) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (i >= n_in) break;
            flags = in[i++] | 0xff00;
        }
        if (flags & 1) {
            if (i >= n_in) break;
            const uint8_t c = in[i++];
            out.push_back(c);
            win[r] = c, r = (r + 1) & 0xfff;
        } else {
            if (i + 1 >= n_in) break;
            const size_t o = in[i] | (in[i + 1] & 0xf0) << 4, len = (in[i + 1] & 0x0f) + 3;
            i += 2;
            for (size_t k = 0; k < len && out.size() < n; k++) {
                const uint8_t c = win[(o + k) & 0xfff];
                out.push_back(c);
                win[r] = c, r = (r + 1) & 0xfff;
            }
        }
    }
    out.resize(n, 0);
    return out;
}

std::vector<uint8_t> picture(const std::vector<uint8_t> &like, const uint8_t *rgba, int width, int height,
                             const std::vector<uint8_t> &idx) {
    auto be16 = [&](size_t at) { return (uint32_t)like[at] << 8 | like[at + 1]; };
    if (like.size() < 24 || memcmp(like.data(), "GC\0\0", 4)) throw std::runtime_error("not a GC picture");
    const int w = (int)be16(12), h = (int)be16(14);
    const uint32_t size = be16(20) << 16 | be16(22);
    const int bits = size == (uint32_t)(w * h * 2) ? 16 : size == (uint32_t)(w * h) ? 8 : 0;
    if (!bits) throw std::runtime_error("a GC picture of a kind HIRAKU doesn't know");
    if (width != w || height != h)
        throw std::runtime_error("the picture is " + std::to_string(w) + "x" + std::to_string(h) + ", the PNG " +
                                 std::to_string(width) + "x" + std::to_string(height));
    std::vector<uint8_t> out(like.begin(), like.begin() + 24);
    if (bits == 16) {
        for (int k = 0; k < w * h; k++) {
            const uint8_t *c = rgba + 4 * k;
            const uint32_t p = (c[0] * 31 + 127) / 255 | (c[1] * 31 + 127) / 255 << 5 | (c[2] * 31 + 127) / 255 << 10 |
                               (c[3] >= 128 ? 0x8000u : 0);
            out.push_back((uint8_t)p), out.push_back((uint8_t)(p >> 8));
        }
        return out;
    }
    if (idx.size() < 1024) throw std::runtime_error("an 8-bit picture: its colours are in its folder's system.idx");
    int pal[256][4];
    const uint8_t *src = idx.data() + idx.size() - 1024;
    for (int i = 0; i < 256; i++) {   // the GS's order: in every 32, entries 8-15 and 16-23 swapped
        const int j = (i & ~0x18) | ((i & 8) << 1) | ((i & 16) >> 1);
        for (int c = 0; c < 3; c++) pal[i][c] = src[4 * j + c];
        pal[i][3] = std::min(255, src[4 * j + 3] * 255 / 128);
    }
    std::map<uint32_t, uint8_t> memo;
    for (int k = 0; k < w * h; k++) {
        const uint8_t *c = rgba + 4 * k;
        uint32_t key;
        memcpy(&key, c, 4);
        auto it = memo.find(key);
        if (it == memo.end()) {
            int best = 0;
            long bd = -1;
            for (int v = 0; v < 256; v++) {
                long d;
                if (c[3] < 8) d = (long)pal[v][3] * 256 + v;   // transparent: the palette's most transparent colour
                else {
                    const long dr = pal[v][0] - c[0], dg = pal[v][1] - c[1], db = pal[v][2] - c[2], da = pal[v][3] - c[3];
                    d = dr * dr * 3 + dg * dg * 4 + db * db * 2 + da * da * 3;
                }
                if (bd < 0 || d < bd) bd = d, best = v;
            }
            it = memo.emplace(key, (uint8_t)best).first;
        }
        out.push_back(it->second);
    }
    return out;
}

std::unique_ptr<Files> Files::open(const std::string &image) {
    std::vector<uint8_t> d((size_t)kDirSectors * 512);
    if (!overlay::read(image, (uint64_t)kDirSector * 512, d.data(), d.size())) return nullptr;
    auto f = std::unique_ptr<Files>(new Files);
    f->image_ = image;
    uint32_t sum = 0;
    for (size_t at = 0; at + 24 <= d.size(); at += 24) {
        const uint8_t *p = &d[at];
        Entry e{u32(p), u32(p + 4), u32(p + 8), u32(p + 12), u32(p + 16), u32(p + 20)};
        if (!e.sector && !e.size && !e.flags) {
            if (e.sum != sum || f->entries_.empty()) return nullptr;
            // room for files that grow: past the last file, from the next MB (zeros on the disks seen)
            uint64_t end = 0;
            for (const Entry &x : f->entries_) end = std::max<uint64_t>(end, x.sector + (x.size + 511) / 512);
            f->free_ = (uint32_t)((end + 2047) / 2048 * 2048);
            return f;
        }
        for (int i = 0; i < 24; i++) sum += p[i];
        f->entries_.push_back(e);
        f->disk_.push_back(e);
    }
    return nullptr;
}

void Files::put(const std::string &name, const std::vector<uint8_t> &content, const std::string &label) {
    uint32_t hash;
    if (!name.empty() && name[0] == '#') {
        hash = (uint32_t)strtoul(name.c_str() + 1, nullptr, 16);
    } else {
        hash = name_hash(name);
    }
    Entry *e = nullptr;
    for (Entry &x : entries_)
        if (x.hash == hash) e = &x;
    // how the game reads it: the one there's form (as on the disk), or for a new file its name's kind
    enum { Lzss, Stored, Raw } form = Lzss;
    if (const Entry *d = find(name); e && d) {
        uint8_t head[8] = {};
        if (!overlay::read(image_, (uint64_t)d->sector * 512, head, sizeof head)) throw std::runtime_error(image_ + ": can't read " + name);
        if (!memcmp(head, "13P0", 4)) throw std::runtime_error(name + " is the game's program: change it with [[patch]]");
        form = tagged(head) ? Lzss : u32(head) == d->size - 4 ? Stored : Raw;
    } else if (name[0] == '#') {
        throw std::runtime_error(name + ": no file with that hash on the disk");
    } else {
        form = name.size() > 4 && name.substr(name.size() - 4) == ".gzz" ? Lzss : name.size() > 4 && name.substr(name.size() - 4) == ".idx" ? Stored : Raw;
    }
    std::vector<uint8_t> data;
    if (form == Lzss) {
        if (content.size() > kMaxUnpacked)
            throw std::runtime_error(name + ": " + std::to_string(content.size()) + " bytes; the game takes files of up to " +
                                     std::to_string(kMaxUnpacked));
        put32(data, (uint32_t)content.size());
        const auto packed = lzss_pack(content.data(), content.size());
        data.insert(data.end(), packed.begin(), packed.end());
    } else if (form == Stored) {
        put32(data, (uint32_t)content.size());
        data.insert(data.end(), content.begin(), content.end());
    } else {
        data = content;
    }
    if (data.empty()) throw std::runtime_error(name + ": the mod's file is empty");
    const uint32_t sectors = (uint32_t)((data.size() + 511) / 512);
    uint32_t at;
    if (e && sectors <= (e->size + 511) / 512) {
        at = e->sector;   // in its own place
    } else {
        // past the last file: check the room is unused (zeros) before taking it
        at = free_;
        std::vector<uint8_t> z((size_t)sectors * 512);
        if (!overlay::read(image_, (uint64_t)at * 512, z.data(), z.size()))
            throw std::runtime_error(image_ + ": no room for " + name);
        for (uint8_t c : z)
            if (c) throw std::runtime_error(image_ + ": no free room on the disk for " + name + " (it grew)");
        free_ += sectors;
    }
    uint32_t sum = 0;
    for (uint8_t c : data) sum += c;
    const uint32_t size = (uint32_t)data.size();
    data.resize((size_t)sectors * 512, 0);
    overlay::add(image_, (uint64_t)at * 512, std::move(data), label);
    if (!e) {
        if ((entries_.size() + 2) * 24 > (size_t)kDirSectors * 512) throw std::runtime_error(image_ + ": the directory is full");
        entries_.push_back({hash, 0, 0, 0, 0, 0});
        e = &entries_.back();
    }
    e->sector = at;
    e->size = size;
    e->sum = sum;
    changed_ = true;
    if (labels_.find(label) == std::string::npos) labels_ += (labels_.empty() ? "" : ", ") + label;
}

const Files::Entry *Files::find(const std::string &name) const {
    const uint32_t hash = !name.empty() && name[0] == '#' ? (uint32_t)strtoul(name.c_str() + 1, nullptr, 16) : name_hash(name);
    const Entry *e = nullptr;
    for (const Entry &x : disk_)
        if (x.hash == hash) e = &x;
    return e;
}

// The loader (boot record C of the boot header: sector 0, or its copy at 0x200 or 0x400; packed
// with its own LZ) holds the keys: a table of 40 tags ("13G0", "13S0"...), their keys and IVs, and
// Blowfish's starting tables, each word in 8 bytes (tools/popnfs.py, tools/pythonfs.py)
const Files::Keys &Files::keys() const {
    if (keys_) return *keys_;
    keys_ = std::make_unique<Keys>();
    static const uint8_t magic[8] = {0x1e, 0xb3, 0xf7, 0x08, 0x3f, 0x6a, 0xd6, 0x0c};
    uint8_t h[0x200];
    bool found = false;
    for (uint64_t at : {0x0, 0x200, 0x400})
        if (overlay::read(image_, at, h, sizeof h) && !memcmp(h, magic, 8)) {
            found = true;
            break;
        }
    if (!found) throw std::runtime_error(image_ + ": no boot header (for the loader's keys)");
    const uint64_t off = (uint64_t)u32(h + 0x100) * 256;
    const uint32_t size = u32(h + 0x100 + 16);
    std::vector<uint8_t> comp(size);
    if (!overlay::read(image_, off, comp.data(), size)) throw std::runtime_error(image_ + ": can't read the loader");
    std::vector<uint8_t> ldr;
    for (size_t pos = 0, cnt = 0, flags = 0; pos < comp.size() && ldr.size() < (1u << 26);) {
        if (!cnt) {
            flags = comp[pos++], cnt = 8;
            continue;
        }
        const bool lit = flags & 1;
        flags >>= 1, cnt--;
        if (lit) ldr.push_back(comp[pos++]);
        else {
            if (pos + 1 >= comp.size()) break;
            const size_t len = (comp[pos] & 0x0f) + 3, d = (size_t)(comp[pos] >> 4) << 8 | comp[pos + 1];
            pos += 2;
            for (size_t k = 0; k < len; k++) ldr.push_back(d && d <= ldr.size() ? ldr[ldr.size() - d] : 0);
        }
    }
    auto find = [&](const void *what, size_t n) -> size_t {
        auto it = std::search(ldr.begin(), ldr.end(), (const uint8_t *)what, (const uint8_t *)what + n);
        if (it == ldr.end()) throw std::runtime_error(image_ + ": no key table in the loader");
        return (size_t)(it - ldr.begin());
    };
    const size_t t = find("13G0\0" "13S0\0", 10);
    if (t + 0x28f0 + 8 * 40 > ldr.size()) throw std::runtime_error(image_ + ": the loader's key table is cut short");
    for (int i = 0; i < 40; i++) {
        const std::string tag((const char *)&ldr[t + 5 * i], 4);
        const char *k = (const char *)&ldr[t + 0xc8 + 0x101 * i];
        const std::string key(k, strnlen(k, 0x101));
        const uint8_t *iv = &ldr[t + 0x28f0 + 8 * i];
        const std::string ivs = {(char)iv[3], (char)iv[2], (char)iv[1], (char)iv[0], (char)iv[7], (char)iv[6], (char)iv[5], (char)iv[4]};
        keys_->tags.push_back({tag, {key, ivs}});
    }
    const uint32_t p0w = 0x243f6a88, s0w = 0xd1310ba6;
    const size_t p0 = find(&p0w, 4), s0 = find(&s0w, 4);
    if (p0 + 8 * 18 > ldr.size() || s0 + 8 * 1024 > ldr.size()) throw std::runtime_error(image_ + ": the loader's Blowfish tables are cut short");
    for (int i = 0; i < 18; i++) keys_->p.push_back(u32(&ldr[p0 + 8 * i]));
    for (int i = 0; i < 1024; i++) keys_->s.push_back(u32(&ldr[s0 + 8 * i]));
    return *keys_;
}

// Blowfish in CBC mode, the key's tables made from the loader's starting ones, the chain started
// over from the IV every 0x19000 bytes; every whole block
std::vector<uint8_t> Files::decrypt(const std::vector<uint8_t> &b) const {
    const Keys &k = keys();
    const std::string tag((const char *)b.data(), 4);
    const std::pair<std::string, std::string> *kv = nullptr;
    for (const auto &t : k.tags)
        if (t.first == tag) kv = &t.second;
    if (!kv) return b;
    std::vector<uint32_t> P = k.p, S = k.s;
    const std::string &key = kv->first;
    for (int i = 0; i < 18; i++) {
        uint32_t x = 0;
        for (int j = 0; j < 4; j++) x = x << 8 | (uint8_t)key[(size_t)(i * 4 + j) % key.size()];
        P[i] ^= x;
    }
    auto F = [&](uint32_t x) { return ((S[x >> 24] + S[256 + ((x >> 16) & 255)]) ^ S[512 + ((x >> 8) & 255)]) + S[768 + (x & 255)]; };
    auto enc = [&](uint32_t &l, uint32_t &r) {
        for (int i = 0; i < 16; i++) l ^= P[i], r ^= F(l), std::swap(l, r);
        std::swap(l, r);
        r ^= P[16], l ^= P[17];
    };
    uint32_t l = 0, r = 0;
    for (int i = 0; i < 18; i += 2) enc(l, r), P[i] = l, P[i + 1] = r;
    for (int i = 0; i < 1024; i += 2) enc(l, r), S[i] = l, S[i + 1] = r;
    auto be = [](const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; };
    uint32_t pl = be((const uint8_t *)kv->second.data()), pr = be((const uint8_t *)kv->second.data() + 4);
    std::vector<uint8_t> out;
    for (size_t at = 8; at + 8 <= b.size(); at += 8) {
        if ((at - 8) % 0x19000 == 0)   // the chain starts over from the IV every 0x19000 bytes
            pl = be((const uint8_t *)kv->second.data()), pr = be((const uint8_t *)kv->second.data() + 4);
        const uint32_t cl = be(&b[at]), cr = be(&b[at + 4]);
        uint32_t x = cl, y = cr;
        for (int i = 17; i > 1; i--) x ^= P[i], y ^= F(x), std::swap(x, y);
        std::swap(x, y);
        y ^= P[1], x ^= P[0];
        x ^= pl, y ^= pr;
        for (uint32_t w : {x, y})
            for (int s = 24; s >= 0; s -= 8) out.push_back((uint8_t)(w >> s));
        pl = cl, pr = cr;
    }
    return out;
}

std::vector<uint8_t> Files::get(const std::string &name) const {
    const Entry *e = find(name);
    if (!e || !e->size) return {};
    std::vector<uint8_t> raw(e->size);
    if (!overlay::read(image_, (uint64_t)e->sector * 512, raw.data(), raw.size())) throw std::runtime_error(image_ + ": can't read " + name);
    if (raw.size() < 8) return raw;
    if (tagged(raw.data())) {   // encrypted, then packed (the game unpacks from every block)
        const std::vector<uint8_t> plain = decrypt(raw);
        if (plain.size() < 4) return {};
        return lzss_unpack(plain.data() + 4, plain.size() - 4, u32(plain.data()));
    }
    if (u32(raw.data()) == e->size - 4) return std::vector<uint8_t>(raw.begin() + 4, raw.end());   // stored
    if (name.size() > 4 && name.substr(name.size() - 4) == ".gzz")
        return lzss_unpack(raw.data() + 4, raw.size() - 4, u32(raw.data()));
    return raw;
}

void Files::finish() {
    if (!changed_) return;
    std::vector<uint8_t> d;
    d.reserve((size_t)kDirSectors * 512);
    for (const Entry &e : entries_)
        for (uint32_t x : {e.hash, e.sector, e.size, e.flags, e.sum, e.last}) put32(d, x);
    uint32_t sum = 0;
    for (uint8_t c : d) sum += c;
    for (uint32_t x : {0u, 0u, 0u, 0u, sum, 0u}) put32(d, x);
    d.resize((size_t)kDirSectors * 512, 0);
    overlay::add(image_, (uint64_t)kDirSector * 512, std::move(d), "the disk's directory (" + labels_ + ")");
    changed_ = false;
}

}  // namespace hk::pythondisk
