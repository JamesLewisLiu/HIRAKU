// SPDX-License-Identifier: GPL-3.0-or-later
#include "lights.h"

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "../shell/core.h"
#include "../shell/cabinet.h"
#include "../shell/net.h"
#include "hidout.h"

namespace hk::host {

namespace {

std::vector<cab::Lamp> g_defs;
std::vector<std::vector<hidout::Spec>> g_outputs;   // per lamp
bool g_any_hid = false;
std::string g_hello, g_sock_path;
int g_listen = -1;
std::vector<int> g_clients;

// The emulation thread hands lamp states to the lights thread, which does all
// the (possibly slow) device and socket I/O.
std::thread g_thread;
std::mutex g_mu;
std::condition_variable g_cv;
std::vector<uint8_t> g_pending;
bool g_have_pending = false, g_stop = false, g_running = false;
uint64_t g_seen_changes = ~0ull;

bool send_all(int fd, const std::string &s) {
    return net::send_now(fd, s.data(), s.size()) == (ssize_t)s.size();
}

void broadcast(const std::string &s) {
    for (size_t k = 0; k < g_clients.size();)
        if (send_all(g_clients[k], s)) k++;
        else {   // gone, or too slow to keep up: drop it
            net::close(g_clients[k]);
            g_clients.erase(g_clients.begin() + (long)k);
        }
}

void thread_main() {
    hidout::Writer hid;
    std::vector<uint8_t> last(g_defs.size(), 0);
    auto drive = [&](size_t k, bool on) {
        for (auto &s : g_outputs[k]) hid.set(s, on);
    };
    for (size_t k = 0; k < g_defs.size(); k++) drive(k, false);
    hid.flush();
    for (;;) {
        std::vector<uint8_t> state;
        bool stop;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait_for(lk, std::chrono::milliseconds(50), [] { return g_stop || g_have_pending; });
            if (g_have_pending) state.swap(g_pending);
            g_have_pending = false;
            stop = g_stop;
        }
        if (g_listen >= 0)
            for (int fd; (fd = net::accept(g_listen)) >= 0;) {
                net::set_nonblocking(fd);
                std::string all = g_hello;
                for (size_t k = 0; k < g_defs.size(); k++) all += g_defs[k].name + "=" + (last[k] ? "1\n" : "0\n");
                if (send_all(fd, all)) g_clients.push_back(fd);
                else net::close(fd);
            }
        if (state.size() == last.size()) {
            std::string lines;
            for (size_t k = 0; k < state.size(); k++)
                if (state[k] != last[k]) {
                    drive(k, state[k]);
                    lines += g_defs[k].name + "=" + (state[k] ? "1\n" : "0\n");
                }
            last = state;
            if (!lines.empty()) broadcast(lines);
            hid.flush();
        }
        if (stop) break;
    }
    for (size_t k = 0; k < g_defs.size(); k++) drive(k, false);   // a quit game leaves no lamps on
    hid.flush();
    hid.close();
}

}  // namespace

bool lights_init(const std::string &game, const std::string &family, const std::string &file, const std::string &socket_path) {
    g_defs = cab::lamps(family);
    g_outputs.assign(g_defs.size(), {});
    if (!file.empty()) {
        std::ifstream in(file);
        if (!in) fprintf(stderr, "lights: cannot read %s\n", file.c_str());
        std::string text((std::istreambuf_iterator<char>(in)), {});
        for (auto &en : cab::parse_list_file(text)) {
            size_t k = 0;
            while (k < g_defs.size() && g_defs[k].name != en.name) k++;
            if (k == g_defs.size()) {
                fprintf(stderr, "%s:%d: this game has no lamp '%s'\n", file.c_str(), en.line, en.name.c_str());
                continue;
            }
            g_outputs[k].clear();
            for (auto &o : en.items) {
                hidout::Spec s;
                if (hidout::parse_spec(o, s)) {
                    g_outputs[k].push_back(s);
                    g_any_hid = true;
                } else {
                    fprintf(stderr, "%s:%d: cannot parse output '%s'\n", file.c_str(), en.line, o.c_str());
                }
            }
        }
    }
    const size_t colon = socket_path.rfind(':');
    if (!socket_path.empty() && colon != std::string::npos && socket_path.find_first_of("/\\") == std::string::npos) {
        // HOST:PORT, TCP (Windows' launcher: it has no Unix sockets to read)
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)atoi(socket_path.c_str() + colon + 1));
        g_listen = net::socket(AF_INET, SOCK_STREAM, 0);
        if (g_listen >= 0) net::set_int(g_listen, SOL_SOCKET, SO_REUSEADDR, 1);
        if (g_listen < 0 || inet_pton(AF_INET, socket_path.substr(0, colon).c_str(), &a.sin_addr) != 1 ||
            !net::set_nonblocking(g_listen) || bind(g_listen, (sockaddr *)&a, sizeof a) != 0 || listen(g_listen, 8) != 0) {
            fprintf(stderr, "lights: cannot listen on %s: %s\n", socket_path.c_str(), net::error().c_str());
            if (g_listen >= 0) net::close(g_listen);
            g_listen = -1;
        } else {
            printf("lights: socket %s\n", socket_path.c_str());
        }
    } else if (!socket_path.empty()) {
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        if (socket_path.size() >= sizeof a.sun_path) {
            fprintf(stderr, "lights: socket path too long: %s\n", socket_path.c_str());
        } else {
            strcpy(a.sun_path, socket_path.c_str());
            net::remove_path(socket_path);   // a previous run's
            g_listen = net::socket(AF_UNIX, SOCK_STREAM, 0);
            if (g_listen < 0 || !net::set_nonblocking(g_listen) || bind(g_listen, (sockaddr *)&a, sizeof a) != 0 ||
                listen(g_listen, 8) != 0) {
                fprintf(stderr, "lights: cannot listen on %s: %s\n", socket_path.c_str(), net::error().c_str());
                if (g_listen >= 0) net::close(g_listen);
                g_listen = -1;
            } else {
                g_sock_path = socket_path;
                printf("lights: socket %s\n", socket_path.c_str());
            }
        }
    }
    if (!g_any_hid && g_listen < 0) return false;
    g_hello = "hiraku-lights 1 " + game + " " + family + "\n";
    g_stop = false;
    g_running = true;
    g_thread = std::thread(thread_main);
    return true;
}

void lights_frame() {
    if (!g_running) return;
    uint64_t c = core().lamp_changes();
    if (c == g_seen_changes) return;
    g_seen_changes = c;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_pending = core().lamp_state();
        g_have_pending = true;
    }
    g_cv.notify_one();
}

void lights_close() {
    if (!g_running) return;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_stop = true;
    }
    g_cv.notify_one();
    g_thread.join();
    g_running = false;
    for (int fd : g_clients) net::close(fd);
    g_clients.clear();
    if (g_listen >= 0) {
        net::close(g_listen);
        net::remove_path(g_sock_path);
        g_listen = -1;
    }
}

}  // namespace hk::host
