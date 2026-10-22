// SPDX-License-Identifier: GPL-3.0-or-later
#include "shaderview.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace hk::host::shaderview {

namespace {

constexpr int COLS = 38;   // characters a line (8 pixels each)
constexpr int ROWS = 26;   // settings shown at once
constexpr int LINE = 10, PAD = 6;

int g_side = 0;            // 0 hidden, 1 right, 2 left
int g_sel = -1, g_top = 0; // the picked setting, the first shown (indexes into picture::settings)
std::string g_msg;         // what the last save said, for a few seconds
uint64_t g_msg_until = 0;
bool g_dirty = true;
picture::Mode g_drawn_mode = picture::Mode::off;
unsigned g_drawn_gen = ~0u;
std::vector<uint32_t> g_pixels;
int g_w = 0, g_h = 0;
unsigned g_id = 0;

bool heading(const picture::Setting &s) { return s.min == s.max; }

// "--- Signal ---" -> "Signal"
std::string heading_name(const std::string &label) {
    size_t a = label.find_first_not_of("- "), b = label.find_last_not_of("- ");
    return a == std::string::npos ? label : label.substr(a, b - a + 1);
}

// A value with as many decimals as its step has
std::string number(float v, float step) {
    int d = 0;
    if (step > 0)
        while (d < 4 && std::fabs(step * std::pow(10.0f, (float)d) - std::round(step * std::pow(10.0f, (float)d))) > 1e-3f) d++;
    char t[32];
    snprintf(t, sizeof t, "%.*f", d, v);
    return t;
}

// A label's name before its bracket ("Colour decoder" of "Colour decoder (0 auto, 1 standard, ...)"),
// and the choices the bracket names, if it's a list of them ("0 auto, 1 standard": 0 -> "auto")
std::string short_name(const std::string &label) {
    const size_t b = label.find(" (");
    return b == std::string::npos ? label : label.substr(0, b);
}
std::string choice(const std::string &label, float v) {
    const size_t a = label.find(" ("), b = label.rfind(')');
    if (a == std::string::npos || b == std::string::npos || b < a || std::fabs(v - std::round(v)) > 1e-4f) return {};
    const std::string list = label.substr(a + 2, b - a - 2);
    size_t p = 0;
    while (p < list.size()) {
        size_t e = list.find(", ", p);
        const std::string item = list.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? list.size() : e + 2;
        char *end = nullptr;
        const long k = strtol(item.c_str(), &end, 10);
        if (end == item.c_str() || *end != ' ') return {};   // not a list of choices
        if (k == std::lround(v)) return end + 1;
    }
    return {};
}

const char *shader_name(picture::Mode m) {
    return m == picture::Mode::lola ? "Lola Retro" : m == picture::Mode::full ? "crt-royale" : "crt-guest-advanced-fast";
}

// The next setting from `from` in direction `dir` that isn't a heading (from itself if it isn't)
int settle(const std::vector<picture::Setting> &list, int from, int dir) {
    const int n = (int)list.size();
    from = std::clamp(from, 0, std::max(0, n - 1));
    for (int i = from; i >= 0 && i < n; i += dir)
        if (!heading(list[i])) return i;
    for (int i = from; i >= 0 && i < n; i -= dir)
        if (!heading(list[i])) return i;
    return -1;
}

// word-wrapped into lines of `cols`
std::vector<std::string> wrap(const std::string &text, size_t cols, size_t lines) {
    std::vector<std::string> out;
    std::string line;
    size_t p = 0;
    while (p < text.size() && out.size() < lines) {
        size_t sp = text.find(' ', p);
        std::string word = text.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
        p = sp == std::string::npos ? text.size() : sp + 1;
        if (!line.empty() && line.size() + 1 + word.size() > cols) out.push_back(line), line.clear();
        line += (line.empty() ? "" : " ") + word.substr(0, cols);
    }
    if (!line.empty() && out.size() < lines) out.push_back(line);
    return out;
}

void colour(SDL_Renderer *r, uint32_t rgb, uint8_t a = 255) {
    SDL_SetRenderDrawColor(r, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, a);
}

void text(SDL_Renderer *r, int col, int line, uint32_t rgb, const std::string &t) {
    colour(r, rgb);
    SDL_RenderDebugText(r, (float)(PAD + col * 8), (float)(PAD + line * LINE), t.c_str());
}

void draw(picture::Mode mode, bool have, const std::vector<picture::Setting> &list) {
    const int shown = have ? std::min(ROWS, (int)list.size()) : 1;
    const int lines = 2 + shown + 1 + 3 + 1 + 3;
    const int w = COLS * 8 + 2 * PAD, h = lines * LINE + 2 * PAD - 2;
    SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    if (SDL_Renderer *r = SDL_CreateSoftwareRenderer(s)) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        colour(r, 0x5a5a64, 235);
        SDL_RenderClear(r);
        colour(r, 0x101014, 215);
        const SDL_FRect in{1, 1, (float)w - 2, (float)h - 2};
        SDL_RenderFillRect(r, &in);
        int y = 0;
        text(r, 0, y++, 0xffffff, std::string("Shader (F8): ") + (mode == picture::Mode::off ? "none" : shader_name(mode)));
        if (!have) {
            text(r, 0, y++, 0xc8c8c8, mode == picture::Mode::off ? "Sharp pixels: F10 picks a CRT shader" : "The shader isn't loaded");
        } else {
            int changed = 0;
            for (const auto &st : list) changed += st.changed;
            const int first = g_top + 1, last = std::min((int)list.size(), g_top + ROWS);
            text(r, 0, y++, !g_msg.empty() ? 0x78dc8c : changed ? 0xffbe3c : 0x9696a0,
                 !g_msg.empty() ? g_msg.substr(0, COLS)
                 : changed      ? std::to_string(changed) + " changed, not saved (Enter)"
                                : std::to_string(first) + "-" + std::to_string(last) + " of " + std::to_string(list.size()) +
                                 (last < (int)list.size() ? ", more below" : ""));
            for (int i = g_top; i < g_top + shown && i < (int)list.size(); i++, y++) {
                const auto &st = list[i];
                if (heading(st)) {
                    text(r, 0, y, 0x8c8cb4, heading_name(st.label).substr(0, COLS));
                    continue;
                }
                if (i == g_sel) {
                    colour(r, 0x34407a, 235);
                    const SDL_FRect bar{(float)PAD - 2, (float)(PAD + y * LINE - 1), (float)(COLS * 8 + 4), (float)LINE};
                    SDL_RenderFillRect(r, &bar);
                }
                // the value: the choice's name where the label lists them, else the number
                std::string v = choice(st.label, st.value);
                v = v.empty() ? number(st.value, st.step).substr(0, 8) : v.substr(0, 14);
                const size_t room = COLS - 2 - v.size();
                std::string name = short_name(st.label);
                if (name.size() > room) name = name.substr(0, room - 1) + ".";
                text(r, 0, y, 0xffbe3c, st.changed ? "*" : " ");
                text(r, 1, y, i == g_sel ? 0xffffff : 0xc8c8c8, name);
                // amber: not the default
                const bool own = std::fabs(st.value - st.preset) > 1e-6f * std::max(1.0f, std::fabs(st.preset));
                text(r, COLS - (int)v.size(), y, own ? 0xffbe3c : 0xffffff, v);
            }
            y++;
            if (g_sel >= 0 && g_sel < (int)list.size()) {
                const auto &st = list[g_sel];
                const auto desc = wrap(st.label, COLS, 3);
                for (int k = 0; k < 3; k++) text(r, 0, y + k, 0xdcdcdc, k < (int)desc.size() ? desc[k] : "");
                const std::string def = choice(st.label, st.preset);
                text(r, 0, y + 3, 0x9696a0,
                     (number(st.min, st.step) + " to " + number(st.max, st.step) + ", step " + number(st.step, st.step) +
                      ", default " + (def.empty() ? number(st.preset, st.step) : def))
                         .substr(0, COLS));
            }
            y += 4;
        }
        y = lines - 3;
        text(r, 0, y++, 0x9696a0, "Up/Down: pick  Left/Right: change");
        text(r, 0, y++, 0x9696a0, "Shift: x10  Bksp: saved  Del: default");
        text(r, 0, y++, 0x9696a0, "Enter: save all games (Shift: this)");
        SDL_RenderPresent(r);
        SDL_DestroyRenderer(r);
        g_pixels.resize((size_t)w * h);
        for (int yy = 0; yy < h; yy++)
            memcpy(&g_pixels[(size_t)yy * w], (const uint8_t *)s->pixels + (size_t)yy * s->pitch, (size_t)w * 4);
        g_w = w, g_h = h;
        g_id++;
    }
    SDL_DestroySurface(s);
}

// The picked setting a real one, and in the rows shown (with its heading where it is just above)
void place(const std::vector<picture::Setting> &list) {
    if (list.empty()) return;
    if (g_sel < 0 || g_sel >= (int)list.size() || heading(list[g_sel])) g_sel = settle(list, std::max(0, g_sel), 1);
    if (g_sel < 0) return;
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + ROWS) g_top = g_sel - ROWS + 1;
    if (g_top == g_sel && g_sel > 0 && heading(list[g_sel - 1])) g_top = g_sel - 1;
    g_top = std::clamp(g_top, 0, std::max(0, (int)list.size() - ROWS));
}

}  // namespace

void toggle() {
    g_side = (g_side + 1) % 3;
    g_dirty = true;
}
bool shown() { return g_side != 0; }
bool on_left() { return g_side == 2; }

bool key(const SDL_KeyboardEvent &k, picture::Mode mode) {
    if (!g_side || (k.mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_GUI))) return false;   // Alt+Enter: fullscreen
    switch (k.key) {
    case SDLK_UP: case SDLK_DOWN: case SDLK_PAGEUP: case SDLK_PAGEDOWN: case SDLK_HOME: case SDLK_END:
    case SDLK_LEFT: case SDLK_RIGHT: case SDLK_BACKSPACE: case SDLK_DELETE: case SDLK_RETURN: case SDLK_KP_ENTER:
        break;
    default:
        return false;
    }
    if (k.type != SDL_EVENT_KEY_DOWN) return true;   // its key-ups aren't the game's either
    std::vector<picture::Setting> list;
    if (!picture::settings(mode, list) || list.empty()) return true;
    place(list);
    if (g_sel < 0) return true;
    const bool shift = k.mod & SDL_KMOD_SHIFT;
    const picture::Setting &st = list[g_sel];
    switch (k.key) {
    case SDLK_UP: g_sel = settle(list, g_sel - 1, -1); break;
    case SDLK_DOWN: g_sel = settle(list, g_sel + 1, 1); break;
    case SDLK_PAGEUP: g_sel = settle(list, g_sel - (ROWS - 1), -1); break;
    case SDLK_PAGEDOWN: g_sel = settle(list, g_sel + (ROWS - 1), 1); break;
    case SDLK_HOME: g_sel = settle(list, 0, 1); break;
    case SDLK_END: g_sel = settle(list, (int)list.size() - 1, -1); break;
    case SDLK_LEFT:
    case SDLK_RIGHT: {
        const float step = st.step > 0 ? st.step : 0.01f;
        float v = st.value + (k.key == SDLK_RIGHT ? 1 : -1) * step * (shift ? 10 : 1);
        v = std::clamp(v, st.min, st.max);
        v = std::clamp(st.min + std::round((v - st.min) / step) * step, st.min, st.max);   // on the step's grid
        picture::set_setting(mode, st.name, v);
        break;
    }
    case SDLK_BACKSPACE: picture::set_setting(mode, st.name, st.saved); break;
    case SDLK_DELETE: picture::set_setting(mode, st.name, st.preset); break;
    default: {   // Enter
        picture::save_settings(mode, shift, g_msg);
        g_msg_until = SDL_GetTicks() + 4000;
        break;
    }
    }
    g_dirty = true;
    return true;
}

bool panel(Panel &p, picture::Mode mode) {
    if (!g_side) return false;
    std::vector<picture::Setting> list;
    const bool have = picture::settings(mode, list) && !list.empty();
    if (!g_msg.empty() && SDL_GetTicks() > g_msg_until) g_msg.clear(), g_dirty = true;
    if (g_dirty || mode != g_drawn_mode || picture::settings_generation() != g_drawn_gen || g_pixels.empty()) {
        if (mode != g_drawn_mode) g_sel = -1, g_top = 0;
        if (have) place(list);
        draw(mode, have, list);
        g_drawn_mode = mode;
        g_drawn_gen = picture::settings_generation();
        g_dirty = false;
    }
    if (g_pixels.empty()) return false;
    p.pixels = &g_pixels;
    p.w = g_w, p.h = g_h;
    p.id = g_id;
    return true;
}

}  // namespace hk::host::shaderview
