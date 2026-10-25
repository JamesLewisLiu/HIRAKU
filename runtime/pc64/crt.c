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
//   - The play screen's characters, smaller and in the 4:3 picture's side columns (as pop'n's 4:3
//     games have them): the game moves each to its place each frame (AFP's layer position, which
//     popn.dll imports by ordinal); for its characters' layers the whole matrix is set instead. Their
//     windows are made smaller in the layout (hiraku-pc's single-screen/); the songs' pictures the
//     game puts in them, fitted to them.
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

// The characters (1080-line coordinates): scaled by CHARA_SCALE about a point at the bottom of the
// 16:9 picture's side columns (x 300 and 1620), that point moved into the 4:3 picture's (x 427 and
// 1493: between its edges at 240 and 1680 and the lanes at 614 and 1306), a little up
#define CHARA_SCALE 0.8f
#define CHARA_SCALE_LOST 0.64f   // the lost move (a close-up, wider than the others)
#define CHARA_LOST 2
#define CHARA_FROM_Y 1080.0f
#define CHARA_TO_Y 1052.0f
static const float CHARA_FROM_X[2] = {300, 1620}, CHARA_TO_X[2] = {427, 1493};

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

// The play screen's characters. The game keeps its two players' characters in a table (0xe0 bytes
// each): from +8 the layers (BM2D::CLayer *, its AFP layer number at +8) of the character's ten
// moves (win, win, lost, neutral, neutral, fever, great, good, miss, neutral), at +0xb4 and +0xb8
// the places (two shorts each) it puts them. Found by the code that draws them: lea rax,[table];
// movzx edi,cl; imul rbx,rdi,0xe0; mov r14d,0x352.
typedef int (*set_position_fn)(unsigned layer, const float *at);
typedef int (*set_params_fn)(unsigned layer, unsigned which, const float *values);
static set_position_fn g_set_position;
static set_params_fn g_set_params;
static const BYTE *g_players;

// The move (0-9) LAYER shows of a player's character, or -1
static int character(unsigned layer, const float *at) {
    for (int p = 0; p < 2; p++) {
        const BYTE *player = g_players + p * 0xe0;
        const short *places = (const short *)(player + 0xb4);
        if (!((at[0] == places[0] && at[1] == places[1]) || (at[0] == places[2] && at[1] == places[3])))
            continue;   // (a pooled layer of another scene)
        for (int i = 0; i < 10; i++) {
            const BYTE *l = *(const BYTE *const *)(player + 8 + 8 * i);
            if (l && *(const unsigned *)(l + 8) == layer) return i;
        }
    }
    return -1;
}

static int set_position(unsigned layer, const float *at) {
    const int move = at && layer ? character(layer, at) : -1;
    if (move >= 0) {
        const int right = at[0] >= 960;
        const float s = move == CHARA_LOST ? CHARA_SCALE_LOST : CHARA_SCALE;
        const float m[6] = {s, 0, 0, s, CHARA_TO_X[right] + s * (at[0] - CHARA_FROM_X[right]),
                            CHARA_TO_Y + s * (at[1] - CHARA_FROM_Y)};
        return g_set_params(layer, 0x20, m);   // the matrix: a, b, c, d, tx, ty
    }
    return g_set_position(layer, at);
}

// The songs' pictures in the characters' windows: the game looks up the background layout's clips
// for them by name (AFP: the first of a layer's clips named so, then the next), then gives them their
// texture at its own size; fitted to the clip instead (the layout made the clip smaller)
typedef int (*find_clip_fn)(unsigned layer, const char *name);
typedef int (*next_clip_fn)(int clip, int how);
typedef int (*set_texture_fn)(int clip, const void *texture, char fit);
static find_clip_fn g_find_clip;
static next_clip_fn g_next_clip;
static set_texture_fn g_set_texture;
#define WINDOW_CLIPS 16
static volatile LONG g_window_clips[WINDOW_CLIPS], g_window_next;

static int find_clip(unsigned layer, const char *name) {
    const int first = g_find_clip(layer, name);
    const char *bare = name && *name == '/' ? name + 1 : name;
    if (bare && first >= 0 && (!strcmp(bare, "bg_l_usr") || !strcmp(bare, "bg_r_usr")))
        for (int clip = first, n = 0; clip >= 0 && n < 4; clip = g_next_clip(clip, 6), n++) {
            int known = 0;
            for (int i = 0; i < WINDOW_CLIPS; i++) known |= g_window_clips[i] == clip;
            if (!known) g_window_clips[(InterlockedIncrement(&g_window_next) - 1) % WINDOW_CLIPS] = clip;
        }
    return first;
}

static int set_texture(int clip, const void *texture, char fit) {
    for (int i = 0; i < WINDOW_CLIPS && !fit; i++) fit = g_window_clips[i] == clip;
    return g_set_texture(clip, texture, fit);
}

void pc64_crt_install(HMODULE game) {
    if (!pc64_crt()) return;
    static const BYTE code[] = {0x0f, 0xb6, 0xf9, 0x48, 0x69, 0xdf, 0xe0, 0x00, 0x00, 0x00, 0x41, 0xbe, 0x52, 0x03, 0x00, 0x00};
    BYTE *base = (BYTE *)game;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections && !g_players; i++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_CNT_CODE)) continue;
        const BYTE *p = base + sec->VirtualAddress, *end = p + sec->Misc.VirtualSize - sizeof code;
        for (p += 7; p < end; p++)
            if (p[-7] == 0x48 && p[-6] == 0x8d && p[-5] == 0x05 && !memcmp(p, code, sizeof code)) {
                g_players = p + *(const int32_t *)(p - 4);
                break;
            }
    }
    HMODULE afp = GetModuleHandleA("afp-core.dll");
    g_set_params = afp ? (set_params_fn)(void *)GetProcAddress(afp, MAKEINTRESOURCEA(41)) : NULL;
    if (!g_players || !g_set_params) {
        pc64_log("crt: the play screen's characters not found: left as they are");
        return;
    }
    g_set_position = afp ? (set_position_fn)(void *)GetProcAddress(afp, MAKEINTRESOURCEA(47)) : NULL;
    if (!g_set_position || !pc64_import_ordinal(game, "afp-core.dll", 47, (void *)set_position)) {
        pc64_log("crt: the game's layer positions not found: its characters left as they are");
        return;
    }
    for (int i = 0; i < WINDOW_CLIPS; i++) g_window_clips[i] = -1;
    g_find_clip = (find_clip_fn)(void *)GetProcAddress(afp, MAKEINTRESOURCEA(103));
    g_next_clip = (next_clip_fn)(void *)GetProcAddress(afp, MAKEINTRESOURCEA(106));
    g_set_texture = (set_texture_fn)(void *)GetProcAddress(afp, MAKEINTRESOURCEA(137));
    if (!g_find_clip || !g_next_clip || !g_set_texture || !pc64_import_ordinal(game, "afp-core.dll", 137, (void *)set_texture) ||
        !pc64_import_ordinal(game, "afp-core.dll", 103, (void *)find_clip))
        pc64_log("crt: the game's clip lookups not found: the songs' pictures left at their size");
    pc64_log("crt: the play screen's characters (table at +0x%llx) scaled by %.2f",
             (unsigned long long)(g_players - base), CHARA_SCALE);
}
