// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Mods (docs/MODS.md): folders with a mod.toml, laid over the game as it runs. load() reads them
// before the machine starts (their media ranges go to shell/overlay.h, so the core's images see
// them from the first read); frame() puts their memory patches in place, once a frame.
#include <string>
#include <vector>

namespace hk::mods {

// Each folder's mod.toml, for `game`, in order (a later mod wins where two touch the same place).
// root is the HIRAKU folder (dumps/<game>/machine.txt names the media). false, with a message in
// err, when a mod can't be used as it is.
bool load(const std::vector<std::string> &dirs, const std::string &game, const std::string &root, std::string &err);

// The memory patches: each put in place when its original bytes are there (call once a frame)
void frame();

// The mods loaded, for the log: "name version (folder)"
std::vector<std::string> describe();

}  // namespace hk::mods
