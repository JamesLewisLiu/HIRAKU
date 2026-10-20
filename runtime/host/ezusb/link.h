/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What HIRAKU and hiraku-ezusb.exe share on Windows (runtime/host/popnio.h): a page of named shared
 * memory, "Local\hiraku-ezusb-PID". The helper is 32-bit, as the ezusb.dll it loads (the game's,
 * which drives the pop'n music cabinet's I/O board through Cypress's driver, or a replacement such as
 * UltimatePopnController's); HIRAKU is 64-bit, so it can't load the DLL itself. Each side writes its
 * own fields, 32-bit and aligned (so whole); the helper reads the controls at about 1 kHz.
 *
 * The helper is started as
 *     hiraku-ezusb.exe MAPPING DLL PARENT_PID
 * and does what a game does: usbStart(0), usbFirmResult() until 0, usbCoinMode(3), then usbPadRead
 * and usbLamp. It stops when `quit` is set or HIRAKU is gone, with the lamps off and usbEnd(). */
#ifndef HK_EZUSB_LINK_H
#define HK_EZUSB_LINK_H

#include <stdint.h>

#define HK_EZUSB_MAGIC 0x31555a45u /* "EZU1" */

enum {
    HK_EZUSB_LOADING = 0,     /* loading the DLL */
    HK_EZUSB_STARTING = 1,    /* usbStart(0) */
    HK_EZUSB_FIRMWARE = 2,    /* usbFirmResult(), until 0 (the real board: about 17 s) */
    HK_EZUSB_COIN_MODE = 3,   /* usbCoinMode(3) */
    HK_EZUSB_RUNNING = 4,     /* pad, coins and reads are live */
    HK_EZUSB_NO_DLL = -1,     /* the DLL wouldn't load (result: GetLastError()) */
    HK_EZUSB_NO_EXPORT = -2,  /* it lacks one of the calls */
    HK_EZUSB_FAILED = -3      /* a call failed or timed out (result: its value) */
};

typedef struct {
    uint32_t magic;
    volatile int32_t state;     /* helper: HK_EZUSB_* */
    volatile int32_t result;    /* helper: the failing call's value */
    volatile uint32_t pad;      /* helper: usbPadRead's word, 1 = pressed */
    volatile uint32_t coins;    /* helper: usbCoinGet(0), if the DLL has it */
    volatile uint32_t reads;    /* helper: one more each usbPadRead */
    volatile uint32_t lamps;    /* HIRAKU: usbLamp's word */
    volatile uint32_t quit;     /* HIRAKU: 1 = stop */
} hk_ezusb_link;

#endif
