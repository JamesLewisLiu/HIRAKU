// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "games.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <QTextStream>

#include "config.h"

namespace {

// The top-level keys of a game.toml (title, family, ...): all the launcher needs.
void readManifest(const QString &path, Game &g) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    bool first = true;
    static const QRegularExpression kv(R"(^\s*([A-Za-z_]+)\s*=\s*"([^"]*)\")");
    static const QRegularExpression head(R"(^#\s*.*\),\s*(.+?)\s+(\d{4})\s*$)");   // "# Title (CODE), Konami 2002"
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (first) {
            first = false;
            auto m = head.match(line);
            if (m.hasMatch()) {
                g.maker = m.captured(1);
                g.year = m.captured(2);
            }
        }
        if (line.trimmed().startsWith('[')) break;
        auto m = kv.match(line);
        if (!m.hasMatch()) continue;
        QString k = m.captured(1), v = m.captured(2);
        if (k == "id") g.id = v;
        else if (k == "title") g.title = v;
        else if (k == "family") g.family = v;
        else if (k == "mame") g.mame = v;
        else if (k == "core") g.core = v;
        else if (k == "first_boot") g.firstBoot = v;
        else if (k == "install_cassette") g.installCassette = v == "yes";
    }
}

}  // namespace

QString Game::statusText() const {
    switch (status) {
    case Ready: return "Ready";
    case NotBuilt: return "Not built";
    case NotInstalled: return "Not installed";
    }
    return {};
}

QString Game::howTo() const {
    QString root = cfg::rootDir();
    if (core == "sys573" && status == NotInstalled)   // System 573: the set and the CD
        return QString("HIRAKU has no data for %1 yet. You need your own dumps: the MAME ROM set \"%2\" "
                       "(.zip or .7z) and its CD (CHD).\n\n"
                       "Drop them on this window, or File > Add game files. The CD is unpacked with MAME's chdman.")
            .arg(title, mame);
    if (core == "twinkle" && status == NotInstalled)   // Twinkle: the set, two CDs and the hard disk
        return QString("HIRAKU has no data for %1 yet. You need your own dumps: the MAME ROM set \"%2\" "
                       "(.zip or .7z), its program CD, the player's Video CD (or, from 6th style on, its DVD) "
                       "and its hard disk (CHDs).\n\n"
                       "Drop them on this window, or File > Add game files. They are unpacked with MAME's "
                       "chdman. A DVD style keeps about 10 GB; unpacking 7th style's 40 GB hard disk needs that much free "
                       "space at first (its zeros become holes).")
            .arg(title, mame);
    if (core == "python") {   // Konami Python: PCSX2 Reliquary hosted, built in Reliquary's tree
        if (status == NotInstalled)
            return QString("HIRAKU has no data for %1 yet. You need your own dumps, as PCSX2 Reliquary takes "
                           "them: the game's .py1 with its folder (disk CHD, interface board ROMs, dongle, "
                           "memory card), the arcade BIOS (COH-H31000) and the MagicGate key files in bios/ and "
                           "keys/ folders beside the sets.\n\n"
                           "Drop the .py1 on this window, or File > Add game files. The disk stays where it is.")
                .arg(title);
        if (status == NotBuilt)
            return QString("The data for %1 is installed, but %2 has not been built yet. It is built with "
                           "PCSX2 Reliquary's source (docs/PLAY.md, Konami Python).\n\nIn %3:\n\n"
                           "oracle/pcsx2/build-reliquary.sh all")
                .arg(title, exe, root);
        return {};
    }
    if (family.startsWith("dj") || core == "sys573" || core == "twinkle") {   // DJ Main, 573, Twinkle: no recompilation
        if (status == NotInstalled)
            return QString("HIRAKU has no data for %1 yet. You need your own dumps: the MAME ROM set \"%2\" "
                           "(.zip or .7z) and its hard disk (CHD).\n\n"
                           "Drop them on this window, or File > Add game files. The disk is unpacked "
                           "with MAME's chdman, which needs about 1.5 GB of space.")
                .arg(title, mame);
        if (status == NotBuilt)
            return QString("The data for %1 is installed, but %2 has not been built yet.\n\nIn %3:\n\n"
                           "cmake -S . -B build/cmake -G Ninja && ninja -C build/cmake hiraku-%4")
                .arg(title, exe, root, id);
        return {};
    }
    QString build = QString("analyze/ghidra.sh %1 && analyze/prep.py %1\n"
                            "cmake -S . -B build/cmake -G Ninja && ninja -C build/cmake hiraku-%1")
                        .arg(id);
    if (status == NotInstalled)
        return QString("HIRAKU has no data for %1 yet. You need your own dumps: the program disc, "
                       "its second disc or HDD, and the MAME ROM set \"%2\" (docs/PLAY.md lists them per game).\n\n"
                       "Drop them on this window, or File > Add game files: CHDs, disc images (.cue/.iso/.img) "
                       "and the set (.zip/.7z), all at once. CHDs are unpacked with MAME's chdman.")
            .arg(title, mame);
    if (status == NotBuilt) {
        bool prepped = QFileInfo::exists(root + "/build/" + id + "/recomp.txt");
        return QString("The data for %1 is installed, but %2 has not been built yet.\n\nIn %3:\n\n%4")
            .arg(title, exe, root,
                 prepped ? QString("cmake -S . -B build/cmake -G Ninja && ninja -C build/cmake hiraku-%1").arg(id) : build);
    }
    return {};
}

QList<Game> scanGames() {
    QList<Game> out;
    QString root = cfg::rootDir(), bin = cfg::binDir();
    QDir dir(root + "/games");
    for (const QString &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString manifest = dir.filePath(sub + "/game.toml");
        if (!QFileInfo::exists(manifest)) continue;
        Game g;
        g.id = sub;
        readManifest(manifest, g);
        if (g.title.isEmpty()) g.title = g.id;
        // the binaries folder (Settings), next to the launcher, a copied build's bin/,
        // else where the README's build puts it
        bool built = false;
        for (const QString &d : {bin, QCoreApplication::applicationDirPath(), root + "/bin", root + "/build/cmake"}) {
            QFileInfo exe(d + "/" + cfg::exeName("hiraku-" + g.id));
            if (exe.isFile() && exe.isExecutable()) {
                g.exe = exe.absoluteFilePath();
                built = true;
                break;
            }
        }
        if (!built) g.exe = bin + "/" + cfg::exeName("hiraku-" + g.id);
        bool installed = QFileInfo::exists(root + "/dumps/" + g.id + "/machine.txt");
        g.status = !installed ? Game::NotInstalled : built ? Game::Ready : Game::NotBuilt;
        out.push_back(g);
    }
    return out;
}

QIcon familyIcon(const QString &family) {
    struct Look { QColor bg; const char *text; };
    Look l = family == "popn" ? Look{QColor(0xe8, 0x4a, 0x8a), "pop'n"}
           : family == "djbm" ? Look{QColor(0x22, 0x2a, 0x5a), "bm"}
           : family == "djpopn" ? Look{QColor(0xf0, 0xb4, 0x1e), "pop'n"}
           : family == "pypopn" ? Look{QColor(0x3c, 0xb4, 0xdc), "pop'n"}
           : family == "djpstage" ? Look{QColor(0xe0, 0x50, 0x90), "stage"}
           : family == "ppp"  ? Look{QColor(0xf0, 0x8a, 0x24), "PPP"}
           : family == "kbm"  ? Look{QColor(0x2e, 0x9e, 0x6a), "KBM"}
           : family == "ddr"  ? Look{QColor(0x8a, 0x2b, 0xc8), "DDR"}
           : family == "ddrsolo" ? Look{QColor(0x8a, 0x2b, 0xc8), "Solo"}
           : family == "drmn" ? Look{QColor(0xc8, 0x3a, 0x2b), "DM"}
           : family == "gtrf" ? Look{QColor(0x2b, 0x8a, 0xc8), "GF"}
           : family == "iidx" ? Look{QColor(0x18, 0x18, 0x20), "IIDX"}
                              : Look{QColor(0x3a, 0x4c, 0xb8), "bm III"};
    QIcon icon;
    for (int size : {32, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(l.bg);
        p.drawRoundedRect(QRectF(1, 1, size - 2, size - 2), size / 6.0, size / 6.0);
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(size * (strlen(l.text) > 4 ? 0.26 : 0.32));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, l.text);
        icon.addPixmap(pm);
    }
    return icon;
}
