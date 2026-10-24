// SPDX-License-Identifier: GPL-3.0-or-later
// The single-screen mode (HK_PC64_OUTPUT=crt, PLAN.md section 32): for an older cabinet with one
// 4:3 screen, nine buttons and a 10-key, no touch screen and no big buttons.
//   - The picture: the main screen's middle 4:3 (1440x1080), scaled down on the GPU to 640x480
//     (display.c, to_link); the touch screen isn't shown.
//   - The touches the game waits for, made by button 5: in each of these scenes its presses touch
//     the scene's places in turn (title: START; entry: START without a pass; mode select: OK, then
//     CREDIT START).
//   - The music select in nine-button mode: on entering it, once a credit, its category list is
//     opened (the "play with 9 buttons" panel) and "by title" chosen: every category but "giant
//     pop-kun" (the big buttons' charts, which can't be played here) uses the nine buttons.
#include "pc64.h"

#include <stdlib.h>
#include <string.h>

struct place { int x, y; };
static const struct { int scene; int count; struct place at[3]; } SCENES[] = {
    {12, 1, {{640, 470}}},               // title: START
    {26, 1, {{640, 470}}},               // entry: START (no pass)
    {20, 2, {{640, 736}, {640, 350}}},   // mode select: OK, CREDIT START
};
#define MUSIC_SELECT 28
#define CATEGORIES_X 300                 // the music select's category panel (touch screen, 1280x800)
#define CATEGORIES_Y 100
#define BY_TITLE_X 755                   // ... and in its list, "by title"
#define BY_TITLE_Y 192
#define TITLE 12

int pc64_crt(void) {
    static int on = -1;
    if (on < 0) {
        const char *o = getenv("HK_PC64_OUTPUT");
        on = o && !strcmp(o, "crt");
    }
    return on;
}

// The touches, made on a thread of their own (a touch takes 160 ms: the board's reads go on): a
// place, or two with a second between (a list opened, then a choice in it)
static HANDLE g_wake;
static volatile LONG g_pending;
static struct place g_touches[2];
static int g_count;
static DWORD WINAPI toucher(void *unused) {
    (void)unused;
    for (;;) {
        WaitForSingleObject(g_wake, INFINITE);
        if (!InterlockedExchange(&g_pending, 0)) continue;
        for (int i = 0; i < g_count; i++) {
            if (i) Sleep(1000);
            pc64_touch(g_touches[i].x, g_touches[i].y);
        }
    }
    return 0;
}
static void touch_soon(int count, struct place a, struct place b) {
    if (!g_wake) {
        g_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
        CreateThread(NULL, 0, toucher, NULL, 0, NULL);
    }
    if (g_pending) return;   // one sequence at a time
    g_touches[0] = a, g_touches[1] = b, g_count = count;
    InterlockedExchange(&g_pending, 1);
    SetEvent(g_wake);
}

// The board's inputs as the game reads them (board.c, each status read): button 5's presses in the
// scenes above made touches, and the music select's nine buttons chosen
void pc64_crt_inputs(unsigned in) {
    static int was_down, scene_seen = -1, step, nine_done;
    static DWORD scene_at;
    if (!pc64_crt()) return;
    const int scene = pc64_scene;
    if (scene != scene_seen) {   // a new scene: its first place first
        scene_seen = scene, step = 0, scene_at = GetTickCount();
        if (scene == TITLE) nine_done = 0;   // a new credit
    }
    if (scene == MUSIC_SELECT && !nine_done && GetTickCount() - scene_at > 1500) {   // once it has come in
        nine_done = 1;
        pc64_log("crt: the music select in nine-button mode (its songs by title)");
        touch_soon(2, (struct place){CATEGORIES_X, CATEGORIES_Y}, (struct place){BY_TITLE_X, BY_TITLE_Y});
    }
    const int down = (in & PC64_IN_BUTTON(5)) != 0;
    if (down && !was_down)
        for (size_t i = 0; i < sizeof SCENES / sizeof SCENES[0]; i++)
            if (SCENES[i].scene == scene) {
                const struct place p = SCENES[i].at[step % SCENES[i].count];
                step++;
                pc64_log("crt: button 5 touches %d,%d (scene %d)", p.x, p.y, scene);
                touch_soon(1, p, p);
            }
    was_down = down;
}
