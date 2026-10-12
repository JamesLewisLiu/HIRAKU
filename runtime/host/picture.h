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
// OriginalSize, OutputSize, FinalViewportSize, <alias>Size, FrameCount, parameters).
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
Mode draw(const uint32_t *rgb, int w, int h, int x, int y, int vw, int vh, int win_w, int win_h, Mode m);
void shutdown();

// HK_PICTURE_SNAP=FRAME,FILE.ppm writes the window's picture at that frame (checking a shader).

}  // namespace hk::host::picture
