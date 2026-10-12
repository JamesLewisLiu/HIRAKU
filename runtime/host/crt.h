// SPDX-License-Identifier: GPL-3.0-or-later
// The CRT picture: guest(r)'s crt-guest-advanced-fast (libretro slang-shaders), ported to
// OpenGL 3.3 (shaders/crt-guest-advanced-fast/). The preset's passes, sizes and formats:
//
//   frame (W x H, the game's picture: also "StockPass")
//   pre-shaders    W x H        8 bit   PrePass        colour, vignette in alpha
//   linearize      W x H        fp16    LinearizePass  gamma in, interlacing
//   pass1          viewport x H fp16    Pass1          horizontal filter
//   bloom_h        640 x H      fp16
//   bloom_v        640 x 480    fp16    BloomPass
//   pass2          viewport     fp16                   scanlines
//   deconvergence  viewport     window                 mask, bloom, glow, curvature
//
// Every pass output is sampled bilinear with a black border, as RetroArch does for the
// preset. The parameters are the shaders' #pragma parameter defaults, then a file of
// `name = value` lines (a RetroArch preset with its parameter lines works as is).
#pragma once

#include <cstdint>
#include <string>

namespace hk::host::crt {

// Needs a current OpenGL 3.3 core context. dir holds the .glsl files (and lut/);
// params is "" or the parameters file. False (with the reason printed) if it can't run.
bool init(const std::string &dir, const std::string &params);
// Read the parameters file again (Shift+F10 in the window).
void reload_params();
// Draw a w x h XRGB8888 frame into the rectangle (x, y, vw, vh), from the top left, of a
// win_w x win_h default framebuffer; the rest is black. Not `crt`: sharp pixels instead.
void draw(const uint32_t *rgb, int w, int h, int x, int y, int vw, int vh, int win_w, int win_h, bool crt);
void shutdown();

// HK_CRT_SNAP=FRAME,FILE.ppm writes the window's picture at that frame (checking a port).

}  // namespace hk::host::crt
