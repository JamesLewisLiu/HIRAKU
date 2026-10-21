// SPDX-License-Identifier: GPL-3.0-or-later
// What hiraku-pc and its DLLs in a PC game's process share (PLAN.md section 30): the cabinet's
// controls, lamps, coins and card, a few words each way. hiraku-pc makes it a file and the DLLs map
// that file (runtime/pc/cabinet.c), which works the same under Wine on Linux, where the file is in
// /dev/shm, and on Windows. Plain C: the DLLs are C, built 32-bit.
#pragma once
#include <stdint.h>

#define HK_PC_LINK_MAGIC 0x4b50484bu   // "HKPK"
#define HK_PC_LINK_VERSION 1u

// The pad's bits, as Konami's ezusb.dll gives them to pop'n music (usbPadRead)
#define HK_PC_PAD_SERVICE (1u << 6)
#define HK_PC_PAD_TEST (1u << 7)
#define HK_PC_PAD_BUTTON(n) (1u << (7 + (n)))   // buttons 1-9: bits 8-16

// The reader's 10-key: bits 0-9 its digits, then these
#define HK_PC_KEY_00 (1u << 10)
#define HK_PC_KEY_DECIMAL (1u << 11)

struct hk_pc_link {
    uint32_t magic, version;   // set by hiraku-pc once the rest is ready
    // hiraku-pc to the game
    volatile uint32_t pad;          // HK_PC_PAD_* held
    volatile uint32_t coins;        // coins put in since the start
    volatile uint32_t keypad;       // HK_PC_KEY_* and digits held, player 1's reader
    volatile uint32_t card_seq;     // moves on when a card is held to the reader: card_uid is it
    uint8_t card_uid[8];
    // The game's window has the keyboard, so the DLL reads the keys bound to pad bits itself: for
    // each pad bit, up to two Windows virtual-key codes (0: none)
    uint8_t keys[32][2];
    uint8_t coin_keys[2];           // a coin each time one goes down
    uint8_t card_keys[2];           // card_uid held to the reader each time one goes down
    uint8_t keypad_keys[12][2];     // the 10-key: digits 0-9, then 00 and the decimal point
    // the game to hiraku-pc
    volatile uint32_t lamps;        // usbLamp's last bits
    volatile uint32_t ext_io;       // usbSetExtIo's last bits
    volatile uint32_t frames;       // pictures shown
    volatile uint32_t game_alive;   // moves on while the game polls its board
};
