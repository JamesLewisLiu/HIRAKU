// SPDX-License-Identifier: GPL-3.0-or-later
#include "cabinet.h"

#include <cctype>
#include <map>

namespace hk::cab {

namespace {

std::vector<Control> make_bm3() {
    std::vector<Control> c;
    const char *p1keys[5] = {"key:Z, joy0:button0", "key:S, joy0:button1", "key:X, joy0:button2", "key:D, joy0:button3",
                             "key:C, joy0:button4"};
    const char *p2keys[5] = {"key:M", "key:K", "key:Comma", "key:L", "key:Period"};
    for (int p = 1; p <= 2; p++) {
        std::string P = "P" + std::to_string(p), g = "Player " + std::to_string(p);
        for (int k = 1; k <= 5; k++)
            c.push_back({P + " Button " + std::to_string(k), "Key " + std::to_string(k), g, false, p == 1 ? p1keys[k - 1] : p2keys[k - 1]});
        c.push_back({P + " Turntable", "Turntable (analog axis)", g, true, p == 1 ? "joy0:axis0" : ""});
        c.push_back({P + " Turntable -", "Turntable − (as a button)", g, false, p == 1 ? "key:Left, key:Left Shift" : "key:Down"});
        c.push_back({P + " Turntable +", "Turntable + (as a button)", g, false, p == 1 ? "key:Right, key:A" : "key:Up"});
        c.push_back({P + " Foot", "Foot pedal", g, false, p == 1 ? "key:Space, joy0:button7" : ""});
        c.push_back({p == 1 ? "1 Player Start" : "2 Players Start", "Start", g, false, p == 1 ? "key:1, joy0:button8" : "key:2"});
    }
    c.push_back({"Coin 1", "Coin", "Cabinet", false, "key:5, joy0:button9"});
    c.push_back({"Test", "Test", "Cabinet", false, "key:F2"});
    c.push_back({"Service", "Service", "Cabinet", false, "key:9"});
    return c;
}

// pop'n music: the nine buttons zig-zag like the cabinet's two rows (odd = lower)
std::vector<Control> make_popn() {
    std::vector<Control> c;
    const char *keys = "ZSXDCFVGB";
    const char *colour[9] = {"white", "yellow", "green", "blue", "red", "blue", "green", "yellow", "white"};
    for (int k = 1; k <= 9; k++)
        c.push_back({"P1 Button " + std::to_string(k), "Button " + std::to_string(k) + " (" + colour[k - 1] + ")", "Player", false,
                     std::string("key:") + keys[k - 1] + ", joy0:button" + std::to_string(k - 1)});
    c.push_back({"Coin 1", "Coin", "Cabinet", false, "key:5, joy0:button9"});
    c.push_back({"Test", "Test", "Cabinet", false, "key:F2"});
    c.push_back({"Service", "Service", "Cabinet", false, "key:9"});
    return c;
}

// ParaParaParadise: the five sensor bars left to right, plus the two buttons
std::vector<Control> make_ppp() {
    std::vector<Control> c;
    const char *keys = "ZXCVB";
    for (int k = 1; k <= 5; k++)
        c.push_back({"P1 Button " + std::to_string(k + 2), "Sensor bar " + std::to_string(k) + (k == 1 ? " (left)" : k == 5 ? " (right)" : ""),
                     "Player", false, std::string("key:") + keys[k - 1] + ", joy0:button" + std::to_string(k - 1)});
    c.push_back({"P1 Button 1", "Left button (menu up)", "Player", false, "key:Left, joy0:button5"});
    c.push_back({"P1 Button 2", "Right button (menu down)", "Player", false, "key:Right, joy0:button6"});
    c.push_back({"1 Player Start", "Start / OK", "Player", false, "key:1, joy0:button8"});
    c.push_back({"Coin 1", "Coin", "Cabinet", false, "key:5, joy0:button9"});
    c.push_back({"Test", "Test", "Cabinet", false, "key:F2"});
    c.push_back({"Service", "Service", "Cabinet", false, "key:9"});
    return c;
}

// Keyboardmania: player 1's 24 keys (C1..B2) on two letter rows, as MAME maps them;
// player 2's octave 1 on the keypad (7 8 9 4 5 6 1 2 3 0 . Enter), octave 2 on
// Insert Home PageUp Delete End PageDown, the arrows (Left Down Right Up), / and *
std::vector<Control> make_kbm() {
    std::vector<Control> c;
    static const char *notes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *p1 = "QWERTYUIOASDFGHJKLZXCVBN";
    static const char *p2[24] = {"Keypad 7", "Keypad 8", "Keypad 9", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad 1", "Keypad 2",
                                 "Keypad 3", "Keypad 0", "Keypad .", "Keypad Enter", "Insert", "Home", "PageUp", "Delete",
                                 "End", "PageDown", "Left", "Down", "Right", "Up", "Keypad /", "Keypad *"};
    for (int p = 1; p <= 2; p++) {
        std::string P = "P" + std::to_string(p), g = "Player " + std::to_string(p);
        for (int k = 0; k < 24; k++)
            c.push_back({P + " Key " + std::to_string(k + 1),
                         "Key " + std::to_string(k + 1) + " (" + notes[k % 12] + std::to_string(k / 12 + 1) + ")", g, false,
                         p == 1 ? std::string("key:") + p1[k] : std::string("key:") + p2[k]});
        c.push_back({p == 1 ? "1 Player Start" : "2 Players Start", "Start", g, false, p == 1 ? "key:1" : "key:2"});
    }
    c.push_back({"Coin 1", "Coin", "Cabinet", false, "key:5"});
    c.push_back({"Test", "Test", "Cabinet", false, "key:F2"});
    c.push_back({"Service", "Service", "Cabinet", false, "key:9"});
    return c;
}

// Dance Dance Revolution (System 573, MAME ksys573.cpp "ddr"): four panels per player on the
// GN845-PWB(B) stage, and the base panel's Start and buttons. A dance pad usually sends its
// arrows as buttons 0-3.
std::vector<Control> make_ddr() {
    std::vector<Control> c;
    const char *dirs[4] = {"Left", "Down", "Up", "Right"};
    const char *p1[4] = {"key:Left, joy0:button0", "key:Down, joy0:button1", "key:Up, joy0:button2", "key:Right, joy0:button3"};
    const char *p2[4] = {"key:Keypad 4, joy1:button0", "key:Keypad 2, joy1:button1", "key:Keypad 8, joy1:button2",
                         "key:Keypad 6, joy1:button3"};
    for (int p = 1; p <= 2; p++) {
        std::string P = "P" + std::to_string(p), g = "Player " + std::to_string(p);
        for (int k = 0; k < 4; k++) c.push_back({P + " " + dirs[k], std::string(dirs[k]) + " panel", g, false, p == 1 ? p1[k] : p2[k]});
        c.push_back({p == 1 ? "1 Player Start" : "2 Players Start", "Start", g, false, p == 1 ? "key:1, joy0:button9" : "key:2, joy1:button9"});
        for (int k = 1; k <= 3; k++) c.push_back({P + " Button " + std::to_string(k), "Panel button " + std::to_string(k), g, false, ""});
    }
    c.push_back({"Coin 1", "Coin", "Cabinet", false, "key:5, joy0:button8"});
    c.push_back({"Test", "Test", "Cabinet", false, "key:F2"});
    c.push_back({"Service", "Service", "Cabinet", false, "key:9"});
    return c;
}

// DrumMania (System 573, MAME ksys573.cpp "drmn"): five pads, the bass pedal, the select buttons
// and Start. On the keyboard the pads sit left to right as on the kit.
std::vector<Control> make_drmn() {
    return {
        {"Hi-Hat", "Hi-hat", "Drums", false, "key:S, joy0:button0"},
        {"Snare", "Snare", "Drums", false, "key:D, joy0:button1"},
        {"High Tom", "High tom", "Drums", false, "key:J, joy0:button2"},
        {"Low Tom", "Low tom", "Drums", false, "key:K, joy0:button3"},
        {"Cymbal", "Cymbal", "Drums", false, "key:L, joy0:button4"},
        {"Bass Drum", "Bass pedal", "Drums", false, "key:Space, joy0:button5"},
        {"Select L", "Select left", "Panel", false, "key:Left"},
        {"Select R", "Select right", "Panel", false, "key:Right"},
        {"1 Player Start", "Start", "Panel", false, "key:1, key:Return, joy0:button9"},
        {"Coin 1", "Coin", "Cabinet", false, "key:5, joy0:button8"},
        {"Test", "Test", "Cabinet", false, "key:F2"},
        {"Service", "Service", "Cabinet", false, "key:9"},
    };
}

// The board's status LEDs (lamp_output_w, active low): every family has them.
void add_status(std::vector<Lamp> &l) {
    for (int k = 0; k < 8; k++) l.push_back({"Status LED " + std::to_string(k + 1), "Board (diagnostic)", 0, k, true});
}

void add_run(std::vector<Lamp> &l, const std::string &base, const std::string &group, int reg, std::vector<int> bits, int first = 1) {
    for (size_t k = 0; k < bits.size(); k++) l.push_back({base + " " + std::to_string(first + (int)k), group, reg, bits[k], false});
}

// DJ Main beatmania (MAME djmain.cpp beatmania_btn): five keys and a turntable
// per player, the effect button. The turntable is an 8-bit dial ("TT1/TT2").
std::vector<Control> make_djbm() {
    std::vector<Control> c;
    const char *p1keys[5] = {"key:Z, joy0:button0", "key:S, joy0:button1", "key:X, joy0:button2", "key:D, joy0:button3",
                             "key:C, joy0:button4"};
    const char *p2keys[5] = {"key:M", "key:K", "key:Comma", "key:L", "key:Period"};
    for (int p = 1; p <= 2; p++) {
        std::string P = "P" + std::to_string(p), g = "Player " + std::to_string(p);
        for (int k = 1; k <= 5; k++)
            c.push_back({P + " Button " + std::to_string(k), "Key " + std::to_string(k), g, false, p == 1 ? p1keys[k - 1] : p2keys[k - 1]});
        c.push_back({P + " Turntable", "Turntable (analog axis)", g, true, p == 1 ? "joy0:axis0" : ""});
        c.push_back({P + " Turntable -", "Turntable − (as a button)", g, false, p == 1 ? "key:Left, key:Left Shift" : "key:Down"});
        c.push_back({P + " Turntable +", "Turntable + (as a button)", g, false, p == 1 ? "key:Right, key:A" : "key:Up"});
        c.push_back({p == 1 ? "1 Player Start" : "2 Players Start", "Start", g, false, p == 1 ? "key:1, joy0:button8" : "key:2"});
    }
    c.push_back({"Effect", "Effect", "Cabinet", false, "key:E, joy0:button7"});
    c.push_back({"Coin 1", "Coin", "Cabinet", false, "key:5, joy0:button9"});
    c.push_back({"Test", "Test", "Cabinet", false, "key:F2"});
    c.push_back({"Service", "Service", "Cabinet", false, "key:9"});
    c.push_back({"Reset", "Reset switch", "Cabinet", false, ""});
    return c;
}

// DJ Main pop'n music (MAME djmain.cpp popnmusic_btn): nine buttons, no start button
std::vector<Control> make_djpopn() {
    std::vector<Control> c = make_popn();
    c.push_back({"Reset", "Reset switch", "Cabinet", false, ""});
    return c;
}

// DJ Main pop'n stage (MAME djmain.cpp popnstage): ten buttons, and three
// selection buttons that MAME reads as the start buttons
std::vector<Control> make_djpstage() {
    std::vector<Control> c;
    const char *keys = "ZSXDCFVGBH";
    for (int k = 1; k <= 10; k++)
        c.push_back({"P1 Button " + std::to_string(k), "Button " + std::to_string(k), "Player", false,
                     std::string("key:") + keys[k - 1] + ", joy0:button" + std::to_string(k - 1)});
    c.push_back({"1 Player Start", "Select left", "Selection", false, "key:Left"});
    c.push_back({"2 Players Start", "Select OK (middle)", "Selection", false, "key:Return, key:1"});
    c.push_back({"3 Players Start", "Select right", "Selection", false, "key:Right"});
    c.push_back({"Coin 1", "Coin", "Cabinet", false, "key:5, joy0:button12"});
    c.push_back({"Test", "Test", "Cabinet", false, "key:F2"});
    c.push_back({"Service", "Service", "Cabinet", false, "key:9"});
    return c;
}

// DJ Main: reg 0 = 0x5d0000 (light_ctrl_1), reg 1 = 0x5d2000 (light_ctrl_2), as
// MAME decodes them for beatmania; reg 2 = the board's row of 8 LEDs (active low).
std::vector<Lamp> make_djbm_lamps() {
    std::vector<Lamp> l;
    add_run(l, "P1 Button Lamp", "Player 1", 0, {0, 1, 2, 3, 4});
    add_run(l, "P2 Button Lamp", "Player 2", 0, {6, 7, 8});
    add_run(l, "P2 Button Lamp", "Player 2", 1, {12, 13}, 4);
    l.push_back({"P1 Start Lamp", "Player 1", 1, 0});
    l.push_back({"P2 Start Lamp", "Player 2", 1, 1});
    l.push_back({"Effect Lamp", "Cabinet", 1, 2});
    l.push_back({"Left Red Highlight", "Highlights", 0, 10, true});
    l.push_back({"Left Blue Highlight", "Highlights", 0, 9, true});
    l.push_back({"Right Red Highlight", "Highlights", 0, 11, true});
    l.push_back({"Right Blue Highlight", "Highlights", 0, 5, true});
    l.push_back({"Neon (SSR)", "Cabinet", 1, 11});
    for (int k = 0; k < 8; k++) l.push_back({"Status LED " + std::to_string(k + 1), "Board (diagnostic)", 2, k, true});
    return l;
}

// pop'n music on DJ Main drives the same two registers. MAME doesn't name the
// bits for pop'n; this is what pop'n music 1 does with them (oracle/scripts/
// popn1j-play.his, in lockstep with MAME): each button's bit lights while it is
// pressed (reg 0: buttons 1-5 on bits 0-4, 6-9 on bits 6-9, as beatmania's two
// rows), bits 6-10 of reg 1 run a 5-step chase (the top lamps, as on Firebeat
// pop'n; which is topmost is not known) and bits 0-3 light patterns (4 side
// lamps, likewise). Reg 1 bit 15 pulses once per coin (the counter) and bit 14
// is the coin blocker; the other bits never change.
std::vector<Lamp> make_djpopn_lamps() {
    std::vector<Lamp> l;
    add_run(l, "Button Lamp", "Buttons", 0, {0, 1, 2, 3, 4, 6, 7, 8, 9});
    add_run(l, "Top Lamp", "Top", 1, {6, 7, 8, 9, 10});
    add_run(l, "Side Lamp", "Side", 1, {0, 1, 2, 3});
    for (int k = 0; k < 8; k++) l.push_back({"Status LED " + std::to_string(k + 1), "Board (diagnostic)", 2, k, true});
    return l;
}

// MAME firebeat_popn_state::lamp_output_popn_w
std::vector<Lamp> make_popn_lamps() {
    std::vector<Lamp> l;
    add_run(l, "Button Lamp", "Buttons", 0, {8, 9, 10, 11, 12, 13, 14, 15, 31});
    add_run(l, "Top Lamp", "Top (1 = topmost)", 0, {19, 20, 21, 22, 23});
    add_run(l, "Side Lamp", "Side pillars", 0, {27, 28, 29, 30});
    add_status(l);
    return l;
}

// MAME firebeat_ppp_state::lamp_output_ppp_w, lamp_output2_ppp_w, lamp_output3_ppp_w
std::vector<Lamp> make_ppp_lamps() {
    std::vector<Lamp> l = {{"Left Lamp", "Cabinet", 0, 8}, {"Right Lamp", "Cabinet", 0, 9}, {"Door Lamp", "Cabinet", 0, 10},
                           {"OK Lamp", "Cabinet", 0, 11}, {"Slim Lamp", "Cabinet", 0, 15}};
    add_run(l, "Stage LED", "Stage", 0, {24, 25, 26, 27, 16, 17, 18, 19});
    add_run(l, "Top LED", "Top", 1, {16, 17, 18, 19, 0, 1, 2, 3});
    add_run(l, "Lamp", "Lamps", 2, {16, 18, 20, 22});
    add_status(l);
    return l;
}

// MAME firebeat_kbm_state::lamp_output_kbm_w
std::vector<Lamp> make_kbm_lamps() {
    std::vector<Lamp> l = {{"Start 1P Lamp", "Cabinet", 0, 24}, {"Start 2P Lamp", "Cabinet", 0, 25},
                           {"Door Lamp", "Cabinet", 0, 28}, {"Neon", "Cabinet", 0, 11}};
    add_run(l, "Lamp", "Lamps", 0, {8, 9, 10});
    add_status(l);
    return l;
}

// beatmania III: MAME decodes no cabinet lamps (lamp_output2_w/3_w only log)
std::vector<Lamp> make_bm3_lamps() {
    std::vector<Lamp> l;
    add_status(l);
    return l;
}

// DDR: the I/O board's outputs as MAME's ddr_output_callback names them. reg 0, bit = output
// number (analog: gx700pwbf latch * 8 + bit; digital: k573dio's); the speaker neon is 30 on the
// analog board and 28 on the digital one, both shown as 30.
std::vector<Lamp> make_ddr_lamps() {
    std::vector<Lamp> l;
    const char *dirs[4] = {"Up", "Left", "Right", "Down"};
    for (int p = 0; p < 2; p++)
        for (int k = 0; k < 4; k++)
            l.push_back({"P" + std::to_string(p + 1) + " Foot " + dirs[k], "Pad " + std::to_string(p + 1), 0, p * 8 + k});
    l.push_back({"P1 Start Lamp", "Cabinet", 0, 17});
    l.push_back({"P2 Start Lamp", "Cabinet", 0, 18});
    l.push_back({"Body Right Low", "Cabinet", 0, 20});
    l.push_back({"Body Left Low", "Cabinet", 0, 21});
    l.push_back({"Body Left High", "Cabinet", 0, 22});
    l.push_back({"Body Right High", "Cabinet", 0, 23});
    l.push_back({"Speaker Neon", "Cabinet", 0, 30});
    return l;
}

// DrumMania: the digital board's outputs as MAME's drmn_output_callback names them (DrumMania
// 2nd and later). reg 0, bit = output number.
std::vector<Lamp> make_drmn_lamps() {
    return {
        {"Hi-Hat", "Drums", 0, 0}, {"High Tom", "Drums", 0, 1}, {"Low Tom", "Drums", 0, 2}, {"Snare", "Drums", 0, 3},
        {"Cymbal", "Drums", 0, 12}, {"Start Lamp", "Panel", 0, 13}, {"Select Lamp", "Panel", 0, 14},
        {"Spot Left/Right", "Cabinet", 0, 8}, {"Neon Top", "Cabinet", 0, 9}, {"Neon Woofer", "Cabinet", 0, 11},
    };
}

// pop'n stage: not decoded yet (no dump to watch); listed by register and bit
std::vector<Lamp> make_djpstage_lamps() {
    std::vector<Lamp> l;
    for (int r = 0; r < 2; r++)
        for (int b = 0; b < 16; b++) l.push_back({"Light " + std::string(1, (char)('A' + r)) + std::to_string(b), "Lights (undecoded)", r, b});
    for (int k = 0; k < 8; k++) l.push_back({"Status LED " + std::to_string(k + 1), "Board (diagnostic)", 2, k, true});
    return l;
}

std::string trim(std::string s) {
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

}  // namespace

const std::vector<Control> &controls(const std::string &family) {
    static const std::map<std::string, std::vector<Control>> all = {
        {"bm3", make_bm3()}, {"popn", make_popn()}, {"ppp", make_ppp()}, {"kbm", make_kbm()},
        {"djbm", make_djbm()}, {"djpopn", make_djpopn()}, {"djpstage", make_djpstage()}, {"ddr", make_ddr()},
        {"drmn", make_drmn()}};
    auto f = all.find(family);
    return f != all.end() ? f->second : all.at("bm3");
}

const std::vector<Lamp> &lamps(const std::string &family) {
    static const std::map<std::string, std::vector<Lamp>> all = {
        {"bm3", make_bm3_lamps()}, {"popn", make_popn_lamps()}, {"ppp", make_ppp_lamps()}, {"kbm", make_kbm_lamps()},
        {"djbm", make_djbm_lamps()}, {"djpopn", make_djpopn_lamps()}, {"djpstage", make_djpstage_lamps()},
        {"ddr", make_ddr_lamps()}, {"drmn", make_drmn_lamps()}};
    auto f = all.find(family);
    return f != all.end() ? f->second : all.at("bm3");
}

std::string default_bindings(const std::string &family) {
    std::string s;
    for (auto &c : controls(family))
        if (!c.defaults.empty()) s += c.name + " = " + c.defaults + "\n";
    return s;
}

std::vector<Entry> parse_list_file(const std::string &text) {
    std::vector<Entry> out;
    size_t pos = 0;
    int line_no = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        std::string line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? text.size() : e + 1;
        line_no++;
        if (auto h = line.find('#'); h != std::string::npos) line = line.substr(0, h);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        Entry en{trim(line.substr(0, eq)), {}, line_no};
        std::string rhs = line.substr(eq + 1);
        size_t p = 0;
        while (p <= rhs.size()) {
            size_t c = rhs.find(',', p);
            std::string b = trim(rhs.substr(p, c == std::string::npos ? std::string::npos : c - p));
            if (!b.empty()) en.items.push_back(b);
            if (c == std::string::npos) break;
            p = c + 1;
        }
        out.push_back(std::move(en));
    }
    return out;
}

std::string format_entry(const std::string &name, const std::vector<std::string> &items) {
    std::string s = name + " =";
    for (size_t k = 0; k < items.size(); k++) s += (k ? ", " : " ") + items[k];
    return s;
}

}  // namespace hk::cab
