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

// HK_PC64_OVERLAY=DIR: files there replace the game's at the same place under its folder (a mod's
// files: DIR\plain_data\tex\system29\x.ifs for the game's plain_data\tex\system29\x.ifs), as AVS2
// opens them (CreateFileW/A in avs2-core)
static char g_overlay[MAX_PATH];
static HANDLE(WINAPI *r_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE(WINAPI *r_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

// The overlay's file for `path` (a path under the game's folder, or relative to it), if there is one
static int overlaid(const char *path, char *out, size_t size) {
    if (!g_overlay[0] || !path) return 0;
    const char *rest = NULL;
    const size_t n = strlen(g_game);
    if (n && !_strnicmp(path, g_game, n) && (path[n] == '\\' || path[n] == '/')) rest = path + n + 1;
    else if (!strchr(path, ':') && path[0] != '\\' && path[0] != '/') rest = !strncmp(path, "./", 2) || !strncmp(path, ".\\", 2) ? path + 2 : path;
    if (!rest) return 0;
    snprintf(out, size, "%s\\%s", g_overlay, rest);
    for (char *c = out; *c; c++)
        if (*c == '/') *c = '\\';
    const DWORD a = GetFileAttributesA(out);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static HANDLE WINAPI CreateFileA_(LPCSTR path, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    char moved[MAX_PATH * 2];
    if (!(acc & GENERIC_WRITE) && overlaid(path, moved, sizeof moved)) {
        pc64_log("overlay: %s", moved);
        return r_CreateFileA(moved, acc, share, sa, disp, flags, tmpl);
    }
    return r_CreateFileA(path, acc, share, sa, disp, flags, tmpl);
}
static HANDLE WINAPI CreateFileW_(LPCWSTR path, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    char narrow[MAX_PATH * 2], moved[MAX_PATH * 2];
    if (g_overlay[0] && path && !(acc & GENERIC_WRITE) && WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof narrow, NULL, NULL) &&
        overlaid(narrow, moved, sizeof moved)) {
        WCHAR wide[MAX_PATH * 2];
        if (MultiByteToWideChar(CP_UTF8, 0, moved, -1, wide, MAX_PATH * 2)) {
            pc64_log("overlay: %s", moved);
            return r_CreateFileW(wide, acc, share, sa, disp, flags, tmpl);
        }
    }
    return r_CreateFileW(path, acc, share, sa, disp, flags, tmpl);
}

void pc64_files_install(HMODULE game, const char *game_dir) {
    if (game_dir) snprintf(g_game, sizeof g_game, "%s", game_dir);
    r_avs_fs_mount = pc64_import_ordinal(game, "avs2-core.dll", 76, (void *)avs_fs_mount_);
    const char *overlay = getenv("HK_PC64_OVERLAY");
    HMODULE avs = GetModuleHandleA("avs2-core.dll");
    if (overlay && *overlay && avs) {
        snprintf(g_overlay, sizeof g_overlay, "%s", overlay);
        if (!g_game[0]) GetCurrentDirectoryA(sizeof g_game, g_game);
        r_CreateFileW = pc64_import(avs, "kernel32.dll", "CreateFileW", (void *)CreateFileW_);
        r_CreateFileA = pc64_import(avs, "kernel32.dll", "CreateFileA", (void *)CreateFileA_);
        pc64_log("overlay: the game's files from %s first", g_overlay);
    }
    if (!r_avs_fs_mount) pc64_log("the game doesn't mount its data through AVS2 (ordinal 76)");
}
