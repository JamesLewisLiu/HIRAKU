// SPDX-License-Identifier: GPL-3.0-or-later
#include "lampview.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "../shell/cabinet.h"
#include "../shell/core.h"

namespace hk::host::lampview {

namespace {

bool g_shown = false;
std::vector<uint32_t> g_pixels;
int g_w = 0, g_h = 0;
unsigned g_id = 0;
uint64_t g_changes = ~0ull;   // the core's lamp_changes() the picture shows

// A lit lamp's colour, from its name (Dance Maniax's "Blue IO Lamp 3", "Spot Light Pink"...);
// amber when the name says none
uint32_t colour(const std::string &name) {
    static const struct { const char *word; uint32_t rgb; } named[] = {
        {"Blue", 0x3c8cff}, {"Red", 0xff3c3c}, {"Yellow", 0xffdc28}, {"Pink", 0xff6ec8},
        {"Green", 0x3cdc5a}, {"White", 0xffffff}, {"Orange", 0xff8c28}, {"Purple", 0xb45aff},
    };
    for (const auto &n : named)
        if (name.find(n.word) != std::string::npos) return n.rgb;
    return 0xffbe3c;
}

void set_colour(SDL_Renderer *r, uint32_t rgb, uint8_t a = 255) {
    SDL_SetRenderDrawColor(r, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, a);
}

// A lamp's name and where the game drives it: its output (bit) number, after the register's
// where the core has several ("Blue IO Lamp 0 [16]", "Top Lamp 1 [1.6]")
std::string label(const cab::Lamp &l) {
    return l.name + " [" + (l.reg ? std::to_string(l.reg) + "." : "") + std::to_string(l.bit) + "]";
}

// The panel: a title line, then each group's name and its lamps in up to 4 columns, a square
// (filled in the lamp's colour when lit) before each name
void draw(const std::vector<cab::Lamp> &lamps, const std::vector<uint8_t> &state, uint64_t changes) {
    std::vector<std::string> groups;
    for (const cab::Lamp &l : lamps)
        if (std::find(groups.begin(), groups.end(), l.group) == groups.end()) groups.push_back(l.group);
    size_t name_cols = 0;
    for (const cab::Lamp &l : lamps) name_cols = std::max(name_cols, label(l).size());
    const int pad = 6, line = 12, cell = (int)(name_cols + 3) * 8;
    const std::string title = "Lights (F9): " + core().family() + ", " + std::to_string(changes) + " changes";
    int cols = 1, rows = 1;   // the title
    for (const std::string &g : groups) {
        int n = 0;
        for (const cab::Lamp &l : lamps) n += l.group == g;
        const int c = std::min(n, 4);
        cols = std::max(cols, c);
        rows += 1 + (n + c - 1) / c;
    }
    if (lamps.empty()) rows++;
    const std::string none = "No lamps decoded for this game";
    const int w = std::max({cols * cell, (int)title.size() * 8, lamps.empty() ? (int)none.size() * 8 : 0}) + 2 * pad;
    const int h = rows * line - 4 + 2 * pad;
    SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    if (SDL_Renderer *r = SDL_CreateSoftwareRenderer(s)) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        set_colour(r, 0x5a5a64, 235);
        SDL_RenderClear(r);
        set_colour(r, 0x101014, 215);
        const SDL_FRect in{1, 1, (float)w - 2, (float)h - 2};
        SDL_RenderFillRect(r, &in);
        int y = pad;
        set_colour(r, 0xffffff);
        SDL_RenderDebugText(r, (float)pad, (float)y, title.c_str());
        y += line;
        if (lamps.empty()) {
            set_colour(r, 0xc8c8c8);
            SDL_RenderDebugText(r, (float)pad, (float)y, none.c_str());
        }
        for (const std::string &g : groups) {
            set_colour(r, 0x9696a0);
            SDL_RenderDebugText(r, (float)pad, (float)y, g.c_str());
            y += line;
            int k = 0;
            for (size_t i = 0; i < lamps.size(); i++) {
                if (lamps[i].group != g) continue;
                const int row = k / cols;
                const float x = (float)(pad + (k % cols) * cell), yy = (float)(y + row * line);
                const bool lit = i < state.size() && state[i];
                const SDL_FRect box{x, yy, 8, 8};
                if (lit) {
                    set_colour(r, colour(lamps[i].name));
                    SDL_RenderFillRect(r, &box);
                } else {
                    set_colour(r, 0x505058);
                    SDL_RenderRect(r, &box);
                }
                set_colour(r, lit ? 0xffffff : 0x8c8c8c);
                SDL_RenderDebugText(r, x + 12, yy, label(lamps[i]).c_str());
                k++;
            }
            y += ((k + cols - 1) / cols) * line;
        }
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

}  // namespace

void set_shown(bool on) { g_shown = on; }
void toggle() { g_shown = !g_shown; }
bool shown() { return g_shown; }

bool panel(Panel &p) {
    if (!g_shown) return false;
    const uint64_t changes = core().lamp_changes();
    if (changes != g_changes || g_pixels.empty()) {
        draw(cab::lamps(core().family()), core().lamp_state(), changes);
        g_changes = changes;
    }
    if (g_pixels.empty()) return false;
    p.pixels = &g_pixels;
    p.w = g_w, p.h = g_h;
    p.id = g_id;
    return true;
}

}  // namespace hk::host::lampview
