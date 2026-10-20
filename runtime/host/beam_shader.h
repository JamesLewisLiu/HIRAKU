// The CRT beam simulator's pass (runtime/host/picture.h, set_beam), ported from the Blur Busters
// CRT Beam Simulator (third_party/crt-beam-simulator/crt-simulator.glsl), whose notice follows.
// HIRAKU's changes: the source frames are the game's own two latest pictures (the one the beam is
// drawing, and the one before it fading), not a demo's scrolled copies; the beam's place comes from
// the refresh's real moment in the game's frame (pacing::phase), not a refresh counter, so any
// display/game ratio works without the LCD-inversion slew; the frame the beam starts after this
// one isn't there yet, so a refresh that crosses into it draws this one's again there (the light
// stays the same); the scan can run left to right in each of several tubes (monitors turned on
// their side).
//
// MIT License
//
// Copyright 2024 Mark Rejhon (@BlurBusters) & Timothy Lottes (@NOTimothyLottes)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the “Software”), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once

namespace hk::host::picture {

// The quad (0..1) over the viewport, not turned over: uv (0, 0) is the picture's bottom left, as
// the pictures kept for the beam are (drawn as the window is)
inline const char *const BEAM_VS = R"(#version 330
layout(location = 0) in vec4 Position;
out vec2 uv;
void main() { uv = Position.xy; gl_Position = vec4(2.0 * Position.xy - 1.0, 0.0, 1.0); }
)";

inline const char *const BEAM_FS = R"(#version 330
in vec2 uv;
out vec4 FragColor;
uniform sampler2D Now;       // the frame the beam is drawing (the game's latest)
uniform sampler2D Before;    // the frame before it, fading behind the beam
uniform float Phase;         // where the beam is when this refresh starts: 0 top, 1 the next frame's top
uniform float FramesPerHz;   // display refreshes in a frame of the game (any ratio: 2.4 at 144 Hz)
uniform float Gamma;         // the display's gamma (the light is spread out in linear terms)
uniform float Gain;          // brightness against motion blur (GAIN_VS_BLUR)
uniform int Scan;            // 1: top to bottom; 3: left to right in each tube
uniform float Tubes;         // tubes side by side (Scan 3)

// sRGB with the display's gamma, as the original (whose clamp(x, lo, hi) has lo above hi for most
// values, which GLSL leaves undefined: written here as the min(max()) drivers do)
float linear2srgb(float c) {
    vec3 j = vec3(0.0031308 * 12.92, 12.92, 1.0 / Gamma);
    vec2 k = vec2(1.055, -0.055);
    return min(max(j.x, c * j.y), pow(c, j.z) * k.x + k.y);
}
float srgb2linear(float c) {
    vec3 j = vec3(0.04045, 1.0 / 12.92, Gamma);
    vec2 k = vec2(1.0 / 1.055, 0.055 / 1.055);
    return c > j.x ? pow(c * k.x + k.y, j.z) : c * j.y;
}
vec3 linear(vec3 c) { return vec3(srgb2linear(c.r), srgb2linear(c.g), srgb2linear(c.b)); }

#define OVERLAP(a0, a1, b0, b1) max(0.0, min(a1, b1) - max(a0, b0))

void main() {
    vec3 before = linear(texture(Before, uv).rgb) * FramesPerHz * Gain;   // photon budgets, in refreshes
    vec3 now = linear(texture(Now, uv).rgb) * FramesPerHz * Gain;
    float tubePos = Scan == 3 ? fract(uv.x * Tubes) : 1.0 - uv.y;
    float tubeFrame = tubePos * FramesPerHz;   // when the beam reaches this pixel, in refreshes
    float fStart = Phase * FramesPerHz, fEnd = fStart + 1.0;   // this refresh
    vec3 result;
    for (int ch = 0; ch < 3; ch++) {
        // each frame's light starts where the beam passes and lasts as long as its budget: the frame
        // before from a frame ago, this one from now, and this one again where the beam has gone on
        // into the next frame (not drawn yet)
        float b = before[ch], n = now[ch];
        result[ch] = OVERLAP(tubeFrame - FramesPerHz, tubeFrame - FramesPerHz + b, fStart, fEnd) +
                     OVERLAP(tubeFrame, tubeFrame + n, fStart, fEnd) +
                     OVERLAP(tubeFrame + FramesPerHz, tubeFrame + FramesPerHz + n, fStart, fEnd);
    }
    FragColor = vec4(linear2srgb(result.r), linear2srgb(result.g), linear2srgb(result.b), 1.0);
}
)";

}  // namespace hk::host::picture
