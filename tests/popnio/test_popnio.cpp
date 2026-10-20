// SPDX-License-Identifier: GPL-3.0-or-later
// A pop'n music cabinet's USB I/O board (runtime/host/popnio.h) against a pretend board: bare at
// first, it takes a made-up firmware from a made-up ezusb.dll, comes back saying "KONAMI", and then
// answers as the real one does (ezusb.dll's dialogue): its buttons and coin become a pretend pop'n
// machine's inputs, the machine's lamps go out in its word. Nothing sent may be one of the board's
// EEPROM or security commands.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "host/popnio.h"
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

using namespace hk::host::popnio;

// What the pretend board saw (written by the board's thread)
struct Seen {
    std::mutex m;
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x4000, 0);
    std::vector<std::string> log;   // "cpucs 1", "load", "cpucs 0", "out 09 03", "bulk in", ...
    std::array<uint8_t, 64> last_out{};
    int outs = 0, bad = 0;
};
static Seen g_seen;

class PretendBoard : public Usb {
public:
    std::vector<uint8_t> want;   // the firmware's bytes, flattened
    std::atomic<int> polls{0};
    std::atomic<uint32_t> buttons{0};
    std::atomic<uint16_t> coins{0};

    std::vector<Device> scan() override {
        std::lock_guard<std::mutex> l(m_mutex);
        Device mouse{"1-3", 0x046d, 0xc077, false, "Logitech"};
        Device board{"1-2", 0x04b4, 0x8613, true, ""};
        if (m_running && std::chrono::steady_clock::now() > m_back) board.bare = false, board.name = "KONAMI";
        else if (m_running) return {mouse};   // gone while it starts its firmware
        return {mouse, board};
    }
    bool open(const std::string &port) override {
        std::lock_guard<std::mutex> l(m_mutex);
        m_open = port == "1-2";
        return m_open;
    }
    void close() override { m_open = false; }
    bool control_out(uint8_t request, uint16_t value, uint16_t, const uint8_t *data, uint16_t n) override {
        std::lock_guard<std::mutex> l(g_seen.m);
        if (!allowed_control(request, value, data, n)) g_seen.bad++;
        if (value == 0xe600) {
            g_seen.log.push_back(std::string("cpucs ") + (data[0] ? "1" : "0"));
            if (!data[0]) {
                std::lock_guard<std::mutex> k(m_mutex);
                m_running = g_seen.ram == want;   // the firmware starts if it's all there
                m_back = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
            }
        } else {
            memcpy(&g_seen.ram[value], data, n);
            if (g_seen.log.empty() || g_seen.log.back() != "load") g_seen.log.push_back("load");
        }
        return true;
    }
    int transfer(uint8_t ep, uint8_t *data, int n, int) override {
        if (!m_open || !m_running) return XFER_ERROR;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        if (ep == 0x81) {   // the status: 03 1d 85, a sequence number, the coins, the controls active low
            memset(data, 0, n);
            data[0] = 0x03, data[1] = 0x1d, data[2] = 0x85, data[3] = (uint8_t)m_seq++, data[7] = 0xfd;
            data[5] = (uint8_t)(coins >> 8), data[6] = (uint8_t)coins;
            const uint32_t pad = ~(uint32_t)buttons;
            for (int k = 0; k < 4; k++) data[8 + k] = (uint8_t)(pad >> 8 * k);
            polls++;
            return n;
        }
        std::lock_guard<std::mutex> l(g_seen.m);
        if (ep == 0x01) {
            if (!allowed_out(data, n)) g_seen.bad++;
            if (data[2] || data[3]) {
                char s[16];
                snprintf(s, sizeof s, "out %02x %02x", data[2], data[3]);
                g_seen.log.push_back(s);
            } else if (g_seen.outs == 0) {
                g_seen.log.push_back("out");
            }
            memcpy(g_seen.last_out.data(), data, 64);
            g_seen.outs++;
            return n;
        }
        if (ep == 0x86) {
            g_seen.log.push_back("bulk in");
            return XFER_TIMEOUT;   // whether the firmware answers these isn't known: it mustn't matter
        }
        if (ep == 0x02) {
            if (!allowed_bulk(data, n)) g_seen.bad++;
            g_seen.log.push_back("bulk out");
            return n;
        }
        return XFER_ERROR;
    }

private:
    std::mutex m_mutex;
    bool m_open = false, m_running = false;
    std::chrono::steady_clock::time_point m_back;
    int m_seq = 0;
};

// A pretend pop'n music machine (Firebeat's lamps)
struct Machine : hk::Core {
    std::string fam = "popn";
    std::vector<uint8_t> lamps;
    std::vector<std::string> inputs;
    Machine() {
        for (auto &l : hk::cab::lamps(fam))
            lamps.push_back(l.name == "Button Lamp 1" || l.name == "Button Lamp 5" || l.name == "Top Lamp 2" || l.name == "Side Lamp 3");
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
    void turntable_add(int, int) override {}
    const std::vector<uint8_t> &lamp_state() const override { return lamps; }
    uint64_t lamp_changes() const override { return 0; }
};

static bool has(const std::vector<std::string> &v, const std::string &s) {
    for (auto &x : v)
        if (x == s) return true;
    return false;
}

// A made-up ezusb.dll: noise, and in it a made-up firmware as Cypress's 70-byte records (not in
// address order, some following on), ended by a type 3 record
static std::vector<uint8_t> made_up_dll(std::vector<uint8_t> &flat) {
    std::mt19937 rng(7);
    std::vector<uint8_t> dll(60000);
    for (auto &b : dll) b = (uint8_t)rng();
    flat.assign(0x4000, 0);
    std::vector<std::pair<uint16_t, int>> recs;   // address, length
    for (uint16_t a = 0x0000; a < 0x1800; a += 32) recs.push_back({a, 32});
    recs.push_back({0x1800, 7});
    recs.push_back({0x0043, 3});   // a short one out of order, over earlier bytes (as the real one has)
    size_t o = 12345;
    for (auto [addr, len] : recs) {
        uint8_t *r = &dll[o];
        memset(r, 0, 70);
        r[0] = (uint8_t)len, r[2] = (uint8_t)addr, r[3] = (uint8_t)(addr >> 8);
        for (int k = 0; k < len; k++) r[5 + k] = flat[addr + k] = (uint8_t)rng();
        o += 70;
    }
    memset(&dll[o], 0, 70);
    dll[o] = 4, dll[o + 4] = 3;
    return dll;
}

int main() {
    // the lamps' bits (PopnForwarder's and bemanitools' layout)
    CHECK(lamp_bits("Button Lamp 1") == 0x00f00000u);
    CHECK(lamp_bits("Button Lamp 9") == 0x80000000u);
    CHECK(lamp_bits("Top Lamp 1") == 0x01u && lamp_bits("Top Lamp 5") == 0x10u);
    CHECK(lamp_bits("Side Lamp 1") == 0x100u && lamp_bits("Side Lamp 4") == 0x800u);
    CHECK(lamp_bits("P1 Button Lamp 1") == 0 && lamp_bits("Status LED 1") == 0);
    for (const char *f : {"popn", "pypopn", "djpopn"}) {   // every pop'n cabinet's lamps are on the board
        int on = 0;
        for (auto &l : hk::cab::lamps(f)) on += lamp_bits(l.name) != 0;
        CHECK(on == 18);
    }
    // the safety checks
    auto poll = out_packet(0xffffffff);
    CHECK(allowed_out(poll.data(), 64) && poll[9] == 0x0f && poll[8] == 0x1f);
    for (auto [node, cmd] : std::vector<std::pair<int, int>>{{2, 3}, {1, 3}, {0xfe, 0}, {2, 2}, {0x0c, 5}, {0x40, 3}}) {
        auto p = out_packet(0, (uint8_t)node, (uint8_t)cmd);
        CHECK(!allowed_out(p.data(), 64));
    }
    const uint8_t one = 1, two = 2;
    CHECK(allowed_control(0xa0, 0xe600, &one, 1) && !allowed_control(0xa0, 0xe600, &two, 1));
    CHECK(!allowed_control(0xa2, 0x0000, &one, 1) && !allowed_control(0xa9, 0x0000, &one, 1) && !allowed_control(0xa3, 0x0000, &one, 1));
    CHECK(!allowed_control(0xa0, 0x3fff, poll.data(), 2) && allowed_control(0xa0, 0x3ffe, poll.data(), 2));

    // the firmware found by its shape, in ezusb.dll's chunks
    std::vector<uint8_t> flat;
    const std::vector<uint8_t> dll = made_up_dll(flat);
    const auto image = find_firmware(dll);
    size_t total = 0, biggest = 0;
    std::vector<uint8_t> ram(0x4000, 0);
    for (auto &c : image) {
        total += c.data.size(), biggest = std::max(biggest, c.data.size());
        memcpy(&ram[c.addr], c.data.data(), c.data.size());
    }
    CHECK(total == 0x1800 + 7 + 3);
    CHECK(biggest <= 0x3e0 + 64 && image.size() >= 7);
    CHECK(ram == flat);
    CHECK(find_firmware(std::vector<uint8_t>(5000, 0)).empty());

    // the whole of it: a bare board, its firmware, the start-up, polling
    const std::string dllpath = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") + "/hiraku-test-ezusb.dll";
    std::ofstream(dllpath, std::ios::binary).write((const char *)dll.data(), (std::streamsize)dll.size());
    auto board = std::make_unique<PretendBoard>();
    PretendBoard *b = board.get();
    b->want = flat;
    use_usb(std::move(board));
    CHECK(hk::host::popnio_option("--popn-io", [] { return std::string("auto"); }));
    CHECK(hk::host::popnio_option("--popn-io-firmware", [&] { return dllpath; }));
    hk::host::popnio_start();

    Machine m;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < until && b->polls < 20) {
        hk::host::popnio_frame(m);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    b->buttons = 1u << 10 | 1u << 7;   // button 3, TEST
    b->coins = 1;
    for (int k = 0; k < 10; k++) {
        hk::host::popnio_frame(m);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    b->buttons = 0;
    for (int k = 0; k < 4; k++) {
        hk::host::popnio_frame(m);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    hk::host::popnio_stop();
    remove(dllpath.c_str());

    std::string log;
    {
        std::lock_guard<std::mutex> l(g_seen.m);
        for (auto &s : g_seen.log) log += s + ", ";
        CHECK(g_seen.bad == 0);
        CHECK(g_seen.ram == flat);
        // Firebeat's Button Lamp 1 and 5, Top Lamp 2, Side Lamp 3
        const uint32_t word = g_seen.last_out[8] | g_seen.last_out[9] << 8 | g_seen.last_out[10] << 16 | (uint32_t)g_seen.last_out[11] << 24;
        CHECK(word == (0x00f00000u | 1u << 27 | 1u << 1 | 1u << 10));
        if (word != (0x00f00000u | 1u << 27 | 1u << 1 | 1u << 10)) fprintf(stderr, "lamps %08x\n", word);
    }
    const std::string want = "cpucs 1, load, cpucs 0, out, bulk in, bulk in, bulk out, bulk out, out 09 03, ";
    CHECK(log == want);
    if (log != want) fprintf(stderr, "dialogue: %s\n", log.c_str());
    CHECK(has(m.inputs, "P1 Button 3=1") && has(m.inputs, "P1 Button 3=0"));
    CHECK(has(m.inputs, "Test=1") && has(m.inputs, "Test=0"));
    CHECK(has(m.inputs, "Coin 1=1") && has(m.inputs, "Coin 1=0"));
    CHECK(!has(m.inputs, "P1 Button 1=1") && !has(m.inputs, "Service=1"));
    for (auto &s : m.inputs) printf("input %s\n", s.c_str());
    if (failures) fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
