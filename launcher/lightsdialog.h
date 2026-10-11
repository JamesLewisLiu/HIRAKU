// SPDX-License-Identifier: GPL-3.0-or-later
// Assign a game family's cabinet lamps to HID controller LEDs, test them, and
// watch a running game's lamps live (through the runtime's lights socket).
#pragma once

#include <QDialog>
#include <QLocalSocket>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "firebeat/cabinet.h"
#include "host/hidout.h"

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;

class LightsDialog : public QDialog {
    Q_OBJECT
public:
    explicit LightsDialog(const QString &family, QWidget *parent = nullptr);
    ~LightsDialog() override;
protected:
    void reject() override;
private:
    struct Row {
        hk::cab::Lamp lamp;
        QLabel *dot = nullptr;
        QPushButton *button = nullptr;
    };
    using Outputs = QMap<QString, QStringList>;   // lamp name -> outputs
    void showFamily(const QString &family);
    Outputs load(const QString &family) const;
    bool save();
    void refreshDevices();
    void refreshRow(int k);
    void assign(int k);
    void drive(const QString &spec, bool on);
    void allOff();
    void setDot(int k, bool on);
    void onSocketData();

    QComboBox *m_familyBox;
    QLabel *m_devLabel, *m_permLabel, *m_liveLabel;
    QPushButton *m_permButton;
    QScrollArea *m_scroll;
    QString m_family;
    QVector<Row> m_rows;
    QMap<QString, Outputs> m_edits;
    QSet<QString> m_dirty;
    std::vector<hk::hidout::Device> m_devices;
    hk::hidout::Writer m_writer;
    QSet<QString> m_driven;                      // outputs this dialog lit: turned off on close
    QLocalSocket m_socket;
    QTimer m_reconnect;
    QByteArray m_buf;
    QString m_liveFamily, m_liveGame;
    QMap<QString, bool> m_liveState;             // lamp -> lit, from the socket
};
