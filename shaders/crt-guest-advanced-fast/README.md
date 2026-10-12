# crt-guest-advanced-fast

guest(r)'s CRT shader, as in libretro's slang-shaders (`crt/crt-guest-advanced-fast.slangp`),
ported to OpenGL 3.3 for HIRAKU's window (`--crt`, or the launcher's Settings > Picture).

- Upstream: <https://github.com/libretro/slang-shaders>, `crt/shaders/guest/fast/` and
  `crt/shaders/guest/advanced/lut/`. Ported from the revision of 2026-03-25 (commit 77c52ce).
- Copyright (C) 2018-2025 guest(r), with Dr. Venom. GNU General Public License, version 2
  or (at your option) any later version; HIRAKU uses it under version 3. Each file keeps its
  original notice.

## What changed in the port

- Vulkan GLSL (push constants, UBO, `layout(set, binding)`) → GLSL 3.30 core with plain
  uniforms. HIRAKU's `runtime/host/crt.cpp` puts the same few lines in front of each pass:
  `SourceSize`, `OriginalSize`, `OutputSize`, `FrameCount`, `COMPAT_TEXTURE` and `plant()`.
- Scalar swizzles (`1.0.xxx`) written as constructors (`vec3(1.0)`); the global
  initialisers that read parameters (`FINE_BLOOM`, `invsqrsigma`) moved into `main`;
  `gl_FragCoord` redefinitions renamed (`FRAGCOORD`). The mask table index is clamped at 0
  (`shadowMask = -1` read `mwidths[-1]`).
- The preset's first two passes (`stock.slang`, copies of the picture) are the picture's
  own texture: it is also `StockPass`.
- Everything else is guest's code, values and pass layout: `#pragma parameter` lines give
  the defaults (read by `crt.cpp`), and the sizes, fp16 targets and bilinear sampling with a
  black border are the preset's.

`example.params` lists the parameters worth changing. All of them are in the
`#pragma parameter` lines of the `.glsl` files.
