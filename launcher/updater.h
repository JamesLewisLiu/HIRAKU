// SPDX-License-Identifier: GPL-3.0-or-later
// Updates a release package to GitHub's latest release, through tools/update.py: the check, the
// dialog that shows what's new and downloads it, and the hand-over to the script, which puts the
// new files in once the launcher has closed and starts it again.
#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QProcess>

class QLabel;
class QProgressBar;
class QPushButton;

namespace upd {
QString packageRoot();      // the folder above the launcher's bin/
QString currentVersion();   // its VERSION file: "" in a checkout or a build by hand
bool available();           // a release package, which can update itself
void cleanUp();             // what the last update left: update/ (the download), update-old/
}

// One look at the latest release (tools/update.py check)
class UpdateCheck : public QObject {
    Q_OBJECT
public:
    explicit UpdateCheck(QObject *parent = nullptr);
    void start();
signals:
    // ok: rel is the release (current, latest, newer, name, notes, page, size); else error says why
    void done(bool ok, const QJsonObject &rel, const QString &error);
private:
    QProcess m_proc;
};

// What's new, then the download; accepted = ready: the launcher closes and startInstall() runs
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    UpdateDialog(const QJsonObject &rel, QWidget *parent = nullptr);
    // the script that puts the new files in, started on its own: false if it couldn't start
    bool startInstall(QString *error) const;
    void reject() override;
private:
    void download();
    QJsonObject m_rel;
    QProcess m_proc;
    QLabel *m_status;
    QProgressBar *m_bar;
    QPushButton *m_go;
    QString m_err;
};
