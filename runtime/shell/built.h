// SPDX-License-Identifier: GPL-3.0-or-later
// The game a per-game binary (hiraku-<id>, with the game's recompiled code in it) was built for:
// a one-line file CMake writes for each (CMakeLists.txt, hiraku_built_game). A core's own binary
// (hiraku-djmain, -sys573, -twinkle, -firebeat, -pc) has none: --game ID, or its name hiraku-ID
// (a link to it, as the games without recorded code are), says the game (runtime/main.cpp).
#pragma once

namespace hk {

extern const char *const built_game;   // nullptr in a core's own binary

}  // namespace hk
