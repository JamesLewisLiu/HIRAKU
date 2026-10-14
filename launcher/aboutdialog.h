// SPDX-License-Identifier: GPL-3.0-or-later
// Help > About HIRAKU…: what HIRAKU is, who made it, the software in it or behind it, and the
// licences. The licence texts are read from the checkout (LICENSE, third_party/), so they are
// the ones that ship with the code.
#pragma once

#include <QDialog>

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
