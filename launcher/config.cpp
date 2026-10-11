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

QString configDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/hiraku";
}

QSettings &settings() {
    static QSettings s(configDir() + "/launcher.ini", QSettings::IniFormat);
    return s;
}

QString rootDir() {
    QString r = settings().value("paths/root").toString();
    return r.isEmpty() ? QString(HK_ROOT) : r;
}

QString binDir() {
    QString b = settings().value("paths/bin").toString();
    return b.isEmpty() ? QCoreApplication::applicationDirPath() : b;
}

QString nvramBase() { return settings().value("paths/nvram").toString(); }

QString nvramDir(const QString &game) {
    QString base = nvramBase();
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/hiraku";
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
    return family;
}

QStringList families() { return {"bm3", "popn", "ppp", "kbm"}; }

}  // namespace cfg
