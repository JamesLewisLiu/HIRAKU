// SPDX-License-Identifier: GPL-3.0-or-later
// A local e-AMUSE3 server for Python 1 pop'n (PLAN.md section 21, "e-AMUSEMENT"): what
// Konami's servers answered in 2003, from the game's own requests and response parsers.
//
// The protocol is the one later e-AMUSEMENT games use (bemaniutils documents it): an HTTP/1.0
// POST whose body is XML, RC4-encrypted with key = MD5(the 6 bytes of "X-Eamuse-Info:
// 1-xxxxxxxx-xxxx" + a shared secret). A request is <call srcid=.. model=..><MODULE
// method=.. .../></call>; the answer <response><MODULE status="0" ...>...</MODULE></response>.
// Modules: services (the others' URLs), numbering, package, userdata, userid, rank,
// p_friend, eemall, posevent, pcbtracker, ntp.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hk::py {

class EamuseServer {
public:
    // dir: where the server keeps what it must remember (cards, player data), or "" for nothing
    explicit EamuseServer(std::string dir) : dir_(std::move(dir)) {}

    // One HTTP request (head and body, as sent) to the whole HTTP response.
    std::string handle(const std::string &request);

    // Each call in clear (request and answer), for HK_PY_NET_LOG.
    std::function<void(const std::string &)> log;
    // The cabinet's wall clock, as seconds since 1970 read as UTC (ntp.get sends it as is, and the
    // game sets its clock to it): in the core, the emulated clock (reproducible runs); in
    // hiraku-eamuse, the host's clock in Japan time unless told otherwise.
    std::function<int64_t()> now;

    // The protocol's own pieces, public for tools and tests.
    static std::vector<uint8_t> key(const std::string &info);   // "1-xxxxxxxx-xxxx"
    static void rc4(const std::vector<uint8_t> &key, std::string &data);
    // An HTTP message (request or answer) as text: its first line and its body in clear.
    static std::string clear(const std::string &message);

public:
    struct Board;   // a ranking (easerver.cpp)

private:
    // One module element of a call: <MODULE method=.. ...>inner</MODULE>. A call can carry
    // several (posevent sends three at once); the answer has one element each, in order.
    struct Request {
        std::string module, method;
        std::map<std::string, std::string> attrs;
        std::string inner;   // what is inside it, raw
    };
    struct Call {
        std::string srcid, model;
        std::vector<Request> requests;
    };
    static bool parse(const std::string &xml, Call &c);
    std::string answer(const Call &c, const Request &r, const std::string &host);
    std::string message() const;   // the owner's title-screen message (package "message"), or ""

    struct Entered {   // a result entered on a board (enter)
        std::map<std::string, std::string> kept;   // the entrant's best, as kept
        size_t place, entries;
        bool update;   // the result is the new best
    };
    Entered enter(const Board &b, const Request &r);
    std::string rank_register(const Board &b, const Request &r);
    // pop'n 10 on: their own calls and answers ("" leaves the call to pop'n 9's)
    std::string answer_later(const std::string &game, const Request &r, const std::string &host);
    std::string rank_get(const Board &b, const Request &r);
    static std::string item(const Board &b, const std::map<std::string, std::string> &entry, size_t place);
    std::string read_file(const std::string &name) const;
    void write_file(const std::string &name, const std::string &data) const;

    std::string dir_;
    uint32_t serial_ = 0;   // for the answers' X-Eamuse-Info: deterministic
};

}  // namespace hk::py
