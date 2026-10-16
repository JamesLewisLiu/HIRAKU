// SPDX-License-Identifier: GPL-3.0-or-later
#include "lightsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "config.h"
#include "controlsdialog.h"

using hk::hidout::Device;

namespace {

QString qs(const std::string &s) { return QString::fromStdString(s); }

// Pick one output of one device; the selected output lights up while it is selected.
class AssignDialog : public QDialog {
public:
    AssignDialog(const QString &lamp, const std::vector<Device> &devices, std::function<void(const QString &, bool)> drive,
                 QWidget *parent)
        : QDialog(parent), m_devices(devices), m_drive(std::move(drive)) {
        setWindowTitle("Assign " + lamp);
        resize(460, 420);
        auto *v = new QVBoxLayout(this);
        v->addWidget(new QLabel("Choose the LED for <b>" + lamp.toHtmlEscaped() + "</b>. The selected output lights up, "
                                "so you can see which one it is."));
        m_box = new QComboBox;
        for (auto &d : m_devices)
            if (d.error.empty()) m_box->addItem(qs(d.name) + "  (" + qs(d.id) + ")", qs(d.id));
        v->addWidget(m_box);
        m_list = new QListWidget;
        v->addWidget(m_list, 1);
        m_all = new QCheckBox("Show every output (also those that do not look like lamps)");
        v->addWidget(m_all);
        connect(m_all, &QCheckBox::toggled, this, [this] { fill(); });
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        v->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_box, &QComboBox::currentIndexChanged, this, [this] { fill(); });
        connect(m_list, &QListWidget::currentRowChanged, this, [this] { light(); });
        connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
        fill();
    }
    ~AssignDialog() override {
        if (!m_lit.isEmpty()) m_drive(m_lit, false);
    }
    QString result() const { return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString(); }
private:
    const Device *device() const {
        for (auto &d : m_devices)
            if (qs(d.id) == m_box->currentData().toString()) return &d;
        return nullptr;
    }
    void fill() {
        m_list->clear();
        const Device *d = device();
        if (!d) return;
        for (int f = 0; f < (int)d->layout.fields.size(); f++) {
            if (!m_all->isChecked() && !hk::hid::light_like(d->layout, (size_t)f)) continue;
            auto *it = new QListWidgetItem(qs(hk::hidout::field_label(*d, f)));
            it->setData(Qt::UserRole, qs(hk::hidout::format_spec(*d, f)));
            m_list->addItem(it);
        }
    }
    void light() {
        if (!m_lit.isEmpty()) m_drive(m_lit, false);
        m_lit = result();
        if (!m_lit.isEmpty()) m_drive(m_lit, true);
    }
    const std::vector<Device> &m_devices;
    std::function<void(const QString &, bool)> m_drive;
    QComboBox *m_box;
    QListWidget *m_list;
    QCheckBox *m_all;
    QString m_lit;
};

QString dotStyle(bool on) {
    return on ? "QLabel { background: #ffcc33; border: 1px solid #b38600; border-radius: 7px; }"
              : "QLabel { background: palette(mid); border: 1px solid palette(dark); border-radius: 7px; }";
}

}  // namespace

LightsDialog::LightsDialog(const QString &family, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Lights");
    resize(640, 720);
    auto *v = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("Cabinet:"));
    m_familyBox = new QComboBox;
    fillCabinetBox(m_familyBox, false);
    top->addWidget(m_familyBox, 1);
    v->addLayout(top);

    auto *dev = new QHBoxLayout;
    m_devLabel = new QLabel;
    m_devLabel->setWordWrap(true);
    dev->addWidget(m_devLabel, 1);
    auto *refresh = new QPushButton("Rescan");
    refresh->setAutoDefault(false);
    connect(refresh, &QPushButton::clicked, this, [this] {
        allOff();
        m_writer.close();
        refreshDevices();
    });
    dev->addWidget(refresh);
    v->addLayout(dev);

    auto *perm = new QHBoxLayout;
    m_permLabel = new QLabel;
    m_permLabel->setWordWrap(true);
    perm->addWidget(m_permLabel, 1);
    m_permButton = new QPushButton("How to allow access…");
    m_permButton->setAutoDefault(false);
    perm->addWidget(m_permButton);
    v->addLayout(perm);
    connect(m_permButton, &QPushButton::clicked, this, [this] {
        // a udev rule giving the logged-in user access to the devices that refused us, added to the
        // rules already in the file, and the commands that install it (sh, bash, zsh and fish alike)
        const QString path = "/etc/udev/rules.d/70-hiraku-lights.rules";
        QStringList rules;
        QFile old(path);
        if (old.open(QIODevice::ReadOnly))
            for (const QString &line : QString::fromUtf8(old.readAll()).split('\n'))
                if (line.startsWith("KERNEL==") && !rules.contains(line)) rules << line;
        for (auto &d : m_devices) {
            if (d.error.empty()) continue;
            QString rule = QString("KERNEL==\"hidraw*\", ATTRS{idVendor}==\"%1\", ATTRS{idProduct}==\"%2\", TAG+=\"uaccess\"")
                               .arg(d.vid, 4, 16, QChar('0'))
                               .arg(d.pid, 4, 16, QChar('0'));
            if (!rules.contains(rule)) rules << rule;
        }
        QString commands = "printf '%s\\n' \\\n";
        for (const QString &r : rules) commands += "  '" + r + "' \\\n";
        commands += "  | sudo tee " + path + " >/dev/null\n";
        commands += "sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=hidraw\n";
        QDialog d(this);
        d.setWindowTitle("Allow access to controller LEDs");
        auto *l = new QVBoxLayout(&d);
        auto *t = new QLabel("Linux only lets programs write to a controller's LEDs (its /dev/hidraw node) when a udev "
                             "rule allows it. Run these commands in a terminal: they write the rule to <tt>" + path +
                             "</tt> (keeping the controllers already in it) and apply it. They ask for your password. "
                             "Then press <b>Rescan</b>; if the controller is still refused, replug it. See docs/LIGHTS.md.");
        t->setWordWrap(true);
        l->addWidget(t);
        auto *e = new QPlainTextEdit(commands);
        e->setReadOnly(true);
        e->setFont(QFont("monospace"));
        l->addWidget(e);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
        auto *copy = bb->addButton("Copy", QDialogButtonBox::ActionRole);
        connect(copy, &QPushButton::clicked, &d, [commands] { QApplication::clipboard()->setText(commands); });
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        l->addWidget(bb);
        d.resize(820, 300);
        d.exec();
    });

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    v->addWidget(m_scroll, 1);

    m_liveLabel = new QLabel;
    m_liveLabel->setWordWrap(true);
    v->addWidget(m_liveLabel);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    for (auto *b : box->buttons()) qobject_cast<QPushButton *>(b)->setAutoDefault(false);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        if (save()) {
            allOff();
            accept();
        }
    });
    connect(box, &QDialogButtonBox::rejected, this, &LightsDialog::reject);

    refreshDevices();
    connect(m_familyBox, &QComboBox::currentIndexChanged, this, [this] { showFamily(m_familyBox->currentData().toString()); });
    int idx = m_familyBox->findData(family);
    m_familyBox->setCurrentIndex(idx < 0 ? m_familyBox->findData("popn") : idx);
    showFamily(m_familyBox->currentData().toString());

    // live lamps from a running game
    connect(&m_socket, &QLocalSocket::readyRead, this, &LightsDialog::onSocketData);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        m_liveGame.clear();
        m_liveState.clear();
        showFamily(m_family);
    });
    connect(&m_reconnect, &QTimer::timeout, this, [this] {
        if (m_socket.state() == QLocalSocket::UnconnectedState) m_socket.connectToServer(cfg::lightsSocket());
    });
    m_reconnect.start(1000);
    m_socket.connectToServer(cfg::lightsSocket());
}

LightsDialog::~LightsDialog() {
    m_socket.disconnect(this);   // its destructor signals `disconnected`, after our other members are gone
    m_socket.abort();
    allOff();
}

void LightsDialog::reject() {
    if (!m_dirty.isEmpty()) {
        auto r = QMessageBox::question(this, "Lights", "Save the changed lights?",
                                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) return;
        if (r == QMessageBox::Save && !save()) return;
    }
    allOff();
    QDialog::reject();
}

void LightsDialog::refreshDevices() {
    m_devices = hk::hidout::enumerate();
    QStringList ok, refused;
    for (auto &d : m_devices) (d.error.empty() ? ok : refused) << qs(d.name);
    m_devLabel->setText(!ok.isEmpty()      ? "Controllers with LEDs: " + ok.join(" · ")
                        : refused.isEmpty() ? "No controller with LEDs found."
                                            : "Found controllers with LEDs, but cannot open them (see below).");
    m_permLabel->setText(refused.isEmpty() ? QString()
                                           : "No access to: " + refused.join(" · ") + ". Without it their LEDs cannot be set.");
    m_permLabel->setVisible(!refused.isEmpty());
    m_permButton->setVisible(!refused.isEmpty());
}

LightsDialog::Outputs LightsDialog::load(const QString &family) const {
    Outputs o;
    QFile f(cfg::lightsFile(family));
    if (!f.open(QIODevice::ReadOnly)) return o;
    for (auto &en : hk::cab::parse_list_file(f.readAll().toStdString())) {
        QStringList l;
        for (auto &it : en.items) l << qs(it);
        o[qs(en.name)] = l;
    }
    return o;
}

bool LightsDialog::save() {
    for (const QString &fam : m_dirty) {
        QDir().mkpath(cfg::configDir() + "/lights");
        QSaveFile f(cfg::lightsFile(fam));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Lights", "Cannot write " + f.fileName() + ": " + f.errorString());
            return false;
        }
        QString text = "# HIRAKU lights for " + cfg::familyName(fam) + ", written by the launcher.\n"
                       "# Format: runtime/host/lights.h; outputs: runtime/host/hidout.h.\n";
        const Outputs &o = m_edits[fam];
        for (auto &l : hk::cab::lamps(fam.toStdString())) {
            QStringList outs = o.value(qs(l.name));
            if (outs.isEmpty()) continue;
            std::vector<std::string> items;
            for (const QString &s : outs) items.push_back(s.toStdString());
            text += qs(hk::cab::format_entry(l.name, items)) + "\n";
        }
        f.write(text.toUtf8());
        if (!f.commit()) {
            QMessageBox::warning(this, "Lights", "Cannot write " + f.fileName() + ": " + f.errorString());
            return false;
        }
    }
    m_dirty.clear();
    return true;
}

void LightsDialog::showFamily(const QString &family) {
    m_family = family;
    if (!m_edits.contains(family)) m_edits[family] = load(family);
    m_rows.clear();
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    if (family == "bm3") {
        auto *note = new QLabel("MAME does not decode beatmania III's cabinet lamps yet, so only the board's diagnostic "
                                "LEDs are known.");
        note->setWordWrap(true);
        v->addWidget(note);
    }
    QHash<QString, QGridLayout *> grids;   // one box per group, where the group first comes (IIDX's are interleaved)
    QHash<QString, int> used;              // rows in each
    int labelWidth = 0;   // one width for every group, so the outputs line up
    for (auto &l : hk::cab::lamps(family.toStdString()))
        labelWidth = std::max(labelWidth, fontMetrics().horizontalAdvance(qs(l.name)) + 8);
    for (auto &l : hk::cab::lamps(family.toStdString())) {
        QString g = qs(l.group);
        QGridLayout *&grid = grids[g];
        if (!grid) {
            auto *box = new QGroupBox(g);
            grid = new QGridLayout(box);
            grid->setColumnStretch(2, 1);
            v->addWidget(box);
        }
        int r = used[g]++;
        Row row;
        row.lamp = l;
        row.dot = new QLabel;
        row.dot->setFixedSize(14, 14);
        row.dot->setStyleSheet(dotStyle(false));
        row.dot->setToolTip("Lit while a running game lights this lamp");
        row.button = new QPushButton;
        row.button->setAutoDefault(false);
        row.button->setMinimumWidth(240);
        auto *test = new QToolButton;
        test->setText("Test");
        test->setToolTip("Lights the assigned LEDs while held");
        auto *clr = new QToolButton;
        clr->setText("×");
        clr->setToolTip("Clear");
        int index = m_rows.size();
        connect(row.button, &QPushButton::clicked, this, [this, index] { assign(index); });
        connect(test, &QToolButton::pressed, this, [this, index] {
            for (const QString &s : m_edits[m_family].value(qs(m_rows[index].lamp.name))) drive(s, true);
        });
        connect(test, &QToolButton::released, this, [this, index] {
            for (const QString &s : m_edits[m_family].value(qs(m_rows[index].lamp.name))) drive(s, false);
        });
        connect(clr, &QToolButton::clicked, this, [this, index] {
            m_edits[m_family].remove(qs(m_rows[index].lamp.name));
            m_dirty.insert(m_family);
            refreshRow(index);
        });
        grid->addWidget(row.dot, r, 0);
        auto *name = new QLabel(qs(l.name));
        name->setMinimumWidth(labelWidth);
        grid->addWidget(name, r, 1);
        grid->addWidget(row.button, r, 2);
        grid->addWidget(test, r, 3);
        grid->addWidget(clr, r, 4);
        m_rows.push_back(row);
        refreshRow(index);
        if (m_liveFamily == family) setDot(index, m_liveState.value(qs(l.name)));
    }
    v->addStretch(1);
    m_scroll->setWidget(page);
    if (m_liveGame.isEmpty())
        m_liveLabel->setText("Live view: start a game with “Publish lamps” on (Settings) and its lamps light up here.");
    else if (m_liveFamily != m_family)
        m_liveLabel->setText("Live view: " + m_liveGame + " is running, which has a different cabinet.");
    else
        m_liveLabel->setText("Live view: showing " + m_liveGame + "'s lamps.");
}

void LightsDialog::refreshRow(int k) {
    Row &row = m_rows[k];
    QStringList outs = m_edits[m_family].value(qs(row.lamp.name)), shown;
    for (const QString &s : outs) {
        hk::hidout::Spec sp;
        QString name = s;
        if (hk::hidout::parse_spec(s.toStdString(), sp))
            for (auto &d : m_devices)
                if (d.vid == sp.vid && d.pid == sp.pid && (sp.interface < 0 || d.interface == sp.interface))
                    name = qs(d.name) + " · " + qs(hk::hidout::field_label(d, sp.field));
        shown << name;
    }
    row.button->setText(shown.isEmpty() ? "— (click to assign)" : shown.join(", "));
    row.button->setToolTip(outs.join(", "));
}

void LightsDialog::assign(int k) {
    bool any = false;
    for (auto &d : m_devices) any |= d.error.empty();
    if (!any) {
        QMessageBox::information(this, "Lights", "No controller with LEDs is available. Plug one in and press Rescan.");
        return;
    }
    QString lamp = qs(m_rows[k].lamp.name);
    AssignDialog d(lamp, m_devices, [this](const QString &s, bool on) { drive(s, on); }, this);
    if (d.exec() != QDialog::Accepted || d.result().isEmpty()) return;
    QStringList &l = m_edits[m_family][lamp];
    if (!l.contains(d.result())) l << d.result();
    m_dirty.insert(m_family);
    refreshRow(k);
}

void LightsDialog::drive(const QString &spec, bool on) {
    hk::hidout::Spec s;
    if (!hk::hidout::parse_spec(spec.toStdString(), s)) return;
    m_writer.set(s, on);
    m_writer.flush();
    if (on) m_driven.insert(spec);
    else m_driven.remove(spec);
}

void LightsDialog::allOff() {
    for (const QString &s : QSet<QString>(m_driven)) drive(s, false);
}

void LightsDialog::setDot(int k, bool on) { m_rows[k].dot->setStyleSheet(dotStyle(on)); }

void LightsDialog::onSocketData() {
    m_buf += m_socket.readAll();
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(m_buf.left(nl));
        m_buf.remove(0, nl + 1);
        if (line.startsWith("hiraku-lights ")) {
            QStringList p = line.split(' ');
            m_liveGame = p.value(2);
            m_liveFamily = p.value(3);
            m_liveState.clear();
            showFamily(m_family);
            continue;
        }
        int eq = line.lastIndexOf('=');
        if (eq < 0) continue;
        QString name = line.left(eq);
        bool on = line.mid(eq + 1) == "1";
        m_liveState[name] = on;
        if (m_liveFamily != m_family) continue;
        for (int k = 0; k < m_rows.size(); k++)
            if (qs(m_rows[k].lamp.name) == name) setDot(k, on);
    }
}
