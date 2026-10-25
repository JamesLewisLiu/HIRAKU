// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU's ezusb.dll for the C02 cabinet, beatmania IIDX 9th style's (PLAN.md section 33): what
// Konami's gives the game from the cabinet's EZ-USB board, given from hiraku-pc instead
// (runtime/pc/link.h). The board is another than pop'n's: its calls have plain C names (pop'n's
// are C++ names), and it has the turntables, the effect sliders, the 16-segment ticker, the top
// lamps and neon, an FPGA its firmware is written to at start-up, and an H8 behind it with the
// magnetic card readers and the 10-keys. Every call Konami's DLL exports is here (ezusb_c02.def).
//
// What the calls give back, read from Konami's DLL (cdecl, all of them): 0 is done or fine, 0x60
// busy (the game asks again, the "...Done" calls), -33 the board not answering. The inputs come
// from the board's status packet: the pad's four bytes active low (usbPadRead turns them active
// high), a byte for each turntable, a nibble for each slider. The pad's bits (as spice2x, GPL-3.0,
// sets that packet for the same DLL): player 1's keys 1-7 bits 8-14, player 2's 15-21, the starts
// 24 and 25, VEFX 26, EFFECT 27, test 28, service 29.
//
// Step 1 (bring-up): each call is logged the first times it's made (all of them with
// HK_PC_USB_LOG), the security plug answers as bm2dx.exe checks it, a new cabinet's settings pass
// (set_up_cabinet). The turntables, sliders, ticker and lamps go through the link (link.h, v7). The
// card readers and the 10-keys come later.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cabinet.h"

#define CALL __cdecl

static int usb_log(void) {
    static int on = -1;
    if (on < 0) on = getenv("HK_PC_USB_LOG") != NULL;
    return on;
}

// A call and what it gave back, in the log: its first three, or all with HK_PC_USB_LOG. Arguments
// are printed as four words whatever the call takes (cdecl: the caller's, read and left)
static int said(const char *name, int *calls, int a, int b, int c, int d, int r) {
    if (*calls < 3 || usb_log()) hk_log("usb: %s(%#x, %#x, %#x, %#x) = %#x%s", name, a, b, c, d, r, *calls == 2 && !usb_log() ? " (no more of these logged)" : "");
    (*calls)++;
    return r;
}
#define SAID(r) said(__func__, &n_, a, b, c, d, (r))
#define CALL4(name) int CALL name(int a, int b, int c, int d)
#define STUB(name, r) CALL4(name) { static int n_; return SAID(r); }

// --- starting, ending, the watchdog: the board is there and fine
STUB(usbStart, 0)
STUB(usbStartWithFile, 0)
STUB(usbEnd, 0)
STUB(usbFirmResult, 0)
STUB(usbGetError, 0)
STUB(usbFactoryModeInit, 0)
CALL4(usbCheckAlive) { static int n_; return usb_log() ? SAID(1) : 1; }   // 0: the board gone (three in a row: USB I/O ERROR)
STUB(usbWdtStart, 0)
STUB(usbWdtStartDone, 0)   // 999: the watchdog had reset the PC
STUB(usbWdtStop, 0)
CALL4(usbWdtReset) { static int n_; return usb_log() ? SAID(0) : 0; }
CALL4(usbWdtResetOld) { static int n_; return usb_log() ? SAID(0) : 0; }

// --- the FPGA (its firmware, data\hex\030408.hex, written at start-up) and the serial port through it
STUB(usbFpgaInit, 0)
STUB(usbFpgaInitDone, 0)
STUB(usbFpgaCheck, 0)
STUB(usbFpgaCheckDone, 0)
STUB(usbFpgaWrite, 0)
STUB(usbFpgaWriteDone, 0)
STUB(usbFpgaSioStart, 0)
STUB(usbSioOnOff, 0)
STUB(usbSioRead, 0)
STUB(usbSioReadDone, 0)
STUB(usbSioWrite, 0)
STUB(usbSioWriteDone, 0)
STUB(usbSioBufClear, 0)
STUB(usbSioRdbufClear, 0)
STUB(usbSioRdbufClearDone, 0)
STUB(usbSioWrbufClear, 0)
STUB(usbSioWrbufClearDone, 0)
STUB(usbSramCheck, 0)
STUB(usbSramCheckDone, 0)
STUB(usbSramCheckGetAddr, 0)
STUB(usbSramCheckGetType, 0)

// --- the security plug, the EEPROM, the PCB's id. bm2dx.exe's check (0x403100): usbGetSecurity's 8
// bytes are 'G', any, "C02", 'J' and 'A', 'B' or 'C' (the game, its model, Japan, the cabinet's
// spec); else SECURITY ERROR. usbBootSecurity("GEC02", 0, 0, 0) wants 0 (0x60: still checking)
STUB(usbBootSecurity, 0)
STUB(usbCheckSecurity, 0)
STUB(usbSetupSecurityComplete, 0)
CALL4(usbGetSecurity) {
    static int n_;
    if (a) memcpy((void *)(intptr_t)a, "GEC02JAA", 8);
    return SAID(0);
}
STUB(usbSecurityInit, 0)
STUB(usbSecurityInitDone, 0)
STUB(usbSecuritySelect, 0)
STUB(usbSecuritySelectDone, 0)
STUB(usbSecurityGetId, 0)
STUB(usbSecurityGetIdDone, 0)
STUB(usbSecurityRead, 0)
STUB(usbSecurityReadDone, 0)
STUB(usbSecurityWrite, 0)
STUB(usbSecurityWriteDone, 0)
STUB(usbSecurityTest, 0)
STUB(usbGetPCBID, 0)   // 10 bytes, left as the game zeroed them (e-AMUSEMENT will want one)
STUB(usbGetKEYID, 0)
STUB(usbEepRead, 0)
STUB(usbEepReadDone, 0)
STUB(usbEepWrite, 0)
STUB(usbEepWriteDone, 0)
STUB(usbEepTest, 0)
STUB(usbReadEepHistory, 0)

// --- the controls
CALL4(usbPadRead) {
    static int n_;
    if (a) *(uint32_t *)(intptr_t)a = hk_pad_now();
    return usb_log() ? SAID(0) : 0;
}
// A turntable's position, 0-255 (player 1: 0, player 2: 1)
CALL4(usbGetTurnTable) {
    static int n_;
    const int v = hk_link()->turntable[a & 1];
    return usb_log() ? SAID(v) : v;
}
// An effect slider's position, 0-15 (0-4)
CALL4(usbGetSlide) {
    static int n_;
    const int v = a >= 0 && a < 5 ? hk_link()->slider[a] & 15 : 0;
    return usb_log() ? SAID(v) : v;
}
// The coins put in so far (usbCoinGet(0); the game counts the new ones)
CALL4(usbCoinGet) {
    static int n_;
    static int last = -1;
    const int coins = (int)(hk_coins_now() & 0xffff);
    if (coins != last || usb_log()) said(__func__, &n_, a, b, c, d, coins), last = coins;
    return a ? -33 : coins;
}
STUB(usbCoinGet2, 0)
STUB(usbCoinMode, 0)   // 1 or 2 (bm2dx.exe 0x45d580)
STUB(usbCoinUp, 0)
STUB(usbCoinMeterUp, 0)
STUB(usbCoinMeterDown, 0)
STUB(usbMute, 0)
STUB(usbGetMute, 0)

// --- the lamps: the keys' (bits 0-13, player 1's first), the LEDs, the top unit's lamps and neon
CALL4(usbLamp) {
    static int n_;
    hk_link()->lamps = (uint32_t)a;
    return usb_log() ? SAID(0) : 0;
}
CALL4(usbLed) {
    static int n_;
    hk_link()->led = (uint8_t)(a & 15);
    return usb_log() ? SAID(0) : 0;
}
CALL4(usbTopUnitLamp) {
    static int n_;
    hk_link()->top_lamp = (uint8_t)a;
    return usb_log() ? SAID(0) : 0;
}
CALL4(usbTopUnitNeon) {
    static int n_;
    hk_link()->top_neon = (uint8_t)(a & 1);
    return usb_log() ? SAID(0) : 0;
}

// --- the 16-segment ticker: 9 characters, sent when they change (bm2dx.exe 0x421a40)
CALL4(usb16Seg) {
    static int n_;
    (void)b, (void)c, (void)d;
    if (a) {
        memcpy((void *)hk_link()->ticker, (const void *)(intptr_t)a, 9);
        if (n_ < 3 || usb_log()) {
            char text[10];
            memcpy(text, (const void *)(intptr_t)a, 9);
            text[9] = 0;
            hk_log("usb: usb16Seg \"%s\"", text);
        }
    }
    n_++;
    return 0;
}
STUB(usb16SegDone, 0)
STUB(usb16SegScroll, 0)
STUB(usb16SegScrollDone, 0)
STUB(usb16SegScrollStop, 0)

// --- the H8 behind the board: the magnetic card readers and the 10-keys
STUB(usbH8UartInit, 0)
STUB(usbH8IdConfirm, 0)
STUB(usbH8GetVersion, 0)
STUB(usbH8ProgExec, 0)
STUB(usbH8Init, 0)
STUB(usbH8AllInit, 0)
STUB(usbH8CardInit, 0)
STUB(usbH8CardGetStatus, 0)
STUB(usbH8CardCtrl, 0)
STUB(usbH8CardFormat, 0)
STUB(usbH8KeyboardInit, 0)
STUB(usbH8KeyboardGetStatus, 0)
STUB(usbH8KeyboardReadData, 0)
STUB(usbH8KeyboardGetSize, 0)
STUB(usbH8KeyboardGetSizeDone, 0)
STUB(usbCardOpen, 0)
STUB(usbCardClose, 0)
STUB(usbCardGetStatus, 0)
STUB(usbCardRead, 0)
STUB(usbCardWrite, 0)
STUB(usbCardFormat, 0)
STUB(usbCardEject, 0)
STUB(usbCardWaitEnd, 0)

// --- a new cabinet's checks. The operator settings (bm2dx.exe JAH: 0x437f10, 0xb4c bytes with a
// checksum) start as the game's defaults, and on a cabinet that has none saved yet the start-up says
// BACKUP DATA ERROR and the settings' check CLOCK ERROR (the clock never set: a word of them zero),
// both sending it to the operator menu (ALL FACTORY SETTINGS, CLOCK SETUP). HIRAKU's cabinet is set up:
// both branches are made to go on, with the defaults. Found by shape, one each in all eight
// revisions (JAA to JAH).

// The first place `pat` matches in the program's code (0xff in `mask`: that byte as it is), or NULL
static uint8_t *find(const uint8_t *pat, const uint8_t *mask, size_t n) {
    char *base = (char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; s++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t *code = (uint8_t *)base + sec->VirtualAddress;
        for (size_t i = 0; i + n <= sec->Misc.VirtualSize; i++) {
            size_t k = 0;
            while (k < n && (code[i + k] & mask[k]) == (pat[k] & mask[k])) k++;
            if (k == n) return code + i;
        }
    }
    return NULL;
}

static void nop2(uint8_t *p, const char *what) {
    DWORD old;
    VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old);
    p[0] = p[1] = 0x90;
    VirtualProtect(p, 2, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 2);
    hk_log("hiraku-pc: C02: %s goes on (%p)", what, (void *)p);
}

static void set_up_cabinet(void) {
    // the settings' check: mov eax,[esp+0x70]; add esp,8; test eax,eax; je CLOCK ERROR; cmp dword [esp],-1; je
    static const uint8_t clock[] = {0x8b, 0x44, 0x24, 0x70, 0x83, 0xc4, 0x08, 0x85, 0xc0, 0x74, 0, 0x83, 0x7c, 0x24, 0x00, 0xff, 0x74};
    static const uint8_t clock_m[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t *p = find(clock, clock_m, sizeof clock);
    if (p) nop2(p + 9, "a clock never set");
    else hk_log("hiraku-pc: C02: the settings' clock check not found");
    // the start-up's: the settings loaded into a flag (call loader; mov [F],eax; xor eax,eax; ret 4),
    // which a later stage reads (mov eax,[F]; mov [x],ebx; cmp eax,edi; jne BACKUP DATA ERROR)
    static const uint8_t load[] = {0xe8, 0, 0, 0, 0, 0xa3, 0, 0, 0, 0, 0x33, 0xc0, 0xc2, 0x04, 0x00};
    static const uint8_t load_m[] = {0xff, 0, 0, 0, 0, 0xff, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t *l = find(load, load_m, sizeof load);
    if (l) {
        uint8_t use[] = {0xa1, l[6], l[7], l[8], l[9], 0x89, 0x1d, 0, 0, 0, 0, 0x3b, 0xc7, 0x75};
        static const uint8_t use_m[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0xff, 0xff, 0xff};
        p = find(use, use_m, sizeof use);
        if (p) nop2(p + 13, "no settings saved");
    }
    if (!l || !p) hk_log("hiraku-pc: C02: the start-up's settings check not found");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID r) {
    (void)h, (void)r;
    if (why == DLL_PROCESS_ATTACH) {
        hk_cabinet_start();
        set_up_cabinet();
    }
    return TRUE;
}
