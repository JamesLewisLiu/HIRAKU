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
        if (name == "--fine-sound") { fine_sound_ = true; return true; }
        if (name == "--cassette") { cassette_slot_ = value(); return true; }
        if (name == "--npu") { npu_ = true; return true; }
        if (name == "--session-host") { session_host_ = value(); return true; }
        if (name == "--session-join") { session_join_ = value(); return true; }
        if (name == "--session-quantum") { session_quantum_ = std::stoi(value()); return true; }
        if (name == "--cd") { cd_ = value(); return true; }
        if (name == "--eamuse") { eamuse_ = value(); return true; }
        return false;
    }
    std::string options_help() const override {
        return "  --rtc SECONDS     the timekeeper's time at power-on (default: now), seconds since 1970\n"
               "  --mame-mix        sound at MAME's level: the SPU and CD audio summed at full gain, clipping\n"
               "                    where they add up past full scale (default: half, which never clips)\n"
               "  --fine-sound      hand the sound to the host every millisecond, not at MAME's 50 Hz stream\n"
               "                    updates: lower, steadier latency (the MP3 decoded ahead of its update)\n"
               "  --cassette install  boot with the install cassette (first boot of games that install from CD\n"
               "                    with one, ddr3mj: when it says so, power off and boot again without)\n"
               "  --npu             fit the network PCB unit (e-AMUSEMENT GuitarFreaks and DrumMania): its hard\n"
               "                    disk is the save folder's npu_hdd/\n"
               "  --eamuse local|URL  with the unit, when the game has e-AMUSEMENT on: the server in the core\n"
               "                    (default, offline; its data in the save folder's eamuse/) or a server at\n"
               "                    http://host[:port][/path] (online)\n"
               "  --cd hdd          boot the unit's data installer CD (implies --npu): it installs the\n"
               "                    e-AMUSEMENT data on the unit's disk; power off when it says it is done\n"
               "  --session-host SOCKET  session play: this cabinet (the one with the multi-session unit,\n"
               "                    DrumMania) waits at SOCKET (a path, or host:port) for the other\n"
               "  --session-join SOCKET  session play: this cabinet (GuitarFreaks) joins the host's unit\n"
               "  --session-quantum US   the lockstep quantum in microseconds (default 500; both sides alike)\n";
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
        get("pccard1", cfg.pccard1);
        get("pccard2", cfg.pccard2);
        get("cassette", cfg.cassette);
        cfg.memory_card_reader = machine["memory_card_reader"] == "1";
        cfg.msu = machine["msu"] == "1";
        if (cfg.msu) cfg.msu_cd = o.data_path(machine["msu_cd"]);
        get("msu_flash", cfg.msu_flash);
        get("msu_rtc", cfg.msu_rtc);
        cfg.cd = o.data_path(machine["cd"]);
        if (!cd_.empty()) {
            if (cd_ != "hdd") {
                fprintf(stderr, "--cd: hdd (the network unit's data installer)\n");
                return false;
            }
            if (machine["hdd_cd"].empty()) {
                fprintf(stderr, "%s has no network unit installer CD (run tools/ingest.py with its CHD)\n", o.game.c_str());
                return false;
            }
            cfg.cd = o.data_path(machine["hdd_cd"]);
            npu_ = true;
        }
        if (npu_) {
            if (machine["npu"] != "1") {
                fprintf(stderr, "%s has no network unit\n", o.game.c_str());
                return false;
            }
            if (o.nvram_dir.empty()) {
                fprintf(stderr, "--npu: the unit's disk needs a save folder (--nvram)\n");
                return false;
            }
            cfg.pccard2 = "npu";
            cfg.eamuse = eamuse_;
            // the card units come with it: one on DrumMania, one per guitar on GuitarFreaks
            cfg.card_readers = machine["family"] == "gtrf" ? 2 : 1;
        }
        cfg.nvram_dir = o.nvram_dir;
        cfg.session_host = session_host_;
        cfg.session_join = session_join_;
        cfg.session_quantum_us = session_quantum_;
        cfg.rtc_time = rtc_ >= 0 ? rtc_ : (int64_t)time(nullptr);
        if (mame_mix_) cfg.mix_gain = 1.0f;
        cfg.fine_sound = fine_sound_;
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
    bool set_present_hook(PresentHook h) override { k573::set_present_hook(std::move(h)); return true; }
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
    bool card_read(int player, const std::string &number) override { return k573::card_read(player, number); }
    int script_button_delay() const override { return 1; }   // MAME latches digital inputs a frame late
    void turntable_add(int, int) override {}
    const std::vector<uint8_t> &lamp_state() const override { return k573::lamp_state(); }
    uint64_t lamp_changes() const override { return k573::lamp_changes(); }

private:
    std::function<void()> on_fatal_;
    long long rtc_ = -1;
    bool mame_mix_ = false;
    bool fine_sound_ = false;
    std::string cassette_slot_;
    bool npu_ = false;
    std::string eamuse_ = "local";
    std::string cd_;
    std::string session_host_, session_join_;
    int session_quantum_ = 500;
};

}  // namespace

Core &core() {
    static Sys573Core c;
    return c;
}

}  // namespace hk
