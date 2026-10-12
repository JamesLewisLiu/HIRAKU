// SPDX-License-Identifier: GPL-3.0-or-later
// A game's unlocks (games/<id>/unlocks.toml), as tools/unlock.py reads and writes them in the
// game's save: pick a level, Apply. The per-credit codes are shown for doing it by hand.
#pragma once

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QLabel;
class QVBoxLayout;

class UnlocksDialog : public QDialog {
    Q_OBJECT
public:
    // running: the game is running now (its save would be overwritten when it exits)
    UnlocksDialog(const QString &game, const QString &title, bool running, QWidget *parent = nullptr);
    static bool available(const QString &game);   // games/<id>/unlocks.toml exists
private:
    QJsonObject run(const QStringList &extra, QString *err);
    void rebuild();

    QString m_game, m_title;
    bool m_running;
    QVBoxLayout *m_body;
    QWidget *m_content = nullptr;
};
