// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <cstdio>

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QMimeData>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "config.h"
#include "controlsdialog.h"
#include "lightsdialog.h"
#include "picturedialog.h"
#include "unlocksdialog.h"
#include "settingsdialog.h"

namespace {

enum Col { Title, Series, Year, Status, NCols };

// Sorts by the clicked column, then series, year and title
class GameItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &o) const override {
        int c = treeWidget() ? treeWidget()->sortColumn() : 0;
        auto key = [c](const QTreeWidgetItem &i) {
            return QStringList{i.text(c), i.text(Series), i.text(Year), i.text(Title)}.join('\x1f');
        };
        return QString::localeAwareCompare(key(*this), key(o)) < 0;
    }
};

bool lightsFileUsed(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (!line.trimmed().startsWith('#') && line.contains("hid:")) return true;
    return false;
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle("HIRAKU");
    setWindowIcon(familyIcon("bm3"));
    resize(900, 600);

    // ---- actions ----
    m_play = new QAction(QIcon::fromTheme("media-playback-start"), "Play", this);
    m_play->setShortcut(QKeySequence(Qt::Key_Return));
    m_stop = new QAction(QIcon::fromTheme("media-playback-stop"), "Stop", this);
    m_controls = new QAction(QIcon::fromTheme("input-gaming"), "Controls", this);
    m_lights = new QAction(QIcon::fromTheme("dialog-information", QIcon::fromTheme("weather-clear")), "Lights", this);
    auto *settings = new QAction(QIcon::fromTheme("preferences-system"), "Settings", this);
    auto *pictureAct = new QAction(QIcon::fromTheme("video-display", QIcon::fromTheme("preferences-desktop-display")), "Picture", this);
    pictureAct->setToolTip("Sharp pixels or a CRT (light, full), and the CRT's settings: for all games, or the selected one");
    connect(pictureAct, &QAction::triggered, this, [this] {
        auto *g = selected();
        PictureDialog d(g ? g->id : QString(), g ? g->title : QString(), false, this);
        if (d.exec() == QDialog::Accepted) showDetails();
    });
    auto *addFiles = new QAction(QIcon::fromTheme("list-add"), "Add game files…", this);
    addFiles->setToolTip("Add a game from its files: the MAME ROM set (.zip/.7z), CHDs or disc images.\n"
                         "You can also drop them on this window.");
    connect(addFiles, &QAction::triggered, this, [this] {
        QStringList f = QFileDialog::getOpenFileNames(this, "Add game files", QString(),
            "Game files (*.zip *.7z *.chd *.cue *.iso *.img *.bin);;All files (*)");
        if (!f.isEmpty()) addGameFiles(f);
    });
    auto *addFolder = new QAction(QIcon::fromTheme("folder-new"), "Add a game folder…", this);
    connect(addFolder, &QAction::triggered, this, [this] {
        QString d = QFileDialog::getExistingDirectory(this, "Add a game folder (its ROM set, CHDs, images)");
        if (!d.isEmpty()) addGameFiles({d});
    });
    setAcceptDrops(true);
    auto *refreshAct = new QAction(QIcon::fromTheme("view-refresh"), "Refresh", this);
    refreshAct->setShortcut(QKeySequence::Refresh);
    m_showAll = new QAction("Show games that are not installed", this);
    m_showAll->setCheckable(true);
    m_showAll->setChecked(cfg::settings().value("ui/showAll", true).toBool());

    connect(m_play, &QAction::triggered, this, [this] {
        if (auto *g = selected()) play(g->id);
    });
    connect(m_stop, &QAction::triggered, this, &MainWindow::stop);
    connect(m_controls, &QAction::triggered, this, [this] {
        auto *g = selected();
        ControlsDialog d(g ? g->family : "bm3", this);
        d.exec();
    });
    connect(m_lights, &QAction::triggered, this, [this] {
        auto *g = selected();
        LightsDialog d(g ? g->family : "popn", this);
        d.exec();
    });
    connect(settings, &QAction::triggered, this, [this] {
        SettingsDialog d(this);
        if (d.exec() == QDialog::Accepted) refresh();
    });
    connect(refreshAct, &QAction::triggered, this, &MainWindow::refresh);
    connect(m_showAll, &QAction::toggled, this, [this](bool on) {
        cfg::settings().setValue("ui/showAll", on);
        applyFilter();
    });

    // ---- toolbar ----
    auto *tb = addToolBar("Main");
    tb->setObjectName("main toolbar");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->addAction(m_play);
    tb->addAction(m_stop);
    tb->addSeparator();
    tb->addAction(m_controls);
    tb->addAction(m_lights);
    tb->addAction(pictureAct);
    tb->addAction(settings);
    tb->addSeparator();
    tb->addAction(refreshAct);
    tb->addAction(addFiles);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Search");
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(240);
    tb->addWidget(m_filter);
    connect(m_filter, &QLineEdit::textChanged, this, &MainWindow::applyFilter);

    // ---- the list and the details under it ----
    m_list = new QTreeWidget;
    m_list->setColumnCount(NCols);
    m_list->setHeaderLabels({"Title", "Series", "Year", "Status"});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setIconSize(QSize(32, 32));
    m_list->setSortingEnabled(true);
    m_list->setAlternatingRowColors(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(Title, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(Series, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(Year, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(Status, QHeaderView::ResizeToContents);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (auto *g = selected()) play(g->id);
    });
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
        updateActions();
        showDetails();
    });
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, &MainWindow::contextMenu);

    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_details->setMargin(8);
    auto *details = new QScrollArea;
    details->setWidgetResizable(true);
    details->setFrameShape(QFrame::NoFrame);
    details->setWidget(m_details);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(m_list);
    split->addWidget(details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    split->setSizes({420, 180});
    setCentralWidget(split);

    // ---- log ----
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFont("monospace"));
    m_logDock = new QDockWidget("Log", this);
    m_logDock->setObjectName("log");
    m_logDock->setWidget(m_log);
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
    m_logDock->hide();

    // ---- menus ----
    auto *file = menuBar()->addMenu("&File");
    file->addAction(m_play);
    file->addAction(m_stop);
    file->addSeparator();
    file->addAction(addFiles);
    file->addAction(addFolder);
    file->addSeparator();
    file->addAction("Open the HIRAKU folder", this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(cfg::rootDir())); });
    file->addAction("Open the settings folder", this, [] {
        QDir().mkpath(cfg::configDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(cfg::configDir()));
    });
    file->addSeparator();
    file->addAction("Quit", QKeySequence::Quit, this, &QWidget::close);
    auto *opts = menuBar()->addMenu("&Options");
    opts->addAction(m_controls);
    opts->addAction(m_lights);
    opts->addAction(pictureAct);
    opts->addAction(settings);
    auto *view = menuBar()->addMenu("&View");
    view->addAction(m_showAll);
    view->addAction(m_logDock->toggleViewAction());
    view->addAction(refreshAct);
    auto *help = menuBar()->addMenu("&Help");
    help->addAction("How to play (docs/PLAY.md)", this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(cfg::rootDir() + "/docs/PLAY.md"));
    });
    help->addAction("Lights (docs/LIGHTS.md)", this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(cfg::rootDir() + "/docs/LIGHTS.md"));
    });
    help->addAction("About HIRAKU", this, [this] {
        QMessageBox::about(this, "About HIRAKU",
                           "<b>HIRAKU</b><br>Konami Firebeat games, statically recompiled to native code.<br><br>"
                           "HIRAKU ships no game data: it runs your own dumps.<br>Licence: GPL-3.0-or-later.");
    });

    // ---- the game process ----
    m_ingest.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_ingest, &QProcess::readyRead, this, [this] {
        QString t = QString::fromLocal8Bit(m_ingest.readAll());
        m_ingestOutput += t;
        log(t);
    });
    connect(&m_ingest, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        refresh();
        QStringList added, errors;
        for (const QString &l : m_ingestOutput.split('\n')) {
            if (l.startsWith("wrote") && l.contains("machine.txt")) added << l.section('/', -2, -2);
            if (l.startsWith("ERROR") || l.startsWith("NEXT")) errors << l.mid(9).trimmed();
        }
        bool ok = st == QProcess::NormalExit && code == 0;
        QString msg = ok ? "Added: " + added.join(", ") : "The files could not be added.";
        if (!errors.isEmpty()) msg += "\n\n" + errors.join("\n");
        log("\n--- " + (ok ? QString("added ") + added.join(", ") : QString("ingest failed")) + " ---\n");
        if (ok) QMessageBox::information(this, "HIRAKU", msg);
        else QMessageBox::warning(this, "HIRAKU", msg);
        startIngest();   // the next batch, if any
    });
    connect(&m_ingest, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        QMessageBox::warning(this, "HIRAKU", "Could not run tools/ingest.py (python3 is needed):\n" + m_ingest.errorString());
    });
    m_proc.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_proc, &QProcess::readyRead, this, [this] { log(QString::fromLocal8Bit(m_proc.readAll())); });
    connect(&m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        QString title = find(m_running) ? find(m_running)->title : m_running;
        bool ok = m_stopping || (st == QProcess::NormalExit && (code == 0 || code == 130));
        QString msg = st == QProcess::CrashExit ? title + " crashed" : QString("%1 exited (code %2)").arg(title).arg(code);
        log("\n--- " + msg + " ---\n");
        statusBar()->showMessage(ok ? title + " stopped" : msg);
        if (!ok) m_logDock->show();
        m_running.clear();
        m_stopping = false;
        if (isHidden()) show();
        updateActions();
    });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        QMessageBox::warning(this, "HIRAKU", "Could not start " + m_proc.program() + ":\n" + m_proc.errorString());
        m_running.clear();
        if (isHidden()) show();
        updateActions();
    });

    restoreGeometry(cfg::settings().value("ui/geometry").toByteArray());
    restoreState(cfg::settings().value("ui/state").toByteArray());
    refresh();
    m_list->sortByColumn(cfg::settings().value("ui/sortColumn", Series).toInt(),
                         (Qt::SortOrder)cfg::settings().value("ui/sortOrder", Qt::AscendingOrder).toInt());
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (!m_running.isEmpty()) {
        auto r = QMessageBox::question(this, "HIRAKU", "A game is running. Stop it and quit?");
        if (r != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        stop();
        m_proc.waitForFinished(5000);
    }
    auto &s = cfg::settings();
    s.setValue("ui/geometry", saveGeometry());
    s.setValue("ui/state", saveState());
    s.setValue("ui/sortColumn", m_list->sortColumn());
    s.setValue("ui/sortOrder", (int)m_list->header()->sortIndicatorOrder());
    if (auto *g = selected()) s.setValue("ui/lastGame", g->id);
    e->accept();
}

void MainWindow::refresh() {
    QString keep = selected() ? selected()->id : cfg::settings().value("ui/lastGame").toString();
    m_games = scanGames();
    m_list->clear();
    for (const Game &g : m_games) {
        auto *it = new GameItem(m_list);
        it->setText(Title, g.title);
        it->setIcon(Title, familyIcon(g.family));
        it->setText(Series, cfg::familyName(g.family));
        it->setText(Year, g.year);
        it->setText(Status, g.statusText());
        it->setData(Title, Qt::UserRole, g.id);
        it->setToolTip(Title, g.id + (g.mame.isEmpty() ? QString() : "  (MAME: " + g.mame + ")"));
        if (g.status != Game::Ready) {
            QColor grey = m_list->palette().color(QPalette::Disabled, QPalette::Text);
            for (int c = 0; c < NCols; c++) it->setForeground(c, grey);
        } else {
            it->setForeground(Status, QColor(0x2e, 0x9e, 0x4a));
        }
        if (g.id == keep) m_list->setCurrentItem(it);
    }
    applyFilter();
    if (!m_list->currentItem() || m_list->currentItem()->isHidden())
        for (int k = 0; k < m_list->topLevelItemCount(); k++)
            if (!m_list->topLevelItem(k)->isHidden()) {
                m_list->setCurrentItem(m_list->topLevelItem(k));
                break;
            }
    int ready = 0;
    for (auto &g : m_games) ready += g.status == Game::Ready;
    statusBar()->showMessage(QString("%1 of %2 games ready to play").arg(ready).arg(m_games.size()));
    updateActions();
    showDetails();
}

void MainWindow::applyFilter() {
    QString f = m_filter->text().trimmed();
    for (int k = 0; k < m_list->topLevelItemCount(); k++) {
        auto *it = m_list->topLevelItem(k);
        const Game *g = find(it->data(Title, Qt::UserRole).toString());
        bool show = g && (m_showAll->isChecked() || g->status != Game::NotInstalled);
        if (show && !f.isEmpty())
            show = g->title.contains(f, Qt::CaseInsensitive) || g->id.contains(f, Qt::CaseInsensitive) ||
                   cfg::familyName(g->family).contains(f, Qt::CaseInsensitive) || g->year.contains(f);
        it->setHidden(!show);
    }
}

const Game *MainWindow::find(const QString &id) const {
    for (const Game &g : m_games)
        if (g.id == id) return &g;
    return nullptr;
}

const Game *MainWindow::selected() const {
    auto *it = m_list->currentItem();
    return it && !it->isHidden() ? find(it->data(Title, Qt::UserRole).toString()) : nullptr;
}

void MainWindow::updateActions() {
    const Game *g = selected();
    bool running = !m_running.isEmpty();
    m_play->setEnabled(g && g->status == Game::Ready && !running);
    m_stop->setEnabled(running);
}

void MainWindow::showDetails() {
    const Game *g = selected();
    if (!g) {
        m_details->setText(m_games.isEmpty() ? "No games found in " + cfg::rootDir().toHtmlEscaped() +
                                                   "/games. Is the HIRAKU folder right (Settings)?"
                                             : QString());
        return;
    }
    QString h = "<b>" + g->title.toHtmlEscaped() + "</b>";
    if (!g->maker.isEmpty()) h += " · " + g->maker.toHtmlEscaped() + " " + g->year;
    h += " · " + g->statusText();
    if (g->status != Game::Ready) {
        QString how = g->howTo().toHtmlEscaped();
        int pre = how.indexOf("\n\n", how.indexOf("\n\n") + 2);   // the commands, after the second paragraph break
        h += "<p>" + how.left(pre).replace("\n\n", "</p><p>") + "</p><pre>" + how.mid(pre + 2) + "</pre>";
    } else {
        bool fresh = !QFileInfo::exists(cfg::nvramDir(g->id));
        if (fresh && !g->firstBoot.isEmpty())
            h += "<p><b>First boot:</b> " + g->firstBoot.toHtmlEscaped() + "</p>";
        h += "<p>Saves: <tt>" + cfg::nvramDir(g->id).toHtmlEscaped() + "</tt>" + (fresh ? " (none yet)" : "") + "</p>";
        QString m = picture::mode(g->id);
        bool own = cfg::settings().contains("picture/" + g->id) || QFileInfo::exists(picture::configDir(g->id));
        if (UnlocksDialog::available(g->id)) h += "<p>Unlocks and codes: right-click the game &gt; Unlocks.</p>";
        h += QString("<p>Picture: %1%2</p>")
                 .arg(m == "full" ? "CRT full (crt-royale)" : m == "light" ? "CRT light (crt-guest-advanced-fast)" : "sharp pixels",
                      own ? ", with its own settings" : "");
    }
    m_details->setText(h);
}

QStringList MainWindow::arguments(const Game &g) const {
    auto &s = cfg::settings();
    QStringList a = {"--window", QString::number(s.value("video/scale", 2).toInt())};
    if (s.value("video/fullscreen", false).toBool()) a << "--fullscreen";
    // the picture mode, and its settings: all games', then the game's (picturedialog.h)
    a << "--picture" << picture::mode(g.id) << "--picture-config" << picture::configDir() << "--picture-config"
      << picture::configDir(g.id);
    a << "--audio-ms" << QString::number(s.value("audio/ms", 8).toInt());
    QString controls = cfg::controlsFile(g.family), lights = cfg::lightsFile(g.family);
    if (QFileInfo::exists(controls)) a << "--bindings" << controls;
    if (lightsFileUsed(lights)) a << "--lights" << lights;
    if (s.value("lights/socket", true).toBool()) a << "--lights-socket" << cfg::lightsSocket();
    QString sf = s.value("audio/soundfont").toString();
    if (g.family == "kbm" && !sf.isEmpty()) a << "--soundfont" << sf;
    if (!cfg::nvramBase().isEmpty()) a << "--nvram" << cfg::nvramDir(g.id);
    a << UnlocksDialog::macroArguments(g.id);   // codes typed at every start
    a << QProcess::splitCommand(s.value("game/extraArgs").toString());
    return a;
}

void MainWindow::play(const QString &id) {
    const Game *g = find(id);
    if (!g) {
        statusBar()->showMessage("No game " + id);
        return;
    }
    if (!m_running.isEmpty()) return;
    if (g->status != Game::Ready) {
        QMessageBox box(this);
        box.setWindowTitle(g->title);
        box.setIcon(QMessageBox::Information);
        box.setText(g->title + " is not ready to play.");
        box.setInformativeText(g->howTo());
        box.exec();
        return;
    }
    m_log->clear();
    QStringList args = arguments(*g);
    log("$ " + g->exe + " " + args.join(' ') + "\n");
    m_running = g->id;
    m_stopping = false;
    m_proc.setWorkingDirectory(cfg::rootDir());
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("HIRAKU_ROOT", cfg::rootDir());   // the games use the launcher's root
        m_proc.setProcessEnvironment(env);
    }
    m_proc.start(g->exe, args);
    statusBar()->showMessage("Running " + g->title);
    cfg::settings().setValue("ui/lastGame", g->id);
    updateActions();
    if (cfg::settings().value("ui/hideWhileRunning", false).toBool()) hide();
}

void MainWindow::stop() {
    if (m_running.isEmpty()) return;
    m_stopping = true;
    m_proc.terminate();   // SIGTERM: the game saves its NVRAM and exits at the next frame
    QTimer::singleShot(5000, this, [this] {
        if (m_proc.state() != QProcess::NotRunning) m_proc.kill();
    });
}

void MainWindow::contextMenu(const QPoint &pos) {
    auto *it = m_list->itemAt(pos);
    if (!it) return;
    m_list->setCurrentItem(it);
    const Game *g = selected();
    if (!g) return;
    QString id = g->id, title = g->title, family = g->family;
    QMenu menu;
    menu.addAction(m_play);
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme("input-gaming"), "Controls for " + cfg::familyName(family) + "…", this, [this, family] {
        ControlsDialog d(family, this);
        d.exec();
    });
    menu.addAction("Lights for " + cfg::familyName(family) + "…", this, [this, family] {
        LightsDialog d(family, this);
        d.exec();
    });
    menu.addAction(QIcon::fromTheme("video-display"), "Picture for " + title + "…", this, [this, id, title] {
        PictureDialog d(id, title, true, this);
        if (d.exec() == QDialog::Accepted) showDetails();
    });
    if (UnlocksDialog::available(id))
        menu.addAction(QIcon::fromTheme("unlock", QIcon::fromTheme("object-unlocked")), "Unlocks…", this, [this, id, title] {
            UnlocksDialog d(id, title, m_running == id, this);
            d.exec();
        });
    menu.addSeparator();
    QString nv = cfg::nvramDir(id);
    auto *open = menu.addAction(QIcon::fromTheme("folder-open"), "Open the save folder", this,
                                [nv] { QDesktopServices::openUrl(QUrl::fromLocalFile(nv)); });
    open->setEnabled(QFileInfo::exists(nv));
    auto *reset = menu.addAction(QIcon::fromTheme("edit-delete"), "Reset save data…", this, [this, nv, title, id] {
        if (m_running == id) {
            QMessageBox::information(this, title, "Stop the game first.");
            return;
        }
        auto r = QMessageBox::question(this, title,
                                       "Move " + title + "'s save data (operator settings, rankings, sound flash) to the trash?\n\n" + nv +
                                           "\n\nThe next start is a first boot, as on a new cabinet.");
        if (r != QMessageBox::Yes) return;
        if (!QFile::moveToTrash(nv)) QMessageBox::warning(this, title, "Could not move " + nv + " to the trash.");
        showDetails();
    });
    reset->setEnabled(QFileInfo::exists(nv));
    menu.addSeparator();
    if (g->status == Game::Ready) {
        QStringList args = arguments(*g);
        for (QString &a : args)
            if (a.contains(' ')) a = "'" + a + "'";
        QString cmd = g->exe + " " + args.join(' ');
        menu.addAction("Copy the command line", this, [cmd] { QApplication::clipboard()->setText(cmd); });
    }
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e) {
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (!paths.isEmpty()) {
        e->acceptProposedAction();
        addGameFiles(paths);
    }
}

void MainWindow::addGameFiles(const QStringList &paths) {
    m_ingestQueue << paths;
    if (m_ingest.state() == QProcess::NotRunning) startIngest();
}

void MainWindow::startIngest() {
    if (m_ingestQueue.isEmpty()) return;
    QStringList paths = m_ingestQueue.takeFirst();
    QString script = cfg::rootDir() + "/tools/ingest.py";
    if (!QFileInfo::exists(script)) {
        QMessageBox::warning(this, "HIRAKU", script + " is missing: is the HIRAKU folder set right (Settings)?");
        return;
    }
    m_ingestOutput.clear();
    m_logDock->show();
    log("\n$ tools/ingest.py " + paths.join(' ') + "\n");
    m_ingest.setWorkingDirectory(cfg::rootDir());
    m_ingest.start("python3", QStringList{script} + paths);
}

void MainWindow::log(const QString &text) {
    if (m_echo) { fputs(text.toLocal8Bit().constData(), stderr); fflush(stderr); }
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(text);
    m_log->moveCursor(QTextCursor::End);
}
