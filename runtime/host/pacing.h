// SPDX-License-Identifier: GPL-3.0-or-later
// Display pacing: smooth frames on a fixed-refresh display.
//
// The games set their own frame rates (59.94 Hz beatmania III, 58.49 Hz pop'n), which a 60, 120 or
// 240 Hz display can't divide evenly: shown as they come, some frames stay up a refresh longer than
// others and the scrolling judders. When the display's rate is within a few percent of a whole
// multiple of the game's, display pacing runs the machine on the display's refresh grid instead:
// every frame is shown for exactly k refreshes, and the machine runs at the display's rate (pop'n
// 2.6 % fast at 60 Hz, its sound resampled to match). The emulated machine is unchanged; only the
// mapping of its time to real time is.
//
// Each frame: the machine runs (a "burst"), the window draws it, waits until the frame's commit
// time, presents, and waits until the next burst should start. The commit time is a "lead" before
// a vblank of the grid; the burst starts a "budget" before the commit (the slow end of recent
// burst + draw times). The sound's playback rate follows the game's speed, steered to keep its
// queue at the --audio-ms target.
//
// On Wayland:
// - presentation-time gives the exact vblank times and refresh period, and when each frame reached
//   the screen. The lead is found from that: the commit moves later until a frame misses the
//   compositor's deadline, then steps back with a margin (pacing.cpp, presented()).
// - fifo-v1, when the compositor has it (KWin 6.7 does), holds each frame for its k refreshes:
//   the frame's commit and k-1 empty ones, each waiting for the one before. Without it, KWin
//   showed 1-10 % of frames a refresh early or late (its deadline wanders with its render-time
//   estimate); with it, 0.03 % at 144 Hz (2 refreshes per frame) and 0.17 % at 240 Hz (4).
// Elsewhere (X11) the grid runs on the display mode's refresh rate, with an unknown phase.
//
// Off when the display doesn't fit (144 Hz for 58.49 Hz games), with fast-forward, or with
// --display-sync off (for variable-refresh displays, which follow the game at its own rate).
// Testing: HK_DISPLAY_SYNC_SKEW (the most the speed may change, 0.03), HK_PACING_DEBUG (a line per
// presented frame), HK_PACING_NO_FIFO, HK_PACING_SWEEP (keep moving the commit, to map the deadline).
#pragma once

#include <cstdint>
#include <string>

struct SDL_Window;

namespace hk::host::pacing {

void init(SDL_Window *win, bool enabled);
void shutdown();
// The window moved to another display, or its mode changed: re-read the refresh rate.
void display_changed();
// A new frame, `period` seconds of emulated time after the previous one: decide whether display
// pacing is on for it. Call first thing in the frame (before drawing).
bool frame(double period);
bool active();
// Emulated seconds per real second (1 when off): the sound plays at this rate.
double speed();
// Display pacing: wait for the frame's commit time, then call commit() right before the swap and
// committed() right after it.
void wait_commit();
void commit();
void committed();
// Display pacing, after the swap: wait until the next frame's burst should start.
void wait_next();
// Read the compositor's presentation events (after SDL's event pump).
void pump();
// Display sync off: ask for this frame's presentation feedback anyway (right before the swap), to
// count the frames that went to the screen directly (scanout_describe).
void observe();
std::string describe();
// How many frames were shown, and how many of them straight from HIRAKU's buffer (direct scanout,
// not composited: a fullscreen window with nothing over it); "" if the compositor doesn't say.
std::string scanout_describe();

}  // namespace hk::host::pacing
