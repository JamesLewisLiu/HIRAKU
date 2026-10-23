// SPDX-License-Identifier: GPL-3.0-or-later
// The DJ Main core: the board (board.h) behind the shell's interface (shell/core.h).
// The machine comes from dumps/<game>/machine.txt (tools/ingest.py): the family,
// the ROM file lists and the raw hard disk image.
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "shell/kvfile.h"
#include "../shell/core.h"
#include "board.h"
#include "k054539.h"
#include "m68k.h"

namespace hk {

namespace {

std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, ',');)
        if (!item.empty()) out.push_back(item);
    return out;
}

class DjMainCore final : public Core {
public:
    bool init(const Options &o) override {
        auto machine = read_kv(o.root + "/dumps/" + o.game + "/machine.txt");
        if (machine.empty() || machine["core"] != "djmain") {
            fprintf(stderr, "no DJ Main dumps/%s/machine.txt - run tools/ingest.py --romset %s.7z --chd <hdd>.chd first\n",
                    o.game.c_str(), o.game.c_str());
            return false;
        }
        dj::Config cfg;
        cfg.family = machine["family"];
        cfg.rom_dir = o.root + "/dumps/" + o.game + "/rom";
        cfg.program = split(machine["program"]);
        cfg.sprites = split(machine["sprites"]);
        cfg.tiles = split(machine["tiles"]);
        cfg.hdd = o.data_path(machine["hdd"]);
        // the disk's geometry (games/<id>/game.toml [hdd] chs, from its CHD as MAME's drive gets it)
        if (!machine["hdd_chs"].empty())
            sscanf(machine["hdd_chs"].c_str(), "%u,%u,%u", &cfg.hdd_chs[0], &cfg.hdd_chs[1], &cfg.hdd_chs[2]);
        if (!machine["map"].empty()) cfg.map = machine["map"];
        // ATA passwords as 64 hex digits (games/<id>/game.toml [hdd], from MAME's init_<game>())
        auto hex = [](const std::string &h) {
            std::vector<uint8_t> v;
            for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back((uint8_t)std::stoul(h.substr(i, 2), nullptr, 16));
            return v;
        };
        cfg.ata_master_password = hex(machine["ata_master_password"]);
        cfg.ata_user_password = hex(machine["ata_user_password"]);
        cfg.ata_log = o.ata_log;
        if (!dj::init(cfg)) return false;
        printf("hiraku %s: DJ Main, 68EC020 interpreted\n", o.game.c_str());
        return true;
    }

    const std::string &family() const override { return dj::family(); }
    int monitor_khz() const override { return 24; }   // 512x384: medium resolution
    void set_frame_hook(FrameHook h) override { dj::set_frame_hook(std::move(h)); }
    bool set_present_hook(PresentHook h) override { dj::set_present_hook(std::move(h)); return true; }
    void set_audio_sink(AudioSink s) override { dj::set_audio_sink(std::move(s)); }
    void set_on_fatal(std::function<void()> f) override { on_fatal_ = std::move(f); }
    [[noreturn]] void run() override { dj::run(); }
    void shutdown() override {}

    void report() override {
        const auto &st = dj::stats();
        printf("frames %llu, virtual time %.3f s, pc %06x, %llu vblank IRQs, %llu IDE IRQs, %llu unmapped accesses, %.1f s of sound\n",
               (unsigned long long)dj::frame_count(), seconds(), dj::pc(), (unsigned long long)st.irq4,
               (unsigned long long)st.irq1, (unsigned long long)st.unmapped, (double)st.sound_samples / 48000.0);
    }
    void debug_line(uint64_t f) override {
        printf("f%llu pc %06x sr %04x a7 %08x d0 %08x\n", (unsigned long long)f, dj::pc(),
               m68k_get_reg(nullptr, M68K_REG_SR), m68k_get_reg(nullptr, M68K_REG_A7), m68k_get_reg(nullptr, M68K_REG_D0));
    }

    double seconds() const override { return (double)dj::cycles() / dj::CPU_HZ; }
    const uint8_t *mem(uint32_t addr, uint32_t len) const override { return dj::mem(addr, len); }
    uint64_t frame_count() const override { return dj::frame_count(); }
    bool set_vsync_rate(double hz) override { return dj::set_frame_rate(hz), true; }
    double native_rate() const override { return (double)dj::CPU_HZ / dj::FRAME_CYCLES; }
    const uint32_t *render_between(double t, int &w, int &h) override {
        w = dj::SCREEN_W, h = dj::SCREEN_H;
        return dj::render_between(t);
    }

    bool set_input(const std::string &name, int value) override { return dj::set_input(name, value); }
    int script_button_delay() const override { return 1; }
    int audio_rate() const override { return dj::K054539::RATE; }
    bool audio_per_frame() const override { return true; }   // board.cpp run(): sound_flush()
    void turntable_add(int player, int delta) override { dj::turntable_add(player, delta); }
    const std::vector<uint8_t> &lamp_state() const override { return dj::lamp_state(); }
    uint64_t lamp_changes() const override { return dj::lamp_changes(); }

private:
    std::function<void()> on_fatal_;
};

}  // namespace

Core &core() {
    static DjMainCore c;
    return c;
}

}  // namespace hk
