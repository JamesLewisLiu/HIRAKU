// SPDX-License-Identifier: GPL-3.0-or-later
#include "modsdialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "config.h"

ModsDialog::Info ModsDialog::read(const QString &dir) {
    Info i;
    i.folder = QFileInfo(dir).fileName();
    QFile f(dir + "/mod.toml");
    if (!f.open(QIODevice::ReadOnly)) return i;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith("[")) break;   // the top's keys only
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed();
        QString v = line.mid(eq + 1).trimmed();
        if (!v.startsWith('"')) continue;
        v = v.mid(1, v.indexOf('"', 1) - 1);
        if (key == "name") i.name = v;
        else if (key == "version") i.version = v;
        else if (key == "description") i.description = v;
        else if (key == "game") i.game = v;
    }
    if (i.name.isEmpty()) i.name = i.folder;
    return i;
}

QString ModsDialog::summary(const QString &game) {
    QStringList names;
    for (const QString &m : cfg::modsOn(game)) names << read(cfg::modsDir(game) + "/" + m).name;
    return names.join(", ");
}

ModsDialog::ModsDialog(const QString &game, const QString &title, QWidget *parent) : QDialog(parent), m_game(game) {
    setWindowTitle("Mods for " + title);
    auto *v = new QVBoxLayout(this);
    auto *head = new QLabel("Mods change the game as it runs, without changing its data. The ones ticked run, in this "
                            "order (a later one wins where two change the same thing). With any on, the game keeps "
                            "its saves apart from the clean game's (started from a copy of them).");
    head->setWordWrap(true);
    v->addWidget(head);
    auto *row = new QHBoxLayout;
    m_list = new QListWidget;
    row->addWidget(m_list, 1);
    auto *side = new QVBoxLayout;
    auto *up = new QPushButton("Up"), *down = new QPushButton("Down");
    connect(up, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down, &QPushButton::clicked, this, [this] { move(1); });
    side->addWidget(up);
    side->addWidget(down);
    side->addStretch(1);
    row->addLayout(side);
    v->addLayout(row, 1);
    m_about = new QLabel;
    m_about->setWordWrap(true);
    m_about->setMinimumHeight(48);
    v->addWidget(m_about);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r < 0) { m_about->clear(); return; }
        const Info i = read(cfg::modsDir(m_game) + "/" + m_list->item(r)->data(Qt::UserRole).toString());
        m_about->setText(i.description.isEmpty() ? "(no description)" : i.description);
    });
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *open = bb->addButton("Open the mods folder", QDialogButtonBox::ActionRole);
    connect(open, &QPushButton::clicked, this, [this] {
        QDir().mkpath(cfg::modsDir(m_game));
        QDesktopServices::openUrl(QUrl::fromLocalFile(cfg::modsDir(m_game)));
    });
    auto *refresh = bb->addButton("Look again", QDialogButtonBox::ActionRole);
    connect(refresh, &QPushButton::clicked, this, &ModsDialog::fill);
    connect(bb, &QDialogButtonBox::accepted, this, &ModsDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
    resize(560, 420);
    fill();
}

// the ones on first, in their order, then the rest by name; a mod for another game is left out
void ModsDialog::fill() {
    m_list->clear();
    const QStringList on = cfg::modsOn(m_game);
    QStringList folders = on;
    QStringList rest = QDir(cfg::modsDir(m_game)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &f : rest)
        if (!folders.contains(f) && QFileInfo::exists(cfg::modsDir(m_game) + "/" + f + "/mod.toml")) folders << f;
    for (const QString &f : folders) {
        const Info i = read(cfg::modsDir(m_game) + "/" + f);
        if (!i.game.isEmpty() && i.game != m_game) continue;
        auto *it = new QListWidgetItem(i.name + (i.version.isEmpty() ? "" : " " + i.version) + "  (" + f + ")");
        it->setData(Qt::UserRole, f);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(on.contains(f) ? Qt::Checked : Qt::Unchecked);
        m_list->addItem(it);
    }
    if (m_list->count() == 0)
        m_about->setText("No mods for this game yet: put each in a folder of its own, with its mod.toml, in " +
                         cfg::modsDir(m_game) + " (docs/MODS.md).");
}

void ModsDialog::move(int by) {
    const int r = m_list->currentRow(), to = r + by;
    if (r < 0 || to < 0 || to >= m_list->count()) return;
    QListWidgetItem *it = m_list->takeItem(r);
    m_list->insertItem(to, it);
    m_list->setCurrentRow(to);
}

void ModsDialog::accept() {
    QStringList on;
    for (int r = 0; r < m_list->count(); r++)
        if (m_list->item(r)->checkState() == Qt::Checked) on << m_list->item(r)->data(Qt::UserRole).toString();
    cfg::setModsOn(m_game, on);
    QDialog::accept();
}
