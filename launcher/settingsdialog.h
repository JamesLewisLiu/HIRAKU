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
    QCheckBox *m_fullscreen, *m_displaySync, *m_fineSound, *m_socket, *m_hideLauncher;
    QComboBox *m_picture, *m_audioDevice, *m_audioBuffer;
    QLineEdit *m_soundfont, *m_root, *m_bin, *m_nvram, *m_extra;
    // e-AMUSEMENT, as spicecfg's Network page (runtime/shell/eaoptions.h)
    QCheckBox *m_smartea;
    QComboBox *m_autocard;
    QLineEdit *m_eaUrl, *m_pcbid, *m_card[2];
};
