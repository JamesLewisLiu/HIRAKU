// SPDX-License-Identifier: GPL-3.0-or-later
// Where the launcher finds things and keeps its settings.
//   ~/.config/hiraku/launcher.ini        settings
//   ~/.config/hiraku/controls/<set>.txt  bindings files (runtime/host/input.h), one per controls set
//   ~/.config/hiraku/lights/<family>.txt    lights files (runtime/host/lights.h)
// In a portable install (a HIRAKU folder holding a folder `user`: runtime/shell/userdir.h) these
// are in user/config instead, and the saves in user/data: the folder is the whole install.
#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

namespace cfg {

QSettings &settings();
QString configDir();
bool portable();                            // a portable install: settings and saves in rootDir()/user
QString rootDir();                         // the HIRAKU checkout: games/, dumps/
QString binDir();                          // where hiraku-<game> executables are
QString nvramBase();                       // "" = the runtime's default (~/.local/share/hiraku, or user/data)
QString nvramDir(const QString &game);     // where the game's NVRAM is (or will be)
QString controlsFile(const QString &family);  // the family's controls set's file
void mergeControls();                       // once: a set's members' old files joined into the set's
QString lightsFile(const QString &family);
QString lightsSocket();                    // $XDG_RUNTIME_DIR/hiraku-lights.sock

QString familyName(const QString &family); // "pop'n music"
QList<QPair<QString, QStringList>> familySeries();   // ("beatmania", {djbm, iidx, bm3}), ...
QStringList families();                    // every family, in familySeries() order

// Families whose cabinets differ only in extras share their controls: one bindings file, and each
// game ignores the inputs it doesn't have (beatmania: IIDX's keys 6 and 7, III's foot pedal;
// pop'n music: DJ Main's reset switch, Python's 10-key)
QString controlsSet(const QString &family);          // djbm, iidx, bm3 -> beatmania; djpopn, pypopn -> popn
QStringList controlsSetMembers(const QString &set);  // popn -> djpopn, popn, pypopn; ddr -> ddr
QString controlsSetName(const QString &set);         // "pop'n music"
QString controlsMemberName(const QString &family);   // within its set: "IIDX", "Konami Python"
QList<QPair<QString, QStringList>> controlsSeries(); // the sets, as familySeries() (one list, no header)

}  // namespace cfg
