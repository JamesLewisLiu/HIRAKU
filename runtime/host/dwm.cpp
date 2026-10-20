// SPDX-License-Identifier: GPL-3.0-or-later
#include "dwm.h"

#include <windows.h>
#include <dwmapi.h>

#include "../core/prof.h"

namespace hk::host::dwm {

bool vblank(int64_t &last_ns, int64_t &period_ns) {
    using Timing = HRESULT(WINAPI *)(HWND, DWM_TIMING_INFO *);
    static const Timing timing = [] {
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        return m ? reinterpret_cast<Timing>(reinterpret_cast<void *>(GetProcAddress(m, "DwmGetCompositionTimingInfo")))
                 : nullptr;
    }();
    if (!timing) return false;
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof ti;
    if (timing(nullptr, &ti) != S_OK || !ti.qpcRefreshPeriod || !ti.qpcVBlank) return false;   // the whole desktop's (8.1 on)
    LARGE_INTEGER f, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&now);
    const int64_t now_ns = (int64_t)prof::now_ns();
    const double ns_per_count = 1e9 / (double)f.QuadPart;
    last_ns = now_ns - (int64_t)((double)(now.QuadPart - (int64_t)ti.qpcVBlank) * ns_per_count);
    period_ns = (int64_t)((double)ti.qpcRefreshPeriod * ns_per_count + 0.5);
    return period_ns > 0;
}

}  // namespace hk::host::dwm
