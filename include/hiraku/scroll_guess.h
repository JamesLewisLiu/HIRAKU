// SPDX-License-Identifier: GPL-3.0-or-later
// In-between pictures (smooth motion, docs/SMOOTH.md): where a moving object is a fraction of a
// frame on. The cores match each object with the frame before's and know its last move; carried on
// by that move, notes judder, because a game scrolls at a steady speed in whole pixels: pop'n music
// 8's notes step 2, 3, 3, 2, 3, 3 pixels a frame (2.67 on average). After a 3, the in-between
// pictures carry a note 3 on, the next frame moves it 2, and it steps back a pixel; at 240 Hz about
// one refresh in twenty went back, and the notes were off a steady line by 0.57 px (sd).
//
// So the move most of a frame's moving objects share is taken as the scroll (the notes, the bar
// lines: they move together), and its position kept over the last frames. What moves with it
// (within a pixel of its move, the same way) is carried along the steady line through those
// positions: the average speed, from where the line puts this frame, which can be up to a pixel
// before the frame's own position (the first refresh of a frame then draws it a little back).
// Simulated at 240 and 144 Hz for 1, 2.67, 3.5 and 4.6 pixels a frame: 0.29 px off the line (the
// whole pixels' own rounding), no step back. Everything else keeps its last move.
//
// Header-only: the Firebeat GCU (runtime/dev/k057714.cpp), DJ Main (runtime/djmain/board.cpp) and
// the PlayStation GPU of Twinkle and System 573 (third_party/mame/hiraku-edits.py) use it.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

namespace hk {

class ScrollGuess {
public:
    static constexpr int N = 6;   // frames the line goes through

    // A frame's objects' last moves (those that moved), before its in-between pictures; `frame`
    // tells a new frame from another picture of the same one (the scroll is counted once a frame)
    void frame(uint64_t frame, const std::vector<std::pair<int, int>> &moves) {
        if (frame == m_frame) return;
        m_frame = frame;
        std::map<std::pair<int, int>, int> count;
        for (const auto &m : moves)
            if (m.first || m.second) count[m]++;
        std::pair<int, int> common{0, 0};
        int most = 0;
        for (const auto &[m, n] : count)
            if (n > most) most = n, common = m;
        m_common = common;
        if (most < 2) {   // nothing scrolls (one object moving is not a scroll): start again
            m_pos.clear();
            return;
        }
        if (m_pos.size() >= 2) {   // a new speed: the line starts again
            const double vx = (m_pos.back().first - m_pos.front().first) / double(m_pos.size() - 1);
            const double vy = (m_pos.back().second - m_pos.front().second) / double(m_pos.size() - 1);
            if (std::fabs(common.first - vx) > 1.5 || std::fabs(common.second - vy) > 1.5) m_pos.clear();
        }
        if (m_pos.empty()) m_pos.push_back({0, 0});   // the frame before
        m_pos.push_back({m_pos.back().first + common.first, m_pos.back().second + common.second});
        if ((int)m_pos.size() > N + 1) m_pos.erase(m_pos.begin());
    }

    // Where an object whose last move was (dx, dy) goes, `t` of a frame on (t from 0; a frame shown
    // for two vblanks: the fraction of its move): (ox, oy) from its place in the frame
    void offset(int dx, int dy, double t, int &ox, int &oy) const {
        const bool scrolls = (dx || dy) && m_pos.size() >= 3 && std::abs(dx - m_common.first) <= 1 &&
                             std::abs(dy - m_common.second) <= 1 && dx * m_common.first + dy * m_common.second > 0;
        if (!scrolls) {
            ox = (int)std::lround(dx * t), oy = (int)std::lround(dy * t);
            return;
        }
        // the line through the kept positions: their average speed, through their mean
        const int n = (int)m_pos.size();
        const double vx = (m_pos.back().first - m_pos.front().first) / double(n - 1);
        const double vy = (m_pos.back().second - m_pos.front().second) / double(n - 1);
        double lx = 0, ly = 0;   // where the line puts this frame: the mean of each position carried on to it
        for (int i = 0; i < n; i++) lx += m_pos[i].first + (n - 1 - i) * vx, ly += m_pos[i].second + (n - 1 - i) * vy;
        lx /= n, ly /= n;
        ox = (int)std::lround(lx - m_pos.back().first + vx * t), oy = (int)std::lround(ly - m_pos.back().second + vy * t);
    }

private:
    uint64_t m_frame = UINT64_MAX;
    std::pair<int, int> m_common{0, 0};
    std::vector<std::pair<int, int>> m_pos;   // the scroll's position, frame by frame (the last: this frame)
};

}  // namespace hk
