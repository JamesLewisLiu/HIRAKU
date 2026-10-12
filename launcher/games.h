// SPDX-License-Identifier: GPL-3.0-or-later
// The supported games (games/*/game.toml) and how far each is from playable.
#pragma once

#include <QIcon>
#include <QList>
#include <QString>

struct Game {
    QString id, title, family, mame;
    QString core;                        // firebeat (default), djmain, sys573
    QString year, maker;                 // from the manifest's first comment line
    QString firstBoot;                   // what a new cabinet needs in test mode
    enum Status { Ready, NotBuilt, NotInstalled } status = NotInstalled;
    QString exe;                         // hiraku-<id>: in binDir, or build/cmake
    QString statusText() const;
    QString howTo() const;               // what to do to make it Ready
};

QList<Game> scanGames();
QIcon familyIcon(const QString &family);
