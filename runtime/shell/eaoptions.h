// SPDX-License-Identifier: GPL-3.0-or-later
// e-AMUSEMENT options, by spice2x's names and with its meanings, so a cabinet set up for spice2x
// (spicecfg's Network page) carries over to HIRAKU as it is (docs/PLAY.md, "e-AMUSEMENT"):
//
//   -ea                       the server inside HIRAKU (also what happens with no -url); ignored
//                             with -url, as in spice2x
//   -url URL                  the service URL: host:port[/path], or http://host:port[/path]
//   -smartea                  with -url: the server inside HIRAKU if that server doesn't answer
//                             at start
//   -p PCBID                  the cabinet's PCBID (20 hex digits), sent in place of the game's own
//   -card0 / -card1 NUMBER    player 1's / 2's card number (16 hex digits, E004...). Without it,
//                             card0.txt / card1.txt: the working directory's, then the save folder's
//   -autocard off|p1|p2|both  present the card(s) to the reader whenever it waits for one, so
//                             Insert needn't be pressed
//
// Each also takes two dashes (--url ...). main.cpp gives every single-dash long option two.
#pragma once

#include <cctype>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>

namespace hk {

struct EaOptions {
    bool ea = false, smartea = false;
    std::string url, pcbid;
    std::string card[2];               // "" = none given
    bool autocard[2] = {false, false};

    // One option (two dashes). false: not one of these. A bad value sets *error.
    bool option(const std::string &name, const std::function<std::string()> &value, std::string *error) {
        if (name == "--ea") { ea = true; return true; }
        if (name == "--smartea") { smartea = true; return true; }
        if (name == "--url") {
            url = value();
            if (url.empty()) *error = "-url: empty";
            return true;
        }
        if (name == "--p" || name == "--pcbid") {
            pcbid = upper(value());
            if (!hex(pcbid, 20)) *error = "-p: a PCBID is 20 hex digits, not \"" + pcbid + "\"";
            return true;
        }
        if (name == "--card0" || name == "--card1") {
            std::string &c = card[name.back() - '0'];
            c = upper(value());
            if (!hex(c, 16)) *error = name.substr(1) + ": a card number is 16 hex digits (E004...), not \"" + c + "\"";
            return true;
        }
        if (name == "--autocard") {
            const std::string v = value();
            autocard[0] = v == "p1" || v == "both";
            autocard[1] = v == "p2" || v == "both";
            if (v != "off" && v != "p1" && v != "p2" && v != "both") *error = "-autocard: off, p1, p2 or both, not \"" + v + "\"";
            return true;
        }
        return false;
    }

    // After the command line: a card not given takes cardN.txt's number (the working
    // directory's, then save_dir's), as spice2x reads them. A file that isn't a number sets *error.
    void read_card_files(const std::string &save_dir, std::string *error) {
        for (int n = 0; n < 2; n++) {
            if (!card[n].empty()) continue;
            const std::string name = "card" + std::to_string(n) + ".txt";
            for (const std::string &path : {name, save_dir.empty() ? std::string() : save_dir + "/" + name}) {
                std::ifstream f(path);
                if (path.empty() || !f) continue;
                std::string c;
                for (char ch : std::string(std::istreambuf_iterator<char>(f), {}))
                    if (!std::isspace((unsigned char)ch)) c += (char)std::toupper((unsigned char)ch);
                if (!hex(c, 16)) { *error = path + ": a card number is 16 hex digits (E004...)"; return; }
                card[n] = c;
                break;
            }
        }
    }

    bool online() const { return !url.empty(); }
    // url with its scheme: spice2x's "host:port" form is plain HTTP
    std::string http_url() const { return url.find("://") == std::string::npos ? "http://" + url : url; }

    static const char *help() {
        return "  e-AMUSEMENT, as spice2x's options (one dash or two):\n"
               "  -ea                  the server inside HIRAKU (the default; ignored with -url)\n"
               "  -url URL             a server on the network: host:port[/path] or http://host:port[/path]\n"
               "  -smartea             with -url: the server inside HIRAKU if that one doesn't answer at start\n"
               "  -p PCBID             the cabinet's PCBID (20 hex digits) in the game's calls\n"
               "  -card0 NUMBER        player 1's card number (16 hex digits, E004...); default: card0.txt in\n"
               "                       the working directory or the save folder (-card1, card1.txt: player 2)\n"
               "  -autocard off|p1|p2|both  the card is presented whenever the reader waits for one\n";
    }

private:
    static std::string upper(std::string s) {
        for (char &c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }
    static bool hex(const std::string &s, size_t n) {
        if (s.size() != n) return false;
        for (char c : s)
            if (!std::isxdigit((unsigned char)c)) return false;
        return true;
    }
};

}  // namespace hk
