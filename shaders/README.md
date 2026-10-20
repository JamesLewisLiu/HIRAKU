# The picture's shaders

Two libretro slang shader presets, converted ahead of time to the GLSL 3.30 that HIRAKU's
window runs (`runtime/host/picture.cpp`):

| Mode  | Folder                     | Shader | Author | Licence |
|-------|----------------------------|--------|--------|---------|
| light | `crt-guest-advanced-fast/` | crt-guest-advanced-fast: scanlines, mask, glow; cheap | guest(r), with Dr. Venom | GPL-2.0-or-later |
| full  | `crt-royale/`              | crt-royale: beam profiles, phosphor-mask textures, bloom, halation, curvature | TroggleMonkey | GPL-2.0-or-later |

HIRAKU uses them under the GPL, version 3. Their sources, as read for the conversion, are in
`third_party/slang-shaders/` with their notices. They come from
<https://github.com/libretro/slang-shaders>, commit e1d7563 (2026-10-08).

## Each folder

- `passN.vert`, `passN.frag` are generated: one pass each, converted from the preset's `.slang`.
- `preset.slangp` is the preset: the sizes, formats, filters, aliases and textures of the
  passes. The original's `#pragma name` and `#pragma format` are written as `aliasN` / `formatN`.
- `parameters.txt` lists every parameter: name, default, minimum, maximum, step and label.
  The launcher's sliders come from it.
- `lut/` holds the preset's textures (masks, colour LUTs).
- `hiraku.params` holds HIRAKU's own defaults, read over the shader's. Both shaders get the
  same change: 480-line pictures (Firebeat) are drawn progressive, with every line a
  scanline, not as 480i.

## Regenerating

```bash
git clone https://github.com/libretro/slang-shaders
tools/slang2glsl.py slang-shaders/crt/crt-royale.slangp shaders/crt-royale --vendor third_party/slang-shaders
```

This needs glslangValidator and SPIRV-Cross (`spirv-cross` on the PATH, or `SPIRV_CROSS=`). It
does what RetroArch does when it loads a preset: it expands the `#include`s, takes out the
pragmas and splits the stages. Then each stage goes to SPIR-V with glslang and back to
GLSL 3.30 with SPIRV-Cross.

The same engine runs other slang presets: frame history (`OriginalHistoryN`) and a pass's output a
frame ago (`PassFeedbackN`, `<alias>Feedback`) included, with `FrameCount` the game's frame number
(smooth motion's extra refreshes of a frame keep it; `tests/picture` checks all three). For
crt-guest-advanced-fast, its output was checked against a hand port of the shader: the
pictures are identical, pixel for pixel.
