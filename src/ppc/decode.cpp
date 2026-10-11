// SPDX-License-Identifier: GPL-3.0-or-later
#include "decode.h"

#include <cstdio>

namespace hk {

const char *op_name(Op op) {
    switch (op) {
#define HK_S(n, ...) case Op::n: return #n;
        HK_OPLIST(HK_S, HK_S, HK_S, HK_S, HK_S, HK_S, HK_S)
#undef HK_S
    case Op::b: return "b";
    case Op::bc: return "bc";
    case Op::bclr: return "bclr";
    case Op::bcctr: return "bcctr";
    case Op::sc: return "sc";
    case Op::rfi: return "rfi";
    case Op::rfci: return "rfci";
    case Op::nop: return "nop";
    case Op::unimpl: return "unimpl";
    case Op::invalid: return "invalid";
    }
    return "?";
}

Insn decode(uint32_t w, uint32_t addr) {
    Insn i;
    i.word = w;
    i.addr = addr;
    i.rt = (w >> 21) & 31;
    i.ra = (w >> 16) & 31;
    i.rb = (w >> 11) & 31;
    i.rc = w & 1;
    i.oe = (w >> 10) & 1;
    i.simm = (int16_t)(w & 0xffff);
    i.uimm = w & 0xffff;
    i.crfd = i.rt >> 2;
    i.crfs = i.ra >> 2;
    i.sh = i.rb;
    i.mb = (w >> 6) & 31;
    i.me = (w >> 1) & 31;
    i.nb = i.rb;
    i.to = i.rt;
    i.bo = i.rt;
    i.bi = i.ra;
    i.crm = (w >> 12) & 0xff;
    i.e = (w >> 15) & 1;
    i.spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5);
    i.dcr = i.spr;
    i.lk = w & 1;
    i.aa = (w >> 1) & 1;

    const uint32_t pri = w >> 26;
    switch (pri) {
    case 3: i.op = Op::twi; break;
    case 7: i.op = Op::mulli; break;
    case 8: i.op = Op::subfic; break;
    case 10: i.op = Op::cmpli; break;
    case 11: i.op = Op::cmpi; break;
    case 12: i.op = Op::addic; i.rc = 0; break;
    case 13: i.op = Op::addic; i.rc = 1; break;
    case 14: i.op = Op::addi; break;
    case 15: i.op = Op::addis; break;
    case 16: {
        i.op = Op::bc;
        int32_t bd = (int16_t)(w & 0xfffc);
        i.target = i.aa ? (uint32_t)bd : addr + (uint32_t)bd;
        break;
    }
    case 17: i.op = (w & 2) ? Op::sc : Op::invalid; break;
    case 18: {
        i.op = Op::b;
        int32_t li = (int32_t)((w & 0x03fffffc) << 6) >> 6;
        i.target = i.aa ? (uint32_t)li : addr + (uint32_t)li;
        break;
    }
    case 19:
        switch ((w >> 1) & 0x3ff) {
        case 0: i.op = Op::mcrf; break;
        case 16: i.op = Op::bclr; break;
        case 33: i.op = Op::crnor; break;
        case 50: i.op = Op::rfi; break;
        case 51: i.op = Op::rfci; break;
        case 129: i.op = Op::crandc; break;
        case 150: i.op = Op::nop; break;  // isync
        case 193: i.op = Op::crxor; break;
        case 225: i.op = Op::crnand; break;
        case 257: i.op = Op::crand; break;
        case 289: i.op = Op::creqv; break;
        case 417: i.op = Op::crorc; break;
        case 449: i.op = Op::cror; break;
        case 528: i.op = Op::bcctr; break;
        default: break;
        }
        break;
    case 20: i.op = Op::rlwimi; break;
    case 21: i.op = Op::rlwinm; break;
    case 23: i.op = Op::rlwnm; break;
    case 24: i.op = Op::ori; break;
    case 25: i.op = Op::oris; break;
    case 26: i.op = Op::xori; break;
    case 27: i.op = Op::xoris; break;
    case 28: i.op = Op::andi_; break;
    case 29: i.op = Op::andis_; break;
    case 31: {
        // XO-form arithmetic: 9-bit extended opcode, bit 21 is OE
        switch ((w >> 1) & 0x1ff) {
        case 8: i.op = Op::subfc; break;
        case 10: i.op = Op::addc; break;
        case 11: i.op = Op::mulhwu; break;
        case 40: i.op = Op::subf; break;
        case 75: i.op = Op::mulhw; break;
        case 104: i.op = Op::neg; break;
        case 136: i.op = Op::subfe; break;
        case 138: i.op = Op::adde; break;
        case 200: i.op = Op::subfze; break;
        case 202: i.op = Op::addze; break;
        case 232: i.op = Op::subfme; break;
        case 234: i.op = Op::addme; break;
        case 235: i.op = Op::mullw; break;
        case 266: i.op = Op::add; break;
        case 459: i.op = Op::divwu; break;
        case 491: i.op = Op::divw; break;
        default: break;
        }
        if (i.op != Op::invalid) {
            if ((i.op == Op::mulhw || i.op == Op::mulhwu) && i.oe) i.op = Op::invalid;
            break;
        }
        i.oe = 0;
        switch ((w >> 1) & 0x3ff) {
        case 0: i.op = Op::cmp; break;
        case 4: i.op = Op::tw; break;
        case 19: i.op = Op::mfcr; break;
        case 20: i.op = Op::lwarx; break;
        case 23: i.op = Op::lwzx; break;
        case 24: i.op = Op::slw; break;
        case 26: i.op = Op::cntlzw; break;
        case 28: i.op = Op::and_; break;
        case 32: i.op = Op::cmpl; break;
        case 54: i.op = Op::nop; break;   // dcbst
        case 55: i.op = Op::lwzux; break;
        case 60: i.op = Op::andc; break;
        case 83: i.op = Op::mfmsr; break;
        case 86: i.op = Op::nop; break;   // dcbf
        case 87: i.op = Op::lbzx; break;
        case 119: i.op = Op::lbzux; break;
        case 124: i.op = Op::nor; break;
        case 131: i.op = Op::wrtee; break;
        case 144: i.op = Op::mtcrf; break;
        case 146: i.op = Op::mtmsr; break;
        case 150: i.op = i.rc ? Op::stwcx_ : Op::invalid; break;
        case 151: i.op = Op::stwx; break;
        case 163: i.op = Op::wrteei; break;
        case 183: i.op = Op::stwux; break;
        case 215: i.op = Op::stbx; break;
        case 246: i.op = Op::nop; break;  // dcbtst
        case 247: i.op = Op::stbux; break;
        case 278: i.op = Op::nop; break;  // dcbt
        case 279: i.op = Op::lhzx; break;
        case 284: i.op = Op::eqv; break;
        case 311: i.op = Op::lhzux; break;
        case 316: i.op = Op::xor_; break;
        case 323: i.op = Op::mfdcr; break;
        case 339: i.op = Op::mfspr; break;
        case 343: i.op = Op::lhax; break;
        case 375: i.op = Op::lhaux; break;
        case 407: i.op = Op::sthx; break;
        case 412: i.op = Op::orc; break;
        case 439: i.op = Op::sthux; break;
        case 444: i.op = Op::or_; break;
        case 451: i.op = Op::mtdcr; break;
        case 454: i.op = Op::nop; break;  // dccci
        case 467: i.op = Op::mtspr; break;
        case 470: i.op = Op::nop; break;  // dcbi
        case 476: i.op = Op::nand; break;
        case 486: i.op = Op::unimpl; break;  // dcread
        case 512: i.op = Op::mcrxr; break;
        case 533: i.op = Op::lswx; break;
        case 534: i.op = Op::lwbrx; break;
        case 536: i.op = Op::srw; break;
        case 597: i.op = Op::lswi; break;
        case 598: i.op = Op::nop; break;  // sync
        case 661: i.op = Op::stswx; break;
        case 662: i.op = Op::stwbrx; break;
        case 725: i.op = Op::stswi; break;
        case 790: i.op = Op::lhbrx; break;
        case 792: i.op = Op::sraw; break;
        case 824: i.op = Op::srawi; break;
        case 854: i.op = Op::nop; break;  // eieio
        case 918: i.op = Op::sthbrx; break;
        case 922: i.op = Op::extsh; break;
        case 954: i.op = Op::extsb; break;
        case 966: i.op = Op::nop; break;  // iccci
        case 982: i.op = Op::nop; break;  // icbi
        case 998: i.op = Op::unimpl; break;  // icread
        case 1014: i.op = Op::dcbz; break;
        default: break;
        }
        break;
    }
    case 32: i.op = Op::lwz; break;
    case 33: i.op = Op::lwzu; break;
    case 34: i.op = Op::lbz; break;
    case 35: i.op = Op::lbzu; break;
    case 36: i.op = Op::stw; break;
    case 37: i.op = Op::stwu; break;
    case 38: i.op = Op::stb; break;
    case 39: i.op = Op::stbu; break;
    case 40: i.op = Op::lhz; break;
    case 41: i.op = Op::lhzu; break;
    case 42: i.op = Op::lha; break;
    case 43: i.op = Op::lhau; break;
    case 44: i.op = Op::sth; break;
    case 45: i.op = Op::sthu; break;
    case 46: i.op = Op::lmw; break;
    case 47: i.op = Op::stmw; break;
    default:
        if (pri >= 48) i.op = Op::unimpl;  // floating point: the 403GCX has no FPU
        break;
    }
    return i;
}

std::string disasm(const Insn &i) {
    char buf[96];
    switch (i.op) {
    case Op::b:
        snprintf(buf, sizeof buf, "b%s%s 0x%08x", i.lk ? "l" : "", i.aa ? "a" : "", i.target);
        break;
    case Op::bc:
        snprintf(buf, sizeof buf, "bc%s%s %u,%u,0x%08x", i.lk ? "l" : "", i.aa ? "a" : "", i.bo, i.bi, i.target);
        break;
    case Op::bclr:
    case Op::bcctr:
        snprintf(buf, sizeof buf, "%s%s %u,%u", op_name(i.op), i.lk ? "l" : "", i.bo, i.bi);
        break;
    default:
        snprintf(buf, sizeof buf, "%s rt=%u ra=%u rb=%u simm=%d", op_name(i.op), i.rt, i.ra, i.rb, i.simm);
        break;
    }
    return buf;
}

}  // namespace hk
