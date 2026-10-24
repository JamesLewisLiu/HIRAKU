// SPDX-License-Identifier: GPL-3.0-or-later
// Where the game's files are (PLAN.md section 32). With HK_PC64_SAVE the loader runs in that folder
// (HIRAKU's save folder for the game): AVS2's root, the cabinet's dev/nvram and dev/raw, a copy of
// prop/ are there, and the game's folder is only read. The game mounts its data itself
// (avs_fs_mount, avs2-core's ordinal 76) from folders under the root (./plain_data/sd, ...): those
// mounts are pointed at the game's folder.
#include "pc64.h"

#include <stdlib.h>
#include <string.h>

static char g_game[MAX_PATH];   // the game's folder, when it isn't AVS2's root
static int (*r_avs_fs_mount)(const char *mountpoint, const char *source, const char *fstype, void *data);

static int avs_fs_mount_(const char *mountpoint, const char *source, const char *fstype, void *data) {
    char moved[MAX_PATH * 2];
    if (g_game[0] && source && fstype && !strcmp(fstype, "fs")) {   // a folder (not ramfs's names, imagefs's files)
        const char *rest = !strncmp(source, "./", 2) ? source + 2 : source;
        if (strncmp(rest, "dev/", 4) && !strchr(rest, ':') && rest[0] != '/' && rest[0] != '\\') {
            snprintf(moved, sizeof moved, "%s\\%s", g_game, rest);
            for (char *c = moved; *c; c++)
                if (*c == '/') *c = '\\';
            source = moved;
        }
    }
    const int r = r_avs_fs_mount(mountpoint, source, fstype, data);
    if (source == moved) pc64_log("%s: %s", mountpoint ? mountpoint : "?", source);
    return r;
}

void pc64_files_install(HMODULE game, const char *game_dir) {
    if (game_dir) snprintf(g_game, sizeof g_game, "%s", game_dir);
    r_avs_fs_mount = pc64_import_ordinal(game, "avs2-core.dll", 76, (void *)avs_fs_mount_);
    if (!r_avs_fs_mount) pc64_log("the game doesn't mount its data through AVS2 (ordinal 76)");
}
