// SPDX-License-Identifier: GPL-3.0-or-later
// The executor (exec.h): each function of the spec as steps, built by a mirror of
// recomp/main.cpp's Emitter. Where the emitter writes a line of C, this appends a step that does
// what the line does, so the two must change together.
#include "exec.h"

#include <algorithm>
#include <iterator>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "hiraku/recomp.h"
#include "ppc/cycles.h"
#include "ppc/decode.h"
#include "ppc/spec.h"
#include "runtime.h"

namespace hk::exec {

namespace {

enum class K : uint8_t {
    ord,        // an ordinary instruction: its op_* (the HK_OPLIST)
    tick,       // HK_TICK(a)
    poll,       // HK_POLL(c, a, ...)
    go,         // goto: on at step a
    bc,         // if (!hk_bc_ok(c, bo, bi)) on at step a: the branch's "then" steps follow
    tail,       // f(c); return;  (a tail call: the callee's steps replace these)
    jump,       // hk_jump(c, a); return;
    call,       // HK_CALL(c, f, a)
    call_up,    // c->lr = a; f(c); return;  (a helper that returns to our caller)
    call_dyn,   // HK_CALL_DYN(c, b, a)
    set_lr,     // c->lr = a;
    bclrl,      // { t = c->lr & ~3; c->lr = a; if (hk_bc_ok) HK_CALL_IND(c, t, a); }
    ret,        // HK_RET(c)
    ret_if,     // if (hk_bc_ok) HK_RET(c)
    bcctrl,     // { t = c->ctr & ~3; c->lr = a; [if (hk_bc_ok)] HK_CALL_IND(c, t, a); }
    table,      // [if (hk_bc_ok)] switch (c->ctr & ~3) { the jump table's cases; default: hk_jump; return }
    jump_ctr,   // [if (hk_bc_ok)] { hk_jump(c, c->ctr & ~3); return; }
    sc,         // c->pc = a; hk_syscall(c);
    rfi,        // c->msr = c->srr1; c->pc = c->srr0 & ~3; return;
    rfci,       // c->msr = c->srr3; c->pc = c->srr2 & ~3; return;
    unimpl,     // hk_unimplemented(c, a, b); return;
    op = 32,    // op + Op: an ordinary instruction, its op_* inline in the dispatcher (run())
};

struct Code;

using OpFn = void (*)(hk_ctx *, const Insn &);

struct Step {
    K k{};
    uint8_t bo = 0, bi = 0;
    int32_t tick = 0;         // HK_TICK folded in: charged when the step is reached (see fold())
    bool always = false;      // bcctr's: no condition is evaluated (the emitter writes no `if`)
    uint32_t a = 0, b = 0;
    const Code *fn = nullptr; // tail, call, call_up: the callee
    OpFn op = nullptr;        // ord: op_<name> with the instruction's fields
    Insn i{};                 // ord
};

struct Code {
    uint32_t entry = 0;   // where it's entered (a function's entry, or a secondary one)
    std::vector<Step> steps;
    // table steps' cases, by step: (value, step to go to, or ~0u: hk_jump(c, value))
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> tables;
};

spec::Program g_p;
std::vector<std::unique_ptr<Code>> g_codes;
std::unordered_map<uint32_t, const Code *> g_entry;   // entry (or secondary entry) -> its code
bool g_loaded = false;

// Each ordinary instruction's op_* with the instruction's fields, as the emitter writes the call
#define HK_X0(n) void x_##n(hk_ctx *c, const Insn &) { op_##n(c); }
#define HK_X1(n, a) void x_##n(hk_ctx *c, const Insn &i) { op_##n(c, i.a); }
#define HK_X2(n, a, b) void x_##n(hk_ctx *c, const Insn &i) { op_##n(c, i.a, i.b); }
#define HK_X3(n, a, b, d) void x_##n(hk_ctx *c, const Insn &i) { op_##n(c, i.a, i.b, i.d); }
#define HK_X4(n, a, b, d, e) void x_##n(hk_ctx *c, const Insn &i) { op_##n(c, i.a, i.b, i.d, i.e); }
#define HK_X5(n, a, b, d, e, f) void x_##n(hk_ctx *c, const Insn &i) { op_##n(c, i.a, i.b, i.d, i.e, i.f); }
#define HK_X6(n, a, b, d, e, f, g) void x_##n(hk_ctx *c, const Insn &i) { op_##n(c, i.a, i.b, i.d, i.e, i.f, i.g); }
HK_OPLIST(HK_X0, HK_X1, HK_X2, HK_X3, HK_X4, HK_X5, HK_X6)
#undef HK_X0
#undef HK_X1
#undef HK_X2
#undef HK_X3
#undef HK_X4
#undef HK_X5
#undef HK_X6

OpFn op_of(Op op) {
    switch (op) {
#define HK_F(n, ...) case Op::n: return x_##n;
        HK_OPLIST(HK_F, HK_F, HK_F, HK_F, HK_F, HK_F, HK_F)
#undef HK_F
    default: return nullptr;
    }
}

// recomp/main.cpp's Emitter, appending steps instead of C
struct Builder {
    const spec::Func &f;
    Code &out;
    std::set<uint32_t> body, labels;
    std::map<uint32_t, uint32_t> label_at;              // label address -> step index
    std::vector<std::pair<size_t, uint32_t>> go_fix;    // go steps to point at a label
    std::vector<std::pair<size_t, uint32_t>> table_fix; // (table, case) -> label, by flat index
    std::vector<std::pair<size_t, const Code **>> fn_fix;
    std::vector<std::pair<size_t, uint32_t>> callee;    // steps calling a function, by address
    int pending = 0;

    Builder(const spec::Func &fn_, Code &o) : f(fn_), out(o) {
        for (auto [lo, hi] : f.ranges)
            for (uint32_t a = lo; a < hi; a += 4) body.insert(a);
    }
    bool inside(uint32_t a) const { return body.count(a) != 0; }
    size_t add(const Step &s) { out.steps.push_back(s); return out.steps.size() - 1; }

    void flush() {
        if (pending) {
            Step s;
            s.k = K::tick;
            s.a = (uint32_t)pending;
            add(s);
        }
        pending = 0;
    }
    void find_labels() {
        for (uint32_t a : body) {
            Insn i = decode(g_p.word_at(a), a);
            if ((i.op == Op::b || i.op == Op::bc) && !i.lk && inside(i.target)) labels.insert(i.target);
        }
        for (auto &[at, ts] : f.jumptables)
            for (uint32_t t : ts)
                if (inside(t)) labels.insert(t);
        for (auto [lo, hi] : f.ranges)
            if (inside(hi)) labels.insert(hi);
    }
    void poll(uint32_t at) {
        if (f.nopoll) return;
        Step s;
        s.k = K::poll;
        s.a = at;
        add(s);
    }
    void goto_label(uint32_t target) {
        Step s;
        s.k = K::go;
        go_fix.emplace_back(add(s), target);
    }
    void go(uint32_t from, uint32_t target) {
        if (inside(target)) {
            if (target <= from) poll(from);
            goto_label(target);
        } else if (g_p.funcs.count(target) || g_p.secondary.count(target)) {
            Step s;
            s.k = K::tail;
            callee.emplace_back(add(s), target);
        } else {
            Step s;
            s.k = K::jump;
            s.a = target;
            add(s);
        }
    }
    void call(uint32_t ret, uint32_t target) {
        Step s;
        s.a = ret;
        if (g_p.return_up.count(target)) {
            s.k = K::call_up;
            callee.emplace_back(add(s), target);
        } else if (g_p.funcs.count(target) || g_p.secondary.count(target)) {
            s.k = K::call;
            callee.emplace_back(add(s), target);
        } else {
            s.k = K::call_dyn;
            s.b = target;
            add(s);
        }
    }
    // if (cond) { ...then }: a bc step skipping the then-steps, its target set once they are in
    size_t if_cond(const Insn &i) {
        Step s;
        s.k = K::bc;
        s.bo = i.bo, s.bi = i.bi;
        return add(s);
    }
    void end_if(size_t at) { out.steps[at].a = (uint32_t)out.steps.size(); }

    void insn(uint32_t a) {
        Insn i = decode(g_p.word_at(a), a);
        if (labels.count(a)) {
            flush();
            label_at[a] = (uint32_t)out.steps.size();
        }
        {
            uint32_t prev = 0;
            bool has_prev = a - 4 >= g_p.base && a - g_p.base <= g_p.image.size();
            if (has_prev) has_prev = !hk::ppc::ends_sequence(prev = g_p.word_at(a - 4));
            pending += hk::ppc::cycles(i.word, a, has_prev, prev);
        }
        if (i.is_branch() || i.op == Op::sc || i.op == Op::rfi || i.op == Op::rfci) flush();
        const uint32_t next = a + 4;
        Step s;
        switch (i.op) {
        case Op::b:
            if (i.lk) {
                if (i.target == next) {
                    s.k = K::set_lr;
                    s.a = next;
                    add(s);
                } else {
                    call(next, i.target);
                }
            } else {
                go(a, i.target);
            }
            break;
        case Op::bc:
            if (i.lk) {
                s.k = K::set_lr;
                s.a = next;
                add(s);
                size_t at = if_cond(i);
                call(next, i.target);
                end_if(at);
            } else if (i.always()) {
                go(a, i.target);
            } else {
                size_t at = if_cond(i);
                go(a, i.target);
                end_if(at);
            }
            break;
        case Op::bclr:
            if (i.lk) {
                s.k = K::bclrl;
                s.bo = i.bo, s.bi = i.bi;
                s.a = next;
            } else if (i.always()) {
                s.k = K::ret;
            } else {
                s.k = K::ret_if;
                s.bo = i.bo, s.bi = i.bi;
            }
            add(s);
            break;
        case Op::bcctr:
            s.bo = i.bo, s.bi = i.bi;
            s.always = i.always();
            if (i.lk) {
                s.k = K::bcctrl;
                s.a = next;
                add(s);
            } else if (f.jumptables.count(a)) {
                s.k = K::table;
                s.b = (uint32_t)out.tables.size();
                auto &cases = out.tables.emplace_back();
                for (uint32_t t : std::set<uint32_t>(f.jumptables.at(a).begin(), f.jumptables.at(a).end())) {
                    cases.emplace_back(t, ~0u);
                    if (inside(t)) table_fix.emplace_back(((size_t)s.b << 32) | (cases.size() - 1), t);
                }
                add(s);
            } else {
                s.k = K::jump_ctr;
                add(s);
            }
            break;
        case Op::sc:
            s.k = K::sc;
            s.a = next;
            add(s);
            break;
        case Op::rfi:
            s.k = K::rfi;
            add(s);
            break;
        case Op::rfci:
            s.k = K::rfci;
            add(s);
            break;
        case Op::unimpl:
        case Op::invalid:
            s.k = K::unimpl;
            s.a = a;
            s.b = i.word;
            add(s);
            break;
        case Op::nop:
            break;
        default:
            s.k = (K)((int)K::op + (int)i.op);
            s.i = i;
            s.op = op_of(i.op);
            add(s);
            break;
        }
    }

    static bool falls_through(const Insn &i) {
        switch (i.op) {
        case Op::b: return i.lk && !g_p.return_up.count(i.target);
        case Op::bc: case Op::bclr: case Op::bcctr: return i.lk || !i.always();
        case Op::rfi: case Op::rfci: return false;
        default: return true;
        }
    }

    // recomp's run(entry): the whole body, entered at `entry`
    void run(uint32_t entry) {
        find_labels();
        for (uint32_t e : f.entries) labels.insert(e);
        pending = 0;
        const bool entry_first = !f.ranges.empty() && f.ranges.front().first == entry;
        poll(entry);
        if (!entry_first) {
            labels.insert(entry);
            goto_label(entry);
        }
        for (auto [lo, hi] : f.ranges) {
            for (uint32_t a = lo; a < hi; a += 4) insn(a);
            Insn last = decode(g_p.word_at(hi - 4), hi - 4);
            flush();
            if (falls_through(last)) go(hi - 4, hi);
        }
        for (auto [at, target] : go_fix) out.steps[at].a = label_at.at(target);
        for (auto [key, target] : table_fix) out.tables[key >> 32][key & 0xffffffffu].second = label_at.at(target);
        fold();
    }

    // A tick step charges the cycles of the run before it, and what follows it runs next: unless
    // something jumps to that next step (a label, a branch's "else"), the charge moves into it
    // (charged when it is reached), the same moment. The callee lists are fixed up along.
    void fold() {
        std::vector<Step> &v = out.steps;
        std::vector<char> target(v.size() + 1, 0);
        for (const Step &s : v)
            if (s.k == K::go || s.k == K::bc) target[s.a] = 1;
        for (const auto &cases : out.tables)
            for (auto [val, at] : cases)
                if (at != ~0u) target[at] = 1;
        std::vector<uint32_t> to(v.size() + 1);
        std::vector<Step> w;
        int32_t carry = 0;
        for (size_t k = 0; k < v.size(); k++) {
            if (v[k].k == K::tick && k + 1 < v.size() && !target[k + 1] && v[k].tick == 0) {
                carry += (int32_t)v[k].a;   // folded into the next step
                to[k] = (uint32_t)w.size();
                continue;
            }
            to[k] = (uint32_t)w.size();
            Step s = v[k];
            if (s.k == K::tick) s.tick += (int32_t)s.a, s.a = 0;
            s.tick += carry;
            carry = 0;
            w.push_back(s);
        }
        to[v.size()] = (uint32_t)w.size();
        for (Step &s : w)
            if (s.k == K::go || s.k == K::bc) s.a = to[s.a];
        for (auto &cases : out.tables)
            for (auto &c : cases)
                if (c.second != ~0u) c.second = to[c.second];
        for (auto &[at, addr] : callee) at = to[at];
        v.swap(w);
    }
};


// A recompiled function's body, run: returns where the C function would return. Threaded:
// each step jumps straight to the next one's handler, charging the cycles folded into it first.
void run(hk_ctx *c, const Code *k) {
    static void *handler[256];
    static bool ready = false;
    if (__builtin_expect(!ready, 0)) {
        void *const control[] = {&&ord, &&tick, &&poll, &&go, &&bc, &&tail, &&jump, &&call, &&call_up, &&call_dyn,
                                 &&set_lr, &&bclrl, &&ret, &&ret_if, &&bcctrl, &&table, &&jump_ctr, &&sc, &&rfi,
                                 &&rfci, &&unimpl};
        for (void *&h : handler) h = &&ord;
        for (size_t n = 0; n < std::size(control); n++) handler[n] = control[n];
#define HK_L(n, ...) handler[(int)K::op + (int)Op::n] = &&L_##n;
        HK_OPLIST(HK_L, HK_L, HK_L, HK_L, HK_L, HK_L, HK_L)
#undef HK_L
        ready = true;
    }
    if (__builtin_expect(hk_patched_any, 0) && patched(k->entry)) {   // changed by a mod: interpreted
        hk_interp_function(c, k->entry);
        return;
    }
    const Step *s = k->steps.data(), *st;
#define HK_NEXT() do { hk_budget -= st->tick; goto *handler[(int)st->k]; } while (0)
#define HK_STEP() do { st = s++; HK_NEXT(); } while (0)
#define HK_AT(n) do { s = k->steps.data() + (n); HK_STEP(); } while (0)
    HK_STEP();
ord:
    st->op(c, st->i);
    HK_STEP();
    // each ordinary instruction's op_*, inline (the HK_OPLIST, as the emitter calls them)
#define HK_H0(n) L_##n: op_##n(c); HK_STEP();
#define HK_H1(n, a) L_##n: op_##n(c, st->i.a); HK_STEP();
#define HK_H2(n, a, b) L_##n: op_##n(c, st->i.a, st->i.b); HK_STEP();
#define HK_H3(n, a, b, d) L_##n: op_##n(c, st->i.a, st->i.b, st->i.d); HK_STEP();
#define HK_H4(n, a, b, d, e) L_##n: op_##n(c, st->i.a, st->i.b, st->i.d, st->i.e); HK_STEP();
#define HK_H5(n, a, b, d, e, f) L_##n: op_##n(c, st->i.a, st->i.b, st->i.d, st->i.e, st->i.f); HK_STEP();
#define HK_H6(n, a, b, d, e, f, g) L_##n: op_##n(c, st->i.a, st->i.b, st->i.d, st->i.e, st->i.f, st->i.g); HK_STEP();
    HK_OPLIST(HK_H0, HK_H1, HK_H2, HK_H3, HK_H4, HK_H5, HK_H6)
#undef HK_H0
#undef HK_H1
#undef HK_H2
#undef HK_H3
#undef HK_H4
#undef HK_H5
#undef HK_H6
tick:
    HK_STEP();
poll:
    if (__builtin_expect(hk_budget <= 0, 0)) hk_poll(c, st->a);
    HK_STEP();
go:
    HK_AT(st->a);
bc:
    if (!hk_bc_ok(c, st->bo, st->bi)) HK_AT(st->a);
    HK_STEP();
tail:   // f(c); return: the callee's steps instead (a sibling call, no host stack)
    k = st->fn;
    if (__builtin_expect(hk_patched_any, 0) && patched(k->entry)) {   // changed by a mod: interpreted
        hk_interp_function(c, k->entry);
        return;
    }
    HK_AT(0);
jump:
    hk_jump(c, st->a);
    return;
call:
    c->lr = st->a;
    run(c, st->fn);
    if (__builtin_expect(c->pc != st->a, 0)) hk_bad_return(c, st->a);
    HK_STEP();
call_up:
    c->lr = st->a;
    k = st->fn;
    if (__builtin_expect(hk_patched_any, 0) && patched(k->entry)) {
        hk_interp_function(c, k->entry);
        return;
    }
    HK_AT(0);
call_dyn:
    c->lr = st->a;
    hk_call_dyn(c, st->b);
    if (__builtin_expect(c->pc != st->a, 0)) hk_bad_return(c, st->a);
    HK_STEP();
set_lr:
    c->lr = st->a;
    HK_STEP();
bclrl: {
    const uint32_t t = c->lr & ~3u;
    c->lr = st->a;
    if (hk_bc_ok(c, st->bo, st->bi)) {
        hk_call_dyn(c, t);
        if (__builtin_expect(c->pc != st->a, 0)) hk_bad_return(c, st->a);
    }
    HK_STEP();
}
ret:
    c->pc = c->lr & ~3u;
    return;
ret_if:
    if (hk_bc_ok(c, st->bo, st->bi)) { c->pc = c->lr & ~3u; return; }
    HK_STEP();
bcctrl: {
    const uint32_t t = c->ctr & ~3u;
    c->lr = st->a;
    if (st->always || hk_bc_ok(c, st->bo, st->bi)) {
        hk_call_dyn(c, t);
        if (__builtin_expect(c->pc != st->a, 0)) hk_bad_return(c, st->a);
    }
    HK_STEP();
}
table:
    if (st->always || hk_bc_ok(c, st->bo, st->bi)) {
        const uint32_t v = c->ctr & ~3u;
        const auto &cases = k->tables[st->b];
        auto it = std::lower_bound(cases.begin(), cases.end(), std::make_pair(v, 0u));
        if (it != cases.end() && it->first == v && it->second != ~0u) HK_AT(it->second);
        hk_jump(c, v);
        return;
    }
    HK_STEP();
jump_ctr:
    if (st->always || hk_bc_ok(c, st->bo, st->bi)) { hk_jump(c, c->ctr & ~3u); return; }
    HK_STEP();
sc:
    c->pc = st->a;
    hk_syscall(c);
    HK_STEP();
rfi:
    c->msr = c->srr1;
    c->pc = c->srr0 & ~3u;
    return;
rfci:
    c->msr = c->srr3;
    c->pc = c->srr2 & ~3u;
    return;
unimpl:
    hk_unimplemented(c, st->a, st->b);
    return;
#undef HK_NEXT
#undef HK_STEP
#undef HK_AT
}

}  // namespace

namespace {
std::string prepare();
}

std::string load(const std::string &spec_path, const std::string &image) {
    g_p = spec::Program{};
    if (std::string e = spec::load(g_p, spec_path, image); !e.empty()) return e;
    return prepare();
}

std::string load_text(const std::string &spec_text, const std::string &image) {
    g_p = spec::Program{};
    if (std::string e = spec::load_text(g_p, spec_text, image); !e.empty()) return e;
    return prepare();
}

namespace {
// the loaded spec's functions turned into steps
std::string prepare() {
    g_codes.clear();
    g_entry.clear();
    g_loaded = false;
    spec::find_return_up(g_p);
    // every function, and each secondary entry as its own copy of the body (as the recompiler
    // emits it), then the calls pointed at their callees
    std::vector<std::pair<Code *, std::vector<std::pair<size_t, uint32_t>>>> calls;
    for (auto &[e, f] : g_p.funcs) {
        std::vector<uint32_t> entries{e};
        entries.insert(entries.end(), f.entries.begin(), f.entries.end());
        for (uint32_t entry : entries) {
            auto code = std::make_unique<Code>();
            code->entry = entry;
            Builder b(f, *code);
            b.run(entry);
            g_entry[entry] = code.get();
            calls.emplace_back(code.get(), std::move(b.callee));
            g_codes.push_back(std::move(code));
        }
    }
    for (auto &[code, list] : calls)
        for (auto [at, target] : list) code->steps[at].fn = g_entry.at(target);
    g_loaded = true;
    return "";
}
}  // namespace

bool loaded() { return g_loaded; }
size_t count() { return g_entry.size(); }
bool has(uint32_t addr) { return g_entry.count(addr) != 0; }
const spec::Program *program() { return g_loaded ? &g_p : nullptr; }

bool call(hk_ctx *c, uint32_t addr) {
    auto it = g_entry.find(addr);
    if (it == g_entry.end()) return false;
    run(c, it->second);
    return true;
}

}  // namespace hk::exec
