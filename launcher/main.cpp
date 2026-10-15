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

#include <cstdlib>
#include <string>

#include "mainwindow.h"

// A package with its own Qt (tools/package.sh) starts the launcher through bin/hiraku, a script
// that points LD_LIBRARY_PATH at bin/qt/lib and clears QT_PLUGIN_PATH, keeping the values it found
// in HK_SAVED_<name> (absent: it was unset). The loader read LD_LIBRARY_PATH when this process
// started, and Qt its plugin path when the application was made, so both can now go back as they
// were: the games, the tools and whatever opens a folder start with the user's own environment,
// not the bundled Qt.
static void restore_environment() {
    if (!getenv("HK_SAVED_ENV")) return;
    for (const char *name : {"LD_LIBRARY_PATH", "QT_PLUGIN_PATH"}) {
        const std::string saved = std::string("HK_SAVED_") + name;
        if (const char *v = getenv(saved.c_str())) setenv(name, v, 1);
        else unsetenv(name);
        unsetenv(saved.c_str());
    }
    unsetenv("HK_SAVED_ENV");
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    restore_environment();
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
