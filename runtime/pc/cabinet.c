// SPDX-License-Identifier: GPL-3.0-or-later
#include "cabinet.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define COBJMACROS
#include <d3d9.h>
#include <psapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- the log

static FILE *g_logf;

void hk_log(const char *fmt, ...) {
    if (!g_logf) {
        const char *f = getenv("HK_PC_LOG");
        g_logf = f && *f ? fopen(f, "a") : NULL;
        if (!g_logf) g_logf = stderr;
    }
    va_list a;
    va_start(a, fmt);
    vfprintf(g_logf, fmt, a);
    va_end(a);
    fputc('\n', g_logf);
    fflush(g_logf);
}

// --- the link

static struct hk_pc_link g_alone;   // no hiraku-pc: the keys below, nothing else
static struct hk_pc_link *g_link;

static void default_keys(struct hk_pc_link *l) {
    // HIRAKU's pop'n keys (shell/cabinet.cpp): the buttons zig-zag Z S X D C F V G B, F2 test,
    // 9 service, 5 a coin, Insert the card, the keypad the 10-key
    static const char row[9] = {'Z', 'S', 'X', 'D', 'C', 'F', 'V', 'G', 'B'};
    for (int b = 0; b < 9; b++) l->keys[7 + b + 1][0] = (uint8_t)row[b];
    l->keys[7][0] = VK_F2;
    l->keys[6][0] = '9';
    l->coin_keys[0] = '5';
    l->card_keys[0] = VK_INSERT;
    for (int k = 0; k < 10; k++) l->keypad_keys[k][0] = (uint8_t)(VK_NUMPAD0 + k);
    l->keypad_keys[11][0] = VK_DECIMAL;
}

struct hk_pc_link *hk_link(void) {
    if (g_link) return g_link;
    const char *f = getenv("HK_PC_LINK");
    if (f && *f) {
        HANDLE h = CreateFileA(f, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        HANDLE m = h != INVALID_HANDLE_VALUE ? CreateFileMappingA(h, NULL, PAGE_READWRITE, 0, HK_PC_LINK_SIZE, NULL) : NULL;
        struct hk_pc_link *l = m ? (struct hk_pc_link *)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, HK_PC_LINK_SIZE) : NULL;
        if (m) CloseHandle(m);   // the view keeps the mapping
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (l && l->magic == HK_PC_LINK_MAGIC && l->version == HK_PC_LINK_VERSION) {
            g_link = l;
            return g_link;
        }
        hk_log("hiraku-pc: %s isn't hiraku-pc's link (error %lu): the keyboard alone", f, GetLastError());
    }
    default_keys(&g_alone);
    g_link = &g_alone;
    return g_link;
}

// --- the test script (HK_PAD_SCRIPT)

static struct { double at; uint32_t bits; int ms; } g_ev[256];
static int g_nev = -1;
static DWORD g_t0;

static uint32_t scripted(void) {
    if (g_nev < 0) {
        g_nev = 0;
        g_t0 = GetTickCount();
        const char *f = getenv("HK_PAD_SCRIPT");
        FILE *fh = f && *f ? fopen(f, "r") : NULL;
        if (fh) {
            unsigned long bits;
            while (g_nev < 256 && fscanf(fh, "%lf %lx %d", &g_ev[g_nev].at, &bits, &g_ev[g_nev].ms) == 3)
                g_ev[g_nev++].bits = (uint32_t)bits;
            fclose(fh);
        }
    }
    const double now = (GetTickCount() - g_t0) / 1000.0;
    uint32_t b = 0;
    for (int i = 0; i < g_nev; i++)
        if (now >= g_ev[i].at && now < g_ev[i].at + g_ev[i].ms / 1000.0) b |= g_ev[i].bits;
    return b;
}

static int key_down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }

// Escape in the game's window ends the game, as it closes HIRAKU's other games (runtime/host/
// window.cpp): at once, since the game has nothing to save that it hasn't (its backup is written as
// it changes). Only in its own window: the keys' state is the whole desktop's
static void escape_quits(void) {
    if (!key_down(VK_ESCAPE)) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) {
        static int said;
        if (!said++) hk_log("hiraku-pc: Escape, but another program's window is in front (%lu)", pid);
        return;
    }
    hk_log("hiraku-pc: Escape: the game ends");
    TerminateProcess(GetCurrentProcess(), 0);
}

// The coin mode the game set (usbCoinMode): 1 (pop'n 15 online) counts the coin switch itself, on
// the pad's bit 22, where 3 reads the board's count (usbCoinGet)
static int g_coin_mode = 3;
void hk_set_coin_mode(int mode) { g_coin_mode = mode; }

// In mode 1, each coin is a pulse of the coin switch: on for 4 reads of the pad, off for 4
static uint32_t coin_switch(void) {
    static uint32_t given;
    static int phase;
    const uint32_t coins = hk_coins_now();
    if (g_coin_mode != 1) {
        given = coins;
        return 0;
    }
    if (phase || given != coins) {
        if (++phase > 8) {
            phase = 0;
            given++;
        }
        return phase <= 4 ? 1u << 22 : 0;
    }
    return 0;
}

uint32_t hk_pad_now(void) {
    struct hk_pc_link *l = hk_link();
    escape_quits();
    uint32_t b = l->pad;
    for (int i = 0; i < 32; i++)
        if (key_down(l->keys[i][0]) || key_down(l->keys[i][1])) b |= 1u << i;
    l->game_alive++;
    return b | (scripted() & ~(1u << 22)) | coin_switch();
}

uint32_t hk_coins_now(void) {
    static uint32_t extra;
    static int was;
    struct hk_pc_link *l = hk_link();
    const int now = key_down(l->coin_keys[0]) || key_down(l->coin_keys[1]) || (scripted() & (1u << 22));
    if (now && !was) extra++;
    was = now;
    return l->coins + extra;
}

uint32_t hk_keypad_now(void) {
    struct hk_pc_link *l = hk_link();
    uint32_t b = l->keypad;
    for (int i = 0; i < 12; i++)
        if (key_down(l->keypad_keys[i][0]) || key_down(l->keypad_keys[i][1])) b |= i < 10 ? 1u << i : i == 10 ? HK_PC_KEY_00 : HK_PC_KEY_DECIMAL;
    return b;
}

uint32_t hk_card_seq(void) {
    static uint32_t extra;
    static int was;
    struct hk_pc_link *l = hk_link();
    const int now = key_down(l->card_keys[0]) || key_down(l->card_keys[1]);
    if (now && !was) extra++;
    was = now;
    return l->card_seq + extra;
}

// --- the game's paths (HK_PC_MAP)

static struct { char from[MAX_PATH], to[MAX_PATH]; size_t n; } g_map[8];
static int g_nmap;

static void read_map(void) {
    const char *s = getenv("HK_PC_MAP");
    while (s && *s && g_nmap < 8) {
        const char *end = strchr(s, ';'), *eq = strchr(s, '=');
        if (!end) end = s + strlen(s);
        if (eq && eq > s && eq < end && eq - s < MAX_PATH && end - eq - 1 < MAX_PATH) {
            memcpy(g_map[g_nmap].from, s, eq - s);
            memcpy(g_map[g_nmap].to, eq + 1, end - eq - 1);
            g_map[g_nmap].n = eq - s;
            for (char *c = g_map[g_nmap].from; *c; c++)
                if (*c == '/') *c = '\\';
            hk_log("hiraku-pc: %s -> %s", g_map[g_nmap].from, g_map[g_nmap].to);
            g_nmap++;
        }
        s = *end ? end + 1 : end;
    }
}

// The path the game asked for, or where HK_PC_MAP sends it (in buf)
static const char *mapped(const char *p, char *buf) {
    if (!p) return p;
    for (int i = 0; i < g_nmap; i++) {
        const size_t n = g_map[i].n;
        size_t k = 0;
        for (; k < n && p[k]; k++) {
            const char a = p[k] == '/' ? '\\' : p[k], b = g_map[i].from[k];
            if ((a | 0x20) != (b | 0x20) && a != b) break;
        }
        if (k < n) continue;
        // a whole folder name: "D:\popn15" doesn't take "D:\popn15x"
        if (g_map[i].from[n - 1] != '\\' && p[n] && p[n] != '\\' && p[n] != '/') continue;
        if (snprintf(buf, MAX_PATH, "%s%s", g_map[i].to, p + n) >= MAX_PATH) return p;
        return buf;
    }
    return p;
}

static HANDLE WINAPI my_CreateFileA(LPCSTR p, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fl,
                                    HANDLE t) {
    char b[MAX_PATH];
    return CreateFileA(mapped(p, b), acc, share, sa, cd, fl, t);
}
static BOOL WINAPI my_CreateDirectoryA(LPCSTR p, LPSECURITY_ATTRIBUTES sa) {
    char b[MAX_PATH];
    return CreateDirectoryA(mapped(p, b), sa);
}
static HANDLE WINAPI my_FindFirstFileA(LPCSTR p, LPWIN32_FIND_DATAA d) {
    char b[MAX_PATH];
    return FindFirstFileA(mapped(p, b), d);
}
static DWORD WINAPI my_GetFileAttributesA(LPCSTR p) {
    char b[MAX_PATH];
    return GetFileAttributesA(mapped(p, b));
}
static BOOL WINAPI my_DeleteFileA(LPCSTR p) {
    char b[MAX_PATH];
    return DeleteFileA(mapped(p, b));
}
static BOOL WINAPI my_RemoveDirectoryA(LPCSTR p) {
    char b[MAX_PATH];
    return RemoveDirectoryA(mapped(p, b));
}
static BOOL WINAPI my_MoveFileA(LPCSTR from, LPCSTR to) {
    char b1[MAX_PATH], b2[MAX_PATH];
    return MoveFileA(mapped(from, b1), mapped(to, b2));
}
static DWORD WINAPI my_GetFullPathNameA(LPCSTR p, DWORD n, LPSTR out, LPSTR *file) {
    char b[MAX_PATH];
    return GetFullPathNameA(mapped(p, b), n, out, file);
}

// --- Direct3D 9: a window instead of the whole screen, and the game's own rate

static int g_win_w, g_win_h, g_win_full;   // HK_PC_WINDOW
static double g_hz = 60;
static HWND g_hwnd;
static int g_game_w = 640, g_game_h = 480;

typedef HRESULT(WINAPI *CreateDevice_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *,
                                        IDirect3DDevice9 **);
typedef HRESULT(WINAPI *Reset_t)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef HRESULT(WINAPI *Present_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static CreateDevice_t g_CreateDevice;
static Reset_t g_Reset;
static Present_t g_Present;

static void *patch_slot(void **slot, void *to) {
    DWORD old;
    void *was = *slot;
    if (was == to) return NULL;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
    *slot = to;
    VirtualProtect(slot, sizeof *slot, old, &old);
    return was;
}

// HIRAKU's window shows the pictures (hiraku-pc's link says so): the game's stays hidden
static int host_shows(void) { return hk_link()->host_shows != 0; }

static void windowed(D3DPRESENT_PARAMETERS *pp) {
    if (!g_win_w && !g_win_full && !host_shows()) return;
    g_game_w = pp->BackBufferWidth ? (int)pp->BackBufferWidth : g_game_w;
    g_game_h = pp->BackBufferHeight ? (int)pp->BackBufferHeight : g_game_h;
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    pp->BackBufferFormat = D3DFMT_UNKNOWN;   // the desktop's
}

// The game's window: no border, at HK_PC_WINDOW's size, in the middle of its screen
static void place_window(HWND w) {
    if (w && host_shows()) {
        g_hwnd = w;
        SetWindowLongA(w, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        ShowWindow(w, SW_HIDE);
        return;
    }
    if (!w || (!g_win_w && !g_win_full)) return;
    g_hwnd = w;
    MONITORINFO mi;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    GetMonitorInfoA(MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT m = mi.rcMonitor;
    const int sw = m.right - m.left, sh = m.bottom - m.top;
    const int ww = g_win_full ? sw : g_win_w, wh = g_win_full ? sh : g_win_h;
    SetWindowLongA(w, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongA(w, GWL_EXSTYLE, 0);
    SetWindowPos(w, HWND_TOP, m.left + (sw - ww) / 2, m.top + (sh - wh) / 2, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

// The picture just drawn, into the link's pixels (0x00RRGGBB): the back buffer read back through
// a surface in system memory, of its format (the desktop's, X8R8G8B8, in a window)
static IDirect3DSurface9 *g_readback;
static UINT g_rb_w, g_rb_h;
static D3DFORMAT g_rb_fmt;

static void capture(IDirect3DDevice9 *d) {
    struct hk_pc_link *l = hk_link();
    IDirect3DSurface9 *bb = NULL;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(d, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC desc;
    IDirect3DSurface9_GetDesc(bb, &desc);
    if (desc.Width > HK_PC_MAX_W || desc.Height > HK_PC_MAX_H) {
        IDirect3DSurface9_Release(bb);
        return;
    }
    if (!g_readback || g_rb_w != desc.Width || g_rb_h != desc.Height || g_rb_fmt != desc.Format) {
        if (g_readback) IDirect3DSurface9_Release(g_readback);
        g_readback = NULL;
        if (FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(d, desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM,
                                                                &g_readback, NULL))) {
            hk_log("hiraku-pc: can't read the pictures back (%ux%u, format %d)", desc.Width, desc.Height, (int)desc.Format);
            IDirect3DSurface9_Release(bb);
            return;
        }
        g_rb_w = desc.Width, g_rb_h = desc.Height, g_rb_fmt = desc.Format;
        hk_log("hiraku-pc: the pictures go to HIRAKU's window (%ux%u, format %d)", desc.Width, desc.Height, (int)desc.Format);
    }
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(d, bb, g_readback)) &&
        SUCCEEDED(IDirect3DSurface9_LockRect(g_readback, &lr, NULL, D3DLOCK_READONLY))) {
        uint32_t *out = (uint32_t *)((char *)l + HK_PC_PIXELS);
        for (UINT y = 0; y < desc.Height; y++) {
            const uint8_t *row = (const uint8_t *)lr.pBits + (size_t)y * lr.Pitch;
            uint32_t *o = out + (size_t)y * desc.Width;
            if (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8) {
                const uint32_t *p = (const uint32_t *)row;
                for (UINT x = 0; x < desc.Width; x++) o[x] = p[x] & 0xffffff;
            } else if (desc.Format == D3DFMT_R5G6B5) {
                const uint16_t *p = (const uint16_t *)row;
                for (UINT x = 0; x < desc.Width; x++) {
                    const uint32_t v = p[x], r = v >> 11, g = v >> 5 & 63, b = v & 31;
                    o[x] = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
                }
            } else if (desc.Format == D3DFMT_X1R5G5B5 || desc.Format == D3DFMT_A1R5G5B5) {
                const uint16_t *p = (const uint16_t *)row;
                for (UINT x = 0; x < desc.Width; x++) {
                    const uint32_t v = p[x], r = v >> 10 & 31, g = v >> 5 & 31, b = v & 31;
                    o[x] = (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
                }
            }
        }
        IDirect3DSurface9_UnlockRect(g_readback);
        l->frame_w = desc.Width, l->frame_h = desc.Height;
        MemoryBarrier();
        l->frame_seq = l->frame_seq + 1;
    }
    IDirect3DSurface9_Release(bb);
}

// Until hiraku-pc has shown the picture and its pacing lets the next frame start (or a quarter of
// a second, if it has stopped answering)
static void wait_for_go(void) {
    struct hk_pc_link *l = hk_link();
    const uint32_t seq = l->frame_seq;
    const DWORD t0 = GetTickCount();
    for (int spins = 0; l->frame_go != seq; spins++) {
        if (GetTickCount() - t0 > 250) return;
        if (spins < 200) YieldProcessor();
        else if (spins < 400) SwitchToThread();
        else Sleep(1);
    }
}

static HRESULT WINAPI my_Present(IDirect3DDevice9 *d, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty) {
    if (host_shows()) {   // HIRAKU's window shows it, at the time its pacing chooses
        capture(d);
        hk_link()->frames++;
        wait_for_go();
        return D3D_OK;
    }
    RECT fit;
    if (!dst && g_hwnd && (g_win_w || g_win_full)) {
        // the picture at the window's height, its shape kept, in the middle
        RECT c;
        GetClientRect(g_hwnd, &c);
        const int cw = c.right, ch = c.bottom;
        int w = cw, h = cw * g_game_h / g_game_w;
        if (h > ch) h = ch, w = ch * g_game_w / g_game_h;
        fit.left = (cw - w) / 2, fit.top = (ch - h) / 2, fit.right = fit.left + w, fit.bottom = fit.top + h;
        dst = &fit;
        if (w < cw || h < ch) {   // the bands beside it black
            HDC dc = GetDC(g_hwnd);
            RECT l = {0, 0, fit.left, ch}, r = {fit.right, 0, cw, ch}, t = {0, 0, cw, fit.top}, b = {0, fit.bottom, cw, ch};
            HBRUSH k = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(dc, &l, k), FillRect(dc, &r, k), FillRect(dc, &t, k), FillRect(dc, &b, k);
            ReleaseDC(g_hwnd, dc);
        }
    }
    const HRESULT r = g_Present(d, src, dst, wnd, dirty);
    hk_link()->frames++;
    if (g_hz > 0) {   // no more than g_hz a second, on a steady grid
        static LARGE_INTEGER f, next;
        LARGE_INTEGER now;
        if (!f.QuadPart) QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&now);
        const LONGLONG period = (LONGLONG)(f.QuadPart / g_hz);
        if (!next.QuadPart || now.QuadPart > next.QuadPart + period) next.QuadPart = now.QuadPart;
        next.QuadPart += period;
        for (;;) {
            QueryPerformanceCounter(&now);
            const LONGLONG left = next.QuadPart - now.QuadPart;
            if (left <= 0) break;
            const LONGLONG ms = left * 1000 / f.QuadPart;
            if (ms >= 2) Sleep((DWORD)(ms - 1));
            else YieldProcessor();
        }
    }
    return r;
}

static HRESULT WINAPI my_Reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *pp) {
    windowed(pp);
    return g_Reset(d, pp);
}

static HRESULT WINAPI my_CreateDevice(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                      D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **dev) {
    hk_log("hiraku-pc: the game's Direct3D: %ux%u, %s, %u Hz, interval %#x", pp->BackBufferWidth, pp->BackBufferHeight,
           pp->Windowed ? "windowed" : "full screen", pp->FullScreen_RefreshRateInHz, pp->PresentationInterval);
    windowed(pp);
    place_window(pp->hDeviceWindow ? pp->hDeviceWindow : focus);
    const HRESULT r = g_CreateDevice(self, adapter, type, focus, flags, pp, dev);
    if (r == D3D_OK && dev && *dev) {
        void **vt = *(void ***)*dev;
        void *was = patch_slot(&vt[16], (void *)my_Reset);
        if (was) g_Reset = (Reset_t)was;
        was = patch_slot(&vt[17], (void *)my_Present);
        if (was) g_Present = (Present_t)was;
    } else {
        hk_log("hiraku-pc: CreateDevice failed (%#lx)", (unsigned long)r);
    }
    return r;
}

typedef IDirect3D9 *(WINAPI *Direct3DCreate9_t)(UINT);

static IDirect3D9 *WINAPI my_Direct3DCreate9(UINT sdk) {
    static Direct3DCreate9_t real;
    if (!real) real = (Direct3DCreate9_t)(void *)GetProcAddress(LoadLibraryA("d3d9.dll"), "Direct3DCreate9");
    IDirect3D9 *d = real ? real(sdk) : NULL;
    if (d) {
        void *was = patch_slot(&(*(void ***)d)[16], (void *)my_CreateDevice);
        if (was) g_CreateDevice = (CreateDevice_t)was;
    }
    return d;
}

// The game showing its window again, once HIRAKU's shows the pictures: it stays hidden
static BOOL WINAPI my_ShowWindow(HWND w, int how) {
    if (host_shows() && (w == g_hwnd || !g_hwnd)) how = SW_HIDE;
    return ShowWindow(w, how);
}

// --- hooking a module's imports

static HMODULE g_self;

static void *hook_for(const char *dll, const char *fn) {
    if (!_stricmp(dll, "ws2_32.dll") || !_stricmp(dll, "wsock32.dll")) return hk_net_hook(fn);   // net.c
    if (!_stricmp(dll, "iphlpapi.dll")) return hk_net_card_hook(fn);
    if (!_stricmp(dll, "libsocket.dll") || !_stricmp(dll, "libavs-win32.dll")) return hk_vsock_hook(fn);   // vsock.c
    if (!_stricmp(dll, "libcomm.dll")) return hk_comm_hook(fn);
    static const struct { const char *dll, *fn; void *to; } hooks[] = {
        {"kernel32.dll", "CreateFileA", (void *)my_CreateFileA},
        {"kernel32.dll", "CreateDirectoryA", (void *)my_CreateDirectoryA},
        {"kernel32.dll", "FindFirstFileA", (void *)my_FindFirstFileA},
        {"kernel32.dll", "GetFileAttributesA", (void *)my_GetFileAttributesA},
        {"kernel32.dll", "DeleteFileA", (void *)my_DeleteFileA},
        {"kernel32.dll", "RemoveDirectoryA", (void *)my_RemoveDirectoryA},
        {"kernel32.dll", "MoveFileA", (void *)my_MoveFileA},
        {"kernel32.dll", "GetFullPathNameA", (void *)my_GetFullPathNameA},
        {"d3d9.dll", "Direct3DCreate9", (void *)my_Direct3DCreate9},
        {"user32.dll", "ShowWindow", (void *)my_ShowWindow},
        {"dsound.dll", "DirectSoundCreate8", (void *)hk_DirectSoundCreate8},
        {"dsound.dll", "DirectSoundCreate", (void *)hk_DirectSoundCreate},
    };
    for (size_t i = 0; i < sizeof hooks / sizeof *hooks; i++)
        if (!_stricmp(dll, hooks[i].dll) && !strcmp(fn, hooks[i].fn)) {
            if (hooks[i].to == (void *)my_Direct3DCreate9 && !g_win_w && !g_win_full && g_hz <= 0 && !host_shows()) return NULL;
            if (hooks[i].dll[0] == 'k' && !g_nmap) return NULL;
            // the window and the sound are HIRAKU's only when its window shows the game
            if ((!_stricmp(hooks[i].dll, "user32.dll") || !_stricmp(hooks[i].dll, "dsound.dll")) && !host_shows()) return NULL;
            return hooks[i].to;
        }
    return NULL;
}

// The calls the hooks take that games import by number
static const char *by_ordinal(const char *dll, unsigned ord) {
    static const struct { const char *dll; unsigned ord; const char *fn; } names[] = {
        {"dsound.dll", 1, "DirectSoundCreate"}, {"dsound.dll", 11, "DirectSoundCreate8"},
        {"ws2_32.dll", 2, "bind"},              {"ws2_32.dll", 3, "closesocket"},       {"ws2_32.dll", 4, "connect"},
        {"ws2_32.dll", 17, "recvfrom"},         {"ws2_32.dll", 20, "sendto"},
        {"ws2_32.dll", 23, "socket"},           {"ws2_32.dll", 52, "gethostbyname"},
        {"wsock32.dll", 2, "bind"},             {"wsock32.dll", 3, "closesocket"},      {"wsock32.dll", 4, "connect"},
        {"wsock32.dll", 17, "recvfrom"},        {"wsock32.dll", 20, "sendto"},
        {"wsock32.dll", 23, "socket"},          {"wsock32.dll", 52, "gethostbyname"},
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (names[i].ord == ord && !_stricmp(dll, names[i].dll)) return names[i].fn;
    return NULL;
}

static void patch_imports(HMODULE m) {
    char *base = (char *)m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++) {
        const char *dll = base + imp->Name;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *addrs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            const char *fn;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {   // by number: dsound.dll's and ws2_32.dll's (pop'n 15)
                fn = by_ordinal(dll, (unsigned)IMAGE_ORDINAL(names->u1.Ordinal));
                if (!fn) continue;
            } else {
                fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            }
            void *to = hook_for(dll, fn);
            if (to) patch_slot((void **)&addrs->u1.Function, to);
        }
    }
}

// The game's own modules: the program and the DLLs in its folder (not Windows', not ours)
static void patch_game_modules(void) {
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char *slash = strrchr(exe, '\\');
    const size_t dirlen = slash ? (size_t)(slash - exe) : 0;
    HMODULE mods[256];
    DWORD bytes = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &bytes)) return;
    for (DWORD i = 0; i < bytes / sizeof *mods; i++) {
        char p[MAX_PATH];
        if (mods[i] == g_self || !GetModuleFileNameA(mods[i], p, MAX_PATH)) continue;
        if (mods[i] != GetModuleHandleA(NULL) && (strlen(p) <= dirlen || _strnicmp(p, exe, dirlen) || p[dirlen] != '\\'))
            continue;
        patch_imports(mods[i]);
    }
}

// Modules the game loads later (LoadLibrary) are its own too
static HMODULE WINAPI my_LoadLibraryA(LPCSTR p) {
    HMODULE m = LoadLibraryA(p);
    if (m) patch_game_modules();
    return m;
}

void hk_cabinet_start(void) {
    if (GetEnvironmentVariableA("HK_PC_STARTED", NULL, 0)) return;   // the other DLL did
    SetEnvironmentVariableA("HK_PC_STARTED", "1");
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)hk_cabinet_start, &g_self);
    read_map();
    const char *w = getenv("HK_PC_WINDOW");
    if (w && !strcmp(w, "full")) g_win_full = 1;
    else if (w && sscanf(w, "%dx%d", &g_win_w, &g_win_h) != 2) g_win_w = g_win_h = 0;
    const char *hz = getenv("HK_PC_HZ");
    if (hz && *hz) g_hz = atof(hz);
    hk_link();
    patch_game_modules();
    // LoadLibraryA too, in the program, so modules loaded later get the same
    HMODULE exe = GetModuleHandleA(NULL);
    char *base = (char *)exe;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); dir->VirtualAddress && imp->Name; imp++) {
        if (_stricmp(base + imp->Name, "kernel32.dll")) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *addrs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++)
            if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal) &&
                !strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, "LoadLibraryA"))
                patch_slot((void **)&addrs->u1.Function, (void *)my_LoadLibraryA);
    }
    hk_log("hiraku-pc: the cabinet is HIRAKU's (%s)", g_link == &g_alone ? "keyboard only" : "linked to hiraku-pc");
}
