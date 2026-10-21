// SPDX-License-Identifier: GPL-3.0-or-later
// The cabinet around a PC game, from inside its process (PLAN.md section 30): HIRAKU's own
// ezusb.dll and libacio.dll take the place of Konami's, and the first of them to load also turns
// the game's paths and its Direct3D window into what HIRAKU wants. Set by hiraku-pc through the
// environment:
//   HK_PC_LINK    the file shared with hiraku-pc (runtime/pc/link.h); without it the DLLs run
//                 on their own, on a built-in keyboard layout
//   HK_PC_MAP     where the game's paths go: "FROM=TO;FROM=TO" (D:\popn15\data=Z:\...), a FROM
//                 matching a path's start, any case
//   HK_PC_WINDOW  "WxH": the game in a window of that size, its picture scaled to it; "full": a
//                 window covering the screen, the picture scaled to its height; "" or unset: as the
//                 game asks (full screen at its own mode)
//   HK_PC_HZ      the most pictures a second the game shows (its monitor's rate, default 60): a
//                 window shows as many as the desktop's refresh would let a game made for 60 Hz
//   HK_PC_LOG     a file for the DLLs' log (default: standard error)
//   HK_PAD_SCRIPT for tests: a file of "SECONDS BITS MS" lines, the pad bits held MS milliseconds
//                 that many seconds after the board started (bit 22: a coin)
#pragma once
#include <stdint.h>

#include "link.h"

void hk_log(const char *fmt, ...);
struct hk_pc_link *hk_link(void);   // never null: an empty one when hiraku-pc gave none
void hk_cabinet_start(void);        // once: the path and window hooks (DllMain, process attach)
// The pad as the game reads it: hiraku-pc's, the keys bound to it, the test script's
uint32_t hk_pad_now(void);
uint32_t hk_coins_now(void);
uint32_t hk_keypad_now(void);   // HK_PC_KEY_* and digits: hiraku-pc's and the keys bound to them
uint32_t hk_card_seq(void);     // moves on with each card: hiraku-pc's and the card key's
