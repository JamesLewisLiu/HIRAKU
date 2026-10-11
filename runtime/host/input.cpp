// SPDX-License-Identifier: GPL-3.0-or-later
#include "input.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <vector>

#include "../firebeat/board.h"

namespace hk::host {

namespace {

enum class Src { key, button, axis, axis_neg, axis_pos, hat };
struct Binding {
    Src src;
    int joy = 0, index = 0, hat_dir = 0;
    SDL_Keycode key = 0;
};

std::map<std::string, std::vector<Binding>> g_bind;   // input name -> bindings
std::map<std::string, int> g_held;                    // input name -> number of bound sources down
std::vector<SDL_Joystick *> g_joys;
int16_t g_axis_last[16][16];
bool g_axis_seen[16][16];
double g_tt_scale = 1024.0 / 65536.0 * 4;             // turntable counts per axis unit (one wrap = 4 x 256)

const char *DEFAULTS_BM3 =
    "P1 Button 1 = key:Z, joy0:button0\n"
    "P1 Button 2 = key:S, joy0:button1\n"
    "P1 Button 3 = key:X, joy0:button2\n"
    "P1 Button 4 = key:D, joy0:button3\n"
    "P1 Button 5 = key:C, joy0:button4\n"
    "P1 Turntable = joy0:axis0\n"
    "P1 Turntable - = key:Left, key:Left Shift\n"
    "P1 Turntable + = key:Right, key:A\n"
    "P1 Foot = key:Space, joy0:button7\n"
    "1 Player Start = key:1, joy0:button8\n"
    "P2 Button 1 = key:M\n"
    "P2 Button 2 = key:K\n"
    "P2 Button 3 = key:Comma\n"
    "P2 Button 4 = key:L\n"
    "P2 Button 5 = key:Period\n"
    "P2 Turntable - = key:Down\n"
    "P2 Turntable + = key:Up\n"
    "2 Players Start = key:2\n"
    "Coin 1 = key:5, joy0:button9\n"
    "Test = key:F2\n"
    "Service = key:9\n";

// pop'n music: the nine buttons zig-zag like the cabinet's two rows (odd = lower)
const char *DEFAULTS_POPN =
    "P1 Button 1 = key:Z, joy0:button0\n"
    "P1 Button 2 = key:S, joy0:button1\n"
    "P1 Button 3 = key:X, joy0:button2\n"
    "P1 Button 4 = key:D, joy0:button3\n"
    "P1 Button 5 = key:C, joy0:button4\n"
    "P1 Button 6 = key:F, joy0:button5\n"
    "P1 Button 7 = key:V, joy0:button6\n"
    "P1 Button 8 = key:G, joy0:button7\n"
    "P1 Button 9 = key:B, joy0:button8\n"
    "Coin 1 = key:5, joy0:button9\n"
    "Test = key:F2\n"
    "Service = key:9\n";

// ParaParaParadise: the five sensor bars left to right, plus the two buttons
const char *DEFAULTS_PPP =
    "P1 Button 3 = key:Z, joy0:button0\n"
    "P1 Button 4 = key:X, joy0:button1\n"
    "P1 Button 5 = key:C, joy0:button2\n"
    "P1 Button 6 = key:V, joy0:button3\n"
    "P1 Button 7 = key:B, joy0:button4\n"
    "P1 Button 1 = key:Left, joy0:button5\n"
    "P1 Button 2 = key:Right, joy0:button6\n"
    "1 Player Start = key:1, joy0:button8\n"
    "Coin 1 = key:5, joy0:button9\n"
    "Test = key:F2\n"
    "Service = key:9\n";

std::string trim(std::string s) {
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

bool parse_binding(const std::string &b, Binding &out) {
    if (b.rfind("key:", 0) == 0) {
        // ',' separates bindings, so punctuation keys go by name
        static const std::map<std::string, std::string> alias = {
            {"Comma", ","}, {"Period", "."}, {"Slash", "/"}, {"Semicolon", ";"}, {"Minus", "-"}, {"Equals", "="}};
        std::string k = b.substr(4);
        if (auto a = alias.find(k); a != alias.end()) k = a->second;
        out.src = Src::key;
        out.key = SDL_GetKeyFromName(k.c_str());
        return out.key != SDLK_UNKNOWN;
    }
    int joy, idx;
    char rest[32] = {};
    if (sscanf(b.c_str(), "joy%d:button%d", &joy, &idx) == 2) { out = {Src::button, joy, idx}; return true; }
    if (sscanf(b.c_str(), "joy%d:hat%d:%31s", &joy, &idx, rest) == 3) {
        std::string d = rest;
        int dir = d == "up" ? SDL_HAT_UP : d == "down" ? SDL_HAT_DOWN : d == "left" ? SDL_HAT_LEFT : d == "right" ? SDL_HAT_RIGHT : 0;
        out = {Src::hat, joy, idx, dir};
        return dir != 0;
    }
    if (sscanf(b.c_str(), "joy%d:axis%d%31s", &joy, &idx, rest) >= 2) {
        std::string r = rest;
        out = {r == "-" ? Src::axis_neg : r == "+" ? Src::axis_pos : Src::axis, joy, idx};
        return true;
    }
    return false;
}

void load_text(const std::string &text, const char *origin) {
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
        std::string name = trim(line.substr(0, eq)), rhs = line.substr(eq + 1);
        std::vector<Binding> list;
        size_t p = 0;
        while (p <= rhs.size()) {
            size_t c = rhs.find(',', p);
            std::string b = trim(rhs.substr(p, c == std::string::npos ? std::string::npos : c - p));
            if (!b.empty()) {
                Binding bd{};
                if (parse_binding(b, bd)) list.push_back(bd);
                else fprintf(stderr, "%s:%d: cannot parse binding '%s'\n", origin, line_no, b.c_str());
            }
            if (c == std::string::npos) break;
            p = c + 1;
        }
        g_bind[name] = list;   // a later definition replaces the default
    }
}

void press(const std::string &name, bool down) {
    int &n = g_held[name];
    int before = n;
    n = std::max(0, n + (down ? 1 : -1));
    if ((before > 0) == (n > 0)) return;
    if (name.find("Turntable") != std::string::npos) return;   // handled in input_update
    fb::set_input(name, n > 0 ? 1 : 0);
}

int joy_index(SDL_JoystickID id) {
    for (size_t k = 0; k < g_joys.size(); k++)
        if (SDL_GetJoystickID(g_joys[k]) == id) return (int)k;
    return -1;
}

}  // namespace

void input_init(const std::string &file) {
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    const std::string &fam = fb::family();
    load_text(fam == "popn" ? DEFAULTS_POPN : fam == "ppp" ? DEFAULTS_PPP : DEFAULTS_BM3, "defaults");
    if (!file.empty()) {
        std::ifstream in(file);
        if (in) load_text(std::string((std::istreambuf_iterator<char>(in)), {}), file.c_str());
        else fprintf(stderr, "input: cannot read %s (using defaults)\n", file.c_str());
    }
    int n = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&n);
    for (int k = 0; k < n; k++)
        if (SDL_Joystick *j = SDL_OpenJoystick(ids[k])) {
            g_joys.push_back(j);
            fprintf(stderr, "input: joy%zu = %s (%d buttons, %d axes)\n", g_joys.size() - 1, SDL_GetJoystickName(j),
                    SDL_GetNumJoystickButtons(j), SDL_GetNumJoystickAxes(j));
        }
    SDL_free(ids);
}

void input_joystick_added(unsigned int id) {
    for (auto *j : g_joys)
        if (SDL_GetJoystickID(j) == id) return;   // already open (enumerated at start)
    if (SDL_Joystick *j = SDL_OpenJoystick(id)) {
        g_joys.push_back(j);
        fprintf(stderr, "input: joy%zu = %s (%d buttons, %d axes) connected\n", g_joys.size() - 1, SDL_GetJoystickName(j),
                SDL_GetNumJoystickButtons(j), SDL_GetNumJoystickAxes(j));
    }
}

void input_event(const SDL_Event &e) {
    for (auto &[name, list] : g_bind)
        for (auto &b : list) {
            switch (e.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (b.src == Src::key && !e.key.repeat && e.key.key == b.key) press(name, e.type == SDL_EVENT_KEY_DOWN);
                break;
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP:
                if (b.src == Src::button && joy_index(e.jbutton.which) == b.joy && e.jbutton.button == b.index)
                    press(name, e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN);
                break;
            case SDL_EVENT_JOYSTICK_HAT_MOTION:
                if (b.src == Src::hat && joy_index(e.jhat.which) == b.joy && e.jhat.hat == b.index) {
                    bool down = (e.jhat.value & b.hat_dir) != 0;
                    if (down != (g_held[name] > 0)) press(name, down);
                }
                break;
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                if ((b.src == Src::axis_neg || b.src == Src::axis_pos) && joy_index(e.jaxis.which) == b.joy && e.jaxis.axis == b.index) {
                    bool down = b.src == Src::axis_neg ? e.jaxis.value < -16384 : e.jaxis.value > 16384;
                    if (down != (g_held[name] > 0)) press(name, down);
                }
                break;
            default: break;
            }
        }
}

void input_update(double ms) {
    for (int p = 0; p < 2; p++) {
        std::string base = p == 0 ? "P1 Turntable" : "P2 Turntable";
        // keys: about MAME's KEYDELTA (15 counts per 60 Hz frame)
        static double frac[2] = {0, 0};
        double v = (g_held[base + " +"] > 0 ? 1 : 0) - (g_held[base + " -"] > 0 ? 1 : 0);
        frac[p] += v * ms * 0.9;
        int whole = (int)frac[p];
        frac[p] -= whole;
        if (whole) fb::turntable_add(p, whole);
        // analog axis: follow its motion (wrapping, as turntable encoders do)
        for (auto &b : g_bind[base]) {
            if (b.src != Src::axis || b.joy >= (int)g_joys.size() || b.index >= 16) continue;
            int16_t a = SDL_GetJoystickAxis(g_joys[b.joy], b.index);
            if (g_axis_seen[b.joy][b.index]) {
                int d = (int)a - g_axis_last[b.joy][b.index];
                if (d > 32768) d -= 65536;
                if (d < -32768) d += 65536;
                if (d) fb::turntable_add(p, (int)std::lround(d * g_tt_scale));
            }
            g_axis_last[b.joy][b.index] = a;
            g_axis_seen[b.joy][b.index] = true;
        }
    }
}

std::string input_describe() {
    std::string s;
    for (auto &[name, list] : g_bind) {
        s += "  " + name + " =";
        for (auto &b : list) {
            char buf[64];
            switch (b.src) {
            case Src::key: snprintf(buf, sizeof buf, " key:%s", SDL_GetKeyName(b.key)); break;
            case Src::button: snprintf(buf, sizeof buf, " joy%d:button%d", b.joy, b.index); break;
            case Src::axis: snprintf(buf, sizeof buf, " joy%d:axis%d", b.joy, b.index); break;
            case Src::axis_neg: snprintf(buf, sizeof buf, " joy%d:axis%d-", b.joy, b.index); break;
            case Src::axis_pos: snprintf(buf, sizeof buf, " joy%d:axis%d+", b.joy, b.index); break;
            case Src::hat: snprintf(buf, sizeof buf, " joy%d:hat%d", b.joy, b.index); break;
            }
            s += buf;
        }
        s += "\n";
    }
    return s;
}

}  // namespace hk::host
