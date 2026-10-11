// SPDX-License-Identifier: GPL-3.0-or-later
// Replay the QEMU-generated instruction cases (tests/ppc/gen.py) through
// HIRAKU's semantics and compare every register and the scratch memory.
//
//   test_ppc <dir with cases.bin and names.txt> [-v]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "core/runtime.h"

// test_ppc links no recompiled game: an empty function table.
extern "C" {
const hk_func_entry hk_func_table[1] = {{0, nullptr}};
const unsigned hk_func_count = 0;
}

using namespace hk;

namespace {
constexpr uint32_t SCRATCH = 0x01f00000, CODE = 0x00100000;
constexpr size_t REC = 512;
// The 403 implements only these XER bits; QEMU keeps a few more.
constexpr uint32_t XER_MASK = 0xe000007f;

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: test_ppc <dir> [-v]\n");
        return 2;
    }
    bool verbose = argc > 2 && std::string(argv[2]) == "-v";
    std::string dir = argv[1];
    std::ifstream in(dir + "/cases.bin", std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    std::ifstream nin(dir + "/names.txt");
    std::vector<std::string> names;
    for (std::string s; std::getline(nin, s);) names.push_back(s);
    size_t n = data.size() / REC;
    if (!n || names.size() != n) {
        fprintf(stderr, "test_ppc: %s: %zu records, %zu names\n", dir.c_str(), n, names.size());
        return 2;
    }

    ram_init();
    mmio_set_handler({[](uint32_t p, int) -> uint32_t { fprintf(stderr, "unexpected MMIO read %08x\n", p); return 0; },
                      [](uint32_t p, uint32_t, int) { fprintf(stderr, "unexpected MMIO write %08x\n", p); }});
    std::map<std::string, std::pair<int, int>> per_form;   // name -> (pass, fail)
    int shown = 0;

    for (size_t k = 0; k < n; k++) {
        const uint8_t *rec = data.data() + k * REC;
        hk_ctx c{};
        for (int r = 0; r < 32; r++) c.r[r] = be32(rec + 4 * r);
        c.cr = be32(rec + 128);
        c.xer = be32(rec + 132) & XER_MASK;
        c.ctr = be32(rec + 136);
        c.lr = be32(rec + 140);
        uint32_t insn = be32(rec + 144);
        c.reserve = 0;
        memcpy(hk_ram + SCRATCH, rec + 160, 64);
        uint32_t w = __builtin_bswap32(insn);
        memcpy(hk_ram + CODE, &w, 4);
        c.pc = CODE;
        if (names[k] == "stwcx.") {   // the harness always has a reservation from... nothing: match QEMU's view
            c.reserve = 0;
        }
        interp_step(&c);

        std::string why;
        char b[160];
        for (int r = 0; r < 32; r++)
            if (c.r[r] != be32(rec + 256 + 4 * r)) {
                snprintf(b, sizeof b, " r%d=%08x want %08x;", r, c.r[r], be32(rec + 256 + 4 * r));
                why += b;
            }
        if (c.cr != be32(rec + 384)) { snprintf(b, sizeof b, " cr=%08x want %08x;", c.cr, be32(rec + 384)); why += b; }
        if ((c.xer & XER_MASK) != (be32(rec + 388) & XER_MASK)) {
            snprintf(b, sizeof b, " xer=%08x want %08x;", c.xer, be32(rec + 388));
            why += b;
        }
        if (c.ctr != be32(rec + 392)) { snprintf(b, sizeof b, " ctr=%08x want %08x;", c.ctr, be32(rec + 392)); why += b; }
        if (c.lr != be32(rec + 396)) { snprintf(b, sizeof b, " lr=%08x want %08x;", c.lr, be32(rec + 396)); why += b; }
        if (memcmp(hk_ram + SCRATCH, rec + 416, 64) != 0) why += " memory differs;";

        auto &pf = per_form[names[k]];
        if (why.empty()) {
            pf.first++;
        } else {
            pf.second++;
            if (verbose || pf.second <= 2) {
                if (shown++ < 60) {
                    printf("FAIL %-8s insn %08x:%s\n", names[k].c_str(), insn, why.c_str());
                    if (verbose) {
                        printf("     in:");
                        for (int r = 0; r < 32; r++) printf(" %08x", be32(rec + 4 * r));
                        printf("\n     cr %08x xer %08x\n", be32(rec + 128), be32(rec + 132));
                    }
                }
            }
        }
    }

    int forms_bad = 0, total_bad = 0;
    for (auto &[name, pf] : per_form)
        if (pf.second) {
            forms_bad++;
            total_bad += pf.second;
            printf("  %-8s %d/%d failed\n", name.c_str(), pf.second, pf.first + pf.second);
        }
    printf("test_ppc: %zu cases, %zu forms: %s (%d cases in %d forms failed)\n", n, per_form.size(),
           total_bad ? "FAIL" : "PASS", total_bad, forms_bad);
    return total_bad ? 1 : 0;
}
