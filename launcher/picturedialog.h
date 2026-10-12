// SPDX-License-Identifier: GPL-3.0-or-later
// The picture: off (sharp pixels), light (crt-guest-advanced-fast) or full (crt-royale), for
// all games or one, and each shader's settings as sliders.
//
// Settings are layered, as the game reads them (runtime/host/picture.h): the shader's own
// defaults, HIRAKU's (shaders/<name>/hiraku.params), all games' (<config>/picture/<name>.params),
// then the game's (<config>/picture/games/<id>/<name>.params). Each file holds only what differs
// from the layer under it. Files are written as the sliders move, and a running game re-reads
// them, so changes show at once; Cancel puts the files back.
#pragma once

#include <QDialog>
#include <QMap>
#include <QString>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QTabWidget;
class QToolButton;
class QTimer;
class QWidget;

namespace picture {

// The mode for a game: its own, else all games' ("off", "light", "full").
QString mode(const QString &game);
QString globalMode();
QString configDir();                       // <config>/picture
QString configDir(const QString &game);    // <config>/picture/games/<id>

}  // namespace picture

class PictureDialog : public QDialog {
    Q_OBJECT
public:
    // game: the selected game's id ("" if none); the dialog opens on it when `onGame`
    PictureDialog(const QString &game, const QString &gameTitle, bool onGame, QWidget *parent = nullptr);
    void accept() override;
    void reject() override;

private:
    struct Param {
        QString name, label, section;
        double def = 0, min = 0, max = 0, step = 0;
    };
    struct Shader {
        QString name, title;
        QVector<Param> params;
        QMap<QString, double> base;   // the shader's defaults + HIRAKU's
        QWidget *page = nullptr;
    };
    struct Row {
        QLabel *label;
        QSlider *slider;
        QDoubleSpinBox *spin;
        QToolButton *reset;
    };
    void buildPage(int s);
    void setValue(int s, int row, double v, bool fromSlider);
    void load();                      // the values for the current scope
    void save(int s);                 // write the scope's file for shader s
    QMap<QString, double> inherited(int s) const;
    QString file(int s, bool game) const;

    QString m_game, m_gameTitle;
    bool m_onGame = false;
    QComboBox *m_scope, *m_mode;
    QTabWidget *m_tabs;
    QVector<Shader> m_shaders;
    QMap<QString, QByteArray> m_backup;   // file -> contents at open ("" with the flag: absent)
    QMap<QString, bool> m_existed;
    QString m_globalMode, m_gameMode;
    QVector<QMap<QString, double>> m_values;   // per shader, the current scope's values
    QVector<QVector<Row>> m_rows;
    QVector<QTimer *> m_saveTimers;
    bool m_loading = false;
};
