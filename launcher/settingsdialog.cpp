// SPDX-License-Identifier: GPL-3.0-or-later
#include "settingsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "config.h"

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
    v->addWidget(video);

    auto *audio = new QGroupBox("Audio");
    auto *fa = new QFormLayout(audio);
    m_audioMs = new QSpinBox;
    m_audioMs->setRange(2, 30);
    m_audioMs->setSuffix(" ms");
    m_audioMs->setValue(s.value("audio/ms", 8).toInt());
    m_audioMs->setToolTip("The audio queue the game starts with. It grows by itself after an underrun (docs/PLAY.md, Latency).");
    fa->addRow("Starting audio queue:", m_audioMs);
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
    s.setValue("video/scale", m_scale->value());
    s.setValue("video/fullscreen", m_fullscreen->isChecked());
    s.setValue("audio/ms", m_audioMs->value());
    s.setValue("audio/soundfont", m_soundfont->text().trimmed());
    s.setValue("lights/socket", m_socket->isChecked());
    s.setValue("paths/root", m_root->text().trimmed());
    s.setValue("paths/bin", m_bin->text().trimmed());
    s.setValue("paths/nvram", m_nvram->text().trimmed());
    s.setValue("ui/hideWhileRunning", m_hideLauncher->isChecked());
    s.setValue("game/extraArgs", m_extra->text().trimmed());
    s.sync();
    QDialog::accept();
}
