/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A pretend pop'n music ezusb.dll (32-bit) for tests/popnio/test_ezusb_helper.cpp: busy for a few
 * usbFirmResult calls, as the real one is while the board takes its firmware; then button 3 and TEST
 * held and a coin counted from the 300th usbPadRead to the 500th. usbEnd writes what it was given
 * to the file named by HK_FAKE_EZUSB_OUT. */
#include <stdio.h>
#include <stdlib.h>

static int started = -1, firm_calls = 0, coin_mode = -1, reads = 0;
static unsigned lamp_last = 0, lamp_lit = 0;

int __cdecl usbStart(int n) { started = n; return 0; }
int __cdecl usbFirmResult(void) { return ++firm_calls < 4 ? 0x60 : 0; }
int __cdecl usbCoinMode(int m) { coin_mode = m; return 0; }
int __cdecl usbPadRead(unsigned long *pad) {
    reads++;
    *pad = reads >= 300 && reads < 500 ? (1ul << 10 | 1ul << 7) : 0;
    return 0;
}
int __cdecl usbCoinGet(int slot) { return slot == 0 && reads >= 300 ? 1 : 0; }
int __cdecl usbLamp(int lamps) {
    lamp_last = (unsigned)lamps;
    if (lamps) lamp_lit = (unsigned)lamps;
    return 0;
}
int __cdecl usbEnd(void) {
    const char *path = getenv("HK_FAKE_EZUSB_OUT");
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f) return 0;
    fprintf(f, "start=%d firm_calls=%d coin_mode=%d lit=%08x last=%08x\n", started, firm_calls, coin_mode, lamp_lit, lamp_last);
    fclose(f);
    return 0;
}
