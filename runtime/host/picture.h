// SPDX-License-Identifier: GPL-3.0-or-later
// The picture in the window: sharp pixels, or a CRT through a libretro slang shader preset
// converted to GLSL 3.30 by tools/slang2glsl.py (shaders/<name>/: preset.slangp, passN.vert,
// passN.frag, parameters.txt, lut/). Two of them:
//
//   light  crt-guest-advanced-fast (guest(r)): scanlines, mask, glow; cheap
//   full   crt-royale (TroggleMonkey): beam profiles, real phosphor masks, bloom, halation
//
// The engine does what RetroArch's filter chain does: pass sizes (source, viewport,
// absolute), 8-bit / sRGB / fp16 targets, each output sampled with the next pass's filter,
// wrap mode and mipmaps, the preset's textures, and the semantics (MVP, SourceSize,
// OriginalSize, OutputSize, FinalViewportSize, <alias>Size, FrameCount, parameters), the frames
// before this one (OriginalHistoryN) and a pass's output a frame ago (PassFeedbackN,
// <alias>Feedback; black at first, as in RetroArch).
//
// Parameters: parameters.txt's defaults, then shaders/<name>/hiraku.params (HIRAKU's own
// defaults), then <dir>/<name>.params for each config dir, in order (the launcher's
// global settings, then the game's). Those files are `name = value` lines (a RetroArch
// preset works); they are read again when they change, so a running game follows them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hk::host::picture {

enum class Mode { off, light, full };
const char *mode_name(Mode m);            // "off", "light", "full"
bool parse_mode(const std::string &s, Mode &m);
const char *mode_label(Mode m);           // "sharp pixels", "CRT light (crt-guest-advanced-fast)", ...

// Needs a current OpenGL 3.3 core context. The presets are loaded the first time they are drawn.
bool init(const std::string &shaders_dir, const std::vector<std::string> &config_dirs);
// Load a mode's preset now (compiling its shaders takes up to a second the first time; the
// draw that needs it otherwise does). False if it can't run here.
bool prepare(Mode m);
// Draw a w x h XRGB8888 frame into the rectangle (x, y, vw, vh), from the top left, of a
// win_w x win_h default framebuffer; the rest is black. Returns the mode drawn: `m`, or off
// when its preset can't run here.
// view: which window (0, or 1 for a second one: its own frame texture and preset state)
// frame: the game's frame number, the shaders' FrameCount. The same number as the last draw's is a
// refresh of that frame (smooth motion's in-between pictures): its picture is replaced, but
// FrameCount, the frames before it and the passes' output a frame ago stay as they were, so a
// shader's interlaced fields and afterglow keep the game's pace on a faster display.
// UINT64_MAX: every draw a new frame, counted from the first.
Mode draw(const uint32_t *rgb, int w, int h, int x, int y, int vw, int vh, int win_w, int win_h, Mode m, int view = 0,
          uint64_t frame = UINT64_MAX);
// After draw: an ARGB picture (w x h, with its own alpha, times `alpha`) blended over the window,
// drawn at (x, y, dw, dh) from the top left, its pixels sharp (host/notice.h's banner)
void overlay(const uint32_t *argb, int w, int h, int x, int y, int dw, int dh, float alpha, int win_w, int win_h);
void shutdown();

// HK_PICTURE_SNAP=FRAME,FILE.ppm writes the window's picture at that frame (checking a shader).

}  // namespace hk::host::picture
