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

#include "firebeat/cabinet.h"

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;

class ControlsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ControlsDialog(const QString &family, QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void reject() override;
private:
    struct Row {
        hk::cab::Control control;
        QLabel *label = nullptr;
        QPushButton *button = nullptr;
    };
    using Bindings = QMap<QString, QStringList>;   // control name -> bindings
    void showFamily(const QString &family);
    Bindings load(const QString &family) const;
    bool save();
    void capture(int row, bool append);
    void refreshRow(int row);
    void refreshHighlights();
    void source(const QString &binding, bool on);
    void updateJoystickList();

    QComboBox *m_familyBox;
    QLabel *m_joyLabel;
    QScrollArea *m_scroll;
    QString m_family;
    QVector<Row> m_rows;
    QMap<QString, Bindings> m_edits;                // family -> its bindings, once shown
    QSet<QString> m_dirty;                          // families changed since the last save
    QSet<QString> m_active;                         // sources held down now
    QHash<QString, qint64> m_flashUntil;            // analog axes: lit briefly after motion
    QHash<QString, int> m_hats;                     // "joyN:hatK" -> SDL hat bits
    QElapsedTimer m_clock;
    QTimer m_flashTimer;
    bool m_capturing = false;
};
