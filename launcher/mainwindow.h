// SPDX-License-Identifier: GPL-3.0-or-later
// The game list: pick a game, play it; controls, lights and settings.
#pragma once

#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>

#include "games.h"

class QAction;
class QDockWidget;
class QFile;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;
class UpdateCheck;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void play(const QString &id);
    // ingest a game's files (tools/ingest.py): ROM sets, CHDs, images, folders
    void addGameFiles(const QStringList &paths);
    void setEcho(bool on) { m_echo = on; }   // the log on stderr too (hiraku --add from a terminal)
protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
private:
    void refresh();
    void applyFilter();
    void updateActions();
    void showDetails();
    const Game *selected() const;
    const Game *find(const QString &id) const;
    QStringList arguments(const Game &g) const;
    QStringList pcArguments(const Game &g) const;   // hiraku-pc's (a PC game)
    void stop();
    void contextMenu(const QPoint &pos);
    void controls(const QString &family);   // the Controls dialog, which can end by starting a game to try them
    void log(const QString &text);

    QList<Game> m_games;
    QTreeWidget *m_list;
    QLabel *m_details;
    QLineEdit *m_filter;
    QPlainTextEdit *m_log;
    QFile *m_runLog = nullptr;   // the running game's log file (cfg::logsDir): what the log pane gets
    void openRunLog(const QString &game);
    void closeRunLog();
    QDockWidget *m_logDock;
    QAction *m_play, *m_stop, *m_controls, *m_lights, *m_showAll;
    QProcess m_proc;
    QProcess m_ingest;                // tools/ingest.py, one batch at a time
    QList<QStringList> m_ingestQueue;
    QString m_ingestOutput;
    void startIngest();
    QString m_running;          // game id, while one runs
    // updates (updater.h): the bar that says a new version is out, the release it names
    void checkForUpdate(bool asked);
    void offerUpdate();
    UpdateCheck *m_check = nullptr;
    QWidget *m_updateBar = nullptr;
    QLabel *m_updateText = nullptr;
    QJsonObject m_release;
    bool m_checkAsked = false;
    bool m_updateBegun = false;
    bool m_stopping = false;
    bool m_echo = false;
};
