// SPDX-License-Identifier: GPL-3.0-or-later
#include "controlsdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

#include <SDL3/SDL.h>

#include "config.h"
#include "inputs.h"

namespace {

QString hatDir(int bits) {
    if (bits & SDL_HAT_UP) return "up";
    if (bits & SDL_HAT_DOWN) return "down";
    if (bits & SDL_HAT_LEFT) return "left";
    if (bits & SDL_HAT_RIGHT) return "right";
    return {};
}

// Waits for one key press or controller input and returns it as a binding.
class CaptureDialog : public QDialog {
public:
    CaptureDialog(const QString &what, bool analog, QWidget *parent) : QDialog(parent), m_analog(analog) {
        setWindowTitle("Set binding");
        auto *v = new QVBoxLayout(this);
        auto *title = new QLabel("<b>" + what.toHtmlEscaped() + "</b>");
        title->setAlignment(Qt::AlignCenter);
        v->addWidget(title);
        m_text = new QLabel;
        m_text->setAlignment(Qt::AlignCenter);
        m_text->setMinimumWidth(360);
        v->addWidget(m_text);
        auto *cancel = new QPushButton("Cancel");
        cancel->setAutoDefault(false);
        cancel->setFocusPolicy(Qt::NoFocus);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        v->addWidget(cancel, 0, Qt::AlignCenter);
        auto &joys = Joysticks::instance();
        for (int j = 0; j < joys.count(); j++)
            for (int a = 0; a < joys.axisCount(j); a++) m_base[QString("%1:%2").arg(j).arg(a)] = joys.axisValue(j, a);
        connect(&joys, &Joysticks::button, this, [this](int j, int b, bool down) {
            if (down && !m_analog) finish(QString("joy%1:button%2").arg(j).arg(b));
        });
        connect(&joys, &Joysticks::hat, this, [this](int j, int h, int v) {
            if (!m_analog && !hatDir(v).isEmpty()) finish(QString("joy%1:hat%2:%3").arg(j).arg(h).arg(hatDir(v)));
        });
        connect(&joys, &Joysticks::axis, this, [this](int j, int a, int v) { onAxis(j, a, v); });
        connect(&m_tick, &QTimer::timeout, this, [this] {
            if (--m_left <= 0) reject();
            else showText();
        });
        m_tick.start(1000);
        showText();
    }
    QString result() const { return m_result; }
protected:
    void showEvent(QShowEvent *e) override {
        QDialog::showEvent(e);
        grabKeyboard();
    }
    void hideEvent(QHideEvent *e) override {
        releaseKeyboard();
        QDialog::hideEvent(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->isAutoRepeat()) return;
        if (e->key() == Qt::Key_Escape) { reject(); return; }
        if (m_analog) return;
        QString b = bindingForKey(e);
        if (b.isEmpty()) m_text->setText("That key cannot be bound. Try another one.");
        else finish(b);
    }
private:
    void showText() {
        m_text->setText(m_analog ? QString("Spin the turntable (move an analog axis)…\n\nEsc cancels (%1 s)").arg(m_left)
                                 : QString("Press a key or a controller button…\n\nEsc cancels (%1 s)").arg(m_left));
    }
    void onAxis(int j, int a, int v) {
        int base = m_base.value(QString("%1:%2").arg(j).arg(a), 0);
        int d = v - base;
        if (d > 32768) d -= 65536;   // turntable encoders wrap
        if (d < -32768) d += 65536;
        if (m_analog) {
            if (std::abs(d) > 3000) finish(QString("joy%1:axis%2").arg(j).arg(a));
            return;
        }
        // As a button: the runtime's thresholds; an axis that rests past one (a trigger) must move first
        if (v > 16384 && base <= 16384) finish(QString("joy%1:axis%2+").arg(j).arg(a));
        else if (v < -16384 && base >= -16384) finish(QString("joy%1:axis%2-").arg(j).arg(a));
    }
    void finish(const QString &b) {
        m_result = b;
        accept();
    }
    bool m_analog;
    QLabel *m_text;
    QTimer m_tick;
    int m_left = 6;
    QHash<QString, int> m_base;
    QString m_result;
};

// The controls of a set: each member family's, once, in the order they first come. One that only
// some members have says which, in its label, or for a whole group of them (Python's 10-key) in
// the group's title.
const std::vector<hk::cab::Control> &setControls(const QString &set) {
    static QHash<QString, std::vector<hk::cab::Control>> cache;
    if (auto it = cache.constFind(set); it != cache.constEnd()) return *it;
    QStringList members = cfg::controlsSetMembers(set);
    std::vector<hk::cab::Control> out;
    QHash<QString, QStringList> has;   // control -> the members that have it
    for (const QString &m : members)
        for (auto &c : hk::cab::controls(m.toStdString())) {
            QString n = QString::fromStdString(c.name);
            if (!has.contains(n)) out.push_back(c);
            has[n] << m;
        }
    QHash<QString, QSet<QString>> groupHas;   // group -> its controls' member lists
    for (auto &c : out) groupHas[QString::fromStdString(c.group)] << has[QString::fromStdString(c.name)].join(",");
    for (auto &c : out) {
        const QStringList &h = has[QString::fromStdString(c.name)];
        if (h.size() == members.size()) continue;
        QStringList boards;
        for (const QString &m : h) boards << cfg::controlsMemberName(m);
        std::string only = " (" + boards.join(", ").toStdString() + " only)";
        if (groupHas[QString::fromStdString(c.group)].size() == 1) c.group += only;
        else c.label += only;
    }
    return cache[set] = out;
}

}  // namespace

void fillCabinetBox(QComboBox *box, bool controlSets) {
    auto *model = new QStandardItemModel(box);
    QFont bold = box->font();
    bold.setBold(true);
    for (auto &[series, fams] : controlSets ? cfg::controlsSeries() : cfg::familySeries()) {
        if (!series.isEmpty()) {
            auto *header = new QStandardItem(series);
            header->setFlags(Qt::NoItemFlags);   // not a choice
            header->setFont(bold);
            model->appendRow(header);
        }
        for (const QString &f : fams) {
            auto *it = new QStandardItem(controlSets ? cfg::controlsSetName(f) : cfg::familyName(f));
            it->setData(f, Qt::UserRole);
            model->appendRow(it);
        }
    }
    box->setModel(model);
    box->setMaxVisibleItems(model->rowCount());
}

ControlsDialog::ControlsDialog(const QString &family, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Controls");
    resize(640, 720);
    auto *v = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("Cabinet:"));
    m_familyBox = new QComboBox;
    fillCabinetBox(m_familyBox, true);
    top->addWidget(m_familyBox, 1);
    v->addLayout(top);
    m_setLabel = new QLabel;
    m_setLabel->setWordWrap(true);
    v->addWidget(m_setLabel);

    m_joyLabel = new QLabel;
    m_joyLabel->setWordWrap(true);
    m_joyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_joyLabel);
    auto *hint = new QLabel("Click a binding, then press a key or a controller button. Its <b>▾</b> menu (or a "
                            "right-click) adds a second binding or removes one. A warning sign marks an input that "
                            "presses two controls in one game. Press anything bound and its name lights up.");
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    v->addWidget(m_scroll, 1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    for (auto *ab : box->buttons()) {
        auto *b = qobject_cast<QPushButton *>(ab);
        b->setAutoDefault(false);
        b->setDefault(false);
        b->setFocusPolicy(Qt::NoFocus);   // keys pressed to test bindings must not click them
    }
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        if (save()) accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &ControlsDialog::reject);
    connect(box->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        Bindings d;
        for (auto &c : setControls(m_family)) {
            QStringList l;
            auto entries = hk::cab::parse_list_file(c.name + " = " + c.defaults);
            for (auto &it : entries[0].items) l << normalizeBinding(QString::fromStdString(it));
            d[QString::fromStdString(c.name)] = l;
        }
        m_edits[m_family] = d;
        m_dirty.insert(m_family);
        refreshRows();
    });

    connect(m_familyBox, &QComboBox::currentIndexChanged, this, [this] { showFamily(m_familyBox->currentData().toString()); });
    int idx = m_familyBox->findData(cfg::controlsSet(family));
    m_familyBox->setCurrentIndex(idx < 0 ? m_familyBox->findData(cfg::controlsSet("bm3")) : idx);
    showFamily(m_familyBox->currentData().toString());

    // live feedback
    auto &joys = Joysticks::instance();
    connect(&joys, &Joysticks::devicesChanged, this, &ControlsDialog::updateJoystickList);
    connect(&joys, &Joysticks::button, this, [this](int j, int b, bool down) { source(QString("joy%1:button%2").arg(j).arg(b), down); });
    connect(&joys, &Joysticks::hat, this, [this](int j, int h, int val) {
        QString base = QString("joy%1:hat%2").arg(j).arg(h);
        for (int bit : {SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT}) source(base + ":" + hatDir(bit), val & bit);
    });
    connect(&joys, &Joysticks::axis, this, [this](int j, int a, int val) {
        QString base = QString("joy%1:axis%2").arg(j).arg(a);
        source(base + "+", val > 16384);
        source(base + "-", val < -16384);
        m_flashUntil[base] = m_clock.elapsed() + 200;
        source(base, true);
    });
    m_clock.start();
    connect(&m_flashTimer, &QTimer::timeout, this, [this] {
        qint64 now = m_clock.elapsed();
        for (auto it = m_flashUntil.begin(); it != m_flashUntil.end();)
            if (it.value() < now) {
                m_active.remove(it.key());
                it = m_flashUntil.erase(it);
                refreshHighlights();
            } else {
                ++it;
            }
    });
    m_flashTimer.start(50);
    updateJoystickList();
    qApp->installEventFilter(this);
}

ControlsDialog::Bindings ControlsDialog::load(const QString &family) const {
    std::string text;
    for (auto &c : setControls(family))
        if (!c.defaults.empty()) text += c.name + " = " + c.defaults + "\n";
    QFile f(cfg::controlsFile(family));
    if (f.open(QIODevice::ReadOnly)) text += f.readAll().toStdString();   // the file's lines replace the defaults
    Bindings b;
    for (auto &c : setControls(family)) b[QString::fromStdString(c.name)] = {};
    for (auto &en : hk::cab::parse_list_file(text)) {
        QStringList l;
        for (auto &it : en.items) l << normalizeBinding(QString::fromStdString(it));
        b[QString::fromStdString(en.name)] = l;
    }
    return b;
}

bool ControlsDialog::save() {
    for (const QString &fam : m_dirty) {
        QDir().mkpath(cfg::configDir() + "/controls");
        QSaveFile f(cfg::controlsFile(fam));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Controls", "Cannot write " + f.fileName() + ": " + f.errorString());
            return false;
        }
        QString text = "# HIRAKU controls for " + cfg::controlsSetName(fam) + ", written by the launcher.\n"
                       "# Format: runtime/host/input.h. An input with nothing after '=' is unbound.\n";
        const Bindings &b = m_edits[fam];
        for (auto &c : setControls(fam)) {
            std::vector<std::string> items;
            for (const QString &s : b.value(QString::fromStdString(c.name))) items.push_back(s.toStdString());
            text += QString::fromStdString(hk::cab::format_entry(c.name, items)) + "\n";
        }
        f.write(text.toUtf8());
        if (!f.commit()) {
            QMessageBox::warning(this, "Controls", "Cannot write " + f.fileName() + ": " + f.errorString());
            return false;
        }
    }
    m_dirty.clear();
    return true;
}

void ControlsDialog::reject() {
    if (!m_dirty.isEmpty()) {
        auto r = QMessageBox::question(this, "Controls", "Save the changed controls?",
                                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) return;
        if (r == QMessageBox::Save && !save()) return;
    }
    QDialog::reject();
}

void ControlsDialog::showFamily(const QString &family) {
    m_family = family;
    if (!m_edits.contains(family)) m_edits[family] = load(family);
    m_rows.clear();
    m_sections.clear();
    auto &defs = setControls(family);
    QStringList sharing = cfg::controlsSetMembers(family);
    m_setLabel->setVisible(sharing.size() > 1);
    if (sharing.size() > 1) {
        QStringList names;
        for (const QString &m : sharing) names << cfg::familyName(m);
        QString last = names.takeLast();
        m_setLabel->setText("Shared by " + names.join(", ") + " and " + last +
                            ": each game uses the controls its cabinet has and ignores the rest.");
    }
    // one section per group, in the order the groups first come (IIDX lists "Cabinet" twice)
    QStringList groups;
    QHash<QString, QVector<int>> members;
    for (int k = 0; k < (int)defs.size(); k++) {
        QString g = QString::fromStdString(defs[k].group);
        if (!members.contains(g)) groups << g;
        members[g] << k;
    }
    // Two columns, each section to the shorter one: Player 1 beside Player 2
    QHash<QString, bool> folded;
    QHash<QString, int> column;
    int rows[2] = {0, 0}, labelWidth[2] = {0, 0};   // one width per column, so its bindings line up
    for (const QString &g : groups) {
        folded[g] = cfg::settings().value("controls/folded/" + g, g.startsWith("e-AMUSEMENT")).toBool();
        int c = column[g] = rows[1] < rows[0] ? 1 : 0;
        rows[c] += 2 + (folded[g] ? 0 : members[g].size());
        for (int k : members[g])
            labelWidth[c] = std::max(labelWidth[c], fontMetrics().horizontalAdvance(QString::fromStdString(defs[k].label)) + 16);
    }

    auto *page = new QWidget;
    auto *cols = new QHBoxLayout(page);
    cols->setSpacing(24);
    QVBoxLayout *col[2];
    for (auto *&c : col) {
        c = new QVBoxLayout;
        c->setSpacing(4);
        cols->addLayout(c, 1);
    }
    QFont bold = font();
    bold.setBold(true);
    for (const QString &g : groups) {
        int si = m_sections.size();
        Section sec;
        sec.group = g;
        sec.header = new QToolButton;
        sec.header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        sec.header->setAutoRaise(true);
        sec.header->setFont(bold);
        sec.header->setFocusPolicy(Qt::NoFocus);
        connect(sec.header, &QToolButton::clicked, this, [this, si] {
            bool fold = !m_sections[si].body->isHidden();
            setFolded(si, fold);
            cfg::settings().setValue("controls/folded/" + m_sections[si].group, fold);
        });
        auto *line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        auto *head = new QHBoxLayout;
        head->addWidget(sec.header);
        head->addWidget(line, 1);
        sec.body = new QWidget;
        auto *grid = new QGridLayout(sec.body);
        grid->setContentsMargins(8, 0, 0, 8);
        grid->setVerticalSpacing(3);
        grid->setColumnStretch(1, 1);

        for (int k : members[g]) {
            const auto &c = defs[k];
            int index = m_rows.size();
            Row row;
            row.control = c;
            row.section = si;
            row.label = new QLabel(QString::fromStdString(c.label));
            row.label->setToolTip(QString::fromStdString(c.name));
            row.label->setMinimumWidth(labelWidth[column[g]]);
            row.button = new QToolButton;
            row.button->setFocusPolicy(Qt::NoFocus);
            row.button->setPopupMode(QToolButton::MenuButtonPopup);
            row.button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            row.button->setMinimumWidth(180);
            row.button->setContextMenuPolicy(Qt::CustomContextMenu);
            auto *menu = new QMenu(row.button);
            connect(menu, &QMenu::aboutToShow, this, [this, index, menu] {
                menu->clear();
                bindingMenu(index, menu);
            });
            row.button->setMenu(menu);
            connect(row.button, &QToolButton::clicked, this, [this, index] { capture(index, false); });
            connect(row.button, &QToolButton::customContextMenuRequested, this, [this, index](const QPoint &) {
                QMenu m;
                bindingMenu(index, &m);
                m.exec(QCursor::pos());
            });
            int r = grid->count() / 2;
            grid->addWidget(row.label, r, 0);
            grid->addWidget(row.button, r, 1);
            m_rows.push_back(row);
        }

        auto *box = new QVBoxLayout;
        box->setSpacing(2);
        box->addLayout(head);
        box->addWidget(sec.body);
        m_sections.push_back(sec);
        setFolded(si, folded[g]);
        col[column[g]]->addLayout(box);
    }
    for (auto *c : col) c->addStretch(1);
    m_scroll->setWidget(page);
    refreshRows();
    refreshHighlights();

    // wide enough for both columns, as far as the screen allows (measured with the labels' style applied)
    page->ensurePolished();
    for (auto &row : m_rows) row.label->ensurePolished();
    page->layout()->activate();
    int want = page->sizeHint().width() + m_scroll->verticalScrollBar()->sizeHint().width() + 2 * m_scroll->frameWidth() +
               layout()->contentsMargins().left() + layout()->contentsMargins().right();
    if (screen()) want = std::min(want, screen()->availableGeometry().width());
    if (width() < want) resize(want, height());
}

void ControlsDialog::setFolded(int si, bool folded) {
    Section &sec = m_sections[si];
    sec.body->setVisible(!folded);
    sec.header->setArrowType(folded ? Qt::RightArrow : Qt::DownArrow);
    int n = 0;
    for (auto &row : m_rows) n += row.section == si;
    sec.header->setText(folded ? QString("%1  (%2)").arg(sec.group).arg(n) : sec.group);
    sec.header->setToolTip(folded ? "Show these controls" : "Fold these controls away");
}

void ControlsDialog::bindingMenu(int k, QMenu *menu) {
    QString name = QString::fromStdString(m_rows[k].control.name);
    // queued: the binding dialog opens once the menu has closed
    menu->addAction("Add another binding…", this, [this, k] {
        QMetaObject::invokeMethod(this, [this, k] { capture(k, true); }, Qt::QueuedConnection);
    });
    const QStringList l = m_edits[m_family].value(name);
    if (!l.isEmpty()) menu->addSeparator();
    for (const QString &b : l)
        menu->addAction("Remove " + describeBinding(b), this, [this, name, b] {
            m_edits[m_family][name].removeAll(b);
            m_dirty.insert(m_family);
            refreshRows();
            refreshHighlights();
        });
    if (l.size() > 1)
        menu->addAction("Remove all", this, [this, name] {
            m_edits[m_family][name].clear();
            m_dirty.insert(m_family);
            refreshRows();
            refreshHighlights();
        });
}

void ControlsDialog::refreshRows() {
    // An input bound to two controls of one game presses both there: each says which, per game
    QHash<int, QStringList> clashes;
    for (const QString &m : cfg::controlsSetMembers(m_family)) {
        QSet<std::string> has;
        for (auto &c : hk::cab::controls(m.toStdString())) has.insert(c.name);
        QMap<QString, QVector<int>> users;   // binding -> its rows
        for (int k = 0; k < m_rows.size(); k++)
            if (has.contains(m_rows[k].control.name))
                for (const QString &b : m_edits[m_family].value(QString::fromStdString(m_rows[k].control.name))) users[b] << k;
        for (auto it = users.cbegin(); it != users.cend(); ++it)
            for (int k : *it)
                for (int j : *it)
                    if (j != k) {
                        QString where = cfg::controlsSetMembers(m_family).size() > 1 ? " in " + cfg::familyName(m) : QString();
                        clashes[k] << describeBinding(it.key()) + " also presses " + m_rows[j].label->text() + where;
                    }
    }
    for (int k = 0; k < m_rows.size(); k++) {
        Row &row = m_rows[k];
        const QStringList &l = m_edits[m_family].value(QString::fromStdString(row.control.name));
        QStringList shown;
        for (const QString &b : l) shown << describeBinding(b);
        row.button->setText(shown.isEmpty() ? "—" : shown.join(", "));
        QString tip = l.isEmpty() ? QString("Not bound") : l.join(", ");
        if (clashes.contains(k)) tip += "\n\n" + clashes[k].join("\n");
        row.button->setToolTip(tip + "\n\nClick to bind; the arrow (or a right-click) adds or removes one binding.");
        row.button->setToolButtonStyle(clashes.contains(k) ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonTextOnly);
        row.button->setIcon(clashes.contains(k) ? style()->standardIcon(QStyle::SP_MessageBoxWarning) : QIcon());
    }
}

void ControlsDialog::capture(int k, bool append) {
    const auto &c = m_rows[k].control;
    m_capturing = true;
    CaptureDialog d(QString::fromStdString(c.label) + "  (" + QString::fromStdString(c.group) + ")", c.analog, this);
    bool ok = d.exec() == QDialog::Accepted;
    m_capturing = false;
    m_active.clear();   // the capturing press never reached the highlighter: don't leave it stuck
    if (!ok || d.result().isEmpty()) {
        refreshHighlights();
        return;
    }
    QStringList &l = m_edits[m_family][QString::fromStdString(c.name)];
    if (!append) l.clear();
    if (!l.contains(d.result())) l << d.result();
    m_dirty.insert(m_family);
    refreshRows();
    refreshHighlights();
}

void ControlsDialog::source(const QString &binding, bool on) {
    if (m_capturing) return;
    bool was = m_active.contains(binding);
    if (on == was) return;
    if (on) m_active.insert(binding);
    else m_active.remove(binding);
    refreshHighlights();
}

void ControlsDialog::refreshHighlights() {
    QVector<bool> sectionLit(m_sections.size(), false);
    for (auto &row : m_rows) {
        bool lit = false;
        for (const QString &b : m_edits[m_family].value(QString::fromStdString(row.control.name)))
            if (m_active.contains(b)) lit = true;
        if (lit) sectionLit[row.section] = true;
        row.label->setStyleSheet(lit ? "QLabel { background: palette(highlight); color: palette(highlighted-text); "
                                       "border-radius: 3px; padding: 2px 6px; }"
                                     : "QLabel { padding: 2px 6px; }");
    }
    // a folded section lights its header
    for (int s = 0; s < m_sections.size(); s++)
        m_sections[s].header->setStyleSheet(sectionLit[s] && m_sections[s].body->isHidden()
                                                ? "QToolButton { background: palette(highlight); color: palette(highlighted-text); "
                                                  "border-radius: 3px; }"
                                                : QString());
}

bool ControlsDialog::eventFilter(QObject *o, QEvent *e) {
    // Keys pressed while this dialog is in front test the bindings (and do nothing else)
    if ((e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) && !m_capturing && isActiveWindow() &&
        !QApplication::activePopupWidget()) {   // an open menu keeps its keys
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Escape) return QDialog::eventFilter(o, e);
        if (!k->isAutoRepeat()) {
            QString b = bindingForKey(k);
            if (!b.isEmpty()) source(b, e->type() == QEvent::KeyPress);
        }
        return true;
    }
    return QDialog::eventFilter(o, e);
}

void ControlsDialog::updateJoystickList() {
    QStringList n = Joysticks::instance().names();
    m_joyLabel->setText(n.isEmpty() ? "No controllers connected. Plug one in and it appears here."
                                    : "Controllers: " + n.join(" · "));
}
