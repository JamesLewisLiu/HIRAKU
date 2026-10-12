// SPDX-License-Identifier: GPL-3.0-or-later
#include "unlocksdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "config.h"

bool UnlocksDialog::available(const QString &game) {
    return QFileInfo::exists(cfg::rootDir() + "/games/" + game + "/unlocks.toml");
}

UnlocksDialog::UnlocksDialog(const QString &game, const QString &title, bool running, QWidget *parent)
    : QDialog(parent), m_game(game), m_title(title), m_running(running) {
    setWindowTitle("Unlocks: " + title);
    resize(620, 560);
    auto *v = new QVBoxLayout(this);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget;
    m_body = new QVBoxLayout(inner);
    scroll->setWidget(inner);
    v->addWidget(scroll, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
    rebuild();
}

QJsonObject UnlocksDialog::run(const QStringList &extra, QString *err) {
    QProcess p;
    p.start("python3", QStringList{cfg::rootDir() + "/tools/unlock.py", m_game, "--json", "--nvram", cfg::nvramDir(m_game)} + extra);
    if (!p.waitForFinished(15000)) {
        *err = "tools/unlock.py did not answer (is python3 installed?)";
        return {};
    }
    QJsonObject o = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    if (o.isEmpty()) *err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
    else if (o.contains("error")) *err = o["error"].toString();
    return o;
}

void UnlocksDialog::rebuild() {
    if (m_content) m_content->deleteLater();
    m_content = new QWidget;
    auto *v = new QVBoxLayout(m_content);
    v->setContentsMargins(0, 0, 0, 0);
    m_body->insertWidget(0, m_content);
    QString err;
    QJsonObject st = run({}, &err);
    auto *where = new QLabel("Save: <tt>" + cfg::nvramDir(m_game).toHtmlEscaped() + "</tt>");
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    where->setToolTip(cfg::nvramDir(m_game));
    where->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // a long path is cut, not the dialog widened
    v->addWidget(where);
    if (!err.isEmpty()) {
        auto *e = new QLabel(err);
        e->setWordWrap(true);
        v->addWidget(e);
    }
    if (m_running) {
        auto *r = new QLabel("<b>Stop the game first</b>: it writes its save back when it exits.");
        r->setWordWrap(true);
        v->addWidget(r);
    }
    for (const QJsonValue &uv : st["unlocks"].toArray()) {
        QJsonObject u = uv.toObject();
        auto *box = new QGroupBox(u["title"].toString());
        auto *bv = new QVBoxLayout(box);
        QJsonValue cur = u["current"];
        auto *combo = new QComboBox;
        QJsonArray levels = u["level"].toArray();
        for (const QJsonValue &lv : levels) {
            QJsonObject l = lv.toObject();
            QString code = l["code"].toString();
            combo->addItem(l["name"].toString() + (code.isEmpty() ? "" : "   (code " + code + ")"), l["value"].toInt());
        }
        if (!cur.isNull()) combo->setCurrentIndex(std::max(0, combo->findData(cur.toInt())));
        auto *text = new QLabel;
        text->setWordWrap(true);
        auto showText = [text, levels, combo] {
            for (const QJsonValue &lv : levels)
                if (lv.toObject()["value"].toInt() == combo->currentData().toInt()) text->setText(lv.toObject()["text"].toString());
        };
        showText();
        connect(combo, &QComboBox::currentIndexChanged, this, showText);
        auto *now = new QLabel(cur.isNull() ? QString("No save yet: start the game once (with its first-boot setup), quit, then come back.")
                                            : "Now: <b>" + combo->itemText(combo->findData(cur.toInt())).section("   (", 0, 0).toHtmlEscaped() + "</b>");
        now->setWordWrap(true);
        bv->addWidget(now);
        bv->addWidget(combo);
        bv->addWidget(text);
        auto *how = new QLabel("<i>" + u["how"].toString().toHtmlEscaped() + " Apply writes what the game writes then.</i>");
        how->setWordWrap(true);
        bv->addWidget(how);
        auto *apply = new QPushButton("Apply");
        apply->setEnabled(!m_running && !cur.isNull());
        bv->addWidget(apply, 0, Qt::AlignRight);
        QString id = u["id"].toString();
        connect(apply, &QPushButton::clicked, this, [this, id, combo] {
            QString e;
            run({id + "=" + QString::number(combo->currentData().toInt())}, &e);
            if (!e.isEmpty()) QMessageBox::warning(this, m_title, e);
            rebuild();
        });
        v->addWidget(box);
    }
    for (const QJsonValue &nv : st["notes"].toArray()) {
        QJsonObject n = nv.toObject();
        auto *box = new QGroupBox(n["title"].toString());
        auto *bv = new QVBoxLayout(box);
        auto *t = new QLabel(n["text"].toString().trimmed());
        t->setWordWrap(true);
        t->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bv->addWidget(t);
        v->addWidget(box);
    }
    v->addStretch(1);
}
