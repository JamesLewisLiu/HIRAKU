// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    void accept() override;
private:
    QSpinBox *m_scale, *m_audioMs;
    QCheckBox *m_fullscreen, *m_socket, *m_hideLauncher;
    QComboBox *m_picture;
    QLineEdit *m_soundfont, *m_root, *m_bin, *m_nvram, *m_extra;
};
