// SPDX-License-Identifier: GPL-3.0-or-later
// Windows: a pop'n music cabinet's I/O board through hiraku-ezusb.exe (runtime/host/popnio.h,
// ezusb/link.h) and a pretend 32-bit ezusb.dll (fake_ezusb.c): the helper starts it as a game does,
// its button 3, TEST and coin become a pretend pop'n machine's inputs, the machine's lamps reach
// its usbLamp, and at the end the lamps go off and usbEnd is called.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
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

// A pretend pop'n music machine (Firebeat's lamps)
struct Machine : hk::Core {
    std::string fam = "popn";
    std::vector<uint8_t> lamps;
    std::vector<std::string> inputs;
    Machine() {
        for (auto &l : hk::cab::lamps(fam)) lamps.push_back(l.name == "Button Lamp 9" || l.name == "Top Lamp 3" || l.name == "Side Lamp 2");
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

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test_ezusb_helper FAKE_EZUSB_DLL\n");
        return 2;
    }
    char tmp[MAX_PATH];
    GetTempPathA(sizeof tmp, tmp);
    const std::string report = std::string(tmp) + "hiraku-fake-ezusb.txt";
    DeleteFileA(report.c_str());
    SetEnvironmentVariableA("HK_FAKE_EZUSB_OUT", report.c_str());   // the helper and its DLL inherit it

    CHECK(hk::host::popnio_option("--popn-io", [] { return std::string("auto"); }));
    CHECK(hk::host::popnio_option("--popn-io-dll", [&] { return std::string(argv[1]); }));
    hk::host::popnio_start();
    Machine m;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < until && !(has(m.inputs, "P1 Button 3=0") && has(m.inputs, "Coin 1=0"))) {
        hk::host::popnio_frame(m);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    hk::host::popnio_stop();

    CHECK(has(m.inputs, "P1 Button 3=1") && has(m.inputs, "P1 Button 3=0"));
    CHECK(has(m.inputs, "Test=1") && has(m.inputs, "Test=0"));
    CHECK(has(m.inputs, "Coin 1=1") && has(m.inputs, "Coin 1=0"));
    CHECK(!has(m.inputs, "Service=1"));
    for (auto &s : m.inputs) printf("input %s\n", s.c_str());

    // what the DLL was given: usbStart(0), usbCoinMode(3), Firebeat's Button Lamp 9, Top Lamp 3 and
    // Side Lamp 2, then the lamps off
    char line[256] = {};
    FILE *f = fopen(report.c_str(), "r");
    CHECK(f != nullptr);
    if (f) {
        if (!fgets(line, sizeof line, f)) line[0] = 0;
        fclose(f);
    }
    char want[128];
    snprintf(want, sizeof want, "start=0 firm_calls=4 coin_mode=3 lit=%08x last=00000000\n", 1u << 31 | 1u << 2 | 1u << 9);
    CHECK(std::string(line) == want);
    if (std::string(line) != want) fprintf(stderr, "the DLL saw: %s", line);
    DeleteFileA(report.c_str());
    if (failures) fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
