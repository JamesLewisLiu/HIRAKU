// SPDX-License-Identifier: GPL-3.0-or-later
// key=value files, one pair a line: dumps/<id>/machine.txt, as tools/ingest.py writes them. Lines
// starting with '#' and lines without '=' are skipped; a Windows line end is taken off.
#pragma once

#include <fstream>
#include <map>
#include <string>

namespace hk {

inline std::map<std::string, std::string> read_kv(const std::string &path) {
    std::map<std::string, std::string> kv;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

}  // namespace hk
