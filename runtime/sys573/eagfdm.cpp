// SPDX-License-Identifier: GPL-3.0-or-later
// e-AMUSEMENT for the System 573 GuitarFreaks and DrumMania (eagfdm.h).
#include "sys573/eagfdm.h"

#include <cstdio>
#include <ctime>
#include <thread>

namespace hk::k573 {

bool Eamuse::open(const std::string &where, const std::string &dir, const std::string &log, std::function<int64_t()> now) {
    if (where != "local") {
        remote_ = std::make_unique<ea::Remote>(where);
        if (!remote_->ok()) {
            fprintf(stderr, "--eamuse: %s is not http://host[:port][/path]\n", where.c_str());
            return false;
        }
    }
    if (!log.empty() && !(log_ = fopen(log.c_str(), "w"))) {
        fprintf(stderr, "HK_573_NET_LOG: cannot write %s\n", log.c_str());
        return false;
    }
    server_ = std::make_unique<ea::Server>(dir, &Eamuse::answer);
    server_->now = std::move(now);
    lan_ = std::make_unique<ea::Lan>([this](const ea::Lan::Target &t, const std::string &req, std::function<void(std::string)> done) {
        if (!remote_) {
            done(server_->handle(req));
            return;
        }
        // online: off the emulator's thread; the unit's recv waits for it
        std::thread([this, t, req, done = std::move(done)] {
            std::string error;
            std::string answer = remote_->exchange(t, req, &error);
            if (log_) {
                fprintf(log_, "online %s:%u\n  %s\n  %s\n", t.services ? remote_->host().c_str() : t.host.c_str(),
                        t.services ? remote_->port() : t.port, ea::clear(req).c_str(),
                        answer.empty() ? ("failed: " + error).c_str() : ea::clear(answer).c_str());
                fflush(log_);
            }
            done(std::move(answer));
        }).detach();
    });
    lan_->now = server_->now;
    if (log_) lan_->log = server_->log = [this](const std::string &s) { fprintf(log_, "%s\n", s.c_str()); fflush(log_); };
    if (remote_) printf("e-AMUSEMENT: online, %s\n", where.c_str());
    return true;
}

// What GuitarFreaks and DrumMania ask beyond the common modules
std::string Eamuse::answer(ea::Server &s, const ea::Call &, const ea::Request &r, const std::string &host) {
    const std::string &m = r.module, &f = r.method;
    if (m == "services" && f == "get") {
        // the game looks up "posevent", "pcbtracker", "numbering", "userid" and "local" (the rest);
        // and two addresses: "keepalive" (ping://, pa=, ga=; drmn10m 0x8003caa8) and "ntp" (ntp://)
        std::string a = ea::Server::services(host, {"posevent", "pcbtracker", "numbering", "userid", "local"});
        a.insert(a.rfind("</services>"), "<item name=\"keepalive\" url=\"ping://192.168.50.1/?pa=192.168.50.1&amp;ga=192.168.50.1\"/>"
                                         "<item name=\"ntp\" url=\"ntp://192.168.50.1/\"/>");
        return a;
    }
    if (m == "ntp" && f == "get") {   // pop'n 9's form, for now
        const time_t t = s.now ? (time_t)s.now() : 0;
        struct tm tm;
        gmtime_r(&t, &tm);
        char b[160];
        snprintf(b, sizeof b, "<ntp status=\"0\"><time year=\"%d\" month=\"%d\" day=\"%d\" hour=\"%d\" min=\"%d\" sec=\"%d\"></time></ntp>",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        return b;
    }
    return "";
}

}  // namespace hk::k573
