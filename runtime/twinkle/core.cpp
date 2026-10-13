// SPDX-License-Identifier: GPL-3.0-or-later
// The Twinkle core (runtime/shell/core.h): the board in runtime/twinkle/board.cpp. The machine
// comes from dumps/<game>/machine.txt (tools/ingest.py): the ROM files of MAME's set (BIOS, SPU
// program, dongle), the program CD, the Video CD and the SPU board's hard disk.
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <string>

#include "shell/core.h"
#include "twinkle/board.h"

namespace hk {
namespace {

std::map<std::string, std::string> read_kv(const std::string &path) {
    std::map<std::string, std::string> kv;
    std::ifstream f(path);
    for (std::string line; std::getline(f, line);) {
        auto eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

class TwinkleCore final : public Core {
public:
    bool option(const std::string &name, const std::function<std::string()> &value) override {
        if (name == "--rtc") { rtc_ = std::stoll(value()); return true; }
        if (name == "--mame-mix") { mame_mix_ = true; return true; }
        return false;
    }
    std::string options_help() const override {
        return "  --rtc SECONDS     the RTC's time at power-on (default: now), seconds since 1970\n"
               "  --mame-mix        sound at MAME's level (it can clip on loud stretches; default: 0.8 of it)\n";
    }

    bool init(const Options &o) override {
        auto machine = read_kv(o.root + "/dumps/" + o.game + "/machine.txt");
        if (machine.empty() || machine["core"] != "twinkle") {
            fprintf(stderr, "no Twinkle dumps/%s/machine.txt - run tools/ingest.py with the set and its CHDs first\n",
                    o.game.c_str());
            return false;
        }
        twinkle::Config cfg;
        cfg.game = o.game;
        cfg.family = machine["family"];
        cfg.rom_dir = o.root + "/dumps/" + o.game + "/rom";
        auto get = [&](const char *k, std::string &dst) { if (!machine[k].empty()) dst = machine[k]; };
        get("bios", cfg.bios);
        get("spu", cfg.spu);
        get("dongle", cfg.dongle);
        get("dongle_type", cfg.dongle_type);
        get("dvd_port", cfg.dvd_port);
        cfg.cd = o.data_path(machine["cd"]);
        cfg.video_cd = o.data_path(machine["video_cd"]);
        cfg.hdd = o.data_path(machine["hdd"]);
        unsigned c = 0, h = 0, s = 0;
        if (sscanf(machine["hdd_chs"].c_str(), "%u,%u,%u", &c, &h, &s) != 3) {
            fprintf(stderr, "dumps/%s/machine.txt has no hdd_chs (run tools/ingest.py again)\n", o.game.c_str());
            return false;
        }
        cfg.hdd_cylinders = c;
        cfg.hdd_heads = h;
        cfg.hdd_sectors = s;
        cfg.nvram_dir = o.nvram_dir;
        cfg.rtc_time = rtc_ >= 0 ? rtc_ : (int64_t)time(nullptr);
        if (o.trace) cfg.trace_dir = o.out;
        if (mame_mix_) cfg.mix_gain = 1.0f;
        if (!twinkle::init(cfg)) return false;
        printf("hiraku %s: Twinkle, MAME's PS1 and 68000 interpreted\n", o.game.c_str());
        return true;
    }

    const std::string &family() const override { return twinkle::family(); }
    void set_frame_hook(FrameHook h) override { twinkle::set_frame_hook(std::move(h)); }
    void set_audio_sink(AudioSink s) override { twinkle::set_audio_sink(std::move(s)); }
    void set_on_fatal(std::function<void()> f) override { on_fatal_ = std::move(f); }
    [[noreturn]] void run() override { twinkle::run(); }
    void shutdown() override { twinkle::save_nvram(); }
    void report() override {
        printf("frames %llu, virtual time %.3f s, pc %08x, 68000 pc %06x, %llu lamp changes, display [%s]\n",
               (unsigned long long)twinkle::frame_count(), twinkle::seconds(), twinkle::pc(), twinkle::audio_pc(),
               (unsigned long long)twinkle::lamp_changes(), twinkle::display_text().c_str());
        if (std::string s = twinkle::sound_report(); !s.empty()) printf("%s\n", s.c_str());
    }
    void debug_line(uint64_t f) override {
        printf("f%llu pc %08x 68000 %06x\n", (unsigned long long)f, twinkle::pc(), twinkle::audio_pc());
    }

    double seconds() const override { return twinkle::seconds(); }
    const uint8_t *mem(uint32_t addr, uint32_t len) const override { return twinkle::mem(addr, len); }
    uint64_t frame_count() const override { return twinkle::frame_count(); }
    bool set_input(const std::string &name, int value) override { return twinkle::set_input(name, value); }
    int script_button_delay() const override { return 1; }   // MAME latches digital inputs a frame late
    void turntable_add(int player, int delta) override { twinkle::turntable_add(player, delta); }
    std::string display_text() const override { return twinkle::display_text(); }
    const std::vector<uint8_t> &lamp_state() const override { return twinkle::lamp_state(); }
    uint64_t lamp_changes() const override { return twinkle::lamp_changes(); }

private:
    std::function<void()> on_fatal_;
    long long rtc_ = -1;
    bool mame_mix_ = false;
};

}  // namespace

Core &core() {
    static TwinkleCore c;
    return c;
}

}  // namespace hk
