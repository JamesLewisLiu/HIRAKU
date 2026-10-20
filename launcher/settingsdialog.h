// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    void accept() override;
private:
    QSpinBox *m_scale, *m_audioMs, *m_volume;
    QCheckBox *m_fullscreen, *m_splitScreens, *m_displaySync, *m_fineSound, *m_audioExclusive = nullptr, *m_socket, *m_lightsOverlay, *m_hideLauncher, *m_checkUpdates;
    QComboBox *m_picture, *m_audioDevice, *m_audioBuffer;
    QComboBox *m_motion;                    // on a fast display: frames held, smooth motion, the CRT beam
    QDoubleSpinBox *m_beamGamma, *m_beamGain;
    QLineEdit *m_soundfont, *m_root, *m_bin, *m_nvram, *m_extra;
    // e-AMUSEMENT, as spicecfg's Network page (runtime/shell/eaoptions.h)
    QCheckBox *m_smartea;
    QComboBox *m_autocard;
    QLineEdit *m_eaUrl, *m_pcbid, *m_card[2];
    QComboBox *m_reader[2];                 // serial card readers (runtime/host/readers.h)
    QCheckBox *m_scard, *m_cardio;
    QComboBox *m_bio2;                      // beatmania IIDX's BIO2 board (runtime/host/bio2.h)
    QComboBox *m_popnio;                    // pop'n music's USB I/O board (runtime/host/popnio.h)
    QLineEdit *m_popnioDll;                 // its ezusb.dll
};
