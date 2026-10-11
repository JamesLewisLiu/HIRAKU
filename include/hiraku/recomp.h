/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HIRAKU: what recompiled code needs from the runtime, beyond ppc.h.
 */
#ifndef HIRAKU_RECOMP_H
#define HIRAKU_RECOMP_H

#include "hiraku/ppc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hk_func)(hk_ctx *c);
typedef struct { uint32_t addr; hk_func fn; } hk_func_entry;

/* Generated table.c: every recompiled function, sorted by guest address. */
extern const hk_func_entry hk_func_table[];
extern const unsigned hk_func_count;

/* Virtual-time budget (66 MHz ticks) until the runtime must look again:
 * scheduled device events, or a newly deliverable interrupt (which zeroes it).
 * Recompiled code is charged at its poll points. */
extern int32_t hk_budget;

void hk_poll(hk_ctx *c, uint32_t pc);                 /* run due events, take a pending interrupt */
void hk_syscall(hk_ctx *c);                           /* `sc`; c->pc = the address after it */
void hk_call_dyn(hk_ctx *c, uint32_t target);         /* call any guest address; c->lr already set */
void hk_jump(hk_ctx *c, uint32_t target);             /* tail-transfer to any guest address */
void hk_bad_return(hk_ctx *c, uint32_t expected);     /* callee returned somewhere else */

#define HK_POLL(c, at, cost) do { if (__builtin_expect((hk_budget -= (cost)) <= 0, 0)) hk_poll((c), (at)); } while (0)
#define HK_RET(c) do { (c)->pc = (c)->lr & ~3u; return; } while (0)
#define HK_CHECK(c, ret) do { if (__builtin_expect((c)->pc != (ret), 0)) hk_bad_return((c), (ret)); } while (0)
#define HK_CALL(c, f, ret) do { (c)->lr = (ret); f(c); HK_CHECK((c), (ret)); } while (0)
#define HK_CALL_DYN(c, t, ret) do { (c)->lr = (ret); hk_call_dyn((c), (t)); HK_CHECK((c), (ret)); } while (0)
#define HK_CALL_IND(c, t, ret) do { hk_call_dyn((c), (t)); HK_CHECK((c), (ret)); } while (0)

#ifdef __cplusplus
}
#endif
#endif
