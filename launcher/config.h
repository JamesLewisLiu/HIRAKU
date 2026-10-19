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
// Mods (docs/MODS.md): their folder for a game (<saves>/mods/<game>), the ones on, in order
// (folder names), and where the game's saves are with them on: a folder of their own,
// <game>+<mod>+<mod>, apart from the clean game's (nvramDir)
QString modsDir(const QString &game);
QStringList modsOn(const QString &game);
void setModsOn(const QString &game, const QStringList &mods);
QString saveDir(const QString &game);
QString controlsFile(const QString &family);  // the family's controls set's file
void mergeControls();                       // once: a set's members' old files joined into the set's
QString lightsFile(const QString &family);
QString lightsSocket();                    // $XDG_RUNTIME_DIR/hiraku-lights.sock; on Windows 127.0.0.1:PORT
QString exeName(const QString &base);      // a program's file name: base, or base.exe on Windows
// Python 3 for tools/*.py: the program and the arguments before the script's ("python3"; on
// Windows the package's own python\python.exe, else the "py -3" launcher, else "python"). Empty
// if there's none.
QStringList python();

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
