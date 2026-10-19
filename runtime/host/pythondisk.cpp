// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/pythondisk.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
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
    // how the game reads it: the one there's form, or for a new file its name's kind
    enum { Lzss, Stored, Raw } form = Lzss;
    if (e) {
        uint8_t head[8] = {};
        if (!overlay::read(image_, (uint64_t)e->sector * 512, head, sizeof head)) throw std::runtime_error(image_ + ": can't read " + name);
        if (!memcmp(head, "13P0", 4)) throw std::runtime_error(name + " is the game's program: change it with [[patch]]");
        form = tagged(head) ? Lzss : u32(head) == e->size - 4 ? Stored : Raw;
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
