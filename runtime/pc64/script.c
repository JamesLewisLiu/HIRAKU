// SPDX-License-Identifier: GPL-3.0-or-later
// Presses and touches for tests (HK_SCRIPT), driven by the game's own scenes (its log's
// CSequence::Attach) or by seconds after its first read of the board:
//   HK_SCRIPT="s12+2:service,s12+4:touch:640:470,s35+1:up:service,..."
//   STEP = sSCENE+SECONDS:ACTION (when scene SCENE is entered, SECONDS later) or SECONDS:ACTION
//   ACTION = touch:X:Y (the sub screen, 1280x800) | click:X:Y (a mouse click there) | card (the card,
//   the link's or HK_PC64_CARD's, held to the reader) | [down:|up:]INPUT (held 300 ms, or held, or let go)
//   INPUT = test | service | coin | b1..b9 | red | blue | headphones
// The scenes seen so far (PLAN.md section 32): 12 title, 26 entry, 20 mode select, 21 character
// select, 28 music select, 34 play, 23 result, 35 test menu, 36 I/O check, 37 input check.
#include "pc64.h"

#include <stdlib.h>
#include <string.h>

static unsigned input_bit(const char *name) {
    static const struct { const char *name; unsigned bit; } inputs[] = {
        {"test", PC64_IN_TEST}, {"service", PC64_IN_SERVICE}, {"coin", PC64_IN_COIN}, {"red", PC64_IN_RED},
        {"blue", PC64_IN_BLUE}, {"headphones", PC64_IN_HEADPHONES}};
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++)
        if (!strcmp(name, inputs[i].name)) return inputs[i].bit;
    int b;
    if (sscanf(name, "b%d", &b) == 1 && b >= 1 && b <= 9) return PC64_IN_BUTTON(b);
    return 0;
}

static DWORD WINAPI run(void *arg) {
    char *steps = _strdup((const char *)arg), *save = NULL;
    for (char *step = strtok_s(steps, ",", &save); step; step = strtok_s(NULL, ",", &save)) {
        int seconds = 0, scene = -1, used = 0;
        if (sscanf(step, "s%d+%d:%n", &scene, &seconds, &used) == 2 && used) {
            while (pc64_scene != scene) Sleep(20);
            Sleep((DWORD)seconds * 1000);
        } else if (sscanf(step, "%d:%n", &seconds, &used) == 1 && used) {
            while (!pc64_board_t0 || (int)((GetTickCount() - pc64_board_t0) / 1000) < seconds) Sleep(50);
        } else {
            pc64_log("script: what is %s?", step);
            continue;
        }
        const char *action = step + used;
        int x, y;
        if (sscanf(action, "touch:%d:%d", &x, &y) == 2) {
            pc64_log("script: touch %d,%d (scene %d, picture %u)", x, y, pc64_scene, pc64_frames);
            pc64_touch(x, y);
            continue;
        }
        if (!strcmp(action, "card")) {
            pc64_log("script: card (scene %d, picture %u)", pc64_scene, pc64_frames);
            pc64_card_presses++;
            continue;
        }
        if (sscanf(action, "click:%d:%d", &x, &y) == 2) {
            pc64_log("script: click %d,%d (scene %d, picture %u)", x, y, pc64_scene, pc64_frames);
            pc64_click(x, y);
            continue;
        }
        const int down = !strncmp(action, "down:", 5), up = !strncmp(action, "up:", 3);
        const unsigned bit = input_bit(action + (down ? 5 : up ? 3 : 0));
        if (!bit) {
            pc64_log("script: what is %s?", action);
            continue;
        }
        pc64_log("script: %s (scene %d, picture %u)", action, pc64_scene, pc64_frames);
        if (!up) pc64_held |= bit;
        if (!down && !up) Sleep(300);
        if (!down) pc64_held &= ~bit;
    }
    free(steps);
    return 0;
}

void pc64_script_start(void) {
    const char *script = getenv("HK_SCRIPT");
    if (script) CreateThread(NULL, 0, run, (void *)script, 0, NULL);
}
