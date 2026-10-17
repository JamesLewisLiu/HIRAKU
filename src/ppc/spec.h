// SPDX-License-Identifier: GPL-3.0-or-later
// A recompile spec (analyze/prep.py's recomp.txt) and the program it describes, as both the
// recompiler (recomp/main.cpp: C source) and the executor (runtime/core/exec.cpp: the same code
// run without a compiler, PLAN.md section 27) read them, so they cannot disagree.
//
//   image <path>          program image          func <hex> [nopoll]   a function entry
//   base <hex>            load address           range <lo> <hi>       body range [lo, hi)
//   entry <hex>           a secondary entry      jt <at> <target> ...  bctr at <at>'s targets
//   end
#pragma once

#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "decode.h"

namespace hk::spec {

struct Func {
    uint32_t entry = 0;
    bool nopoll = false;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    std::map<uint32_t, std::vector<uint32_t>> jumptables;  // bctr address -> targets
    std::vector<uint32_t> entries;                         // secondary entries (mid-function)
};

struct Program {
    std::vector<uint8_t> image;
    std::string image_path;                     // the spec's (the recompiler reads it from there)
    uint32_t base = 0;
    std::map<uint32_t, Func> funcs;
    std::map<uint32_t, uint32_t> secondary;     // secondary entry -> owning function
    // Functions that return to their CALLER'S caller: GCC's out-of-line epilogue helpers
    // (_restgpr_*_x) restore the caller's registers and LR from r11 and blr straight past it.
    // A call to one ends the calling function.
    std::set<uint32_t> return_up;

    bool in_image(uint32_t addr) const { return addr >= base && addr - base + 4 <= image.size(); }
    uint32_t word_at(uint32_t addr) const {
        const uint32_t off = addr - base;
        return (uint32_t)image[off] << 24 | (uint32_t)image[off + 1] << 16 | (uint32_t)image[off + 2] << 8 | image[off + 3];
    }
};

// Reads a spec from `in`; the image from `image` when given (the executor: the game's
// program.bin), else from the spec's `image` line. "" when fine, else what is wrong.
inline std::string parse(Program &p, std::istream &in, const std::string &image = "") {
    std::string line;
    Func *cur = nullptr;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string kw;
        ss >> kw;
        if (kw == "image") {
            ss >> p.image_path;
        } else if (kw == "base") {
            ss >> std::hex >> p.base;
        } else if (kw == "func") {
            uint32_t e;
            std::string flag;
            ss >> std::hex >> e >> flag;
            cur = &p.funcs[e];
            cur->entry = e;
            cur->nopoll = flag == "nopoll";
        } else if (cur && kw == "range") {
            uint32_t lo, hi;
            ss >> std::hex >> lo >> hi;
            cur->ranges.emplace_back(lo, hi);
        } else if (cur && kw == "jt") {
            uint32_t at, t;
            ss >> std::hex >> at;
            auto &v = cur->jumptables[at];
            while (ss >> std::hex >> t) v.push_back(t);
        } else if (cur && kw == "entry") {
            uint32_t a;
            ss >> std::hex >> a;
            cur->entries.push_back(a);
            p.secondary[a] = cur->entry;
        } else if (kw == "end") {
            cur = nullptr;
        }
    }
    const std::string &img = image.empty() ? p.image_path : image;
    std::ifstream f(img, std::ios::binary);
    if (!f) return "cannot open image " + img;
    p.image.assign(std::istreambuf_iterator<char>(f), {});
    for (auto &[e, fn] : p.funcs)
        for (auto [lo, hi] : fn.ranges)
            if (!p.in_image(lo) || !p.in_image(hi - 4)) return "a range of the function at " + std::to_string(e) + " is outside the image";
    return "";
}

// A function "returns up" if, following its fall-through chain into the next functions, it
// writes LR (mtlr) without ever having read it (mflr): the LR it returns through is not the one
// it was called with.
inline void find_return_up(Program &p) {
    for (auto &[e, f] : p.funcs) {
        bool reads = false, writes = false;
        std::set<uint32_t> seen;
        for (const Func *cur = &f; cur && seen.insert(cur->entry).second;) {
            const Func *next = nullptr;
            for (auto [lo, hi] : cur->ranges)
                for (uint32_t a = lo; a < hi; a += 4) {
                    Insn i = decode(p.word_at(a), a);
                    if (i.op == Op::mfspr && i.spr == 8) reads = true;
                    if (i.op == Op::mtspr && i.spr == 8 && !reads) writes = true;
                }
            if (!cur->ranges.empty()) {
                uint32_t end = cur->ranges.back().second;
                Insn last = decode(p.word_at(end - 4), end - 4);
                bool ft = !(last.op == Op::b && !last.lk) && !((last.op == Op::bclr || last.op == Op::bcctr) && last.always() && !last.lk);
                auto it = p.funcs.find(end);
                if (ft && it != p.funcs.end()) next = &it->second;
            }
            cur = next;
        }
        if (writes) p.return_up.insert(e);
    }
}

// ... from the file at `path`, or from the text itself
inline std::string load(Program &p, const std::string &path, const std::string &image = "") {
    std::ifstream in(path);
    if (!in) return "cannot open " + path;
    return parse(p, in, image);
}
inline std::string load_text(Program &p, const std::string &text, const std::string &image) {
    std::istringstream in(text);
    return parse(p, in, image);
}

}  // namespace hk::spec
