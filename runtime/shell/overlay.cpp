// SPDX-License-Identifier: GPL-3.0-or-later
#include "shell/overlay.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>

namespace hk::overlay {

namespace {
struct Range {
    uint64_t off;
    std::vector<uint8_t> data;
    std::string label;
};
struct Image {
    std::string key;
    std::vector<Range> ranges;   // in the order added: a later one wins
    std::set<std::string> seen;  // labels read at least once (logged then)
};
std::mutex g_mu;
std::vector<Image> g_images;     // id = index + 1

// the same image however it was named (a link, a relative path)
std::string key_of(const std::string &path) {
    std::error_code ec;
    const auto c = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    return ec ? path : c.string();
}
}  // namespace

void add(const std::string &path, uint64_t offset, std::vector<uint8_t> data, const std::string &label) {
    std::lock_guard<std::mutex> lk(g_mu);
    const std::string k = key_of(path);
    for (Image &i : g_images)
        if (i.key == k) {
            i.ranges.push_back({offset, std::move(data), label});
            return;
        }
    g_images.push_back({k, {{offset, std::move(data), label}}, {}});
}

int find(const std::string &path) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_images.empty()) return 0;
    const std::string k = key_of(path);
    for (size_t i = 0; i < g_images.size(); i++)
        if (g_images[i].key == k) return (int)i + 1;
    return 0;
}

void apply(int id, uint64_t off, void *buf, size_t n) {
    if (id <= 0 || n == 0) return;
    // the ranges don't change once the game runs (they're all added before the media open)
    Image &img = g_images[(size_t)id - 1];
    uint8_t *b = static_cast<uint8_t *>(buf);
    for (const Range &r : img.ranges) {
        const uint64_t a = std::max(off, r.off), e = std::min(off + n, r.off + r.data.size());
        if (a >= e) continue;
        memcpy(b + (a - off), r.data.data() + (a - r.off), (size_t)(e - a));
        if (img.seen.insert(r.label).second)   // the first read of what a mod replaced: say so (once)
            fprintf(stderr, "mods: %s first read (%s, at 0x%llx)\n", r.label.empty() ? "replaced bytes" : r.label.c_str(),
                    img.key.c_str(), (unsigned long long)a);
    }
}

uint64_t extent(int id, uint64_t size) {
    if (id <= 0) return size;
    for (const Range &r : g_images[(size_t)id - 1].ranges) size = std::max<uint64_t>(size, r.off + r.data.size());
    return size;
}

namespace {
Reader g_reader = nullptr;
}

void set_reader(Reader r) { g_reader = r; }

bool read(const std::string &path, uint64_t off, void *buf, size_t n) {
    if (g_reader) return g_reader(path, off, buf, n);
    std::ifstream f(path, std::ios::binary);
    return f && f.seekg((std::streamoff)off) && f.read(static_cast<char *>(buf), (std::streamsize)n);
}

}  // namespace hk::overlay
