// SPDX-License-Identifier: GPL-3.0-or-later
// beatmania IIDX's BIO2 board (runtime/host/bio2.h) against a pretend BI2A on a pseudo-terminal,
// which answers as spice2x's games/iidx/bi2a.cpp does, driving a pretend IIDX machine: the board's
// keys, turntable, slider and coin become the machine's inputs, and the machine's lamps and ticker
// go out in the board's bytes.
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "host/bio2.h"
#include "shell/cabinet.h"
#include "shell/core.h"

static int failures = 0;
#define CHECK(c)                                                            \
    do {                                                                    \
        if (!(c)) {                                                         \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static std::atomic<int> g_polls{0};
static uint8_t g_lamps_seen[0x30];
static std::atomic<bool> g_seen{false};

static void pretend_board(int fd, std::atomic<bool> &stop) {
    std::vector<uint8_t> frame;
    size_t want = 0;
    bool escape = false, framed = false;
    auto reply = [&](const std::vector<uint8_t> &head, const std::vector<uint8_t> &payload) {
        std::vector<uint8_t> body = head;
        body.push_back((uint8_t)payload.size());
        body.insert(body.end(), payload.begin(), payload.end());
        uint8_t sum = 0;
        for (uint8_t b : body) sum += b;
        body.push_back(sum);
        std::vector<uint8_t> out = {0xaa};
        for (uint8_t b : body)
            if (b == 0xaa || b == 0xff) out.push_back(0xff), out.push_back((uint8_t)~b);
            else out.push_back(b);
        if (write(fd, out.data(), out.size()) != (ssize_t)out.size()) stop = true;
    };
    while (!stop) {
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, 50) <= 0) continue;
        uint8_t in[2048];
        ssize_t n = read(fd, in, sizeof in);
        if (n <= 0) continue;
        bool hello = false;
        for (ssize_t i = 0; i < n; i++) {
            uint8_t b = in[i];
            if (b == 0xaa && !escape) {
                hello = !framed;
                frame.clear();
                want = 6;
                continue;
            }
            if (!want) continue;
            if (b == 0xff && !escape) { escape = true; continue; }
            if (escape) b = (uint8_t)~b;
            escape = false;
            frame.push_back(b);
            if (frame.size() == 5) want = 6 + frame[4];
            if (frame.size() < want) continue;
            want = 0;
            framed = true;
            hello = false;
            const uint16_t cmd = (uint16_t)(frame[1] << 8 | frame[2]);
            const std::vector<uint8_t> req(frame.begin() + 5, frame.end() - 1);
            const std::vector<uint8_t> head = {uint8_t(frame[0] ? frame[0] | 0x80 : 0), frame[1], frame[2], frame[3]};
            if (cmd == 0x0001) {
                reply(head, {1});
            } else if (cmd == 0x0002) {
                std::vector<uint8_t> v(44, 0);
                v[0] = 3, v[5] = 4, v[6] = 2, v[7] = 0;
                memcpy(&v[8], "BI2A", 4);
                reply(head, v);
            } else if (cmd == 0x0152 && req.size() == 0x30) {
                memcpy(g_lamps_seen, req.data(), 0x30);
                g_seen = true;
                // from poll 20: P1 key 3 down, the P1 turntable 5 on, slider 1 at 9, a coin
                const int polls = ++g_polls;
                std::vector<uint8_t> s(0x2e, 0);
                s[16] = 100;
                if (polls >= 20) {
                    s[183 / 8] |= 1 << (183 % 8);
                    s[16] = 105;
                    s[0] |= 9 << 4;
                    s[8] = 1;
                }
                reply(head, s);
            } else {
                reply(head, {0});   // start, clear
            }
        }
        if (hello) {
            const uint8_t aa = 0xaa;
            if (write(fd, &aa, 1) != 1) stop = true;
        }
    }
}

// A pretend IIDX machine: its lamps as cab::lamps("iidx") lists them, what it is given
struct Machine : hk::Core {
    std::string fam = "iidx";
    std::vector<uint8_t> lamps;
    std::vector<std::string> inputs;
    int turned[2] = {0, 0};
    Machine() {
        for (auto &l : hk::cab::lamps(fam)) lamps.push_back(l.name == "P1 Key Lamp 2" || l.name == "Spotlight 6" || l.name == "Neon");
    }
    bool init(const Options &) override { return true; }
    const std::string &family() const override { return fam; }
    void set_frame_hook(FrameHook) override {}
    void set_audio_sink(AudioSink) override {}
    void set_on_fatal(std::function<void()>) override {}
    [[noreturn]] void run() override { abort(); }
    void shutdown() override {}
    void report() override {}
    double seconds() const override { return 0; }
    const uint8_t *mem(uint32_t, uint32_t) const override { return nullptr; }
    uint64_t frame_count() const override { return 0; }
    bool set_input(const std::string &name, int value) override {
        inputs.push_back(name + "=" + std::to_string(value));
        return true;
    }
    void turntable_add(int p, int d) override { turned[p] += d; }
    const std::vector<uint8_t> &lamp_state() const override { return lamps; }
    uint64_t lamp_changes() const override { return 0; }
    std::string display_text() const override { return "HELLO"; }
};

static bool has(const std::vector<std::string> &v, const std::string &s) {
    for (auto &x : v)
        if (x == s) return true;
    return false;
}

int main() {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) || unlockpt(master)) {
        perror("posix_openpt");
        return 1;
    }
    const std::string slave = ptsname(master);
    std::atomic<bool> stop{false};
    std::thread fake(pretend_board, master, std::ref(stop));

    Machine m;
    CHECK(hk::host::bio2_option("--bio2", [&] { return slave; }));
    hk::host::bio2_start();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < until && g_polls < 40) {
        hk::host::bio2_frame(m);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    for (int k = 0; k < 8; k++) {   // the coin's press and release
        hk::host::bio2_frame(m);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    hk::host::bio2_stop();
    stop = true;
    fake.join();
    close(master);

    CHECK(g_seen);
    CHECK(g_lamps_seen[0x07 + 1] == 0xff);   // P1 key 2
    CHECK(g_lamps_seen[0x07] == 0);
    CHECK(g_lamps_seen[0x26] == 0xff);       // spotlight 6
    CHECK(g_lamps_seen[0x24] == 0xff);       // the neon
    CHECK(memcmp(&g_lamps_seen[0x17], "HELLO    ", 9) == 0);
    CHECK(has(m.inputs, "P1 Button 3=1"));
    CHECK(has(m.inputs, "Volume 1=9"));
    CHECK(has(m.inputs, "Coin 1=1") && has(m.inputs, "Coin 1=0"));
    CHECK(!has(m.inputs, "P1 Button 1=1"));
    CHECK(m.turned[0] == 20 && m.turned[1] == 0);   // 5 of 256 a turn, as 20 of 1024
    for (auto &s : m.inputs) printf("input %s\n", s.c_str());
    printf("turntable 1: %d\n", m.turned[0]);
    if (failures) fprintf(stderr, "%d failed\n", failures);
    else printf("bio2: all passed\n");
    return failures ? 1 : 0;
}
