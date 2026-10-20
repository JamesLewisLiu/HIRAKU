// SPDX-License-Identifier: GPL-3.0-or-later
// In-between pictures' guess (include/hiraku/scroll_guess.h): notes scrolling a steady speed in
// whole pixels (pop'n music 8's 2, 3, 3...; 1; 3.5; 4.6 a frame), shown on 240 and 144 Hz
// displays (4.1 and 2.46 refreshes a frame) as the window does, each refresh the frame carried on
// to its moment. They should stay on a steady line within the whole pixels' rounding and never
// step back; the last-move guess they replace does both (checked here too, so the test sees it).
#include <cmath>
#include <cstdio>
#include <vector>

#include "hiraku/scroll_guess.h"

namespace {

struct Result { double sd; int back; };

// a pattern of steps a frame, seen on a display with `fph` refreshes a frame; `smooth`: the guess,
// else the last move. Three notes scroll together (the scroll needs two at least)
Result run(const std::vector<int> &pattern, double fph, bool smooth) {
    std::vector<int> pos{0};
    for (int i = 0; i < 300; i++) pos.push_back(pos.back() + pattern[i % pattern.size()]);
    hk::ScrollGuess g;
    std::vector<double> ts, ys;
    for (long j = 0;; j++) {
        const double t = j / fph;
        const int f = (int)std::floor(t);
        if (f >= 290) break;
        if (f < 1) continue;
        const int dy = pos[f] - pos[f - 1];
        g.frame((uint64_t)f, {{0, dy}, {0, dy}, {0, dy}});
        int ox = 0, oy = 0;
        if (smooth) g.offset(0, dy, t - f, ox, oy);
        else oy = (int)std::lround(dy * (t - f));
        if (f >= 10) ts.push_back(t), ys.push_back(pos[f] + oy);
    }
    // off the straight line through them (least squares)
    const double n = (double)ts.size();
    double st = 0, sy = 0, stt = 0, sty = 0;
    for (size_t i = 0; i < ts.size(); i++) st += ts[i], sy += ys[i], stt += ts[i] * ts[i], sty += ts[i] * ys[i];
    const double b = (n * sty - st * sy) / (n * stt - st * st), a = (sy - b * st) / n;
    double e = 0;
    int back = 0;
    for (size_t i = 0; i < ts.size(); i++) {
        e += std::pow(ys[i] - (a + b * ts[i]), 2);
        if (i && ys[i] < ys[i - 1]) back++;
    }
    return {std::sqrt(e / n), back};
}

}  // namespace

int main() {
    int fail = 0;
    const std::vector<std::vector<int>> patterns = {{2, 3, 3}, {1}, {3, 4}, {4, 5, 5, 4, 5}};
    for (const auto &p : patterns)
        for (double fph : {240 / 58.49, 144 / 58.49}) {
            const Result s = run(p, fph, true), l = run(p, fph, false);
            printf("steps %d%s at %.2f refreshes a frame: off the line %.2f px, %d back (the last move: %.2f px, %d back)\n",
                   p[0], p.size() > 1 ? "..." : "", fph, s.sd, s.back, l.sd, l.back);
            if (s.back || s.sd > 0.42) fail++;
        }
    // pop'n music 8's demo play: falling notes step 2 or 3 in the same frame (4 and 3 of them),
    // while 5 objects (a lyric) rise 3: the notes are the scroll, the rising ones go by their own move
    {
        hk::ScrollGuess g;
        for (uint64_t f = 1; f < 10; f++)
            g.frame(f, {{0, 2}, {0, 2}, {0, 2}, {0, 2}, {0, 3}, {0, 3}, {0, 3}, {0, -3}, {0, -3}, {0, -3}, {0, -3}, {0, -3}});
        int ox, oy, rx, ry;
        g.offset(0, 2, 0.5, ox, oy);
        g.offset(0, -3, 0.5, rx, ry);
        printf("mixed steps under a rising group: a note goes %d (the line: 2.43 a frame), a rising object %d\n", oy, ry);
        if (oy != 1 || ry != -2) fail++;   // 0.5 x 2.43 -> 1; -1.5 -> -2
    }
    if (fail) {
        fprintf(stderr, "%d failed\n", fail);
        return 1;
    }
    return 0;
}
