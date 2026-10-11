// SPDX-License-Identifier: GPL-3.0-or-later
#include "spectrum.h"

#include <algorithm>
#include <cmath>

namespace hk::spectrum {

namespace {
constexpr int N = 512, BARS = 6;
constexpr double NOTCHES[] = {95, 240, 600, 1500, 3400, 8200, 18000};
float g_buf[2][N];
int g_fill = 0;
int g_bars[2][BARS];

// MAME takes, per band, the first FFT bin whose frequency falls inside it.
int band_bin(int band, int sr) {
    int cur = 0;
    for (int i = 0; i <= N / 2; i++) {
        double f = (double)i / N * sr;
        if (f < NOTCHES[cur]) continue;
        if (f > NOTCHES[cur + 1]) cur++;
        if (cur >= 7) break;
        if (cur == band && f >= NOTCHES[cur] && f < NOTCHES[cur + 1]) return i;
    }
    return -1;
}

void analyse(int sr) {
    for (int ch = 0; ch < 2; ch++)
        for (int b = 0; b < BARS; b++) {
            int k = band_bin(b, sr);
            double mag = 0;
            if (k >= 0) {
                double re = 0, im = 0;
                for (int n = 0; n < N; n++) {
                    double ph = -2.0 * M_PI * k * n / N;
                    re += g_buf[ch][n] * std::cos(ph);
                    im += g_buf[ch][n] * std::sin(ph);
                }
                mag = std::sqrt(re * re + im * im) / N;
            }
            double val = mag > 0 ? std::log10(mag * 4096) * 20 : 0;
            g_bars[ch][b] = (int)std::min(255.0, std::max(0.0, val));
        }
}
}  // namespace

void push(const float *lr, int frames, int sr) {
    for (int i = 0; i < frames; i++) {
        g_buf[0][g_fill] = lr[2 * i];
        g_buf[1][g_fill] = lr[2 * i + 1];
        if (++g_fill == N) {
            analyse(sr);
            g_fill = 0;
        }
    }
}

uint8_t read(uint32_t offset) {
    int ch = offset >= 0x40;
    int notch = 6 - (((offset >> 2) & 0x0f) >> 1);
    bool upper = offset & 4;
    int val = (notch >= 0 && notch < BARS) ? g_bars[ch][notch] : 0;
    return (uint8_t)((upper ? (val >> 8) : val) & 0xff);
}

}  // namespace hk::spectrum
