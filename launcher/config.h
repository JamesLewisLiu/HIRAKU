// SPDX-License-Identifier: GPL-3.0-or-later
// Where the launcher finds things and keeps its settings.
//   ~/.config/hiraku/launcher.ini        settings
//   ~/.config/hiraku/controls/<family>.txt  bindings files (runtime/host/input.h)
//   ~/.config/hiraku/lights/<family>.txt    lights files (runtime/host/lights.h)
#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

namespace cfg {

QSettings &settings();
QString configDir();
QString rootDir();                         // the HIRAKU checkout: games/, dumps/
QString binDir();                          // where hiraku-<game> executables are
QString nvramBase();                       // "" = the runtime's default (~/.local/share/hiraku)
QString nvramDir(const QString &game);     // where the game's NVRAM is (or will be)
QString controlsFile(const QString &family);
QString lightsFile(const QString &family);
QString lightsSocket();                    // $XDG_RUNTIME_DIR/hiraku-lights.sock

QString familyName(const QString &family); // "pop'n music"
QStringList families();                    // bm3, popn, ppp, kbm

}  // namespace cfg
