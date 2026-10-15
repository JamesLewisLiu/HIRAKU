// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include <unistd.h>

#ifndef HK_ROOT
#define HK_ROOT "."
#endif

namespace cfg {

namespace {
// The root found without the settings (which may live in it): $HIRAKU_ROOT, else the checkout the
// launcher was built from, else the launcher's folder or its parent (a copied build).
QString foundRoot() {
    QString env = qEnvironmentVariable("HIRAKU_ROOT");
    if (!env.isEmpty() && QDir(env + "/games").exists()) return env;
    if (QDir(QString(HK_ROOT) + "/dumps").exists()) return QString(HK_ROOT);
    QDir app(QCoreApplication::applicationDirPath());
    if (app.exists("dumps")) return app.absolutePath();
    if (app.cdUp() && app.exists("dumps")) return app.absolutePath();
    return QString(HK_ROOT);
}

// A portable install's folder, or "": the launcher's own folder or its parent when it holds a
// folder `user` (first: a package built on this PC must not take the checkout it came from),
// else the root above when it does
QString portableRoot() {
    static const QString r = [] {
        QDir app(QCoreApplication::applicationDirPath());
        if (app.exists("user") && app.exists("games")) return app.absolutePath();
        if (app.cdUp() && app.exists("user") && app.exists("games")) return app.absolutePath();
        QDir f(foundRoot());
        return f.exists("user") ? f.absolutePath() : QString();
    }();
    return r;
}
}  // namespace

bool portable() { return !portableRoot().isEmpty(); }

QString configDir() {
    if (portable()) return portableRoot() + "/user/config";
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/hiraku";
}

QSettings &settings() {
    static QSettings s(configDir() + "/launcher.ini", QSettings::IniFormat);
    return s;
}

QString rootDir() {
    // a portable install is its own root, wherever it was moved. Otherwise the setting, else as
    // the game binaries find theirs (foundRoot). A setting without games/ is skipped: one left
    // from another copy of HIRAKU (an older package moved or deleted) would show an empty list.
    if (portable()) return portableRoot();
    QString r = settings().value("paths/root").toString();
    if (!r.isEmpty() && QDir(r + "/games").exists()) return r;
    return foundRoot();
}

QString binDir() {
    QString b = settings().value("paths/bin").toString();
    return b.isEmpty() ? QCoreApplication::applicationDirPath() : b;
}

QString nvramBase() { return settings().value("paths/nvram").toString(); }

QString nvramDir(const QString &game) {
    QString base = nvramBase();
    if (base.isEmpty())
        base = portable() ? portableRoot() + "/user/data"
                          : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/hiraku";
    return base + "/" + game;
}

QString controlsFile(const QString &family) { return configDir() + "/controls/" + family + ".txt"; }
QString lightsFile(const QString &family) { return configDir() + "/lights/" + family + ".txt"; }

QString lightsSocket() {
    QString run = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (run.isEmpty()) return QDir::tempPath() + QString("/hiraku-lights-%1.sock").arg(getuid());
    return run + "/hiraku-lights.sock";
}

QString familyName(const QString &family) {
    if (family == "bm3") return "beatmania III";
    if (family == "popn") return "pop'n music";
    if (family == "ppp") return "ParaParaParadise";
    if (family == "kbm") return "Keyboardmania";
    if (family == "djbm") return "beatmania (DJ Main)";
    if (family == "djpopn") return "pop'n music (DJ Main)";
    if (family == "djpstage") return "pop'n stage (DJ Main)";
    if (family == "ddr") return "Dance Dance Revolution (System 573)";
    if (family == "drmn") return "DrumMania (System 573)";
    if (family == "gtrf") return "GuitarFreaks (System 573)";
    if (family == "pypopn") return "pop'n music (Konami Python)";
    return family;
}

QStringList families() { return {"bm3", "popn", "ppp", "kbm", "djbm", "djpopn", "djpstage", "ddr", "drmn", "gtrf", "pypopn"}; }

}  // namespace cfg
