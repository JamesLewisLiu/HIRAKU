// SPDX-License-Identifier: GPL-3.0-or-later
// hiraku: the launcher. Lists the supported games, maps controls and lights,
// and starts hiraku-<game>.
//
//   hiraku [GAME]      GAME (e.g. popn7) starts that game right away
#include <QApplication>
#include <QTimer>

#include "mainwindow.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("hiraku");
    QApplication::setApplicationDisplayName("HIRAKU");
    QApplication::setDesktopFileName("hiraku");
    MainWindow w;
    w.show();
    QStringList args = app.arguments().mid(1);
    if (!args.isEmpty()) QTimer::singleShot(0, &w, [&w, id = args.first()] { w.play(id); });
    return app.exec();
}
