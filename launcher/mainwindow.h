// SPDX-License-Identifier: GPL-3.0-or-later
// The game list: pick a game, play it; controls, lights and settings.
#pragma once

#include <QMainWindow>
#include <QProcess>

#include "games.h"

class QAction;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void play(const QString &id);
protected:
    void closeEvent(QCloseEvent *e) override;
private:
    void refresh();
    void applyFilter();
    void updateActions();
    void showDetails();
    const Game *selected() const;
    const Game *find(const QString &id) const;
    QStringList arguments(const Game &g) const;
    void stop();
    void contextMenu(const QPoint &pos);
    void log(const QString &text);

    QList<Game> m_games;
    QTreeWidget *m_list;
    QLabel *m_details;
    QLineEdit *m_filter;
    QPlainTextEdit *m_log;
    QDockWidget *m_logDock;
    QAction *m_play, *m_stop, *m_controls, *m_lights, *m_showAll;
    QProcess m_proc;
    QString m_running;          // game id, while one runs
    bool m_stopping = false;
};
