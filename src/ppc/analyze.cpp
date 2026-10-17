// SPDX-License-Identifier: GPL-3.0-or-later
// The control-flow analysis (analyze.h).
#include "analyze.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>

#include "decode.h"

namespace hk::analyze {

namespace {

// The 4xx exception vectors, from the load base (EVPR): as analyze/ExportManifest.java lists them
constexpr uint32_t VECTORS[] = {0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800, 0x900, 0xa00, 0xb00,
                                0xc00, 0xd00, 0xe00, 0xf00, 0x1000, 0x1010, 0x1020, 0x1100, 0x1200, 0x2000};

struct Prog {
    const std::vector<uint8_t> &img;
    uint32_t base, end;
    bool in(uint32_t a) const { return a >= base && a < end && (a & 3) == 0; }
    uint32_t word(uint32_t a) const {
        const uint32_t o = a - base;
        return (uint32_t)img[o] << 24 | (uint32_t)img[o + 1] << 16 | (uint32_t)img[o + 2] << 8 | img[o + 3];
    }
    Insn at(uint32_t a) const { return decode(word(a), a); }
    bool code(uint32_t a) const { return in(a) && at(a).op != Op::invalid; }
};

bool is_mtctr(const Insn &i) { return i.op == Op::mtspr && i.spr == 9; }

// The register an instruction writes, or -1 (the logical and rotate group writes rA; the
// rest rT; stores, compares and branches none; the update forms' rA is left out)
int dest(const Insn &i) {
    switch (i.op) {
    case Op::and_: case Op::andc: case Op::or_: case Op::orc: case Op::xor_: case Op::nand: case Op::nor:
    case Op::eqv: case Op::ori: case Op::oris: case Op::xori: case Op::xoris: case Op::andi_: case Op::andis_:
    case Op::extsb: case Op::extsh: case Op::cntlzw: case Op::slw: case Op::srw: case Op::sraw: case Op::srawi:
    case Op::rlwinm: case Op::rlwnm: case Op::rlwimi:
        return i.ra;
    case Op::stb: case Op::stbu: case Op::stbx: case Op::stbux: case Op::sth: case Op::sthu: case Op::sthx:
    case Op::sthux: case Op::stw: case Op::stwu: case Op::stwx: case Op::stwux: case Op::sthbrx: case Op::stwbrx:
    case Op::stmw: case Op::stswi: case Op::stswx: case Op::stwcx_: case Op::dcbz:
    case Op::cmp: case Op::cmpl: case Op::cmpi: case Op::cmpli: case Op::mtspr: case Op::mtcrf: case Op::mtmsr:
    case Op::mtdcr: case Op::wrtee: case Op::wrteei: case Op::tw: case Op::twi: case Op::mcrf: case Op::mcrxr:
    case Op::crand: case Op::crandc: case Op::creqv: case Op::crnand: case Op::crnor: case Op::cror: case Op::crorc:
    case Op::crxor: case Op::b: case Op::bc: case Op::bclr: case Op::bcctr: case Op::sc: case Op::rfi: case Op::rfci:
    case Op::nop: case Op::invalid: case Op::unimpl:
        return -1;
    default:
        return i.rt;
    }
}

// GCC's switch dispatch before a bctr at `at`, within its block: the table's targets, or none.
//   cmplwi crN, rX, BOUND ... bgt default
//   slwi rI, rX, 2  (rlwinm rI, rX, 2, 0, 29)
//   addis rB, rI, TABLE@ha ; lwz rT, TABLE@l(rB)       or   lis rB, TABLE@ha ; addi rB, rB, TABLE@l ;
//   mtctr rT ; bctr                                          lwzx rT, rB, rI (or rI, rB)
std::vector<uint32_t> jump_table(const Prog &p, uint32_t at) {
    constexpr int WINDOW = 16;   // instructions looked back over
    std::vector<Insn> back;      // at - 4, at - 8, ...
    for (int k = 1; k <= WINDOW; k++) {
        const uint32_t a = at - 4u * k;
        if (!p.code(a)) break;
        Insn i = p.at(a);
        back.push_back(i);
        if (i.op == Op::b || i.op == Op::bclr || i.op == Op::rfi || (i.op == Op::bcctr && !i.lk)) {
            back.pop_back();   // the block before: another flow ends there
            break;
        }
    }
    // the value of register r at instruction index k of `back` (looking further back): a
    // constant from lis/addis 0 / addi / ori, or nothing
    auto constant = [&](int r, size_t from, uint32_t &v) -> bool {
        bool have_lo = false;
        int32_t lo = 0;
        for (size_t k = from; k < back.size(); k++) {
            const Insn &i = back[k];
            if (i.op == Op::addi && i.rt == r && i.ra == r) { lo += i.simm; have_lo = true; continue; }
            if (i.op == Op::ori && i.ra == r && i.rt == r) { lo |= (int32_t)i.uimm; have_lo = true; continue; }
            if (i.op == Op::addis && i.rt == r && i.ra == 0) { v = ((uint32_t)i.simm << 16) + (uint32_t)lo; return true; }
            if (i.op == Op::addi && i.rt == r && i.ra == 0) { v = (uint32_t)(i.simm + lo); return true; }
            if (dest(i) == r) return false;   // anything else writing r: not a constant we can follow
        }
        (void)have_lo;
        return false;
    };
    // the bound on the index register (rlwinm's source): cmplwi rX, N before the dispatch
    auto bound = [&](int x, size_t from) -> int {
        for (size_t k = from; k < back.size(); k++) {
            const Insn &i = back[k];
            if (i.op == Op::cmpli && i.ra == x) return (int)i.uimm;
            if (dest(i) == x) {
                if (i.op == Op::or_ && i.rt == i.rb) { x = i.rt; continue; }   // mr x, y (or x, y, y)
                return -1;
            }
        }
        return -1;
    };
    size_t k = 0;
    while (k < back.size() && !is_mtctr(back[k])) k++;
    if (k == back.size()) return {};
    const int t = back[k].rt;
    for (size_t j = k + 1; j < back.size(); j++) {
        const Insn &i = back[j];
        uint32_t table = 0;
        int index = -1;
        if (i.op == Op::lwz && i.rt == t) {
            // addis rB, rI, hi before it: rI the scaled index
            for (size_t m = j + 1; m < back.size(); m++) {
                const Insn &d = back[m];
                if (d.op == Op::addis && d.rt == i.ra && d.ra != 0) {
                    table = ((uint32_t)d.simm << 16) + (uint32_t)i.simm;
                    index = d.ra;
                    break;
                }
                if (dest(d) == i.ra) break;
            }
        } else if (i.op == Op::lwzx && i.rt == t) {
            uint32_t v;
            if (constant(i.ra, j + 1, v)) table = v, index = i.rb;
            else if (constant(i.rb, j + 1, v)) table = v, index = i.ra;
        } else if (dest(i) == t) {
            return {};
        } else {
            continue;
        }
        if (index < 0 || !p.in(table)) return {};
        // the index: slwi rI, rX, 2 -> rX bounded by cmplwi
        int x = -1;
        size_t scaled = 0;   // where the index is scaled: its bound is checked before that
        for (size_t m = j + 1; m < back.size(); m++) {
            const Insn &d = back[m];
            if (d.op == Op::rlwinm && d.ra == index && d.sh == 2 && d.mb == 0 && d.me == 29) { x = d.rt, scaled = m; break; }
            if (dest(d) == index) break;
        }
        if (x < 0) return {};
        int n = bound(x, scaled + 1);
        if (n < 0 || n > 1024) return {};
        std::vector<uint32_t> out;   // each target once, in the table's order
        for (int e = 0; e <= n; e++) {
            const uint32_t a = table + 4u * e;
            if (!p.in(a)) break;
            if (std::find(out.begin(), out.end(), p.word(a)) == out.end()) out.push_back(p.word(a));
        }
        return out;
    }
    return {};
}

struct Analysis {
    const Prog &p;
    const Options &o;
    std::set<uint32_t> entries;                              // function entries
    std::set<uint32_t> code;                                 // every address flow reached
    std::map<uint32_t, std::vector<uint32_t>> tables;        // bctr -> targets (resolved)
    std::set<uint32_t> unresolved;                           // bctr with no table

    // where flow goes from `a` (not calls): out, and whether it falls through
    void successors(uint32_t a, std::vector<uint32_t> &out, uint32_t *call) {
        Insn i = p.at(a);
        const uint32_t next = a + 4;
        *call = 0;
        switch (i.op) {
        case Op::invalid: return;
        case Op::b:
            if (i.lk) { *call = i.target; out.push_back(next); }
            else out.push_back(i.target);
            return;
        case Op::bc:
            if (i.lk) { *call = i.target; out.push_back(next); return; }
            out.push_back(i.target);
            if (!i.always()) out.push_back(next);
            return;
        case Op::bclr:
            if (i.lk || !i.always()) out.push_back(next);
            return;
        case Op::bcctr:
            if (i.lk) { out.push_back(next); return; }
            {
                auto it = tables.find(a);
                if (it == tables.end()) {
                    auto t = jump_table(p, a);
                    if (!t.empty()) it = tables.emplace(a, t).first;
                    else unresolved.insert(a);
                }
                if (it != tables.end())
                    for (uint32_t x : it->second)
                        if (p.in(x)) out.push_back(x);
            }
            if (!i.always()) out.push_back(next);
            return;
        case Op::rfi: case Op::rfci: return;
        default:
            out.push_back(next);
            return;
        }
    }

    // flow from the roots, calls making entries (phase 1: what code is there)
    void discover(std::deque<uint32_t> work) {
        while (!work.empty()) {
            const uint32_t a = work.front();
            work.pop_front();
            if (!p.code(a) || !code.insert(a).second) continue;
            std::vector<uint32_t> next;
            uint32_t call;
            successors(a, next, &call);
            if (call && p.code(call)) {
                if (entries.insert(call).second) work.push_back(call);
            }
            for (uint32_t n : next) work.push_back(n);
        }
    }

    // a function's body: its flow, not into another function's entry
    std::set<uint32_t> body(uint32_t e) {
        std::set<uint32_t> b;
        std::deque<uint32_t> work{e};
        while (!work.empty()) {
            const uint32_t a = work.front();
            work.pop_front();
            if (!p.code(a) || (a != e && entries.count(a)) || !b.insert(a).second) continue;
            std::vector<uint32_t> next;
            uint32_t call;
            successors(a, next, &call);
            for (uint32_t n : next) work.push_back(n);
        }
        return b;
    }
};

// GCC's function start: stwu r1, -N(r1) and mflr r0
bool frame(const Insn &i) { return i.op == Op::stwu && i.rt == 1 && i.ra == 1 && i.simm < 0; }
bool mflr0(const Insn &i) { return i.op == Op::mfspr && i.spr == 8 && i.rt == 0; }

// either one right after the end of other code or data (a return, a branch, a word that is no
// instruction, zero): where Ghidra's PowerPC function start patterns find functions
bool start_after_end(const Prog &p, uint32_t a) {
    if (!p.code(a) || !(frame(p.at(a)) || mflr0(p.at(a)))) return false;
    if (a == p.base) return true;
    const uint32_t w = p.word(a - 4);
    const Insn prev = p.at(a - 4);
    return w == 0 || prev.op == Op::invalid || (prev.op == Op::b && !prev.lk) || prev.op == Op::rfi || prev.op == Op::rfci ||
           ((prev.op == Op::bclr || prev.op == Op::bcctr) && prev.always() && !prev.lk);
}

// Code, not data that happens to decode: from `a`, straight-line instructions that all decode
// (no unsupported ones) up to a return or a branch, within 64
bool looks_like_code(const Prog &p, uint32_t a) {
    if (!p.in(a)) return false;
    // text: four printable characters ("INIT" reads as a branch)
    const uint32_t w = p.word(a);
    bool text = true;
    for (int s = 0; s < 32; s += 8) text &= ((w >> s) & 0xff) >= 0x20 && ((w >> s) & 0xff) < 0x7f;
    if (text) return false;
    for (int k = 0; k < 64; k++, a += 4) {
        if (!p.in(a)) return false;
        const Insn i = p.at(a);
        if (i.op == Op::invalid || i.op == Op::unimpl) return false;
        if ((i.op == Op::b || i.op == Op::bc) && (i.aa || !p.in(i.target))) return false;   // code branches within itself
        if ((i.op == Op::b && !i.lk) || i.op == Op::rfi || i.op == Op::rfci ||
            ((i.op == Op::bclr || i.op == Op::bcctr) && i.always() && !i.lk))
            return true;
    }
    return false;
}

// both, in either order, anywhere (taken only where nothing else reached)
bool start_pair(const Prog &p, uint32_t a) {
    if (!p.code(a) || !p.code(a + 4)) return false;
    const Insn i = p.at(a), j = p.at(a + 4);
    return (frame(i) && mflr0(j)) || (mflr0(i) && frame(j));
}

}  // namespace

Result run(const std::vector<uint8_t> &image, const Options &o) {
    Prog p{image, o.base, o.base + (uint32_t)(image.size() & ~size_t(3))};
    Analysis an{p, o, {}, {}, {}, {}};
    std::deque<uint32_t> roots;
    auto root = [&](uint32_t a) {
        if (p.code(a) && an.entries.insert(a).second) roots.push_back(a);
    };
    // Ghidra's order: its function start patterns on the raw image first
    for (uint32_t a = p.base; a < p.end; a += 4)
        if (start_after_end(p, a)) root(a);
    an.discover(roots);
    roots.clear();
    root(o.entry);
    for (uint32_t x : o.extra) root(x);
    an.discover(roots);
    // functions whose address the code builds (lis rD, hi; addi/ori rX, rD, lo: a callback, a
    // thread): as Ghidra's constant propagation finds them, where the address is code no flow
    // reached that starts right after other code's end
    auto built_addresses = [&]() -> bool {
        roots.clear();
        for (uint32_t a : std::vector<uint32_t>(an.code.begin(), an.code.end())) {
            const Insn i = p.at(a);
            if (i.op != Op::addis || i.ra != 0) continue;
            for (uint32_t k = 1; k <= 5 && an.code.count(a + 4 * k); k++) {
                const Insn j = p.at(a + 4 * k);
                const bool addi = j.op == Op::addi && j.ra == i.rt, ori = j.op == Op::ori && j.rt == i.rt;
                if (!addi && !ori) {
                    if (dest(j) == i.rt) break;
                    continue;
                }
                const uint32_t v = ((uint32_t)i.simm << 16) + (addi ? (uint32_t)j.simm : j.uimm);
                if (p.in(v) && !an.code.count(v) && looks_like_code(p, v) && v > p.base) {
                    const Insn prev = p.at(v - 4);
                    const bool after_end = p.word(v - 4) == 0 || prev.op == Op::invalid || (prev.op == Op::b && !prev.lk) ||
                                           prev.op == Op::rfi || prev.op == Op::rfci ||
                                           ((prev.op == Op::bclr || prev.op == Op::bcctr) && prev.always() && !prev.lk);
                    if (after_end) root(v);
                }
                break;
            }
        }
        // the same for a pointer in data: an aligned word, not code, pointing at such code
        for (uint32_t a = p.base; a < p.end; a += 4) {
            if (an.code.count(a)) continue;
            const uint32_t v = p.word(a);
            if (!p.in(v) || an.code.count(v) || v == p.base || !looks_like_code(p, v)) continue;
            const Insn prev = p.at(v - 4);
            const bool after_end = p.word(v - 4) == 0 || prev.op == Op::invalid || (prev.op == Op::b && !prev.lk) ||
                                   prev.op == Op::rfi || prev.op == Op::rfci ||
                                   ((prev.op == Op::bclr || prev.op == Op::bcctr) && prev.always() && !prev.lk);
            if (after_end) root(v);
        }
        if (roots.empty()) return false;
        an.discover(roots);
        return true;
    };
    // functions only reached through code pointers (nothing calls them): where a function starts
    // right after other code's end, until nothing more is found; then function starts anywhere
    // that nothing reached, one at a time in address order (a start the flow from an earlier
    // one reaches is not one)
    for (bool more = true; more;) {
        more = false;
        roots.clear();
        for (uint32_t a = p.base; a < p.end; a += 4)
            if (!an.code.count(a) && start_after_end(p, a)) root(a);
        if (!roots.empty()) {
            an.discover(roots);
            more = true;
            continue;
        }
        for (uint32_t a = p.base; a + 4 < p.end; a += 4)
            if (!an.code.count(a) && start_pair(p, a)) {
                roots.clear();
                root(a);
                an.discover(roots);
                more = true;
            }
        if (more) continue;
        more = built_addresses();
    }
    // the exception vectors, last and in order, each unless code already covers it (as
    // ExportManifest.java does them); a vector that branches to its handler (b) makes the
    // handler a function
    for (uint32_t v : VECTORS) {
        const uint32_t a = o.base + v;
        if (!p.in(a) || p.word(a) == 0 || an.code.count(a)) continue;
        roots.clear();
        root(a);
        const Insn i = p.at(a);
        if (i.op == Op::b && !i.lk) root(i.target);
        an.discover(roots);
    }

    // bodies, ranges, and prep.py's secondary entries and checks
    struct F {
        std::set<uint32_t> body;
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        std::set<uint32_t> calls, tails;
    };
    std::map<uint32_t, F> funcs;
    std::map<uint32_t, uint32_t> owner;   // address -> the function whose body has it (the first)
    for (uint32_t e : an.entries) {
        F &f = funcs[e];
        f.body = an.body(e);
        for (uint32_t a : f.body) {
            owner.emplace(a, e);
            const Insn i = p.at(a);
            if ((i.op == Op::b || i.op == Op::bc) && i.lk) f.calls.insert(i.target);
            else if ((i.op == Op::b || i.op == Op::bc) && !f.body.count(i.target)) f.tails.insert(i.target);
        }
        uint32_t lo = 0, prev = 0;
        bool open = false;
        for (uint32_t a : f.body) {
            if (open && a == prev + 4) { prev = a; continue; }
            if (open) f.ranges.emplace_back(lo, prev + 4);
            lo = prev = a, open = true;
        }
        if (open) f.ranges.emplace_back(lo, prev + 4);
    }
    std::map<uint32_t, std::set<uint32_t>> secondary;
    Result r;
    for (auto &[e, f] : funcs)
        for (const auto *set : {&f.calls, &f.tails})
            for (uint32_t t : *set) {
                if (funcs.count(t) || t == e) continue;
                auto it = owner.find(t);
                if (it != owner.end()) secondary[it->second].insert(t);
                else r.notes.push_back("call/tail to " + std::to_string(t) + " outside every function");
            }

    char line[64];
    r.spec = "image " + o.image_path + "\n";
    snprintf(line, sizeof line, "base %08x\n", o.base);
    r.spec += line;
    for (auto &[e, f] : funcs) {
        const bool np = std::any_of(o.nopoll.begin(), o.nopoll.end(), [&](auto &r_) { return r_.first <= e && e < r_.second; });
        snprintf(line, sizeof line, "func %08x%s\n", e, np ? " nopoll" : "");
        r.spec += line;
        for (auto [lo, hi] : f.ranges) {
            snprintf(line, sizeof line, "range %08x %08x\n", lo, hi);
            r.spec += line;
        }
        for (uint32_t t : secondary[e]) {
            snprintf(line, sizeof line, "entry %08x\n", t);
            r.spec += line;
            r.secondary++;
        }
        for (uint32_t a : f.body) {
            auto it = an.tables.find(a);
            if (it == an.tables.end()) continue;
            std::string jt;
            snprintf(line, sizeof line, "jt %08x", a);
            jt = line;
            for (uint32_t t : it->second)
                if (p.in(t)) {
                    snprintf(line, sizeof line, " %08x", t);
                    jt += line;
                }
            r.spec += jt + "\n";
            r.tables++;
        }
        r.spec += "end\n";
    }
    for (uint32_t a : an.unresolved) {
        snprintf(line, sizeof line, "bctr at %08x: no jump table found", a);
        r.notes.push_back(line);
    }
    r.functions = funcs.size();
    return r;
}

}  // namespace hk::analyze
