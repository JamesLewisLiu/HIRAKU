// SPDX-License-Identifier: GPL-3.0-or-later
#include "script.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "../shell/core.h"

namespace hk::script {

namespace {

struct Action {
    std::string verb, field;
    int64_t a = 0, b = 0, c = 0;
    std::string text;
};
struct Repeat {
    uint64_t every, from;
    Action act;
};

std::map<uint64_t, std::vector<Action>> g_at;
std::vector<Repeat> g_repeat;
std::vector<std::tuple<std::string, int, uint32_t>> g_watch;    // name, width, addr
std::vector<std::tuple<std::string, uint32_t, uint32_t>> g_hash; // name, lo, hi
std::map<std::string, uint64_t> g_release;                       // field -> frame
FILE *g_frames = nullptr, *g_log = nullptr;
std::string g_out;

void set_field(const std::string &name, int64_t value) {
    if (!core().set_input(name, (int)value)) fprintf(stderr, "script: unknown input field '%s'\n", name.c_str());
}

std::vector<std::string> tokens(const std::string &line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        char c = line[i];
        if (c == '#') break;
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c == '"') {
            size_t j = line.find('"', i + 1);
            if (j == std::string::npos) throw std::runtime_error("unterminated quote");
            t.push_back(line.substr(i + 1, j - i - 1));
            i = j + 1;
        } else {
            size_t j = line.find_first_of(" \t#", i);
            if (j == std::string::npos) j = line.size();
            t.push_back(line.substr(i, j - i));
            i = j;
        }
    }
    return t;
}

int64_t num(const std::string &s) { return std::stoll(s, nullptr, 0); }

Action parse_action(const std::vector<std::string> &t, size_t k) {
    Action a;
    a.verb = t.at(k);
    if (a.verb == "press") { a.field = t.at(k + 1); a.a = t.size() > k + 2 ? num(t[k + 2]) : 8; }
    else if (a.verb == "hold" || a.verb == "release") a.field = t.at(k + 1);
    else if (a.verb == "set") { a.field = t.at(k + 1); a.a = num(t.at(k + 2)); }
    else if (a.verb == "dump") { a.field = t.at(k + 1); a.a = num(t.at(k + 2)); a.b = num(t.at(k + 3)); }
    else if (a.verb == "mark") { for (size_t j = k + 1; j < t.size(); j++) a.text += (j > k + 1 ? " " : "") + t[j]; }
    else if (a.verb == "snap" || a.verb == "quit" || a.verb == "itrace") {}
    else throw std::runtime_error("unknown action '" + a.verb + "'");
    return a;
}

uint32_t fnv(uint32_t lo, uint32_t hi) {
    uint32_t h = 0x811c9dc5u;
    const uint8_t *m = hi > lo ? core().mem(lo, hi - lo) : nullptr;
    if (!m) return h;
    for (uint32_t a = lo; a + 3 < hi; a += 4) {
        const uint8_t *q = m + (a - lo);
        uint32_t v = (uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3];
        h = (h ^ v) * 0x01000193u;
    }
    return h;
}

}  // namespace

bool load(const std::string &path, const std::string &out_dir) {
    g_out = out_dir;
    std::ifstream in(path);
    if (!in) { fprintf(stderr, "script: cannot open %s\n", path.c_str()); return false; }
    int n = 0;
    for (std::string line; std::getline(in, line);) {
        n++;
        try {
            auto t = tokens(line);
            if (t.empty()) continue;
            if (t[0] == "game") continue;
            if (t[0] == "watch") {
                int w = t.at(2) == "u8" ? 1 : t[2] == "u16" ? 2 : 4;
                g_watch.emplace_back(t[1], w, (uint32_t)num(t.at(3)));
            } else if (t[0] == "hash") {
                g_hash.emplace_back(t[1], (uint32_t)num(t.at(2)), (uint32_t)num(t.at(3)));
            } else if (t[0] == "at") {
                g_at[(uint64_t)num(t.at(1))].push_back(parse_action(t, 2));
            } else if (t[0] == "every") {
                g_repeat.push_back({(uint64_t)num(t.at(1)), 0, parse_action(t, 2)});
            } else if (t[0] == "from") {
                if (t.at(2) != "every") throw std::runtime_error("expected: from <frame> every <n> <action>");
                g_repeat.push_back({(uint64_t)num(t.at(3)), (uint64_t)num(t.at(1)), parse_action(t, 4)});
            } else {
                throw std::runtime_error("unknown directive '" + t[0] + "'");
            }
        } catch (const std::exception &e) {
            fprintf(stderr, "%s:%d: %s\n", path.c_str(), n, e.what());
            return false;
        }
    }
    g_frames = fopen((out_dir + "/frames.txt").c_str(), "w");
    g_log = fopen((out_dir + "/log.txt").c_str(), "w");
    if (g_frames) {
        fprintf(g_frames, "# frame time");
        for (auto &[name, w, a] : g_watch) fprintf(g_frames, " %s", name.c_str());
        for (auto &[name, lo, hi] : g_hash) fprintf(g_frames, " #%s", name.c_str());
        fprintf(g_frames, "\n");
    }
    return true;
}

bool frame(uint64_t f, const std::function<void()> &snap) {
    for (auto it = g_release.begin(); it != g_release.end();) {
        if (f >= it->second) { set_field(it->first, 0); it = g_release.erase(it); }
        else ++it;
    }
    if (g_frames) {
        fprintf(g_frames, "%llu %.6f", (unsigned long long)f, core().seconds());
        for (auto &[name, w, a] : g_watch) {
            const uint8_t *m = core().mem(a, (uint32_t)w);
            uint32_t v = 0;
            for (int k = 0; m && k < w; k++) v = (v << 8) | m[k];
            fprintf(g_frames, " %x", v);
        }
        for (auto &[name, lo, hi] : g_hash) fprintf(g_frames, " %08x", fnv(lo, hi));
        fprintf(g_frames, "\n");
    }
    bool keep = true;
    auto run = [&](const Action &a) {
        if (g_log) fprintf(g_log, "f%llu %s %s\n", (unsigned long long)f, a.verb.c_str(), a.field.c_str());
        if (a.verb == "press") { set_field(a.field, 1); g_release[a.field] = f + (uint64_t)a.a; }
        else if (a.verb == "hold") set_field(a.field, 1);
        else if (a.verb == "release") set_field(a.field, 0);
        else if (a.verb == "set") set_field(a.field, a.a);
        else if (a.verb == "snap") snap();
        else if (a.verb == "dump") {
            char name[256];
            snprintf(name, sizeof name, "%s/dump-%s-f%llu.bin", g_out.c_str(), a.field.c_str(), (unsigned long long)f);
            const uint8_t *m = a.b > a.a ? core().mem((uint32_t)a.a, (uint32_t)(a.b - a.a)) : nullptr;
            if (!m) fprintf(stderr, "script: dump %s: not plain memory\n", a.field.c_str());
            else if (FILE *o = fopen(name, "wb")) {
                fwrite(m, 1, (size_t)(a.b - a.a), o);
                fclose(o);
            }
        } else if (a.verb == "quit") keep = false;
    };
    for (auto &r : g_repeat)
        if (f >= r.from && (f - r.from) % r.every == 0) run(r.act);
    if (auto it = g_at.find(f); it != g_at.end())
        for (auto &a : it->second) run(a);
    return keep;
}

void close() {
    if (g_frames) fclose(g_frames);
    if (g_log) fclose(g_log);
    g_frames = g_log = nullptr;
}

}  // namespace hk::script
