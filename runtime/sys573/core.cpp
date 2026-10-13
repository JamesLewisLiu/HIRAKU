// SPDX-License-Identifier: GPL-3.0-or-later
// The System 573 core (runtime/shell/core.h): the board in runtime/sys573/board.cpp.
// The machine comes from dumps/<game>/machine.txt (tools/ingest.py): the family,
// the ROM files of MAME's set (BIOS, H8 response, cassette and I/O board chips),
// the CD image and the PC Card in slot 2.
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <string>

#include "shell/core.h"
#include "sys573/board.h"
#include "sys573/mips.h"

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

class Sys573Core final : public Core {
public:
    bool option(const std::string &name, const std::function<std::string()> &value) override {
        if (name == "--rtc") { rtc_ = std::stoll(value()); return true; }
        if (name == "--mame-mix") { mame_mix_ = true; return true; }
        if (name == "--cassette") { cassette_slot_ = value(); return true; }
        return false;
    }
    std::string options_help() const override {
        return "  --rtc SECONDS     the timekeeper's time at power-on (default: now), seconds since 1970\n"
               "  --mame-mix        sound at MAME's level: the SPU and CD audio summed at full gain, clipping\n"
               "                    where they add up past full scale (default: half, which never clips)\n"
               "  --cassette install  boot with the install cassette (first boot of games that install from CD\n"
               "                    with one, ddr3mj: when it says so, power off and boot again without)\n";
    }

    bool init(const Options &o) override {
        auto machine = read_kv(o.root + "/dumps/" + o.game + "/machine.txt");
        if (machine.empty() || machine["core"] != "sys573") {
            fprintf(stderr, "no System 573 dumps/%s/machine.txt - run tools/ingest.py --romset %s.7z --cd <disc>.cue first\n",
                    o.game.c_str(), o.game.c_str());
            return false;
        }
        k573::Config cfg;
        cfg.game = o.game;
        cfg.family = machine["family"];
        cfg.rom_dir = o.root + "/dumps/" + o.game + "/rom";
        auto get = [&](const char *k, std::string &dst) { if (!machine[k].empty()) dst = machine[k]; };
        get("bios", cfg.bios);
        get("h8", cfg.h8);
        get("cassette_eeprom", cfg.cassette_eeprom);
        get("cassette_id", cfg.cassette_id);
        get("install_eeprom", cfg.install_eeprom);
        get("install_id", cfg.install_id);
        get("digital_id", cfg.digital_id);
        get("io_board", cfg.io_board);
        get("pccard2", cfg.pccard2);
        get("cassette", cfg.cassette);
        cfg.memory_card_reader = machine["memory_card_reader"] == "1";
        cfg.cd = o.data_path(machine["cd"]);
        cfg.nvram_dir = o.nvram_dir;
        cfg.rtc_time = rtc_ >= 0 ? rtc_ : (int64_t)time(nullptr);
        if (mame_mix_) cfg.mix_gain = 1.0f;
        if (!cassette_slot_.empty()) {
            if (cassette_slot_ != "game" && cassette_slot_ != "install") {
                fprintf(stderr, "--cassette: game or install\n");
                return false;
            }
            if (cassette_slot_ == "install" && cfg.install_eeprom.empty()) {
                fprintf(stderr, "%s has no install cassette\n", o.game.c_str());
                return false;
            }
            cfg.cassette_slot = cassette_slot_;
        }
        if (!k573::init(cfg)) return false;
        printf("hiraku %s: System 573, MAME's PS1 interpreted\n", o.game.c_str());
        return true;
    }

    const std::string &family() const override { return k573::family(); }
    void set_frame_hook(FrameHook h) override { k573::set_frame_hook(std::move(h)); }
    void set_audio_sink(AudioSink s) override { k573::set_audio_sink(std::move(s)); }
    void set_on_fatal(std::function<void()> f) override { on_fatal_ = std::move(f); }
    [[noreturn]] void run() override { k573::run(); }
    void shutdown() override { k573::save_nvram(); }
    void report() override {
        printf("frames %llu, virtual time %.3f s, pc %08x, %llu lamp changes\n", (unsigned long long)k573::frame_count(),
               k573::seconds(), k573::pc(), (unsigned long long)k573::lamp_changes());
        if (std::string s = k573::sound_report(); !s.empty()) printf("%s\n", s.c_str());
        if (std::string s = k573::mips::report(); !s.empty()) printf("%s\n", s.c_str());
    }
    void debug_line(uint64_t f) override { printf("f%llu pc %08x\n", (unsigned long long)f, k573::pc()); }

    double seconds() const override { return k573::seconds(); }
    const uint8_t *mem(uint32_t addr, uint32_t len) const override { return k573::mem(addr, len); }
    uint64_t frame_count() const override { return k573::frame_count(); }
    bool set_input(const std::string &name, int value) override { return k573::set_input(name, value); }
    int script_button_delay() const override { return 1; }   // MAME latches digital inputs a frame late
    void turntable_add(int, int) override {}
    const std::vector<uint8_t> &lamp_state() const override { return k573::lamp_state(); }
    uint64_t lamp_changes() const override { return k573::lamp_changes(); }

private:
    std::function<void()> on_fatal_;
    long long rtc_ = -1;
    bool mame_mix_ = false;
    std::string cassette_slot_;
};

}  // namespace

Core &core() {
    static Sys573Core c;
    return c;
}

}  // namespace hk
