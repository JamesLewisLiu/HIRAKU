// SPDX-License-Identifier: GPL-3.0-or-later
// Inputs as the game will see them: SDL joysticks (polled on a timer) and key
// names in SDL's spelling, so that a captured binding is exactly what the
// runtime's bindings parser (runtime/host/input.cpp) reads back.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

class QKeyEvent;

// "key:Z", or "" when the key cannot be bound (unknown, or '#' which starts a comment)
QString bindingForKey(const QKeyEvent *e);
// "Z", "Pad 0: button 3", "Pad 0: axis 1+", "Pad 1: hat 0 up"
QString describeBinding(const QString &b);
// One spelling per source, so bindings compare as strings ("key:," -> "key:Comma",
// "key:z" -> "key:Z"); unparsable keys are returned unchanged
QString normalizeBinding(const QString &b);

// One SDL joystick per index, in the order a game started now would number them (joy0, joy1, ...)
class Joysticks : public QObject {
    Q_OBJECT
public:
    static Joysticks &instance();
    QStringList names() const;          // "joy0: DAO FPS (14 buttons, 2 axes)"
    int count() const { return (int)m_joys.size(); }
    int axisValue(int joy, int axis) const;
    int axisCount(int joy) const;
signals:
    void devicesChanged();
    void button(int joy, int button, bool down);
    void axis(int joy, int axis, int value);
    void hat(int joy, int hat, int value);   // SDL_HAT_* bits
private:
    Joysticks();
    void poll();
    void reopen();
    QTimer m_timer;
    QVector<void *> m_joys;             // SDL_Joystick *
};
