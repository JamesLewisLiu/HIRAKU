// SPDX-License-Identifier: GPL-3.0-or-later
// A game's DIP switches (games/<id>/dips.txt, from MAME's -listxml by tools/mamedips.py): the
// cabinet's settings that the operator sets on the board, such as the coinage (Free Play) or a
// game's hidden modes, each named as MAME names it. Kept as dips/<game> ("DSW1=0x00,DSW3=0xef":
// the banks that differ from MAME's defaults) and passed to the game as --dip BANK=VALUE.
#pragma once

#include <QDialog>
#include <QList>
#include <QMap>
#include <QStringList>

class QComboBox;

class DipsDialog : public QDialog {
    Q_OBJECT
public:
    DipsDialog(const QString &game, const QString &title, QWidget *parent = nullptr);
    void accept() override;

    struct Switch {
        QString bank, where, name;   // "DSW1", "SW1:8,7,6,5", "Coinage"
        int mask = 0, preset = 0;    // MAME's default within the mask
        QList<QPair<int, QString>> settings;
    };
    static QList<Switch> read(const QString &game);   // empty: the game has none
    static bool available(const QString &game);
    static QStringList arguments(const QString &game);   // "--dip", "DSW1=0x00", ...
    static QString summary(const QString &game);         // the switches not at MAME's default: "Coinage: Free Play"

private:
    static QMap<QString, int> banks(const QString &game);   // each bank's byte: the defaults, then what's kept
    QString m_game;
    QList<Switch> m_switches;
    QList<QComboBox *> m_boxes;
};
