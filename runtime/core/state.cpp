// SPDX-License-Identifier: GPL-3.0-or-later
// Guest RAM, the MMIO bus hook, and the 403's privileged registers.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "runtime.h"

uint8_t *hk_ram = nullptr;
volatile int hk_irq_pending = 0;

namespace hk {

Cpu403 g_cpu;
static MmioHandler g_mmio;
std::function<void()> g_on_fatal;

void ram_init() {
    if (!hk_ram) hk_ram = static_cast<uint8_t *>(calloc(1, HK_RAM_SIZE));
    else memset(hk_ram, 0, HK_RAM_SIZE);
    g_cpu = Cpu403{};
    g_cpu.sprs[spr::PVR] = PVR_403GCX;
}

bool ram_load(const std::string &path, uint32_t phys) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<char> buf((std::istreambuf_iterator<char>(in)), {});
    if (phys + buf.size() > HK_RAM_SIZE) return false;
    memcpy(hk_ram + phys, buf.data(), buf.size());
    return true;
}

void mmio_set_handler(MmioHandler h) { g_mmio = std::move(h); }

void dump_ctx(const hk_ctx *c, FILE *f) {
    for (int i = 0; i < 32; i++) fprintf(f, "r%-2d %08x%s", i, c->r[i], (i % 8 == 7) ? "\n" : "  ");
    fprintf(f, "pc  %08x  lr  %08x  ctr %08x  cr  %08x  xer %08x  msr %08x\n", c->pc, c->lr, c->ctr, c->cr, c->xer, c->msr);
    fprintf(f, "srr0 %08x srr1 %08x sprg %08x %08x %08x %08x\n", c->srr0, c->srr1, c->sprg[0], c->sprg[1], c->sprg[2], c->sprg[3]);
}

void fatal(hk_ctx *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "hiraku: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    if (c) dump_ctx(c, stderr);
    if (g_on_fatal) g_on_fatal();
    exit(3);
}

}  // namespace hk

using namespace hk;

extern "C" {

uint32_t hk_mmio_read(hk_ctx *c, uint32_t phys, int size) {
    if (!g_mmio.read) fatal(c, "MMIO read %08x/%d with no bus", phys, size);
    return g_mmio.read(phys, size);
}

void hk_mmio_write(hk_ctx *c, uint32_t phys, uint32_t value, int size) {
    if (!g_mmio.write) fatal(c, "MMIO write %08x/%d = %08x with no bus", phys, size, value);
    g_mmio.write(phys, value, size);
}

uint32_t hk_mfspr(hk_ctx *c, uint32_t n) {
    switch (n) {
    case spr::SRR0: return c->srr0;
    case spr::SRR1: return c->srr1;
    case spr::SRR2: return c->srr2;
    case spr::SRR3: return c->srr3;
    case spr::SPRG0: case spr::SPRG1: case spr::SPRG2: case spr::SPRG3: return c->sprg[n - spr::SPRG0];
    case spr::TBLO: return (uint32_t)g_cpu.tb;
    case spr::TBHI: return (uint32_t)(g_cpu.tb >> 32);
    default: return g_cpu.sprs[n & 1023];
    }
}

void hk_mtspr(hk_ctx *c, uint32_t n, uint32_t v) {
    switch (n) {
    case spr::SRR0: c->srr0 = v; break;
    case spr::SRR1: c->srr1 = v; break;
    case spr::SRR2: c->srr2 = v; break;
    case spr::SRR3: c->srr3 = v; break;
    case spr::SPRG0: case spr::SPRG1: case spr::SPRG2: case spr::SPRG3: c->sprg[n - spr::SPRG0] = v; break;
    case spr::TBLO: g_cpu.tb = (g_cpu.tb & ~0xffffffffull) | v; break;
    case spr::TBHI: g_cpu.tb = (g_cpu.tb & 0xffffffffull) | (uint64_t)v << 32; break;
    case spr::TSR: g_cpu.sprs[n] &= ~v; break;      // write 1 to clear
    case spr::PVR: break;                           // read-only
    default: g_cpu.sprs[n & 1023] = v; break;
    }
}

uint32_t hk_mfdcr(hk_ctx *, uint32_t n) { return g_cpu.dcrs[n & 1023]; }

void hk_mtdcr(hk_ctx *, uint32_t n, uint32_t v) {
    if (n == dcr::EXISR) g_cpu.dcrs[n] &= ~v;      // write 1 to clear
    else g_cpu.dcrs[n & 1023] = v;
}

void hk_mtmsr(hk_ctx *c, uint32_t v) { c->msr = v; }

void hk_trap(hk_ctx *c, uint32_t pc) { fatal(c, "trap at %08x", pc); }

void hk_unimplemented(hk_ctx *c, uint32_t pc, uint32_t insn) { fatal(c, "unimplemented instruction %08x at %08x", insn, pc); }

}  // extern "C"
