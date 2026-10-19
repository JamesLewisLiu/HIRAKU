// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/mods.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "shell/core.h"
#include "shell/overlay.h"

namespace hk::mods {

namespace {

// ---- mod.toml: the TOML a mod needs (docs/MODS.md) -----------------------------------------
// key = "string" | number (decimal, 0x hex, _ separators) | true | false, # comments, and
// [[patch]] / [[bytes]] / [[file]] tables. Values keep their text; the reader below converts.
struct Table {
    std::string kind;   // "" for the top, else patch / bytes / file
    int line = 0;
    std::map<std::string, std::string> values;
    std::map<std::string, bool> is_string;
};

std::vector<Table> parse(const std::string &text, const std::string &file) {
    std::vector<Table> out(1);
    std::istringstream in(text);
    std::string line;
    int n = 0;
    auto fail = [&](const std::string &why) { throw std::runtime_error(file + ":" + std::to_string(n) + ": " + why); };
    while (std::getline(in, line)) {
        n++;
        // the comment, outside a string
        bool str = false;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) str = !str;
            else if (line[i] == '#' && !str) { line.erase(i); break; }
        }
        size_t a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
        if (a == std::string::npos) continue;
        line = line.substr(a, b - a + 1);
        if (line.rfind("[[", 0) == 0) {
            if (line.size() < 4 || line.substr(line.size() - 2) != "]]") fail("a table header is [[name]]");
            Table t;
            t.kind = line.substr(2, line.size() - 4);
            t.line = n;
            out.push_back(t);
            continue;
        }
        if (line[0] == '[') fail("only [[patch]], [[bytes]] and [[file]] tables");
        const size_t eq = line.find('=');
        if (eq == std::string::npos) fail("expected key = value");
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        Table &t = out.back();
        if (t.values.count(key)) fail("'" + key + "' given twice");
        if (!val.empty() && val[0] == '"') {
            std::string s;
            size_t i = 1;
            for (; i < val.size() && val[i] != '"'; i++) {
                if (val[i] == '\\' && i + 1 < val.size()) {
                    const char e = val[++i];
                    s += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                } else
                    s += val[i];
            }
            if (i >= val.size()) fail("unterminated string");
            t.values[key] = s;
            t.is_string[key] = true;
        } else {
            if (val.empty()) fail("'" + key + "' has no value");
            t.values[key] = val;
            t.is_string[key] = false;
        }
    }
    return out;
}

struct Patch {
    std::string mod;
    uint32_t address = 0;
    std::vector<uint8_t> original, replace;
    bool once = false, done = false, logged = false;
};

struct Mod {
    std::string dir, name, version;
};

std::vector<Patch> g_patches;
std::vector<Mod> g_mods;

std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// the media of dumps/<game>/machine.txt: key -> path (relative paths are the game's folder's)
std::map<std::string, std::string> media(const std::string &root, const std::string &game) {
    std::map<std::string, std::string> m;
    const std::string dir = root + "/dumps/" + game;
    std::istringstream in(read_file(dir + "/machine.txt"));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string v = line.substr(eq + 1);
        const bool absolute = !v.empty() && (v[0] == '/' || v[0] == '\\' || (v.size() > 1 && v[1] == ':'));
        std::string path = absolute ? v : dir + "/" + v;
        // a cue sheet's medium is the data file it names (its first FILE, as chdman extractcd
        // writes one): that's what the core reads
        if (path.size() > 4 && (path.substr(path.size() - 4) == ".cue" || path.substr(path.size() - 4) == ".CUE")) {
            std::istringstream cue(read_file(path));
            std::string l;
            while (std::getline(cue, l)) {
                const size_t q1 = l.find('"'), q2 = q1 == std::string::npos ? q1 : l.find('"', q1 + 1);
                if (l.find("FILE") == std::string::npos || q2 == std::string::npos) continue;
                const std::string f = l.substr(q1 + 1, q2 - q1 - 1);
                const size_t slash = path.find_last_of("/\\");
                path = (f.size() && (f[0] == '/' || (f.size() > 1 && f[1] == ':'))) ? f
                                                                                    : path.substr(0, slash == std::string::npos ? 0 : slash + 1) + f;
                break;
            }
        }
        m[line.substr(0, eq)] = path;
    }
    return m;
}

// [[file]]: a file on an ISO 9660 disc (docs/MODS.md), its data laid over the sectors it holds.
// The data track's sectors are told from where the volume descriptor is: 2048 bytes of data
// each (a cooked image), or 2352 with a 16-byte (mode 1) or 24-byte (mode 2 form 1) header.
void iso_file(const std::string &image, const std::string &name, std::vector<uint8_t> data, const std::string &label) {
    struct Layout { uint64_t sector, header; };
    const Layout layouts[] = {{2048, 0}, {2352, 16}, {2352, 24}};
    const Layout *lay = nullptr;
    uint8_t pvd[2048];
    for (const Layout &l : layouts)
        if (overlay::read(image, 16 * l.sector + l.header, pvd, sizeof pvd) && pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5)) {
            lay = &l;
            break;
        }
    if (!lay) throw std::runtime_error(image + ": no ISO 9660 volume (or it can't be read)");
    auto u32 = [](const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; };
    auto read_sector = [&](uint32_t lba, uint8_t *out) {
        if (!overlay::read(image, (uint64_t)lba * lay->sector + lay->header, out, 2048))
            throw std::runtime_error(image + ": can't read sector " + std::to_string(lba));
    };
    uint32_t lba = u32(pvd + 156 + 2), size = u32(pvd + 156 + 10);   // the root directory
    bool dir = true;
    std::string rest = name;
    while (!rest.empty()) {
        const size_t slash = rest.find('/');
        const std::string part = rest.substr(0, slash);
        rest = slash == std::string::npos ? "" : rest.substr(slash + 1);
        if (part.empty()) continue;
        if (!dir) throw std::runtime_error(image + ": " + name + ": not a directory on the way");
        bool found = false;
        std::vector<uint8_t> sec(2048);
        for (uint32_t s = 0; s * 2048 < size && !found; s++) {
            read_sector(lba + s, sec.data());
            for (size_t at = 0; at < 2048 && sec[at];) {
                const uint8_t *r = &sec[at];
                std::string id((const char *)r + 33, r[32]);
                if (const size_t semi = id.find(';'); semi != std::string::npos) id.erase(semi);   // the version
                if (!id.empty() && id.back() == '.') id.pop_back();
                bool same = id.size() == part.size();
                for (size_t i = 0; same && i < id.size(); i++) same = std::toupper((unsigned char)id[i]) == std::toupper((unsigned char)part[i]);
                if (same && r[32] > 1) {
                    lba = u32(r + 2), size = u32(r + 10), dir = r[25] & 2;
                    found = true;
                    break;
                }
                at += r[0];
            }
        }
        if (!found) throw std::runtime_error(image + ": no " + name + " on the disc");
    }
    if (dir) throw std::runtime_error(image + ": " + name + " is a directory");
    if (data.size() > size)
        throw std::runtime_error(name + ": the mod's file is " + std::to_string(data.size()) + " bytes, the disc's " +
                                 std::to_string(size) + " (a file can't grow yet)");
    data.resize(size, 0);   // shorter: the rest of the file's bytes are zeros
    if (lay->sector == 2048) {
        overlay::add(image, (uint64_t)lba * 2048, std::move(data), label);
        return;
    }
    for (uint32_t s = 0; (uint64_t)s * 2048 < size; s++) {   // raw sectors: each one's 2048 data bytes
        const size_t from = (size_t)s * 2048, n = std::min<size_t>(2048, size - from);
        overlay::add(image, (uint64_t)(lba + s) * lay->sector + lay->header,
                     std::vector<uint8_t>(data.begin() + (long)from, data.begin() + (long)(from + n)), label);
    }
}

}  // namespace

bool load(const std::vector<std::string> &dirs, const std::string &game, const std::string &root, std::string &err) {
    try {
        const auto medium_paths = media(root, game);
        for (const std::string &dir : dirs) {
            const std::string file = dir + "/mod.toml";
            const std::string text = read_file(file);
            if (text.empty()) throw std::runtime_error(file + ": not found (or empty)");
            const std::vector<Table> tables = parse(text, file);
            Mod mod{dir, "", ""};
            for (const Table &t : tables) {
                const std::string where = file + (t.line ? ":" + std::to_string(t.line) : std::string());
                auto fail = [&](const std::string &why) { throw std::runtime_error(where + ": " + why); };
                auto str = [&](const char *k, bool need) -> std::string {
                    auto it = t.values.find(k);
                    if (it == t.values.end()) {
                        if (need) fail(std::string("'") + k + "' is missing");
                        return {};
                    }
                    if (!t.is_string.at(k)) fail(std::string("'") + k + "' is a string (in quotes)");
                    return it->second;
                };
                auto num = [&](const char *k) -> uint64_t {
                    auto it = t.values.find(k);
                    if (it == t.values.end()) fail(std::string("'") + k + "' is missing");
                    std::string v;
                    for (char c : it->second) if (c != '_') v += c;
                    char *end = nullptr;
                    const uint64_t r = std::strtoull(v.c_str(), &end, 0);
                    if (v.empty() || *end) fail(std::string("'") + k + "' isn't a number");
                    return r;
                };
                auto flag = [&](const char *k) {
                    auto it = t.values.find(k);
                    if (it == t.values.end()) return false;
                    if (it->second != "true" && it->second != "false") fail(std::string("'") + k + "' is true or false");
                    return it->second == "true";
                };
                auto hex = [&](const char *k) {
                    std::vector<uint8_t> b;
                    std::string h;
                    for (char c : str(k, true))
                        if (!std::isspace((unsigned char)c)) h += c;
                    if (h.size() % 2) fail(std::string("'") + k + "' has an odd number of hex digits");
                    for (size_t i = 0; i < h.size(); i += 2) {
                        if (!std::isxdigit((unsigned char)h[i]) || !std::isxdigit((unsigned char)h[i + 1])) fail(std::string("'") + k + "' isn't hex");
                        b.push_back((uint8_t)std::stoul(h.substr(i, 2), nullptr, 16));
                    }
                    return b;
                };
                auto known = [&](std::initializer_list<const char *> keys) {
                    for (const auto &[k, v] : t.values) {
                        bool ok = false;
                        for (const char *x : keys) ok |= k == x;
                        if (!ok) fail("unknown key '" + k + "'");
                    }
                };
                auto from = [&]() {
                    const std::string f = str("from", true);
                    if (f.find("..") != std::string::npos) fail("'from' is a file inside the mod's folder");
                    std::string data = read_file(dir + "/" + f);
                    if (data.empty()) fail("'from' file " + f + " not found (or empty)");
                    return std::vector<uint8_t>(data.begin(), data.end());
                };
                if (t.kind.empty()) {
                    known({"name", "version", "game", "author", "description"});
                    mod.name = str("name", true);
                    mod.version = str("version", false);
                    if (const std::string g = str("game", true); g != game) fail("this mod is for " + g + ", not " + game);
                } else if (t.kind == "patch") {
                    known({"address", "original", "replace", "once"});
                    Patch p;
                    p.mod = mod.name;
                    p.address = (uint32_t)num("address");
                    if (t.values.count("original")) p.original = hex("original");   // none: written every frame (a cheat)
                    p.replace = hex("replace");
                    p.once = flag("once");
                    if (p.replace.empty()) fail("'replace' is empty");
                    if (!p.original.empty() && p.original.size() != p.replace.size()) fail("'original' and 'replace' are as long as each other");
                    g_patches.push_back(std::move(p));
                } else if (t.kind == "bytes") {
                    known({"medium", "offset", "from"});
                    const std::string medium = str("medium", true);
                    auto it = medium_paths.find(medium);
                    if (it == medium_paths.end()) fail("no medium '" + medium + "' in dumps/" + game + "/machine.txt");
                    char what[64];
                    snprintf(what, sizeof what, ": %s bytes at 0x%llx", medium.c_str(), (unsigned long long)num("offset"));
                    overlay::add(it->second, num("offset"), from(), mod.name + what);
                } else if (t.kind == "file") {
                    known({"medium", "path", "from"});
                    const std::string medium = str("medium", true);
                    auto it = medium_paths.find(medium);
                    if (it == medium_paths.end()) fail("no medium '" + medium + "' in dumps/" + game + "/machine.txt");
                    try {
                        iso_file(it->second, str("path", true), from(), mod.name + ": " + str("path", true));
                    } catch (const std::exception &e) {
                        fail(e.what());
                    }
                } else {
                    fail("unknown table [[" + t.kind + "]]");
                }
            }
            g_mods.push_back(mod);
        }
    } catch (const std::exception &e) {
        err = e.what();
        return false;
    }
    return true;
}

void frame() {
    for (Patch &p : g_patches) {
        if (p.done) continue;
        const size_t n = p.replace.size();
        auto *m = const_cast<uint8_t *>(core().mem(p.address, (uint32_t)n));
        if (!m || memcmp(m, p.replace.data(), n) == 0) continue;   // not mapped, or in place
        if (!p.original.empty() && memcmp(m, p.original.data(), n) != 0) continue;   // not loaded (yet): wait
        memcpy(m, p.replace.data(), n);
        core().code_changed(p.address, (uint32_t)n);
        if (!p.logged) {
            fprintf(stderr, "mods: %s: patch at 0x%08x (%zu bytes) in place\n", p.mod.c_str(), p.address, n);
            p.logged = true;
        }
        if (p.once) p.done = true;
    }
}

std::vector<std::string> describe() {
    std::vector<std::string> d;
    for (const Mod &m : g_mods) d.push_back(m.name + (m.version.empty() ? "" : " " + m.version) + " (" + m.dir + ")");
    return d;
}

}  // namespace hk::mods
