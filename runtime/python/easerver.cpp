// SPDX-License-Identifier: GPL-3.0-or-later
// A local e-AMUSE3 server for Python 1 pop'n (easerver.h).
#include "python/easerver.h"
#include "python/lobby.h"
#include "shell/sys.h"

#include <sys/stat.h>

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <set>

namespace hk::py {

namespace {

// The protocol's shared secret (as in bemaniutils' protocol code)
constexpr uint8_t SECRET[26] = {0x69, 0xd7, 0x46, 0x27, 0xd9, 0x85, 0xee, 0x21, 0x87, 0x16, 0x15, 0x70, 0xd0,
                                0x8d, 0x93, 0xb1, 0x24, 0x55, 0x03, 0x5b, 0x6d, 0xf0, 0xd8, 0x20, 0x5d, 0xf5};

// MD5 (RFC 1321)
std::vector<uint8_t> md5(const std::vector<uint8_t> &msg) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                              14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::vector<uint8_t> m = msg;
    const uint64_t bits = (uint64_t)msg.size() * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 0; i < 8; i++) m.push_back((uint8_t)(bits >> (8 * i)));
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)m[off + 4 * i] | (uint32_t)m[off + 4 * i + 1] << 8 | (uint32_t)m[off + 4 * i + 2] << 16 |
                   (uint32_t)m[off + 4 * i + 3] << 24;
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            const uint32_t t = d;
            d = c;
            c = b;
            const uint32_t x = a + f + K[i] + w[g];
            b = b + ((x << R[i]) | (x >> (32 - R[i])));
            a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    std::vector<uint8_t> out;
    for (uint32_t v : h)
        for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (8 * i)));
    return out;
}

std::string header(const std::string &head, const char *name) {
    const size_t n = std::strlen(name);
    for (size_t at = head.find("\r\n"); at != std::string::npos; at = head.find("\r\n", at + 2)) {
        if (head.size() - at - 2 < n + 1 || strncasecmp(head.c_str() + at + 2, name, n) != 0 || head[at + 2 + n] != ':') continue;
        size_t b = at + 3 + n;
        while (b < head.size() && head[b] == ' ') b++;
        const size_t e = head.find("\r\n", b);
        return head.substr(b, e == std::string::npos ? std::string::npos : e - b);
    }
    return "";
}

// name="value" pairs from the start of an element (after its name), to its '>'
size_t read_attrs(const std::string &x, size_t at, std::map<std::string, std::string> &out, bool *self_closing) {
    while (at < x.size()) {
        while (at < x.size() && std::isspace((unsigned char)x[at])) at++;
        if (at >= x.size()) break;
        if (x[at] == '/' || x[at] == '>') {
            *self_closing = x[at] == '/';
            const size_t e = x.find('>', at);
            return e == std::string::npos ? x.size() : e + 1;
        }
        const size_t eq = x.find('=', at);
        if (eq == std::string::npos || eq + 1 >= x.size()) break;
        const char q = x[eq + 1];
        const size_t end = x.find(q, eq + 2);
        if (end == std::string::npos) break;
        std::string name = x.substr(at, eq - at);
        while (!name.empty() && std::isspace((unsigned char)name.back())) name.pop_back();
        out[name] = x.substr(eq + 2, end - eq - 2);
        at = end + 1;
    }
    return x.size();
}

// pop'n 13's compression for player records (f_compress, the game's 0x1810140): LZSS with a 4 KiB
// window that starts at 0xfee, a flag byte per 8 items (1 = a literal byte, 0 = a match: 12-bit
// window position and 4-bit length - 3, so 3 to 18 bytes), after the length in 4 bytes. The game
// answers in kind only with the data small enough for its HTTP reader: 0xd6c bytes in clear are
// refused, compressed they are a few hundred.
std::string lzss(const std::string &in) {
    const size_t n = in.size();
    std::string out;
    for (int k = 0; k < 4; k++) out += (char)(n >> (8 * k) & 255);
    size_t i = 0;
    while (i < n) {
        const size_t flags_at = out.size();
        out += '\0';
        uint8_t flags = 0;
        for (int bit = 0; bit < 8 && i < n; bit++) {
            size_t best = 0, from = 0;
            for (size_t q = i > 4078 ? i - 4078 : 0; q < i; q++) {   // earlier output, overlap allowed
                size_t len = 0;
                while (len < 18 && i + len < n && in[q + len] == in[i + len]) len++;
                if (len > best) { best = len; from = q; }
            }
            if (best >= 3) {
                const unsigned at = (unsigned)(0xfee + from) & 0xfff;
                out += (char)(at & 255);
                out += (char)(((at >> 4) & 0xf0) | (best - 3));
                i += best;
            } else {
                flags |= (uint8_t)(1 << bit);
                out += in[i++];
            }
        }
        out[flags_at] = (char)flags;
    }
    return out;
}

std::string base64(const std::string &in) {
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < in.size(); i += 3) {
        const uint32_t v = (uint8_t)in[i] << 16 | (i + 1 < in.size() ? (uint8_t)in[i + 1] << 8 : 0) | (i + 2 < in.size() ? (uint8_t)in[i + 2] : 0);
        o += T[v >> 18 & 63];
        o += T[v >> 12 & 63];
        o += i + 1 < in.size() ? T[v >> 6 & 63] : '=';
        o += i + 2 < in.size() ? T[v & 63] : '=';
    }
    return o;
}

// The bytes of every <b>...</b> in x, base64-decoded and joined (a game's player data, as saved)
std::string unbase64_b(const std::string &x) {
    std::string out;
    for (size_t at = x.find("<b>"); at != std::string::npos; at = x.find("<b>", at + 3)) {
        const size_t e = x.find("</b>", at);
        if (e == std::string::npos) break;
        uint32_t acc = 0;
        int bits = 0;
        for (size_t i = at + 3; i < e; i++) {
            const char c = x[i];
            int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
            if (v < 0) continue;
            acc = acc << 6 | (uint32_t)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += (char)(acc >> bits & 0xff);
            }
        }
    }
    return out;
}

std::string fmt(const char *f, unsigned a, unsigned b) {
    char s[64];
    snprintf(s, sizeof s, f, a, b);
    return s;
}

}  // namespace

std::vector<uint8_t> EamuseServer::key(const std::string &info) {
    // "1-xxxxxxxx-xxxx": the 6 bytes, then the secret
    std::vector<uint8_t> m;
    std::string hex;
    for (char c : info.substr(info.find('-') + 1))
        if (std::isxdigit((unsigned char)c)) hex += c;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) m.push_back((uint8_t)std::stoul(hex.substr(i, 2), nullptr, 16));
    m.insert(m.end(), SECRET, SECRET + sizeof SECRET);
    return md5(m);
}

void EamuseServer::rc4(const std::vector<uint8_t> &k, std::string &d) {
    uint8_t s[256];
    for (int i = 0; i < 256; i++) s[i] = (uint8_t)i;
    for (int i = 0, j = 0; i < 256; i++) {
        j = (j + s[i] + k[i % k.size()]) & 255;
        std::swap(s[i], s[j]);
    }
    int i = 0, j = 0;
    for (char &c : d) {
        i = (i + 1) & 255;
        j = (j + s[i]) & 255;
        std::swap(s[i], s[j]);
        c = (char)((uint8_t)c ^ s[(s[i] + s[j]) & 255]);
    }
}

std::string EamuseServer::clear(const std::string &m) {
    const size_t he = m.find("\r\n\r\n");
    if (he == std::string::npos) return m;
    const std::string head = m.substr(0, he), info = header(head, "X-Eamuse-Info");
    std::string body = m.substr(he + 4);
    if (!info.empty()) rc4(key(info), body);
    return head.substr(0, head.find("\r\n")) + (info.empty() ? " (plain): " : ": ") + body;
}

bool EamuseServer::parse(const std::string &x, Call &c) {
    size_t at = x.find("<call");
    if (at == std::string::npos) return false;
    std::map<std::string, std::string> a;
    bool sc = false;
    at = read_attrs(x, at + 5, a, &sc);
    c.srcid = a["srcid"];
    c.model = a["model"];
    for (at = x.find('<', at); at != std::string::npos && at + 1 < x.size() && x[at + 1] != '/'; at = x.find('<', at)) {
        size_t e = at + 1;
        while (e < x.size() && !std::isspace((unsigned char)x[e]) && x[e] != '/' && x[e] != '>') e++;
        Request r;
        r.module = x.substr(at + 1, e - at - 1);
        sc = false;
        at = read_attrs(x, e, r.attrs, &sc);
        r.method = r.attrs["method"];
        if (!sc) {
            const size_t close = x.find("</" + r.module, at);
            r.inner = x.substr(at, close == std::string::npos ? std::string::npos : close - at);
            at = close == std::string::npos ? x.size() : x.find('>', close) + 1;
        }
        c.requests.push_back(std::move(r));
    }
    return !c.requests.empty();
}

std::string EamuseServer::read_file(const std::string &name) const {
    if (dir_.empty()) return "";
    std::ifstream f(dir_ + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void EamuseServer::write_file(const std::string &name, const std::string &data) const {
    if (dir_.empty()) return;
    sys::make_dir(dir_);
    const std::string path = dir_ + "/" + name, tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << data;
        if (!f) return;
    }
    std::rename(tmp.c_str(), path.c_str());
}

namespace {

bool safe_name(const std::string &s) {   // a card number or id used in a file name
    if (s.empty() || s.size() > 40) return false;
    for (char ch : s)
        if (!std::isalnum((unsigned char)ch) && ch != '-' && ch != '_') return false;
    return true;
}

std::string esc(const std::string &v) {   // an attribute value, XML-escaped
    std::string o;
    for (char ch : v) {
        switch (ch) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += ch;
        }
    }
    return o;
}

std::string attr(const std::map<std::string, std::string> &m, const char *k) {
    const auto it = m.find(k);
    return it == m.end() ? "" : it->second;
}

long num(const std::map<std::string, std::string> &m, const char *k, int base = 10) {
    return std::strtol(attr(m, k).c_str(), nullptr, base);
}

// Records kept as lines of tab-separated key=value fields (values never hold tabs or newlines)
using Record = std::map<std::string, std::string>;

std::string to_line(const Record &r) {
    std::string l;
    for (const auto &[k, v] : r) {
        std::string clean = v;
        for (char &ch : clean)
            if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
        l += (l.empty() ? "" : "\t") + k + "=" + clean;
    }
    return l + "\n";
}

std::vector<Record> from_lines(const std::string &text) {
    std::vector<Record> out;
    size_t at = 0;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(at, e - at);
        at = e + 1;
        if (line.empty()) continue;
        Record r;
        size_t f = 0;
        while (f <= line.size()) {
            size_t t = line.find('\t', f);
            if (t == std::string::npos) t = line.size();
            const std::string field = line.substr(f, t - f);
            if (const size_t eq = field.find('='); eq != std::string::npos) r[field.substr(0, eq)] = field.substr(eq + 1);
            f = t + 1;
        }
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace

// The three rankings (docs/EAMUSE3.md, "rank"): what is kept and how entries compare
struct EamuseServer::Board {
    const char *file;
    const char *who;                   // the request's field naming the entrant
    std::vector<const char *> keep;    // fields from the request kept with the entry
    const char *first, *second;        // ranked by these, larger first
    int second_base;                   // parts is hex
};
namespace {
using Board = EamuseServer::Board;
const Board BOARD_SCORE = {"rank-score", "card_id", {"name", "score", "cool", "great", "good", "bad", "combo", "option", "konami_id", "shop_name", "area_num", "gpcb_id"}, "score", "cool", 10};
const Board BOARD_H = {"rank-hitasura", "card_id", {"name", "music", "combo", "konami_id", "shop_name", "area_num", "gpcb_id"}, "music", "combo", 10};
const Board BOARD_GZ = {"rank-gamblerz", "gpcb_id", {"prog", "parts", "shop_name", "area_num"}, "prog", "parts", 16};
// pop'n 10 on: rank.exp sends what pop'n 9's rank.register did; one board per game (the file's prefix)
const std::vector<const char *> EXP_KEEP = {"name", "score", "cool", "great", "good", "bad", "combo", "option", "shop_name", "area_num"};

bool better(const Board &b, const Record &x, const Record &y) {   // x ranks above y
    const long x1 = num(x, b.first), y1 = num(y, b.first);
    if (x1 != y1) return x1 > y1;
    return num(x, b.second, b.second_base) > num(y, b.second, b.second_base);
}

}  // namespace

std::string EamuseServer::rank_register(const Board &b, const Request &r) {
    const Entered e = enter(b, r);
    return item(b, e.kept, e.place) + "entry=\"" + std::to_string(e.entries) + "\" update=\"" + (e.update ? "1" : "0") + "\"/>";
}

// A result entered on a board: kept if it beats the entrant's best; where the entrant now stands
EamuseServer::Entered EamuseServer::enter(const Board &b, const Request &r) {
    // pop'n 12 on name the course course_num
    const char *course_key = attr(r.attrs, "course").empty() && !attr(r.attrs, "course_num").empty() ? "course_num" : "course";
    const std::string course = b.file == BOARD_GZ.file ? "0" : std::to_string(num(r.attrs, course_key));
    std::string who = attr(r.attrs, b.who);
    if (who.empty()) who = attr(r.attrs, "ref_id");   // pop'n 13 on: the player's ref id
    std::vector<Record> all = from_lines(read_file(b.file));
    Record mine{{"course", course}, {"who", who}};
    for (const char *k : b.keep) mine[k] = attr(r.attrs, k);
    if (mine.count("area_num") && mine["area_num"].empty()) mine["area_num"] = attr(r.attrs, "pref");   // pop'n 12 on
    // pop'n 14 sends neither a score nor a name: the stages' scores added up, and the player's
    // name from the record the server keeps (pdata-F14-REF, the name at 0x00, hex as the others)
    if (mine.count("score") && mine["score"].empty() && r.attrs.count("stage_score")) {
        long total = 0;
        const std::string ss = attr(r.attrs, "stage_score");
        for (size_t at = 0; at < ss.size();) {
            total += std::strtol(ss.c_str() + at, nullptr, 10);
            const size_t comma = ss.find(',', at);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        mine["score"] = std::to_string(total);
    }
    if (mine.count("name") && mine["name"].empty() && safe_name(who)) {
        const std::string rec = read_file("pdata-" + std::string(b.file).substr(0, 3) + "-" + who);
        std::string hex;
        for (size_t i = 0; i < 12 && i < rec.size() && rec[i]; i++) {
            char h[3];
            snprintf(h, sizeof h, "%02X", (unsigned char)rec[i]);
            hex += h;
        }
        mine["name"] = hex;
    }
    bool update = true;
    bool found = false;
    for (Record &e : all) {
        if (e["course"] != course || e["who"] != who) continue;
        found = true;
        if (better(b, mine, e)) e = mine;   // a better result replaces the kept one
        else update = false;
    }
    if (!found) all.push_back(mine);
    std::string text;
    for (const Record &e : all) text += to_line(e);
    if (update) write_file(b.file, text);
    // the entrant's place in the course
    std::vector<Record> board;
    for (const Record &e : all)
        if (e.at("course") == course) board.push_back(e);
    std::stable_sort(board.begin(), board.end(), [&](const Record &x, const Record &y) { return better(b, x, y); });
    Entered out{mine, 1, board.size(), update};
    for (size_t i = 0; i < board.size(); i++)
        if (board[i].at("who") == who) { out.place = i + 1; out.kept = board[i]; }
    return out;
}

std::string EamuseServer::rank_get(const Board &b, const Request &r) {
    const std::vector<Record> all = from_lines(read_file(b.file));
    const bool courses = b.file != BOARD_GZ.file;
    const long c0 = courses ? num(r.attrs, "course_offset") : 0, cn = courses ? std::max(1L, num(r.attrs, "course_num")) : 1;
    const long r0 = std::max(1L, num(r.attrs, "rank_offset")), rn = std::min(100L, std::max(0L, num(r.attrs, "rank_num")));
    std::string out;
    for (long c = c0; c < c0 + cn && c < c0 + 20; c++) {
        std::vector<Record> board;
        for (const Record &e : all)
            if (std::strtol(attr(e, "course").c_str(), nullptr, 10) == c) board.push_back(e);
        std::stable_sort(board.begin(), board.end(), [&](const Record &x, const Record &y) { return better(b, x, y); });
        out += courses ? "<item course=\"" + std::to_string(c) + "\" entry=\"" + std::to_string(board.size()) + "\"/>"
                       : "<item entry=\"" + std::to_string(board.size()) + "\"/>";
        for (long i = r0 - 1; i < (long)board.size() && i < r0 - 1 + rn; i++) out += item(b, board[(size_t)i], (size_t)i + 1) + "/>";
    }
    return out;
}

// An entry as the game reads it: <item rank=.. (the board's fields) area=.. id=..
std::string EamuseServer::item(const Board &b, const Record &e, size_t place) {
    std::string o = "<item rank=\"" + std::to_string(place) + "\" ";
    if (b.file == BOARD_SCORE.file)
        o += "name=\"" + esc(attr(e, "name")) + "\" score=\"" + esc(attr(e, "score")) + "\" cool=\"" + esc(attr(e, "cool")) + "\" ";
    else if (b.file == BOARD_H.file)
        o += "name=\"" + esc(attr(e, "name")) + "\" music=\"" + esc(attr(e, "music")) + "\" combo=\"" + esc(attr(e, "combo")) + "\" ";
    else
        o += "prog=\"" + esc(attr(e, "prog")) + "\" parts=\"" + esc(attr(e, "parts")) + "\" shop_name=\"" + esc(attr(e, "shop_name")) + "\" ";
    const std::string id = !attr(e, "konami_id").empty() ? attr(e, "konami_id") : attr(e, "who");
    return o + "area=\"" + esc(attr(e, "area_num")) + "\" id=\"" + esc(id) + "\" ";
}

std::string EamuseServer::answer(const Call &c, const Request &r, const std::string &host) {
    const std::string &m = r.module, &f = r.method;
    // pop'n 10 on (model C20, D11, E12, ...): their own calls and answer formats; pop'n 9 (C00)
    // and anything not handled there go on below
    if (const std::string game = c.model.substr(0, 3); !game.empty() && game != "C00")
        if (std::string a = answer_later(game, r, host); !a.empty()) return a;
    if (m == "numbering" && f == "assign")   // a new card's number (0x197530): 16 digits
        return "<numbering status=\"0\" number=\"" + next_number() + "\"/>";
    if (m == "userdata" && f == "read") {   // 109: not registered (0x184ca0)
        const std::string card = attr(r.attrs, "card");
        const std::string data = safe_name(card) ? read_file("userdata-" + card) : "";
        if (data.empty()) return "<userdata status=\"109\"/>";
        return with_shared_marks("C00", card, data);   // kept as the answer element itself, the card's marks put in
    }
    if (m == "userdata" && f == "write") {
        const std::string card = attr(r.attrs, "card");
        if (!safe_name(card)) return "<userdata status=\"1\"/>";
        write_file("userdata-" + card, "<userdata status=\"0\" time=\"" + esc(attr(r.attrs, "time")) + "\">" + r.inner + "</userdata>");
        return "<userdata status=\"0\"/>";
    }
    if (m == "userid") {   // KONAMI ID: a user and password, with cards (0x1863e8, 0x186620, 0x186818)
        std::vector<Record> users = from_lines(read_file("users"));
        const std::string card = attr(r.attrs, "card"), user = attr(r.attrs, "user"), pass = attr(r.attrs, "pass");
        auto owner = [&](const std::string &cd) -> Record * {
            for (Record &u : users) {
                const std::string cards = "," + attr(u, "cards") + ",";
                if (!cd.empty() && cards.find("," + cd + ",") != std::string::npos) return &u;
            }
            return nullptr;
        };
        auto named = [&](const std::string &n) -> Record * {
            for (Record &u : users)
                if (attr(u, "user") == n) return &u;
            return nullptr;
        };
        auto save = [&] {
            std::string t;
            for (const Record &u : users) t += to_line(u);
            write_file("users", t);
        };
        if (f == "ctou") {   // card to user: 112 = none
            const Record *u = owner(card);
            if (!u) return "<userid status=\"112\"/>";
            return "<userid status=\"0\" user=\"" + esc(attr(*u, "user")) + "\" active=\"" + esc(attr(*u, "active")) + "\"/>";
        }
        if (f == "adduser") {   // 113: the user exists, 114: the card is someone's
            if (user.empty() || named(user)) return "<userid status=\"113\"/>";
            if (owner(card)) return "<userid status=\"114\"/>";
            users.push_back({{"user", user}, {"pass", pass}, {"cards", card}, {"active", "1"}});
            save();
            return "<userid status=\"0\" alter=\"\"/>";
        }
        if (f == "addcard") {   // 115: no such user, 116: wrong password, 114: the card is someone's
            Record *u = named(user);
            if (!u) return "<userid status=\"115\"/>";
            if (attr(*u, "pass") != pass) return "<userid status=\"116\"/>";
            if (Record *o = owner(card); o && o != u) return "<userid status=\"114\"/>";
            if (!owner(card)) (*u)["cards"] = attr(*u, "cards").empty() ? card : attr(*u, "cards") + "," + card;
            save();
            return "<userid status=\"0\"/>";
        }
    }
    if (m == "rank") {
        if (f == "register") return "<rank status=\"0\">" + rank_register(BOARD_SCORE, r) + "</rank>";
        if (f == "register_h") return "<rank status=\"0\">" + rank_register(BOARD_H, r) + "</rank>";
        if (f == "register_gz") return "<rank status=\"0\">" + rank_register(BOARD_GZ, r) + "</rank>";
        if (f == "get") return "<rank status=\"0\">" + rank_get(BOARD_SCORE, r) + "</rank>";
        if (f == "get_h") return "<rank status=\"0\">" + rank_get(BOARD_H, r) + "</rank>";
        if (f == "get_gz") return "<rank status=\"0\">" + rank_get(BOARD_GZ, r) + "</rank>";
    }
    if (m == "eemall" && f == "i_get") {   // the card's ee'MALL items: eemall-<card>, one number a line
        const std::string card = attr(r.attrs, "card_id");
        std::string items;
        const std::string list = safe_name(card) ? read_file("eemall-" + card) : "";
        size_t at = 0, count = 0;
        while (at < list.size() && count < 99) {
            size_t e = list.find('\n', at);
            if (e == std::string::npos) e = list.size();
            const long n = std::strtol(list.c_str() + at, nullptr, 10);
            if (e > at && n > 0) { items += "<item num=\"" + std::to_string(n) + "\"/>"; count++; }
            at = e + 1;
        }
        return "<eemall status=\"0\">" + items + "</eemall>";
    }
    if (m == "services" && f == "get") {   // the other modules' URLs (0x18a808)
        // the names the methods look up (their entries' second word): rank, p_friend, eemall and
        // ntp all use "local"
        static const char *modules[] = {"posevent", "pcbtracker", "numbering", "package", "userdata", "userid", "local"};
        std::string items;
        for (const char *mod : modules) items += std::string("<item name=\"") + mod + "\" url=\"http://" + esc(host) + "/\"/>";
        return "<services expire=\"600\" status=\"0\">" + items + "</services>";
    }
    if (m == "ntp" && f == "get") {   // <time year.. sec../> (0x19b740)
        const time_t t = now ? (time_t)now() : 0;
        const struct tm tm = sys::utc(t);
        char b[160];
        snprintf(b, sizeof b, "<ntp status=\"0\"><time year=\"%d\" month=\"%d\" day=\"%d\" hour=\"%d\" min=\"%d\" sec=\"%d\"></time></ntp>",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        return b;
    }
    if (m == "posevent" && (f == "sale" || f == "sales"))   // approve is required (0x18af68)
        return "<posevent status=\"0\" approve=\"1\" margin=\"0\"/>";
    if (m == "package" && f == "list") {   // (0x197960) the one package pop'n 9 takes: "message"
        const std::string text = message();
        if (text.empty()) return "<package status=\"0\"/>";
        // the game downloads url, at most size - 1 bytes, and scrolls it on its title (0x1237c0)
        return "<package status=\"0\"><item url=\"http://" + esc(host) + "/message\" name=\"message\" desc=\"\" size=\"" +
               std::to_string(text.size() + 1) + "\" pkgtype=\"all\" sumtype=\"\" sum=\"\" from=\"\" till=\"\"/></package>";
    }
    // services this server keeps nothing for: pcbtracker alive, posevent income, package intend,
    // p_friend get/state/ask
    return "<" + m + " status=\"0\"/>";
}

// pop'n music 10 on (docs/EAMUSE3.md, "Later games"): game is the model's first three
// characters. "" leaves the call to pop'n 9's answers (services, numbering, userid, posevent, ...).
// pop'n 10-14's rankings, as rank.get's <b>. pop'n 10 (0x187be4 copies it to 0x18287c8,
// 0x1f30 bytes): 15 EXPERT courses of {count, 20 entries of 0x18: name (Shift-JIS, 13 bytes),
// place, prefecture, score, cool} (0x12b218 draws them; the score only with the game's score
// display on), then the shops by HOSHI NI NEGAIO wishes: {count, 20 entries of 0x24: shop name
// (25 bytes), place, prefecture, the wishes word (64-bit)} (0x13afa0). pop'n 11 (0x1932a8, 0x1f80
// bytes): the same courses (but the prefecture at 0x0d, the place at 0x0e), then the shops by
// POPN TOURIST finds: {count, 20 entries of 0x28: shop name (24 bytes), place, prefecture, the month (4-6), day, hour and minute it got there, the
// finds word, extra} (0x129240). pop'n 12 (0x1974a8, 0x1f30 bytes, at 0x181c360): pop'n 10's
// sizes, the prefecture before the place in both, and the shops' NINPOUCHO word at 0x20 (the
// scrolls drawn per bit). pop'n 13 (0x17eaed8, 0x22a4 bytes, at 0x160fb20): the same courses, then
// two shop boards (POP'N LAND's, POP'N WORLD's: 0x17eb010) of {count, 20 entries of 0x28: shop
// name (24 bytes), place, prefecture, stage, the area's words (low, high) at 0x20 and 0x24: the
// attractions built, drawn per bit}. pop'n 14 (0x17db420, 0x1894 bytes, at 0x15f28f8): 13 such
// courses, no shop board. Little-endian.
std::string EamuseServer::rank_table(const std::string &game) {
    const bool p11 = game == "D11", p13 = game == "E13", p14 = game == "F14", pref_first = game != "C20";
    std::string t(p11 ? 0x1f80 : p13 ? 0x22a4 : p14 ? 0x1894 : 0x1f30, '\0');
    auto put32 = [&](size_t at, uint32_t v) { for (int k = 0; k < 4; k++) t[at + k] = (char)(v >> (8 * k) & 255); };
    auto bytes = [](const std::string &v) {   // the requests send names as hex Shift-JIS
        std::string out;
        bool hex = !v.empty() && v.size() % 2 == 0;
        for (char c : v) hex = hex && std::isxdigit((unsigned char)c);
        if (!hex) return v;
        for (size_t i = 0; i + 1 < v.size(); i += 2) out += (char)std::strtoul(v.substr(i, 2).c_str(), nullptr, 16);
        return out;
    };
    auto name_at = [&](size_t at, const std::string &v, size_t room) {
        const std::string b = bytes(v);
        for (size_t i = 0; i + 1 < room && i < b.size(); i++) t[at + i] = b[i];
    };
    const Board b{nullptr, "card_id", EXP_KEEP, "score", "cool", 10};
    const std::vector<Record> all = from_lines(read_file(game + "-rank-exp"));
    for (int course = 0; course < (p14 ? 13 : 15); course++) {
        std::vector<Record> board;
        for (const Record &e : all)
            if (attr(e, "course") == std::to_string(course)) board.push_back(e);
        std::stable_sort(board.begin(), board.end(), [&](const Record &x, const Record &y) { return better(b, x, y); });
        const size_t base = (size_t)course * 0x1e4, n = std::min<size_t>(board.size(), 20);
        put32(base, (uint32_t)n);
        for (size_t i = 0; i < n; i++) {
            const size_t e = base + 4 + i * 0x18;
            name_at(e, attr(board[i], "name"), 13);
            t[e + (pref_first ? 0x0e : 0x0d)] = (char)(i + 1);   // pop'n 11 on have the prefecture first
            t[e + (pref_first ? 0x0d : 0x0e)] = (char)num(board[i], "area_num");
            put32(e + 0x10, (uint32_t)num(board[i], "score"));
            put32(e + 0x14, (uint32_t)num(board[i], "cool"));
        }
    }
    if (p14) {   // no shop board
        while (!t.empty() && t.back() == '\0') t.pop_back();
        return t;
    }
    std::vector<Record> shops = from_lines(read_file(game + "-rank-shop"));
    // the shops' event words, as sent: signed decimal ("%d"); pop'n 10's first records named them
    // praystar_lo/hi
    auto word = [](const Record &e, const char *k) {
        std::string v = attr(e, k);
        if (v.empty()) v = attr(e, std::string(k) == "mask" ? "praystar_lo" : "praystar_hi");
        return (uint32_t)std::strtoll(v.c_str(), nullptr, 10);
    };
    // pop'n 10's wishes are 64 bits; pop'n 11 counts its finds word only (0x12b250; extra is not
    // finds), pop'n 12 its NINPOUCHO word
    auto finds = [&](const Record &e) {
        return __builtin_popcountll(word(e, "mask") | (game == "C20" || p13 ? (uint64_t)word(e, "mask_hi") << 32 : 0));
    };
    std::stable_sort(shops.begin(), shops.end(), [&](const Record &x, const Record &y) {
        if (finds(x) != finds(y)) return finds(x) > finds(y);
        return attr(x, "when") < attr(y, "when");   // the earlier there first
    });
    // pop'n 13: POP'N LAND's board, then POP'N WORLD's (each shop's record names its board)
    const int boards = p13 ? 2 : 1;
    for (int bd = 0; bd < boards; bd++) {
        std::vector<Record> mine;
        for (const Record &e : shops)
            if (!p13 || num(e, "board") == bd) mine.push_back(e);
        const size_t size = p11 || p13 ? 0x28 : 0x24, base = 15 * 0x1e4 + (size_t)bd * (4 + 20 * size);
        const size_t n = std::min<size_t>(mine.size(), 20);
        put32(base, (uint32_t)n);
        for (size_t i = 0; i < n; i++) {
            const Record &shop = mine[i];
            const size_t e = base + 4 + i * size;
            if (p13) {
                name_at(e, attr(shop, "shop_name"), 0x18);
                t[e + 0x18] = (char)(i + 1);
                t[e + 0x19] = (char)num(shop, "area_num");
                t[e + 0x1a] = (char)num(shop, "stage");
                put32(e + 0x20, word(shop, "mask"));
                put32(e + 0x24, word(shop, "mask_hi"));
                continue;
            }
            name_at(e, attr(shop, "shop_name"), p11 ? 0x18 : 0x19);
            if (p11) {
                const std::string w = attr(shop, "when");   // MMDDhhmm
                t[e + 0x18] = (char)(i + 1);
                t[e + 0x19] = (char)num(shop, "area_num");
                for (int k = 0; k < 4 && w.size() == 8; k++) t[e + 0x1a + k] = (char)std::strtol(w.substr(2 * k, 2).c_str(), nullptr, 10);
                put32(e + 0x20, word(shop, "mask"));
                put32(e + 0x24, word(shop, "mask_hi"));
            } else {
                t[e + (pref_first ? 0x1a : 0x19)] = (char)(i + 1);
                t[e + (pref_first ? 0x19 : 0x1a)] = (char)num(shop, "area_num");
                if (game == "E12") {   // NINPOUCHO's word
                    put32(e + 0x20, word(shop, "mask"));
                } else {
                    put32(e + 0x1c, word(shop, "mask"));
                    put32(e + 0x20, word(shop, "mask_hi"));
                }
            }
        }
    }
    // the game clears its buffer before decoding (0x187be4): trailing zeros needn't be sent
    while (!t.empty() && t.back() == '\0') t.pop_back();
    return t;
}

// rank.get's answer: the table in <b> pieces of 768 bytes (the game's reader refuses one long
// text, and appends each <b> after the last: 0x19e300)
std::string EamuseServer::rank_b(const std::string &game) {
    const std::string t = rank_table(game);
    std::string out;
    for (size_t at = 0; at < t.size(); at += 768) out += "<b>" + base64(t.substr(at, 768)) + "</b>";
    return out;
}

// A shop's event report (pop'n 10 rank.shop praystar_lo/hi; pop'n 11 and 12 rank shop item/extra;
// pop'n 13 item_lo/item_hi and stage), kept per shop (and board) with when its finds last grew
// (the server's clock: MMDDhhmm, for pop'n 11's board)
void EamuseServer::rank_shop(const std::string &game, const Request &r, const char *mask, const char *mask_hi) {
    std::vector<Record> shops = from_lines(read_file(game + "-rank-shop"));
    const std::string name = attr(r.attrs, "shop_name");
    const time_t now_t = now ? (time_t)now() : 0;
    const struct tm tm = sys::utc(now_t);
    char when[16];
    snprintf(when, sizeof when, "%02d%02d%02d%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    const std::string area = attr(r.attrs, "area_num").empty() ? attr(r.attrs, "pref") : attr(r.attrs, "area_num");   // pop'n 12 on: pref
    Record mine{{"shop_name", name}, {"area_num", area}, {"mask", attr(r.attrs, mask)},
                {"mask_hi", attr(r.attrs, mask_hi)}, {"when", when}};
    if (r.attrs.count("stage")) mine["stage"] = attr(r.attrs, "stage");
    if (game == "E13") {   // the board of the shop's area (answer_game: game active); a shop has one per board
        mine["board"] = "0";
        for (const Record &e : from_lines(read_file(game + "-shop-phase")))
            if (attr(e, "shop_name") == name && num(e, "phase") == 2) mine["board"] = "1";
    }
    auto bits = [&](const Record &e) {
        return __builtin_popcountll((uint32_t)std::strtoll(attr(e, "mask").c_str(), nullptr, 10) |
                                    (game == "C20" || game == "E13" ? (uint64_t)(uint32_t)std::strtoll(attr(e, "mask_hi").c_str(), nullptr, 10) << 32 : 0));
    };
    bool found = false;
    for (Record &e : shops) {
        if (attr(e, "shop_name") != name || attr(e, "board") != attr(mine, "board")) continue;
        if (bits(mine) <= bits(e)) mine["when"] = attr(e, "when");   // no more found: the date stays
        e = mine;
        found = true;
    }
    if (!found) shops.push_back(mine);
    std::string out;
    for (const Record &e : shops) out += to_line(e);
    write_file(game + "-rank-shop", out);
}

std::string EamuseServer::next_number() {
    uint64_t next = 1;
    if (std::string n = read_file("numbering"); !n.empty()) next = std::strtoull(n.c_str(), nullptr, 10);
    write_file("numbering", std::to_string(next + 1) + "\n");
    char digits[17];
    snprintf(digits, sizeof digits, "200301%09llu", (unsigned long long)next);   // 15 digits
    int sum = 0;   // a Luhn check digit ("card16m10")
    for (int i = 14, k = 0; i >= 0; i--, k++) {
        int d = digits[i] - '0';
        if (k % 2 == 0) { d *= 2; if (d > 9) d -= 9; }
        sum += d;
    }
    digits[15] = (char)('0' + (10 - sum % 10) % 10);
    digits[16] = 0;
    return digits;
}

// GuitarFreaks and DrumMania V's e-AMUSEMENT (Konami Python 2; PLAN.md section 23, PY-6), from the
// game's eamservice module (in RAM: cardutil 0x94a348-0x94ae38):
//   - cardutil check <card no= id=/> per player: <card no state pass gdid skill/>; state 0 unknown
//     (not usable), 1 a card not registered (the game registers it; it keeps the gdid given), 2
//     registered (pass: its 4 digits, gdid: the player's 10 characters, skill), 3 expired
//     (0x1c483e4);
//   - cardutil regist <card cardid gdid kingid name pass type update/>, after the name and the
//     password (gdid: check's): <cardutil status/> and nothing in it (a child fails it, 0x94adbc);
//   - gameend regist, after a credit: <stage mode nr><stagedata no netid newmusic extra king clear/>..
//     </stage>, <player session king tower><playerdata card gdid styles perfect great good poor miss
//     time point><stage seqmode flags score spoint skill combo perc bscore/>..</playerdata></player>:
//     kept per player (gfdmv-GDID: each song and seqmode's best score, combo and percentage, its
//     plays; the player's totals);
//   - gametop get <player gdid/>: <player gdid styles pticket ticket> (0x94c990), in it musicdata
//     (base64, the game's own record of the player's songs, decoded into the player's 0xc30 bytes,
//     0x94c890), towerdata, kingdata. Answered without them so far.
// The players: the data folder's gfdmv-cards (cardid, gdid, then pass and name once registered);
// gdids are 10 digits from gfdmv-gdid.
std::string EamuseServer::answer_gfdmv(const std::string &game, const Request &r) {
    const std::string &m = r.module, &f = r.method;
    std::vector<Record> cards = from_lines(read_file("gfdmv-cards"));
    auto find = [&](const std::string &id) -> Record * {
        for (Record &c : cards)
            if (attr(c, "cardid") == id) return &c;
        return nullptr;
    };
    auto save = [&] {
        std::string t;
        for (const Record &e : cards) t += to_line(e);
        write_file("gfdmv-cards", t);
    };
    auto children = [&](const char *name) {
        std::vector<std::map<std::string, std::string>> out;
        const std::string tag = std::string("<") + name;
        for (size_t at = r.inner.find(tag); at != std::string::npos; at = r.inner.find(tag, at + 1)) {
            std::map<std::string, std::string> c;
            bool sc = false;
            read_attrs(r.inner, at + tag.size(), c, &sc);
            out.push_back(std::move(c));
        }
        return out;
    };
    if (m == "cardutil" && f == "check") {
        std::string out = "<cardutil status=\"0\">";
        bool added = false;
        for (const auto &c : children("card")) {
            const std::string id = attr(c, "id");
            Record *p = find(id);
            if (!p && safe_name(id)) {   // a new card: its player's gdid now (regist sends it back)
                uint64_t next = 1;
                if (std::string n = read_file("gfdmv-gdid"); !n.empty()) next = std::strtoull(n.c_str(), nullptr, 10);
                write_file("gfdmv-gdid", std::to_string(next + 1) + "\n");
                char gdid[16];
                snprintf(gdid, sizeof gdid, "%010llu", (unsigned long long)next);
                cards.push_back({{"cardid", id}, {"gdid", gdid}});
                p = &cards.back();
                added = true;
            }
            out += "<card no=\"" + esc(attr(c, "no")) + "\"";
            if (!p) out += " state=\"0\"";
            else if (attr(*p, "pass").empty()) out += " state=\"1\" gdid=\"" + esc(attr(*p, "gdid")) + "\"";
            else out += " state=\"2\" pass=\"" + esc(attr(*p, "pass")) + "\" gdid=\"" + esc(attr(*p, "gdid")) + "\" skill=\"" +
                        esc(attr(*p, "skill").empty() ? "0" : attr(*p, "skill")) + "\"";
            out += "/>";
        }
        if (added) save();
        return out + "</cardutil>";
    }
    if (m == "cardutil" && f == "regist") {
        for (const auto &c : children("card")) {
            Record *p = find(attr(c, "cardid"));
            if (!p) return "<cardutil status=\"1\"/>";
            (*p)["name"] = attr(c, "name");
            (*p)["pass"] = attr(c, "pass");
            if ((*p)["skill"].empty()) (*p)["skill"] = "0";
        }
        save();
        return "<cardutil status=\"0\"/>";
    }
    if (m == "gameend" && f == "regist") {
        // the songs, in the order the players' stages follow
        std::vector<std::string> netids;
        for (const auto &d : children("stagedata")) netids.push_back(attr(d, "netid"));
        for (size_t pd = r.inner.find("<playerdata"); pd != std::string::npos; pd = r.inner.find("<playerdata", pd + 1)) {
            std::map<std::string, std::string> pl;
            bool sc = false;
            read_attrs(r.inner, pd + 11, pl, &sc);
            const std::string gdid = attr(pl, "gdid");
            if (!safe_name(gdid)) continue;
            std::vector<Record> recs = from_lines(read_file("gfdmv-" + gdid));
            Record *total = nullptr;
            for (Record &e : recs)
                if (attr(e, "total") == "1") total = &e;
            if (!total) { recs.push_back({{"total", "1"}}); total = &recs.back(); }
            for (const char *k : {"perfect", "great", "good", "poor", "miss", "time", "point"})
                (*total)[k] = std::to_string(num(*total, k) + num(pl, k));
            (*total)["styles"] = attr(pl, "styles");
            (*total)["credits"] = std::to_string(num(*total, "credits") + 1);
            const size_t end = r.inner.find("</playerdata>", pd);
            size_t i = 0;
            for (size_t st = r.inner.find("<stage ", pd); st != std::string::npos && st < end; st = r.inner.find("<stage ", st + 1), i++) {
                std::map<std::string, std::string> sd;
                read_attrs(r.inner, st + 6, sd, &sc);
                if (i >= netids.size()) break;
                Record *best = nullptr;
                for (Record &e : recs)
                    if (attr(e, "netid") == netids[i] && attr(e, "seqmode") == attr(sd, "seqmode")) best = &e;
                if (!best) { recs.push_back({{"netid", netids[i]}, {"seqmode", attr(sd, "seqmode")}}); best = &recs.back(); }
                for (const char *k : {"score", "combo", "perc", "skill"})
                    if (num(sd, k) > num(*best, k) || attr(*best, k).empty()) (*best)[k] = attr(sd, k);
                (*best)["plays"] = std::to_string(num(*best, "plays") + 1);
            }
            std::string t;
            for (const Record &e : recs) t += to_line(e);
            write_file("gfdmv-" + gdid, t);
        }
        return "<gameend status=\"0\"/>";
    }
    if (m == "gametop" && f == "get") {
        std::string out = "<gametop status=\"0\">";
        for (const auto &pl : children("player")) {
            const std::string gdid = attr(pl, "gdid");
            std::string styles = "0";
            if (safe_name(gdid))
                for (const Record &e : from_lines(read_file("gfdmv-" + gdid)))
                    if (attr(e, "total") == "1" && !attr(e, "styles").empty()) styles = attr(e, "styles");
            out += "<player gdid=\"" + esc(gdid) + "\" styles=\"" + esc(styles) + "\" pticket=\"0\" ticket=\"0\"/>";
        }
        return out + "</gametop>";
    }
    (void)game;
    return "";
}

std::string EamuseServer::answer_later(const std::string &game, const Request &r, const std::string &host) {
    const std::string &m = r.module, &f = r.method;
    if (game == "E02" || game == "F02" || game == "E03" || game == "F03")
        if (std::string a = answer_gfdmv(game, r); !a.empty()) return a;
    if (m == "services" && f == "get") {   // as pop'n 9, and eemall/shopinf look up their own names
        // message: pop'n 13 fails its start-up without it (the library looks a call's URL up by its
        // module, 0x17d94c0: -10 when missing); cardmng, cardreis: pop'n 13's cards; facility:
        // pop'n 14's start-up (5-2009 without it)
        static const char *modules[] = {"posevent", "pcbtracker", "numbering", "package", "userdata", "userid", "local",
                                        "eemall", "shopinf", "message", "cardmng", "cardreis", "facility"};
        std::string items;
        for (const char *mod : modules) items += std::string("<item name=\"") + mod + "\" url=\"http://" + esc(host) + "/\"/>";
        // NET TAISEN (pop'n 12-14): portfw, where the world reaches the cabinet (a call's module must
        // be listed, or the library fails it at once: -10, 0x184ed0); globby, the lobby (0x199400),
        // UDP on this server's host (lobby.h)
        if (game == "E12" || game == "E13" || game == "F14")
            items += "<item name=\"portfw\" url=\"http://" + esc(host) + "/\"/>" +
                     "<item name=\"globby\" url=\"globby://" + esc(host.substr(0, host.find(':'))) + ":" + std::to_string(Lobby::PORT) + "/\"/>";
        // GuitarFreaks and DrumMania V (Python 2: E02 DrumMania V, F02 V2, E03/F03 GuitarFreaks): the
        // library's keepalive (ea3-keepalive.c) pings the router, center and server whose addresses
        // this item gives (ga/ma, pa/ia, the host), and ntp sets its clock; HIRAKU's network for
        // Python 2 (ethnet.h) answers both at 10.50.0.1
        if (game == "E02" || game == "F02" || game == "E03" || game == "F03")
            items += "<item name=\"ntp\" url=\"ntp://10.50.0.1/\"/><item name=\"keepalive\" url=\"http://10.50.0.1/keepalive?pa=10.50.0.1&amp;ia=10.50.0.1&amp;"
                     "ga=10.50.0.1&amp;ma=10.50.0.1&amp;t1=2&amp;t2=10\"/>";
        return "<services expire=\"600\" status=\"0\">" + items + "</services>";
    }
    if (m == "facility" && f == "get") {   // pop'n 14 (0x17d6860): the shop, as text elements in three groups
        return "<facility status=\"0\" expire=\"600\"><location><id>HIRAKU</id><country>JP</country>"
               "<region>13</region><line>0</line></location><portfw><globalip>192.168.50.2</globalip>"
               "<globalport>5700</globalport><privateport>5700</privateport></portfw><public><name>HIRAKU</name>"
               "<flag>0</flag></public></facility>";
    }
    if (m == "cardmng") {   // pop'n 13: a card has an id (written on it) and its player a ref id, in "cards"
        const std::string cardtype = attr(r.attrs, "cardtype");
        std::vector<Record> cards = from_lines(read_file("cards"));
        auto add = [&](const std::string &id) {   // a new card and its player
            uint64_t n = 1;
            if (std::string c = read_file("refid"); !c.empty()) n = std::strtoull(c.c_str(), nullptr, 10);
            write_file("refid", std::to_string(n + 1) + "\n");
            char ref[17];
            snprintf(ref, sizeof ref, "48524B%010llX", (unsigned long long)n);   // "HRK" and a count
            cards.push_back({{"cardid", id}, {"refid", ref}, {"cardtype", cardtype}});
            std::string out;
            for (const Record &c : cards) out += to_line(c);
            write_file("cards", out);
            return std::string(ref);
        };
        auto refid_of = [&](const std::string &id) {
            for (const Record &c : cards)
                if (attr(c, "cardid") == id) return attr(c, "refid");
            return std::string();
        };
        if (f == "getnewid") {   // cardtype newflag: a new card's id and ref id
            const std::string id = next_number();
            return "<cardmng status=\"0\" cardid=\"" + id + "\" refid=\"" + add(id) + "\"/>";
        }
        if (f == "inquire") {   // cardtype cardid update: the card's ref id; newflag 1 when the server didn't
            // know it. pop'n 14 also wants binded (the card has its player, 0x17d51b0) and expired.
            const std::string id = attr(r.attrs, "cardid");
            if (!safe_name(id)) return "<cardmng status=\"1\"/>";
            std::string ref = refid_of(id);
            const bool fresh = ref.empty();
            if (fresh) ref = add(id);
            return "<cardmng status=\"0\" refid=\"" + ref + "\" binded=\"" + (fresh ? "0" : "1") +
                   "\" expired=\"0\" newflag=\"" + (fresh ? "1" : "0") + "\"/>";
        }
        if (f == "gettmpid")   // cardtype oldid: a new id for an older card's player
            return "<cardmng status=\"0\" newid=\"" + next_number() + "\"/>";
        // pop'n 14's e-AMUSEMENT PASS: the PIN is the server's (the card holds only its id)
        auto save = [&] {
            std::string out;
            for (const Record &c : cards) out += to_line(c);
            write_file("cards", out);
        };
        auto card_of_ref = [&](const std::string &ref) -> Record * {
            for (Record &c : cards)
                if (attr(c, "refid") == ref) return &c;
            return nullptr;
        };
        if (f == "getrefid") {   // cardtype cardid newflag passwd: a card made a player, with its PIN
            const std::string id = attr(r.attrs, "cardid");
            if (!safe_name(id)) return "<cardmng status=\"1\"/>";
            std::string ref = refid_of(id);
            if (ref.empty()) ref = add(id);
            if (Record *c = card_of_ref(ref); c && attr(*c, "pass").empty()) {
                (*c)["pass"] = attr(r.attrs, "passwd");
                save();
            }
            return "<cardmng status=\"0\" refid=\"" + ref + "\"/>";
        }
        if (f == "authpass") {   // refid pass: 0, or 116 for a wrong PIN (0x17ac2c8). A card's first PIN is kept.
            Record *c = card_of_ref(attr(r.attrs, "refid"));
            if (!c) return "<cardmng status=\"112\"/>";
            if (attr(*c, "pass").empty()) {
                (*c)["pass"] = attr(r.attrs, "pass");
                save();
            }
            return attr(*c, "pass") == attr(r.attrs, "pass") ? "<cardmng status=\"0\"/>" : "<cardmng status=\"116\"/>";
        }
        // rebind (cardtype newid), bindmodel (refid newflag: the player starts on this game),
        // bindcard (cardtype newid refid): nothing kept
        if (f == "rebind" || f == "bindmodel" || f == "bindcard") return "<cardmng status=\"0\"/>";
    }
    if (m == "userdata") {   // as pop'n 9, kept per game: userdata-<game>-<card>
        std::string card = attr(r.attrs, "card");
        // an older game's card being carried over is read as CARD:MODEL, that game's data
        // (seen from pop'n 10: "E004...:C00"); pop'n 9's (C00) is userdata-<card>
        if (const size_t colon = card.find(':'); f == "read" && colon != std::string::npos) {
            const std::string from = card.substr(colon + 1, 3);
            card.resize(colon);
            if (!safe_name(card) || !safe_name(from)) return "<userdata status=\"1\"/>";
            const std::string data = read_file(from == "C00" ? "userdata-" + card : "userdata-" + from + "-" + card);
            return data.empty() ? "<userdata status=\"109\"/>" : data;
        }
        if (!safe_name(card)) return "<userdata status=\"1\"/>";
        if (f == "read") {
            std::string data = read_file("userdata-" + game + "-" + card);
            if (!data.empty()) return with_shared_marks(game, card, data);   // the card's marks from the other games put in
            // a card carried over from pop'n 9: what pop'n 9 saved (the game converts it)
            if (data.empty()) data = read_file("userdata-" + card);
            return data.empty() ? "<userdata status=\"109\"/>" : data;
        }
        if (f == "write") {
            write_file("userdata-" + game + "-" + card, "<userdata status=\"0\" time=\"" + esc(attr(r.attrs, "time")) + "\">" + r.inner + "</userdata>");
            return "<userdata status=\"0\"/>";
        }
    }
    if (m == "ntp" && f == "get") {   // one attribute: "YYYY-MM-DD hh:mm:ss" and the zone (pop'n 10 0x19da48)
        const time_t t = now ? (time_t)now() : 0;
        const struct tm tm = sys::utc(t);
        char b[96];
        snprintf(b, sizeof b, "<ntp status=\"0\" time=\"%04d-%02d-%02d %02d:%02d:%02d+9\"/>", tm.tm_year + 1900, tm.tm_mon + 1,
                 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        return b;
    }
    if (m == "rank") {
        if (f == "exp") {   // a course played: <rank update entry rank hi_score hi_cool/> (0x19dda8)
            const Board b{nullptr, "card_id", EXP_KEEP, "score", "cool", 10};
            const std::string file = game + "-rank-exp";
            Board named = b;
            named.file = file.c_str();
            const Entered e = enter(named, r);
            return "<rank status=\"0\" update=\"" + std::string(e.update ? "1" : "0") + "\" entry=\"" + std::to_string(e.entries) +
                   "\" rank=\"" + std::to_string(e.place) + "\" hi_score=\"" + esc(attr(e.kept, "score")) + "\" hi_cool=\"" +
                   esc(attr(e.kept, "cool")) + "\"/>";
        }
        // the rankings as a <b> block the game decodes (0x19e300): pop'n 10's table (rank_table)
        if (f == "get") return game == "C20" ? "<rank status=\"0\">" + rank_b(game) + "</rank>" : "<rank status=\"0\"/>";
        // the server's ranking course (0x19e420); 117: none (the game clears it, 0x187f2c)
        if (f == "course") return "<rank status=\"117\"/>";
        if (f == "shop") {   // praystar_lo/hi: the shop's HOSHI NI NEGAIO wishes (pop'n 10)
            rank_shop(game, r, "praystar_lo", "praystar_hi");
            return "<rank status=\"0\"/>";
        }
    }
    if (m == "message" && f == "get") {   // (0x19f4e0) phase: the event phase, set online when >= 0
        // (0x18dee0); -1 leaves the cabinet's own. text: the owner's message.
        const std::string ph = read_file("phase-" + game);
        const long phase = ph.empty() ? -1 : std::strtol(ph.c_str(), nullptr, 10);
        return "<message status=\"0\" phase=\"" + std::to_string(phase) + "\" text=\"" + esc(message()) + "\"/>";
    }
    if (m == "eemall" && f == "get") {   // (0x19eda8) points, then <item num/> and <kisekae path pri/>
        const std::string card = attr(r.attrs, "card_id");
        std::string items;
        const std::string list = safe_name(card) ? read_file("eemall-" + card) : "";
        size_t at = 0, count = 0;
        while (at < list.size() && count < 99) {
            size_t e = list.find('\n', at);
            if (e == std::string::npos) e = list.size();
            const long n = std::strtol(list.c_str() + at, nullptr, 10);
            if (e > at && n > 0) { items += "<item num=\"" + std::to_string(n) + "\"/>"; count++; }
            at = e + 1;
        }
        return "<eemall status=\"0\" now_point=\"0\" add_point=\"0\">" + items + "</eemall>";
    }
    if (m == "shopinf" && f == "getsservip") return "<shopinf status=\"0\"/>";   // no shop servers (0x19f2a8)
    if (m == "eemall" && f == "check") return "<eemall status=\"0\"/>";   // pop'n 11 (0x18c098)
    if (m == "portfw" && f == "get") {   // pop'n 11: no port forward (0x18c4c8)
        // pop'n 12-14: where the world reaches the cabinet's NET TAISEN (0x190520): on HIRAKU's LAN,
        // no router between, its own address, and the port its battle socket listens on (5700)
        if (game == "E12" || game == "E13" || game == "F14") {
            const std::string ip = attr(r.attrs, "privateip");
            return "<portfw status=\"0\" globalip=\"" + esc(ip.empty() ? "192.168.50.2" : ip) + "\" port=\"5700\"/>";
        }
        return "<portfw status=\"0\"/>";
    }
    if (m == game) return answer_game(game, r);   // pop'n 11 on: <D11 type=.. method=..>
    return "";
}

// pop'n 11 on: the module named after the game, a type and a method (pop'n 11's parsers
// 0x18c858-0x18ddf8). The answer is that element again.
std::string EamuseServer::answer_game(const std::string &game, const Request &r) {
    const std::string type = attr(r.attrs, "type"), &f = r.method;
    const std::string open = "<" + game + " status=\"0\"";
    const bool ranked = game == "D11" || game == "E12" || game == "E13" || game == "F14";   // the games whose table is worked out
    if (type == "rank" && ranked && f == "get") return open + ">" + rank_b(game) + "</" + game + ">";
    // item: the shop's POPN TOURIST finds (pop'n 11, with extra) or NINPOUCHO word (pop'n 12);
    // item_lo/item_hi: POP'N LAND's or WORLD's (pop'n 13)
    if (type == "rank" && ranked && f == "shop") {
        if (game == "E13") rank_shop(game, r, "item_lo", "item_hi");
        else if (game == "F14") rank_shop(game, r, "item", "robo");   // kept; pop'n 14 shows no shop board
        else rank_shop(game, r, "item", "extra");
        return open + "/>";
    }
    // pop'n 13: a shop's phase (game active, at a credit's start and end), so its rank shop goes
    // on that area's board (phase 1 POP'N LAND, 2 POP'N WORLD)
    if (game == "E13" && type == "game" && f == "active" && r.attrs.count("phase")) {
        std::vector<Record> shops = from_lines(read_file(game + "-shop-phase"));
        bool found = false;
        for (Record &e : shops)
            if (attr(e, "shop_name") == attr(r.attrs, "shop_name")) { e["phase"] = attr(r.attrs, "phase"); found = true; }
        if (!found) shops.push_back({{"shop_name", attr(r.attrs, "shop_name")}, {"phase", attr(r.attrs, "phase")}});
        std::string out;
        for (const Record &e : shops) out += to_line(e);
        write_file(game + "-shop-phase", out);
        return open + "/>";
    }
    if (type == "rank" && f == "exp") {   // as pop'n 10's rank.exp, on the game's element
        const std::string file = game + "-rank-exp";
        Board b{file.c_str(), "card_id", EXP_KEEP, "score", "cool", 10};
        const Entered e = enter(b, r);
        return open + " update=\"" + std::string(e.update ? "1" : "0") + "\" entry=\"" + std::to_string(e.entries) + "\" rank=\"" +
               std::to_string(e.place) + "\" hi_score=\"" + esc(attr(e.kept, "score")) + "\" hi_cool=\"" + esc(attr(e.kept, "cool")) + "\"/>";
    }
    if (type == "game" && f == "get") {   // phase: as pop'n 10's message.get (-1: the cabinet's own)
        const std::string ph = read_file("phase-" + game);
        const long phase = ph.empty() ? -1 : std::strtol(ph.c_str(), nullptr, 10);
        // pop'n 13 and 14 also read net_event (14's 0x17e9308), the network event running: 1 the
        // INTERNET RANKING (14's 0x17fdc78: the attract mode's course boards, the courses' results
        // and high scores online; every use asks for exactly 1), 2 NET TAISEN, one or the other.
        // 2 unless the data folder's net_event-GAME says otherwise
        std::string flags;
        // NET TAISEN on (docs/EAMUSE3.md): pop'n 12 by play_enable (0x191160); pop'n 13 and 14 by
        // net_event 2 (13's 0x17edd48, 14's 0x17ddef8). Cabinets of
        // one shop may meet (local_matching_enable), seconds to look for opponents, and every class
        // in room 0 so anyone meets anyone (room_num, 12 and 13: ten rooms; the mask 1 << room goes
        // to the lobby). ojama_msec, ojama_gauge(_rate), cpu_level and 14's others are left out:
        // the game's own (pop'n 12: an ojama 1000 ms at least, 0x126f38)
        if (game == "E12") flags += " play_enable=\"1\"";
        if (game == "E13" || game == "F14") {
            const std::string ne = read_file("net_event-" + game);
            flags += " net_event=\"" + std::to_string(ne.empty() ? 2 : std::strtol(ne.c_str(), nullptr, 10)) + "\"";
        }
        if (game == "E12" || game == "E13" || game == "F14") flags += " local_matching_enable=\"1\" matching_sec=\"30\"";
        if (game == "E12" || game == "E13") flags += " room_num=\"0,0,0,0,0,0,0,0,0,0\"";
        return open + " phase=\"" + std::to_string(phase) + "\"" + flags + " message=\"" + esc(message()) + "\"/>";
    }
    if (type == "player") {   // mileage and 9 tickets per card (player_set sends them); course: 4 numbers
        const std::string card = attr(r.attrs, "card_id");
        if (!safe_name(card)) return "<" + game + " status=\"1\"/>";
        const std::string file = "player-" + game + "-" + card;
        if (f == "set") {
            write_file(file, to_line({{"mileage", attr(r.attrs, "mileage")}, {"ticket", attr(r.attrs, "ticket")}}));
            return open + "/>";
        }
        if (f == "get") {
            const std::vector<Record> p = from_lines(read_file(file));
            std::string ticket = p.empty() ? "" : attr(p[0], "ticket");
            while (!ticket.empty() && ticket.back() == ',') ticket.pop_back();
            if (ticket.empty()) ticket = "0,0,0,0,0,0,0,0,0";
            return open + " mileage=\"" + esc(p.empty() || attr(p[0], "mileage").empty() ? "0" : attr(p[0], "mileage")) +
                   "\" ticket=\"" + esc(ticket) + "\" course=\"0,0,0,0\"/>";
        }
    }
    if (type == "playerdata") return answer_playerdata(game, r);
    // rank get (an empty table), rank shop, pf get/ask, game select, renewc reg/ask, game active:
    // nothing kept
    return open + "/>";
}

// pop'n 12's player data (docs/EAMUSE3.md): get and new answer a binary record of 0x708 bytes as
// base64 in <b> (0x1917f8; decoded at 0x18f860 into 0x1867e68); set sends this credit's changes as
// attributes and children (0x1919e8), merged into the record here. The record, as the game fills
// and reads it: 0x00 the name (Shift-JIS, 13 bytes), 0x1b the puzzle position, 0x1c-0x33 six puzzle
// pieces (32-bit), 0x34 the password (four digits, a byte each), 0x38 the option, 0x3a the
// character (16-bit), from 0x3c a 16-bit word per song (2-bit clear marks per chart in bits 0-7,
// flags in 0x100-0x800: 0x1b3958), from about 0x5f0 the network battle's. Scores are kept apart
// (playerdata score asks one chart's).
// One card for all: shared clear marks (docs/EAMUSE3.md, "One card for all"). All six games number
// their songs alike (checked by their song tables), and every chart's clear mark is two bits, 1 a
// clear and 2-3 the better medals, in pop'n 12's order: song word bits 0-1, 2-3, 4-5, 6-7 for
// the four charts (12, 13, 14: their words' low byte; pop'n 13 shows a 12 record so, 0x1812f98).
// pop'n 10 and 11 keep the last two charts the other way round (11 -> 12 swaps them, 0x1a0a38).
// pop'n 9 keeps only a cleared bit per chart, byte 0x416 + song, bits 0-3 in pop'n 12's order
// (pop'n 10 makes them its flags 0x100, 0x200, 0x800, 0x400 and pop'n 11 those marks of 1). The
// marks travel; each game's own bits (flags, scores) stay its own.
namespace {

// the songs each game has (its song table's entries but the "-" ones left in place of songs taken
// out; pop'n 14 renamed some genres and comments, the songs the same)
struct SongRanges { const char *game; std::vector<std::pair<int, int>> ranges; };
const SongRanges SONGS[] = {
    {"C00", {{0, 251}, {253, 256}, {258, 324}}},
    {"C20", {{0, 389}}},
    {"D11", {{0, 427}, {429, 449}}},
    {"E12", {{0, 243}, {247, 251}, {253, 253}, {255, 256}, {258, 259}, {261, 261}, {264, 327}, {330, 336}, {339, 391}, {393, 427}, {429, 594}}},
    {"E13", {{0, 243}, {247, 251}, {253, 253}, {255, 256}, {258, 259}, {261, 261}, {264, 327}, {330, 336}, {339, 391}, {393, 427}, {429, 648}}},
    {"F14", {{0, 243}, {247, 251}, {253, 253}, {255, 256}, {258, 259}, {261, 261}, {264, 327}, {330, 336}, {339, 391}, {393, 427}, {429, 699}}},
};

bool has_song(const std::string &game, size_t n) {
    for (const SongRanges &g : SONGS)
        if (game == g.game) {
            for (const auto &[a, b] : g.ranges)
                if ((int)n >= a && (int)n <= b) return true;
            return false;
        }
    return false;
}

constexpr size_t SONG_MAX = 1024;

uint8_t swap34(uint8_t m) { return (uint8_t)((m & 0x0f) | (m & 0x30) << 2 | (m & 0xc0) >> 2); }

uint8_t best(uint8_t a, uint8_t b) {   // per chart, the better mark
    uint8_t o = 0;
    for (int c = 0; c < 8; c += 2) o |= (uint8_t)(std::max(a >> c & 3, b >> c & 3) << c);
    return o;
}

// where a game keeps its song words in its player data: offset, bytes a word, and whether the
// data is a userdata blob (pop'n 9-11) or a record (12-14)
struct Words { size_t at, bytes; };
Words words_of(const std::string &game) {
    if (game == "C20") return {0x3fc, 2};
    if (game == "D11") return {0x410, 2};
    if (game == "E12") return {0x3c, 2};
    if (game == "E13") return {0x28, 4};
    if (game == "F14") return {0x24, 4};
    return {0x416, 1};   // C00: a byte a song
}

// a song's marks as the game keeps them, in pop'n 12's order
uint8_t marks_in(const std::string &game, const std::string &d, size_t n) {
    const Words w = words_of(game);
    const size_t at = w.at + w.bytes * n;
    if (at + w.bytes > d.size()) return 0;
    const unsigned lo = (uint8_t)d[at], hi = w.bytes > 1 ? (uint8_t)d[at + 1] : 0;
    if (game == "C00") {   // cleared bits
        uint8_t m = 0;
        for (int c = 0; c < 4; c++) if (lo >> c & 1) m |= (uint8_t)(1 << 2 * c);
        return m;
    }
    if (game == "C20") {   // marks (11's order) and the cleared flags of a pop'n 9 player
        uint8_t m = swap34((uint8_t)lo);
        static const int flag_chart[4] = {0, 1, 3, 2};   // 0x100, 0x200, 0x400, 0x800
        for (int b = 0; b < 4; b++)
            if (hi >> b & 1) m = best(m, (uint8_t)(1 << 2 * flag_chart[b]));
        return m;
    }
    if (game == "D11") return swap34((uint8_t)lo);
    return (uint8_t)lo;
}

// a song's marks put into the game's data: each chart the better of the two
void marks_out(const std::string &game, std::string &d, size_t n, uint8_t m) {
    const Words w = words_of(game);
    const size_t at = w.at + w.bytes * n;
    if (at + w.bytes > d.size() || !m) return;
    if (game == "C00") {
        uint8_t b = (uint8_t)d[at];
        if (b == 0xff) b = 0;   // never played
        for (int c = 0; c < 4; c++) if (m >> 2 * c & 3) b |= (uint8_t)(1 << c);
        d[at] = (char)b;
        return;
    }
    const uint8_t mine = game == "C20" || game == "D11" ? swap34(m) : m;
    d[at] = (char)best((uint8_t)d[at], mine);
}

}  // namespace

std::vector<uint8_t> EamuseServer::shared_marks(const std::string &card) const {
    std::vector<uint8_t> marks(SONG_MAX, 0);
    if (!safe_name(card)) return marks;
    std::string ref;   // pop'n 13 and 14 keep their players by ref id
    for (const Record &c : from_lines(read_file("cards")))
        if (attr(c, "cardid") == card) ref = attr(c, "refid");
    struct Source { const char *game; std::string data; };
    const Source sources[] = {
        {"C00", unbase64_b(read_file("userdata-" + card))},
        {"C20", unbase64_b(read_file("userdata-C20-" + card))},
        {"D11", unbase64_b(read_file("userdata-D11-" + card))},
        {"E12", read_file("pdata-E12-" + card)},
        {"E13", safe_name(ref) ? read_file("pdata-E13-" + ref) : ""},
        {"F14", safe_name(ref) ? read_file("pdata-F14-" + ref) : ""},
    };
    for (const Source &s : sources)
        for (size_t n = 0; n < SONG_MAX && !s.data.empty(); n++)
            if (has_song(s.game, n)) marks[n] = best(marks[n], marks_in(s.game, s.data, n));
    return marks;
}

// data: the game's own player data (a userdata answer with <b> pieces for pop'n 9-11, the raw
// record for 12-14), returned with the card's shared marks put in
std::string EamuseServer::with_shared_marks(const std::string &game, const std::string &card, std::string data) const {
    const std::vector<uint8_t> marks = shared_marks(card);
    const bool blob = game == "C00" || game == "C20" || game == "D11";
    std::string d = blob ? unbase64_b(data) : data;
    for (size_t n = 0; n < SONG_MAX; n++)
        if (has_song(game, n)) marks_out(game, d, n, marks[n]);
    if (!blob) return d;
    // back into the same <b> pieces
    std::string out;
    size_t at = 0, from = 0;
    for (size_t b = data.find("<b>"); b != std::string::npos; b = data.find("<b>", b + 3)) {
        const size_t e = data.find("</b>", b);
        if (e == std::string::npos) break;
        const size_t n = unbase64_b(data.substr(b, e + 4 - b)).size();
        out += data.substr(from, b + 3 - from) + base64(d.substr(at, n));
        at += n;
        from = e;
    }
    return out + data.substr(from);
}

// Scores (docs/EAMUSE3.md, "Shared scores"): pop'n 12, 13 and 14 number their charts alike, the
// sheet sent being the chart (0x1b3958, 0x1810e60, 0x17fdfa0: the same code in all three): 0-3
// Normal, Hyper, 5 buttons, EX (the song word's two-bit places), 5 one mode's; 13 and 14 play
// their own 5-button charts as 6 (GRADIUS: 167 notes, 12's sheet 2 313), and have a 7. pop'n
// 9-11 keep no scores (their players, 1.6-2.6 KB, have room for the marks only). So a game
// asking a chart's best gets its own or another's of the card, same song and sheet, for songs
// both games have.
long EamuseServer::shared_score(const std::string &game, const std::string &key, const std::string &song, const std::string &sheet) const {
    std::string card = game == "E12" ? key : "", ref = game == "E12" ? "" : key;
    for (const Record &c : from_lines(read_file("cards"))) {
        if (game == "E12" && attr(c, "cardid") == key) ref = attr(c, "refid");
        if (game != "E12" && attr(c, "refid") == key) card = attr(c, "cardid");
    }
    const long n = std::strtol(song.c_str(), nullptr, 10);
    // the songs pop'n 14 gave new ids (and some new levels): its charts of them its own
    static const std::set<long> F14_ANEW = {450, 451, 452, 455, 457, 460, 461, 462, 465, 466, 469, 477, 478, 481, 484, 485, 487, 492};
    long best = 0;
    for (const char *g : {"E12", "E13", "F14"}) {
        const std::string &who = std::string(g) == "E12" ? card : ref;
        const bool other = game != g, f14 = game == "F14" || std::string(g) == "F14";
        if (!safe_name(who) || (other && (!has_song(game, (size_t)n) || !has_song(g, (size_t)n) || (f14 && F14_ANEW.count(n))))) continue;
        for (const Record &e : from_lines(read_file(std::string("pscore-") + g + "-" + who)))
            if (attr(e, "num") == song && attr(e, "sheet") == sheet) best = std::max(best, num(e, "score"));
    }
    return best;
}

std::string EamuseServer::answer_playerdata(const std::string &game, const Request &r) {
    // each game's record (docs/EAMUSE3.md): its size, the password's, option's and character's
    // places, and the songs' clear words (16 bits each in pop'n 12, 32 in pop'n 13)
    struct Layout { size_t record, password, option, chara, songs, song_bytes, songs_end; };
    // pop'n 14 has no password in it (cardmng.authpass checks it on the server): NONE
    constexpr size_t NONE = ~(size_t)0;
    const bool p13 = game == "E13", p14 = game == "F14";
    const Layout L = p14 ? Layout{0xdb8, NONE, 0x1c, 0x1e, 0x24, 4, 0xdb8}     // 0x16506b8, 0x17fdfa0
                   : p13 ? Layout{0xd6c, 0x1c, 0x20, 0x22, 0x28, 4, 0xd6c}     // 0x166daa8, 0x1810e60
                         : Layout{0x708, 0x34, 0x38, 0x3a, 0x3c, 2, 0x5f0};
    const size_t RECORD = L.record;
    const std::string &f = r.method;
    const std::string open = "<" + game + " status=\"0\"";
    // the player's key: pop'n 13's ref id (cardmng), else pop'n 12's card
    std::string card = attr(r.attrs, "ref_id");
    if (card.empty()) card = attr(r.attrs, "card_id");
    if (card.empty()) card = attr(r.attrs, "new_card_id");
    if (!safe_name(card)) return "<" + game + " status=\"1\"/>";
    const std::string file = "pdata-" + game + "-" + card, scores_file = "pscore-" + game + "-" + card;
    std::string rec = read_file(file);
    auto put16 = [&](size_t at, long v) { if (at + 2 <= rec.size()) { rec[at] = (char)(v & 255); rec[at + 1] = (char)(v >> 8 & 255); } };
    auto put32 = [&](size_t at, long v) { put16(at, v & 0xffff); put16(at + 2, v >> 16 & 0xffff); };
    auto set_name = [&](const std::string &name) {
        for (size_t i = 0; i < 13; i++) rec[i] = i < name.size() && i < 12 ? name[i] : 0;
    };
    auto set_password = [&](const std::string &pw) {
        if (L.password == NONE) return;
        for (size_t i = 0; i < 4; i++) rec[L.password + i] = i < pw.size() && std::isdigit((unsigned char)pw[i]) ? (char)(pw[i] - '0') : 0;
    };
    // pop'n 13 and 14: f_compress (0x17f83c0), the record compressed (lzss)
    auto with_record = [&] {
        return p13 || p14 ? open + " f_compress=\"1\"><b>" + base64(lzss(rec)) + "</b></" + game + ">"
                   : open + "><b>" + base64(rec) + "</b></" + game + ">";
    };

    if (f == "new") {   // a new card: its record (name, password; the rest zero)
        rec.assign(RECORD, 0);
        set_name(attr(r.attrs, "name"));
        set_password(attr(r.attrs, "password"));
        write_file(file, rec);
        return with_record();
    }
    if (f == "get") {   // 109: no player on this card (0x164be8)
        // an older game's card being carried over (docs/EAMUSE3.md, "One card for all"): get with
        // that game's model answers its player as that game saved it, in its <b> pieces (pop'n 12
        // appends them, 0x191910, and converts the 0xa28 bytes of a pop'n 11 player, 0x1a0a38)
        if (const std::string from = attr(r.attrs, "model"); !from.empty() && from != game) {
            if (!safe_name(from)) return "<" + game + " status=\"1\"/>";
            // pop'n 12's player is its record (pdata-E12-CARD), keyed by the card: pop'n 13 takes it
            // uncompressed (f_compress 0) and converts it (0x17e9dd8)
            // pop'n 13's player for pop'n 14 (get ref_id=<the typed POP'N ID> model="E13", 0xd6c
            // compressed as get's, which the game converts): that ref id's, or (one card for all)
            // the player of the card the core put in card_id
            if (from == "E13") {
                std::string old = safe_name(card) ? read_file("pdata-E13-" + card) : "";
                if (old.size() != 0xd6c)
                    for (const Record &c : from_lines(read_file("cards")))
                        if (attr(c, "cardid") == attr(r.attrs, "card_id") && safe_name(attr(c, "refid"))) old = read_file("pdata-E13-" + attr(c, "refid"));
                if (old.size() != 0xd6c) return "<" + game + " status=\"109\"/>";
                return open + " f_compress=\"1\"><b>" + base64(lzss(old)) + "</b></" + game + ">";
            }
            if (from == "E12") {
                const std::string id = attr(r.attrs, "card_id");
                const std::string old = safe_name(id) ? read_file("pdata-E12-" + id) : "";
                if (old.size() != 0x708) return "<" + game + " status=\"109\"/>";
                return open + " f_compress=\"0\"><b>" + base64(old) + "</b></" + game + ">";
            }
            const std::string data = read_file(from == "C00" ? "userdata-" + card : "userdata-" + from + "-" + card);
            const size_t b = data.find("<b>"), e = data.rfind("</b>");
            if (b == std::string::npos || e == std::string::npos) return "<" + game + " status=\"109\"/>";
            return open + ">" + data.substr(b, e + 4 - b) + "</" + game + ">";
        }
        if (rec.size() != RECORD) return "<" + game + " status=\"109\"/>";
        // the card's clear marks from the other pop'n games put in (pop'n 13 and 14 key the player by
        // ref id; the card is card_id, or the ref id's in the cards)
        std::string number = attr(r.attrs, "card_id");
        if (!safe_name(number) || number == "-")
            for (const Record &c : from_lines(read_file("cards")))
                if (attr(c, "refid") == card) number = attr(c, "cardid");
        if (safe_name(number)) rec = with_shared_marks(game, number, rec);
        return with_record();
    }
    if (f == "set") {
        if (rec.size() != RECORD) rec.assign(RECORD, 0);
        if (r.attrs.count("name")) set_name(attr(r.attrs, "name"));
        if (r.attrs.count("password")) set_password(attr(r.attrs, "password"));
        if (r.attrs.count("option")) put16(L.option, num(r.attrs, "option"));
        if (r.attrs.count("chara_num")) put16(L.chara, num(r.attrs, "chara_num"));
        if (!p13 && !p14 && r.attrs.count("puzzle_now")) rec[0x1b] = (char)num(r.attrs, "puzzle_now");
        if (!p13 && !p14 && r.attrs.count("puzzle_piece")) {
            const std::string pp = attr(r.attrs, "puzzle_piece");
            size_t at = 0;
            for (int k = 0; k < 6 && at <= pp.size(); k++) {
                put32(0x1c + 4 * (size_t)k, std::strtol(pp.c_str() + at, nullptr, 10));
                const size_t comma = pp.find(',', at);
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
        }
        // the songs played: their clear word into the record, the score kept if better
        std::vector<Record> scores = from_lines(read_file(scores_file));
        for (size_t at = r.inner.find("<music"); at != std::string::npos; at = r.inner.find("<music", at + 1)) {
            std::map<std::string, std::string> m;
            bool sc = false;
            read_attrs(r.inner, at + 6, m, &sc);
            const long n = num(m, "num");
            if (n < 0) continue;
            const size_t word = L.songs + L.song_bytes * (size_t)n;
            if (word + L.song_bytes <= L.songs_end) {
                if (L.song_bytes == 4) put32(word, num(m, "data"));
                else put16(word, num(m, "data"));
            }
            const std::string sheet = attr(m, "sheet");
            bool found = false;
            for (Record &e : scores)
                if (attr(e, "num") == attr(m, "num") && attr(e, "sheet") == sheet) {
                    found = true;
                    if (num(m, "score") > num(e, "score")) e["score"] = attr(m, "score");
                }
            if (!found) scores.push_back({{"num", attr(m, "num")}, {"sheet", sheet}, {"score", attr(m, "score")}});
        }
        std::string t;
        for (const Record &e : scores) t += to_line(e);
        if (!scores.empty()) write_file(scores_file, t);
        write_file(file, rec);
        return open + "/>";
    }
    if (f == "score")   // one chart's best: <E12 score/> (0x192a00), the card's in pop'n 12-14
        return open + " score=\"" + std::to_string(shared_score(game, card, attr(r.attrs, "music_num"), attr(r.attrs, "sheet_num"))) + "\"/>";
    // conv (old_card_id new_card_id name ...): a player carried over from the game before
    // (docs/EAMUSE3.md, "One card for all"). The game converts the older record it was given (get
    // with that game's model) for itself and keeps it only in memory; its credit's set sends only
    // the songs played. So the record is built here as the game builds it: pop'n 12 from pop'n 11's
    // 0xa28 bytes, whose 600 song words at 0x410 number the songs as pop'n 12 does, each word's bits
    // 4-5 and 6-7 swapped and its high byte dropped (0x1a0a38, checked: the game's own result in
    // RAM is the same); the name as sent.
    if (f == "conv" && game == "E12") {
        const std::string old = attr(r.attrs, "old_card_id");
        if (!safe_name(old)) return "<" + game + " status=\"1\"/>";
        const std::string from = unbase64_b(read_file("userdata-D11-" + old));
        if (rec.size() != RECORD) rec.assign(RECORD, 0);
        set_name(attr(r.attrs, "name"));
        // the PIN, a digit a byte at 0x9f3 (the name follows at 0x9f7): the credit's set sends it
        // too, but a later game asks for it even if this credit never ends properly
        if (from.size() >= 0x9f7)
            for (size_t i = 0; i < 4; i++) rec[L.password + i] = (char)((uint8_t)from[0x9f3 + i] % 10);
        for (size_t n = 0; n < 600 && 0x410 + 2 * n + 2 <= from.size(); n++) {
            const unsigned w = (uint8_t)from[0x410 + 2 * n] | (uint8_t)from[0x410 + 2 * n + 1] << 8;
            put16(L.songs + 2 * n, (w & 0xf) | (w & 0x30) << 2 | (w & 0xc0) >> 2);   // the high byte dropped, as there
        }
        write_file(file, rec);
        return open + "/>";
    }
    // pop'n 13 from pop'n 12's record (pdata-E12-<old card>): pop'n 13 asks only conv with the
    // player's new ref id and keeps nothing itself, so the record is the server's to make. The songs
    // are numbered alike (its display of the older record, 0x1812f98, looks songs up by the same
    // index), and both keep a chart's clear mark in two bits, the four charts at bits 0-1, 2-3, 4-5,
    // 6-7 (pop'n 13's own display, 0x1812c4c): the low byte carries over, 32 bits a song from 0x28.
    // pop'n 12's flags above it don't (their meaning in 13 isn't known). The PIN and the name too.
    if (f == "conv" && game == "E13") {
        const std::string old = attr(r.attrs, "old_card_id");
        if (!safe_name(old)) return "<" + game + " status=\"1\"/>";
        const std::string from = read_file("pdata-E12-" + old);
        if (rec.size() != RECORD) rec.assign(RECORD, 0);
        set_name(attr(r.attrs, "name"));
        if (from.size() == 0x708) {
            for (size_t i = 0; i < 4; i++) rec[L.password + i] = (char)((uint8_t)from[0x34 + i] % 10);
            for (size_t n = 0; 0x3c + 2 * n + 2 <= 0x5f0 && L.songs + 4 * n + 4 <= L.songs_end; n++)
                put32(L.songs + 4 * n, (uint8_t)from[0x3c + 2 * n]);
        }
        write_file(file, rec);
        return open + "/>";
    }
    // pop'n 14: conv (name ref_id card_id popn_id ea_passwd) after the player chose "カーニバルから
    // データを引きつぐ" and typed pop'n 13's transfer number (POP'N ID). The game takes the answer's
    // record as its own, byte for byte (checked in RAM). pop'n 13's record as it is would put song n
    // at pop'n 14's n+1 (14's song words start at 0x24, 13's at 0x28: 0x17fdfa0, 0x17a0170; both
    // number songs alike, by their song tables and the songs played) and 13's PIN where 14 keeps its
    // option and character; so the answer is the record converted to pop'n 14's layout: the name,
    // the option and character (13's 0x20, 0x22 to 0x1c, 0x1e), and each song's word. The server
    // keeps it as pop'n 14's record (the credit's set sends only the songs played). Transfer numbers
    // aren't issued here; with one card for all, the card's ref id is pop'n 13's too.
    if (f == "conv" && p14) {
        const std::string old = read_file("pdata-E13-" + card);
        if (old.size() != 0xd6c) return "<" + game + " status=\"109\"/>";
        rec.assign(RECORD, 0);
        set_name(attr(r.attrs, "name"));
        put16(L.option, (uint8_t)old[0x20] | (uint8_t)old[0x21] << 8);
        put16(L.chara, (uint8_t)old[0x22] | (uint8_t)old[0x23] << 8);
        for (size_t n = 0; 0x28 + 4 * n + 4 <= old.size() && L.songs + 4 * n + 4 <= L.songs_end; n++)
            for (size_t b = 0; b < 4; b++) rec[L.songs + 4 * n + b] = old[0x28 + 4 * n + b];
        write_file(file, rec);
        return open + " f_compress=\"1\"><b>" + base64(lzss(rec)) + "</b></" + game + ">";
    }
    // tmp, renew: not yet
    return open + "/>";
}

// The owner's message for the cabinets' title screens (the file "message"), at most 255 bytes,
// one line; Shift-JIS or ASCII as the owner writes it.
std::string EamuseServer::message() const {
    std::string t = read_file("message");
    if (const size_t nl = t.find_first_of("\r\n"); nl != std::string::npos) t.erase(nl);
    if (t.size() > 255) t.resize(255);
    return t;
}

std::string EamuseServer::handle(const std::string &req) {
    if (req.compare(0, 4, "GET ") == 0) {   // a package's download: the message
        const size_t sp = req.find(' ', 4);
        const std::string path = req.substr(4, sp == std::string::npos ? std::string::npos : sp - 4);
        const std::string text = message();
        const bool ok = !text.empty() && path.size() >= 8 && path.compare(path.size() - 8, 8, "/message") == 0;
        if (log) log("GET " + path + (ok ? " -> the message, " + std::to_string(text.size()) + " bytes" : " -> 404"));
        if (!ok) return "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        return "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(text.size()) + "\r\n\r\n" + text;
    }
    const size_t head_end = req.find("\r\n\r\n");
    const std::string head = req.substr(0, head_end), info = header(head, "X-Eamuse-Info");
    std::string body = head_end == std::string::npos ? "" : req.substr(head_end + 4);
    if (!info.empty()) rc4(key(info), body);
    Call c;
    std::string xml;
    if (parse(body, c)) {
        xml = "<response>";
        for (const Request &r : c.requests) {
            // module elements written out as <x ...></x> (the game's parser also takes <x .../>)
            std::string a = answer(c, r, header(head, "Host"));
            if (a.size() > 2 && a.compare(a.size() - 2, 2, "/>") == 0 && a.find('>') == a.size() - 1)
                a = a.substr(0, a.size() - 2) + "></" + r.module + ">";
            xml += a;
        }
        xml += "</response>";
    }
    if (log) log(std::string("e-AMUSE3 call") + (info.empty() ? " (plain): " : ": ") + body + "\ne-AMUSE3 answer: " + (xml.empty() ? "(not understood)" : xml));
    if (xml.empty()) return "HTTP/1.0 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    return reply(req, xml);
}

std::string EamuseServer::reply(const std::string &req, std::string xml) {
    const std::string info = header(req.substr(0, req.find("\r\n\r\n")), "X-Eamuse-Info");
    // Answer in kind: an encrypted call gets an encrypted answer; a plain one (ntp, rank, ...: their
    // packets have no cipher) a plain answer with no X-Eamuse-Info, which the game insists on (0x190290).
    std::string info_line;
    if (!info.empty()) {
        serial_++;
        const std::string out_info = "1-" + fmt("%08x-%04x", 0x484b0000u + serial_, serial_ & 0xffff);
        rc4(key(out_info), xml);
        info_line = "X-Eamuse-Info: " + out_info + "\r\n";
    }
    return "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(xml.size()) +
           "\r\n" + info_line + "\r\n" + xml;
}

void EamuseServer::edit(std::string &m, const std::function<void(std::string &)> &fn) {
    const size_t he = m.find("\r\n\r\n");
    if (he == std::string::npos) return;
    std::string head = m.substr(0, he);
    const std::string info = header(head, "X-Eamuse-Info");
    std::string body = m.substr(he + 4);
    if (!info.empty()) rc4(key(info), body);
    fn(body);
    if (!info.empty()) rc4(key(info), body);
    for (size_t at = head.find("\r\n"); at != std::string::npos; at = head.find("\r\n", at + 2)) {
        if (strncasecmp(head.c_str() + at + 2, "Content-Length:", 15) != 0) continue;
        const size_t e = head.find("\r\n", at + 2);
        head.replace(at + 2, (e == std::string::npos ? head.size() : e) - at - 2, "Content-Length: " + std::to_string(body.size()));
        break;
    }
    m = head + "\r\n\r\n" + body;
}

void EamuseServer::set_attr(std::string &xml, const std::string &name, const std::string &value) {
    const std::string key = name + "=\"";
    for (size_t at = xml.find(key); at != std::string::npos; at = xml.find(key, at + 1)) {
        if (at == 0 || !std::isspace((unsigned char)xml[at - 1])) continue;   // srcid, not xsrcid
        const size_t b = at + key.size(), e = xml.find('"', b);
        if (e == std::string::npos) return;
        xml.replace(b, e - b, value);
    }
}

}  // namespace hk::py
