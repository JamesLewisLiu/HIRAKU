// SPDX-License-Identifier: GPL-3.0-or-later
// hiraku: the launcher. Lists the supported games, maps controls and lights,
// and starts hiraku-<game>.
//
//   hiraku [GAME]            GAME (e.g. popn7) starts that game right away
//   hiraku --add FILE...     add a game from its files (ROM set, CHDs, images, folders),
//                            as dropping them on the window does; the log goes to stderr too
#include <QApplication>
#include <QIcon>
#include <QTimer>

#include "mainwindow.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("hiraku");
    QApplication::setApplicationDisplayName("HIRAKU");
    QApplication::setDesktopFileName("hiraku");
    QApplication::setWindowIcon(QIcon(":/icon.png"));
    MainWindow w;
    w.show();
    QStringList args = app.arguments().mid(1);
    if (!args.isEmpty() && args.first() == "--add") {
        w.setEcho(true);
        QTimer::singleShot(0, &w, [&w, files = args.mid(1)] { w.addGameFiles(files); });
    } else if (!args.isEmpty()) {
        QTimer::singleShot(0, &w, [&w, id = args.first()] { w.play(id); });
    }
    return app.exec();
}
