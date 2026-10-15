// SPDX-License-Identifier: GPL-3.0-or-later
#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

#include "config.h"
#include "picturedialog.h"

namespace {

// A line edit with a "Browse…" button
QWidget *withBrowse(QLineEdit *e, bool directory, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(e, 1);
    auto *b = new QPushButton("Browse…");
    b->setAutoDefault(false);
    h->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, w, [e, directory, parent] {
        QString p = directory ? QFileDialog::getExistingDirectory(parent, "Choose a folder", e->text())
                              : QFileDialog::getOpenFileName(parent, "Choose a SoundFont", e->text(), "SoundFonts (*.sf2 *.sf3);;All files (*)");
        if (!p.isEmpty()) e->setText(p);
    });
    return w;
}

// A card number line with a "Pick" button, as spicecfg's: the cards HIRAKU already has (each
// game's save folder keeps them as cards/NUMBER.bin), or a new number.
QWidget *withPick(QLineEdit *e, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(e, 1);
    auto *b = new QPushButton("Pick");
    b->setAutoDefault(false);
    h->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, w, [e, b] {
        QMenu menu;
        QDir base(QFileInfo(cfg::nvramDir("x")).path());
        QStringList seen;
        for (const QString &game : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            for (const QFileInfo &f : QDir(base.filePath(game) + "/cards").entryInfoList({"*.bin"}, QDir::Files, QDir::Name)) {
                const QString n = f.completeBaseName();
                if (seen.contains(n)) continue;
                seen << n;
                menu.addAction(n + "  (" + game + ")", [e, n] { e->setText(n); });
            }
        if (!seen.isEmpty()) menu.addSeparator();
        menu.addAction("New card number", [e] {
            QString n = "E004";   // as an e-AMUSEMENT PASS: E004 and 12 hex digits
            for (int i = 0; i < 12; i++) n += QString::number(QRandomGenerator::global()->bounded(16), 16).toUpper();
            e->setText(n);
        });
        menu.addAction("None", [e] { e->clear(); });
        menu.exec(b->mapToGlobal(QPoint(0, b->height())));
    });
    return w;
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Settings");
    resize(560, 0);
    auto &s = cfg::settings();
    auto *v = new QVBoxLayout(this);

    auto *video = new QGroupBox("Video");
    auto *fv = new QFormLayout(video);
    m_scale = new QSpinBox;
    m_scale->setRange(1, 6);
    m_scale->setSuffix("×");
    m_scale->setValue(s.value("video/scale", 2).toInt());
    fv->addRow("Window size:", m_scale);
    m_fullscreen = new QCheckBox("Start in fullscreen (F11 or Alt+Enter toggles)");
    m_fullscreen->setChecked(s.value("video/fullscreen", false).toBool());
    fv->addRow("", m_fullscreen);
    m_displaySync = new QCheckBox("Smooth frames on a 60, 120 or 240 Hz display");
    m_displaySync->setChecked(s.value("video/displaySync", true).toBool());
    m_displaySync->setToolTip("The games run at their own rates (pop'n 58.49 Hz, beatmania III 59.94 Hz), which a "
                              "fixed-refresh display can't show evenly. On: each frame stays up the same number of "
                              "refreshes, and the game runs at the display's rate (pop'n 2.6 % faster, its sound "
                              "with it). Off: the exact speed, for a variable-refresh (FreeSync, G-Sync) display.");
    fv->addRow("", m_displaySync);
    m_picture = new QComboBox;
    m_picture->addItem("Off: sharp pixels", "off");
    m_picture->addItem("CRT light: crt-guest-advanced-fast", "light");
    m_picture->addItem("CRT full: crt-royale", "full");
    m_picture->setCurrentIndex(std::max(0, m_picture->findData(picture::globalMode())));
    m_picture->setToolTip("For all games; a game can have its own (Picture, in the game's menu). In the game, F10 cycles them.");
    auto *pic = new QWidget;
    auto *ph = new QHBoxLayout(pic);
    ph->setContentsMargins(0, 0, 0, 0);
    ph->addWidget(m_picture, 1);
    auto *adjust = new QPushButton("Adjust…");
    adjust->setAutoDefault(false);
    ph->addWidget(adjust);
    connect(adjust, &QPushButton::clicked, this, [this] {
        cfg::settings().setValue("video/picture", m_picture->currentData().toString());
        PictureDialog d(QString(), QString(), false, this);
        if (d.exec() == QDialog::Accepted) m_picture->setCurrentIndex(std::max(0, m_picture->findData(picture::globalMode())));
    });
    fv->addRow("Picture:", pic);
    v->addWidget(video);

    auto *audio = new QGroupBox("Audio");
    auto *fa = new QFormLayout(audio);
    m_audioMs = new QSpinBox;
    m_audioMs->setRange(2, 30);
    m_audioMs->setSuffix(" ms");
    m_audioMs->setValue(s.value("audio/ms", 8).toInt());
    m_audioMs->setToolTip("The audio queue the game starts with. It grows by itself after an underrun (docs/PLAY.md, Latency).");
    fa->addRow("Starting audio queue:", m_audioMs);
    m_fineSound = new QCheckBox("Sound every millisecond on System 573 and Twinkle (not MAME's)");
    m_fineSound->setChecked(s.value("audio/fineSound", false).toBool());
    m_fineSound->setToolTip("MAME hands these machines' sound on 50 times a second, which adds up to 20 ms of "
                            "latency, uneven from moment to moment. Every millisecond is lower and steadier. On "
                            "Twinkle (beatmania IIDX) the keysounds then start when the game triggers them, so the "
                            "sound no longer matches MAME's (--fine-sound; docs/PLAY.md, Latency).");
    fa->addRow("", m_fineSound);
    m_soundfont = new QLineEdit(s.value("audio/soundfont").toString());
    m_soundfont->setPlaceholderText("default: the system's General MIDI SoundFont");
    fa->addRow("Keyboardmania SoundFont:", withBrowse(m_soundfont, false, this));
    v->addWidget(audio);

    auto *lights = new QGroupBox("Lights");
    auto *fl = new QVBoxLayout(lights);
    m_socket = new QCheckBox("Publish lamps on a local socket (live view in Lights, external LED tools)");
    m_socket->setChecked(s.value("lights/socket", true).toBool());
    fl->addWidget(m_socket);
    auto *path = new QLabel("Socket: <tt>" + cfg::lightsSocket().toHtmlEscaped() + "</tt> (protocol: docs/LIGHTS.md)");
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fl->addWidget(path);
    v->addWidget(lights);

    // as spicecfg's Network page, with its option names, so a spice2x setup carries over
    auto *ea = new QGroupBox("e-AMUSEMENT (Konami Python: pop'n music 9-14)");
    auto *fe = new QFormLayout(ea);
    m_eaUrl = new QLineEdit(s.value("eamuse/url", s.value("eamuse/server")).toString());
    m_eaUrl->setPlaceholderText("empty: the server inside HIRAKU (-ea)");
    m_eaUrl->setToolTip("-url. Empty: the server inside HIRAKU, which keeps its data in the game's save folder. "
                        "host:port or http://host:port/path: a server on the network, such as hiraku-eamuse on another "
                        "PC (docs/EAMUSE3.md). The game's e-AMUSEMENT must be ON in test mode.");
    fe->addRow("EA Service URL:", m_eaUrl);
    m_smartea = new QCheckBox("The server inside HIRAKU when that one doesn't answer (-smartea)");
    m_smartea->setChecked(s.value("eamuse/smartea", false).toBool());
    fe->addRow("", m_smartea);
    const QRegularExpression hex20("[0-9A-Fa-f]{0,20}"), hex16("[0-9A-Fa-f]{0,16}");
    m_pcbid = new QLineEdit(s.value("eamuse/pcbid").toString());
    m_pcbid->setValidator(new QRegularExpressionValidator(hex20, m_pcbid));
    m_pcbid->setPlaceholderText("the game's own (pop'n 9: 01401401000100045FEF)");
    m_pcbid->setToolTip("-p: the cabinet's PCBID, 20 hex digits, for a server that only takes the machines it knows.");
    fe->addRow("PCBID:", m_pcbid);
    for (int n = 0; n < 2; n++) {
        m_card[n] = new QLineEdit(s.value(QString("eamuse/card%1").arg(n)).toString());
        m_card[n]->setValidator(new QRegularExpressionValidator(hex16, m_card[n]));
        m_card[n]->setPlaceholderText(n ? "card1.txt (pop'n has one reader: not used)" : "card0.txt, or the save folder's card.bin");
        m_card[n]->setToolTip(QString("-card%1: the card's number, 16 hex digits (E004...), the same as in spice2x, so a "
                                      "server knows the player. Empty: card%1.txt in the save folder, if there is one; "
                                      "else the save folder's card.bin, numbered by the server.%2")
                                  .arg(n)
                                  .arg(n ? " pop'n has one card reader, player 1's." : ""));
        fe->addRow(QString("Player %1 Card:").arg(n + 1), withPick(m_card[n], this));
    }
    m_autocard = new QComboBox;
    for (const char *v : {"off", "p1", "p2", "both"}) m_autocard->addItem(v, v);
    m_autocard->setCurrentIndex(std::max(0, m_autocard->findData(s.value("eamuse/autocard", "off").toString())));
    m_autocard->setToolTip("-autocard: the card is put in whenever the reader waits for one, so Insert needn't be pressed.");
    fe->addRow("Auto Card Insert:", m_autocard);
    v->addWidget(ea);

    auto *paths = new QGroupBox("Folders");
    auto *fp = new QFormLayout(paths);
    m_root = new QLineEdit(s.value("paths/root").toString());
    m_root->setPlaceholderText(cfg::rootDir());
    fp->addRow("HIRAKU folder:", withBrowse(m_root, true, this));
    m_bin = new QLineEdit(s.value("paths/bin").toString());
    m_bin->setPlaceholderText(cfg::binDir());
    fp->addRow("Game executables:", withBrowse(m_bin, true, this));
    m_nvram = new QLineEdit(s.value("paths/nvram").toString());
    m_nvram->setPlaceholderText("default: ~/.local/share/hiraku");
    fp->addRow("Saves (NVRAM):", withBrowse(m_nvram, true, this));
    v->addWidget(paths);

    auto *adv = new QGroupBox("Launcher");
    auto *fx = new QFormLayout(adv);
    m_hideLauncher = new QCheckBox("Hide the launcher while a game runs");
    m_hideLauncher->setChecked(s.value("ui/hideWhileRunning", false).toBool());
    fx->addRow("", m_hideLauncher);
    m_extra = new QLineEdit(s.value("game/extraArgs").toString());
    m_extra->setPlaceholderText("e.g. --bios-seconds 21.65");
    fx->addRow("Extra arguments:", m_extra);
    v->addWidget(adv);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
}

void SettingsDialog::accept() {
    auto &s = cfg::settings();
    const QString pcbid = m_pcbid->text().trimmed(), c0 = m_card[0]->text().trimmed(), c1 = m_card[1]->text().trimmed();
    if ((!pcbid.isEmpty() && pcbid.size() != 20) || (!c0.isEmpty() && c0.size() != 16) || (!c1.isEmpty() && c1.size() != 16)) {
        QMessageBox::warning(this, "e-AMUSEMENT", "A PCBID is 20 hex digits, and a card number 16 (E004...).");
        return;
    }
    s.setValue("video/scale", m_scale->value());
    s.setValue("video/fullscreen", m_fullscreen->isChecked());
    s.setValue("video/displaySync", m_displaySync->isChecked());
    s.setValue("video/picture", m_picture->currentData().toString());
    s.setValue("audio/ms", m_audioMs->value());
    s.setValue("audio/fineSound", m_fineSound->isChecked());
    s.setValue("audio/soundfont", m_soundfont->text().trimmed());
    s.setValue("lights/socket", m_socket->isChecked());
    s.setValue("paths/root", m_root->text().trimmed());
    s.setValue("paths/bin", m_bin->text().trimmed());
    s.setValue("paths/nvram", m_nvram->text().trimmed());
    s.setValue("ui/hideWhileRunning", m_hideLauncher->isChecked());
    s.setValue("game/extraArgs", m_extra->text().trimmed());
    s.remove("eamuse/server");   // now eamuse/url
    s.setValue("eamuse/url", m_eaUrl->text().trimmed());
    s.setValue("eamuse/smartea", m_smartea->isChecked());
    s.setValue("eamuse/pcbid", m_pcbid->text().trimmed().toUpper());
    for (int n = 0; n < 2; n++) s.setValue(QString("eamuse/card%1").arg(n), m_card[n]->text().trimmed().toUpper());
    s.setValue("eamuse/autocard", m_autocard->currentData().toString());
    s.sync();
    QDialog::accept();
}
