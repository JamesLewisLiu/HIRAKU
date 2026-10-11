// SPDX-License-Identifier: GPL-3.0-or-later
#include "controlsdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
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

}  // namespace

ControlsDialog::ControlsDialog(const QString &family, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Controls");
    resize(640, 720);
    auto *v = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("Cabinet:"));
    m_familyBox = new QComboBox;
    for (const QString &f : cfg::families()) m_familyBox->addItem(cfg::familyName(f), f);
    top->addWidget(m_familyBox, 1);
    v->addLayout(top);

    m_joyLabel = new QLabel;
    m_joyLabel->setWordWrap(true);
    m_joyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_joyLabel);
    auto *hint = new QLabel("Click a binding, then press a key or a controller button. <b>+</b> adds another "
                            "binding, <b>×</b> clears. Press anything bound and its row lights up.");
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
        for (auto &c : hk::cab::controls(m_family.toStdString())) {
            QStringList l;
            auto entries = hk::cab::parse_list_file(c.name + " = " + c.defaults);
            for (auto &it : entries[0].items) l << normalizeBinding(QString::fromStdString(it));
            d[QString::fromStdString(c.name)] = l;
        }
        m_edits[m_family] = d;
        m_dirty.insert(m_family);
        for (int k = 0; k < m_rows.size(); k++) refreshRow(k);
    });

    connect(m_familyBox, &QComboBox::currentIndexChanged, this, [this] { showFamily(m_familyBox->currentData().toString()); });
    int idx = m_familyBox->findData(family);
    m_familyBox->setCurrentIndex(idx < 0 ? 0 : idx);
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
    std::string text = hk::cab::default_bindings(family.toStdString());
    QFile f(cfg::controlsFile(family));
    if (f.open(QIODevice::ReadOnly)) text += f.readAll().toStdString();   // the file's lines replace the defaults
    Bindings b;
    for (auto &c : hk::cab::controls(family.toStdString())) b[QString::fromStdString(c.name)] = {};
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
        QString text = "# HIRAKU controls for " + cfg::familyName(fam) + ", written by the launcher.\n"
                       "# Format: runtime/host/input.h. An input with nothing after '=' is unbound.\n";
        const Bindings &b = m_edits[fam];
        for (auto &c : hk::cab::controls(fam.toStdString())) {
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
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    QString group;
    QGridLayout *grid = nullptr;
    QVector<int> groupRows;
    auto &defs = hk::cab::controls(family.toStdString());
    // groups in table order; a long group (Keyboardmania's 24 keys) gets two columns
    QMap<QString, int> groupSize;
    int labelWidth = 0;   // one width for every group, so the bindings line up
    for (auto &c : defs) {
        groupSize[QString::fromStdString(c.group)]++;
        labelWidth = std::max(labelWidth, fontMetrics().horizontalAdvance(QString::fromStdString(c.label)) + 16);
    }
    for (int n : groupSize)
        if (n > 12 && width() < 900) resize(900, height());   // two columns of keys
    int inGroup = 0;
    for (auto &c : defs) {
        QString g = QString::fromStdString(c.group);
        if (g != group) {
            group = g;
            auto *box = new QGroupBox(g);
            grid = new QGridLayout(box);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(5, 1);
            v->addWidget(box);
            inGroup = 0;
        }
        int n = groupSize[g], cols = n > 12 ? 2 : 1, perCol = (n + cols - 1) / cols;
        int r = inGroup % perCol, c0 = (inGroup / perCol) * 4;
        inGroup++;

        Row row;
        row.control = c;
        row.label = new QLabel(QString::fromStdString(c.label));
        row.label->setToolTip(QString::fromStdString(c.name));
        row.label->setMinimumWidth(labelWidth);
        row.button = new QPushButton;
        row.button->setFocusPolicy(Qt::NoFocus);
        row.button->setMinimumWidth(cols > 1 ? 120 : 220);
        row.button->setContextMenuPolicy(Qt::CustomContextMenu);
        auto *add = new QToolButton;
        add->setText("+");
        add->setToolTip("Add another binding");
        add->setFocusPolicy(Qt::NoFocus);
        auto *clr = new QToolButton;
        clr->setText("×");
        clr->setToolTip("Clear");
        clr->setFocusPolicy(Qt::NoFocus);
        int index = m_rows.size();
        connect(row.button, &QPushButton::clicked, this, [this, index] { capture(index, false); });
        connect(add, &QToolButton::clicked, this, [this, index] { capture(index, true); });
        connect(clr, &QToolButton::clicked, this, [this, index] {
            m_edits[m_family][QString::fromStdString(m_rows[index].control.name)].clear();
            m_dirty.insert(m_family);
            refreshRow(index);
        });
        connect(row.button, &QPushButton::customContextMenuRequested, this, [this, index](const QPoint &) {
            // remove a single binding
            QMenu menu;
            QStringList &l = m_edits[m_family][QString::fromStdString(m_rows[index].control.name)];
            for (const QString &b : l) menu.addAction("Remove " + describeBinding(b))->setData(b);
            if (l.isEmpty()) menu.addAction("(nothing bound)")->setEnabled(false);
            if (QAction *a = menu.exec(QCursor::pos()); a && a->data().isValid()) {
                l.removeAll(a->data().toString());
                m_dirty.insert(m_family);
                refreshRow(index);
            }
        });
        grid->addWidget(row.label, r, c0);
        grid->addWidget(row.button, r, c0 + 1);
        grid->addWidget(add, r, c0 + 2);
        grid->addWidget(clr, r, c0 + 3);
        m_rows.push_back(row);
        refreshRow(index);
    }
    v->addStretch(1);
    m_scroll->setWidget(page);
    refreshHighlights();
}

void ControlsDialog::refreshRow(int k) {
    Row &row = m_rows[k];
    const QStringList &l = m_edits[m_family].value(QString::fromStdString(row.control.name));
    QStringList shown;
    for (const QString &b : l) shown << describeBinding(b);
    row.button->setText(shown.isEmpty() ? "—" : shown.join(", "));
    row.button->setToolTip(l.isEmpty() ? "Not bound" : l.join(", ") + "\n\nRight-click to remove one binding.");
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
    refreshRow(k);
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
    for (auto &row : m_rows) {
        bool lit = false;
        for (const QString &b : m_edits[m_family].value(QString::fromStdString(row.control.name)))
            if (m_active.contains(b)) lit = true;
        row.label->setStyleSheet(lit ? "QLabel { background: palette(highlight); color: palette(highlighted-text); "
                                       "border-radius: 3px; padding: 2px 6px; }"
                                     : "QLabel { padding: 2px 6px; }");
    }
}

bool ControlsDialog::eventFilter(QObject *o, QEvent *e) {
    // Keys pressed while this dialog is in front test the bindings (and do nothing else)
    if ((e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) && !m_capturing && isActiveWindow()) {
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
