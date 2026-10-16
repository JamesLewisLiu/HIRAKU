// SPDX-License-Identifier: GPL-3.0-or-later
// Map keys and controller inputs to a game family's cabinet controls.
#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "shell/cabinet.h"

class QComboBox;
class QLabel;
class QMenu;
class QScrollArea;
class QToolButton;
class QWidget;

// The cabinet picker the Controls and Lights dialogs share: the controls sets (cfg::controlsSeries)
// or the families under a header per series (cfg::familySeries), each item's data its set or family
void fillCabinetBox(QComboBox *box, bool controlSets);

class ControlsDialog : public QDialog {
    Q_OBJECT
public:
    // family: shown as its controls set (pop'n music on DJ Main, Firebeat and Python share one)
    explicit ControlsDialog(const QString &family, QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void reject() override;
private:
    struct Row {
        hk::cab::Control control;
        QLabel *label = nullptr;
        QToolButton *button = nullptr;   // click: bind; its arrow (or a right-click): add, remove, clear
        int section = 0;
    };
    // A group of controls under a header that folds it away (the e-AMUSEMENT 10-key starts folded)
    struct Section {
        QString group;
        QToolButton *header = nullptr;
        QWidget *body = nullptr;
    };
    using Bindings = QMap<QString, QStringList>;   // control name -> bindings
    void showFamily(const QString &family);
    Bindings load(const QString &family) const;
    bool save();
    void capture(int row, bool append);
    void bindingMenu(int row, QMenu *menu);
    void setFolded(int section, bool folded);
    void refreshRows();   // their bindings, and the inputs two controls of one game share
    void refreshHighlights();
    void source(const QString &binding, bool on);
    void updateJoystickList();

    QComboBox *m_familyBox;
    QLabel *m_setLabel;
    QLabel *m_joyLabel;
    QScrollArea *m_scroll;
    QString m_family;                               // the controls set shown
    QVector<Row> m_rows;
    QVector<Section> m_sections;
    QMap<QString, Bindings> m_edits;                // set -> its bindings, once shown
    QSet<QString> m_dirty;                          // sets changed since the last save
    QSet<QString> m_active;                         // sources held down now
    QHash<QString, qint64> m_flashUntil;            // analog axes: lit briefly after motion
    QHash<QString, int> m_hats;                     // "joyN:hatK" -> SDL hat bits
    QElapsedTimer m_clock;
    QTimer m_flashTimer;
    bool m_capturing = false;
};
