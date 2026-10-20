# The picture's shaders

Three libretro slang shader presets, converted ahead of time to the GLSL 3.30 that HIRAKU's
window runs (`runtime/host/picture.cpp`):

| Mode  | Folder                     | Shader | Author | Licence |
|-------|----------------------------|--------|--------|---------|
| light | `crt-guest-advanced-fast/` | crt-guest-advanced-fast: scanlines, mask, glow; cheap | guest(r), with Dr. Venom | GPL-2.0-or-later |
| full  | `crt-royale/`              | crt-royale: beam profiles, phosphor-mask textures, bloom, halation, curvature | TroggleMonkey | GPL-2.0-or-later |
| lola  | `lola-retro/`              | Lola Retro: the analog chain (signal, decoder, beam, mask, glass, afterglow) | Zoki | MIT |

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

## Lola Retro

Its sources, as read for the conversion, are in `third_party/lola-retro/` with their MIT
notice. They come from <https://github.com/zoran123456/lola-retro>, commit 2640253
(2026-10-19).

- **One conversion, many looks.** Lola's presets differ only in their parameter values (12 of its
  13; the 13th, "TV show", adds a pass and is the author's test), so HIRAKU converts one
  (`consumer-tv-scart-rgb`) and keeps the others as `looks/<preset>.params`, which the launcher
  offers under **Look**. A look leaves out the curvature, the overscan and the console timing:
  those are the cabinet's set-up and the board's, not the tube's.
- **The cabinet's monitor.** `hiraku-15khz.params`, `hiraku-24khz.params` and
  `hiraku-31khz.params` are read after `hiraku.params` for the game's monitor
  (`Core::monitor_khz`): the default look for each (Amiga 1084S for System 573 and Twinkle, the
  studio monitor for DJ Main, the PC VGA monitor for Firebeat), and for Firebeat Lola's VGA timing
  (`LR_CONSOLE = 7`; its "auto" reads 640x480 as interlaced). `hiraku.params` sets the overscan to
  0: the whole picture on the tube.
- **The beam pass at twice the window's width.** Lola's pass 5 works out each line's beam
  profile on a grid 8192 samples wide; HIRAKU's sizes it at twice the window's width instead (7680
  at 4K, about Lola's). On a 1280x960 window that pass went from 0.52 to 0.17 ms of GPU time and
  the picture stayed the same (13 pixels of 1.2 million changed by more than 1 %, PSNR 56 dB).
  `HK_PICTURE_PASSES=1` prints each pass's GPU time at exit.
- **Cost** (RX 7900 GRE, 1280x960): 0.33-0.37 ms a frame with an RGB look; the composite and RF
  looks add the signal's encoding and decoding (0.59 ms with the beam pass narrowed). crt-royale is
  0.29, crt-guest-advanced-fast 0.17.

Regenerating:

```bash
git clone https://github.com/zoran123456/lola-retro
L=lola-retro/shaders/lola-retro
tools/slang2glsl.py $L/flat/consumer-tv-scart-rgb.slangp shaders/lola-retro --vendor third_party/lola-retro \
    --set scale_type_x5=viewport --set scale_x5=2.0
for f in shaders/lola-retro/looks/*.params; do
  n=$(basename $f .params); t=$(head -1 $f | sed 's/^# //')
  tools/slangp2params.py $L/flat/$n.slangp $f --title "$t" --skip LR_CONSOLE,LR_CURV_X,LR_CURV_Y,LR_CORNER,LR_OVERSCAN
done
```

The `hiraku-<k>khz.params` files are their look's lines (by hand, with a comment saying which).
