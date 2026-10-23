// SPDX-License-Identifier: GPL-3.0-or-later
// What the e-AMUSEMENT server's files (easerver*.cpp) share: the protocol's pieces (eamuse/protocol.h)
// and the server's own. Not for the server's users (easerver.h).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "eamuse/protocol.h"
#include "python/easerver.h"

namespace hk::py {

using ea::attr;
using ea::esc;
using ea::from_lines;
using ea::header;
using ea::num;
using ea::read_attrs;
using ea::Record;
using ea::safe_name;
using ea::to_line;

// The three rankings (docs/EAMUSE3.md, "rank"): what is kept and how entries compare
struct EamuseServer::Board {
    const char *file;
    const char *who;                   // the request's field naming the entrant
    std::vector<const char *> keep;    // fields from the request kept with the entry
    const char *first, *second;        // ranked by these, larger first
    int second_base;                   // parts is hex
};
using Board = EamuseServer::Board;
// easerver_rank.cpp: pop'n 9's three boards; what pop'n 10 on keep of an entry (rank.exp)
extern const Board BOARD_SCORE, BOARD_H, BOARD_GZ;
extern const std::vector<const char *> EXP_KEEP;
bool better(const Board &b, const Record &x, const Record &y);   // x ranks above y on b

// easerver.cpp: LZSS as the games pack (X-Compress: lz77), base64, printf of two numbers
std::string lzss(const std::string &in);
std::string base64(const std::string &in);
std::string unbase64_b(const std::string &x);
std::string fmt(const char *f, unsigned a, unsigned b);
// The PC pop'n games, libavs-win32's e-AMUSEMENT (runtime/pc/): pop'n 15, 16, 17, 18
bool pc_popn(const std::string &game);

}  // namespace hk::py
