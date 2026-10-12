# libretro slang-shaders (part)

The sources of the two shaders in `shaders/`, as `tools/slang2glsl.py` read them, with the
same paths as upstream: <https://github.com/libretro/slang-shaders>, commit
e1d75632a205c70f14a4cc947c46d5abb7b3f7f1 (2026-10-08).

- `crt/crt-guest-advanced-fast.slangp`, `crt/shaders/guest/`:
  Copyright (C) 2018-2025 guest(r), with Dr. Venom. GPL-2.0-or-later (each file's notice).
- `crt/crt-royale.slangp`, `crt/shaders/crt-royale/`, `blurs/shaders/royale/`, `include/`:
  Copyright (C) 2014 TroggleMonkey. GPL-2.0-or-later (`crt/shaders/crt-royale/LICENSE.TXT`,
  each file's notice).

They are not built as they are: `shaders/` holds the GLSL generated from them.
