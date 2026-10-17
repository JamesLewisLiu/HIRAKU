// SPDX-License-Identifier: GPL-3.0-or-later
#include "fit.h"

#include <QEvent>
#include <QScreen>
#include <QWidget>

bool ScreenFit::eventFilter(QObject *o, QEvent *e) {
    if (e->type() != QEvent::Show) return false;
    auto *w = qobject_cast<QWidget *>(o);
    if (!w || !w->isWindow() || w->property("hk_fitted").toBool()) return false;
    w->setProperty("hk_fitted", true);
    const QScreen *screen = w->screen();
    if (!screen) return false;
    const QRect free = screen->availableGeometry();
    // the frame (title bar, borders) around the window's own size
    const QSize frame = w->frameGeometry().size() - w->geometry().size();
    const int maxw = free.width() - frame.width(), maxh = free.height() - frame.height();
    if (w->width() > maxw || w->height() > maxh) {
        w->setMinimumSize(std::min(w->minimumWidth(), maxw), std::min(w->minimumHeight(), maxh));
        w->resize(std::min(w->width(), maxw), std::min(w->height(), maxh));
    }
    QRect g = w->frameGeometry();
    if (!free.contains(g)) {   // (a Wayland compositor places windows itself: this then does nothing)
        g.moveLeft(std::clamp(g.left(), free.left(), std::max(free.left(), free.right() - g.width() + 1)));
        g.moveTop(std::clamp(g.top(), free.top(), std::max(free.top(), free.bottom() - g.height() + 1)));
        w->move(g.topLeft());
    }
    return false;
}
