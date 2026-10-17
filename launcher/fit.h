// SPDX-License-Identifier: GPL-3.0-or-later
// Every launcher window inside the screen it opens on, a 640x480 one included: installed on the
// application, it shrinks a window (dialog, message box, the main window) the first time it is
// shown when it is larger than the screen's free area, and moves it in. The dialogs' contents
// scroll where they are tall (QScrollArea), so they shrink that far.
#pragma once

#include <QObject>

class ScreenFit : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override;
};
