// SPDX-License-Identifier: GPL-3.0-or-later
// The PC games' core (PLAN.md section 30): pop'n music 15 on, Konami's own Windows programs, run as
// they are (on Linux under Wine, on Windows directly) as a machine for HIRAKU's shell
// (runtime/main.cpp: hiraku-pc --game ID). HIRAKU's ezusb.dll and libacio.dll (runtime/pc/) take
// the place of Konami's in a copy of the program folder, and this core is the cabinet they talk to
// through a shared file (runtime/pc/link.h): the controls in, the lamps and the game's pictures out.
// The pictures come in lockstep: the game draws one and waits; the shell shows it in HIRAKU's window
// (scale, full screen, the CRT pictures, the beam simulator) and lets the next one start when its
// pacing says so, as for an emulated machine. The game's own window stays hidden. Its sound is
// mixed by HIRAKU's DirectSound in the DLL (runtime/pc/dsound.c), 735 frames (44.1 kHz) for each
// frame shown, and played through HIRAKU's sound output like an emulated machine's.
//
// Options of its own: --wine PROGRAM (Linux: the Wine to run it with, default `wine` from the PATH),
// and e-AMUSEMENT's -card0 / -card1 (shell/eaoptions.h).
//
// dumps/<ID>/machine.txt (tools/ingest.py) says where the game is:
//   core=pc, family=pcpopn
//   exe=popn15.exe          the program
//   program=PATH            its folder (prog/<date>), copied into the save folder to run
//   data=PATH               the game's data, read where it is
//   drive=D:\popn15         where the game looks for its folders (program: <drive>\prog\<date>)
//   save_drive=E:\          its backup drive, a folder in the save folder
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include "host/pacing.h"
#include "host/popnio.h"
#include "pc/link.h"
#include "shell/cabinet.h"
#include "shell/core.h"
#include "shell/eaoptions.h"
#include "shell/sys.h"
#include "shell/userdir.h"

namespace {

std::map<std::string, std::string> read_machine(const std::string &path) {
    std::map<std::string, std::string> m;
    std::ifstream f(path);
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        m[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return m;
}

void mkdirs(const std::string &path) {
    for (size_t k = 1; k <= path.size(); k++)
        if (k == path.size() || path[k] == '/') hk::sys::make_dir(path.substr(0, k));
}

bool copy_file(const std::string &from, const std::string &to) {
    std::ifstream in(from, std::ios::binary);
    if (!in) return false;
    std::ofstream out(to + ".part", std::ios::binary);
    out << in.rdbuf();
    out.close();
    return out.good() && hk::sys::replace_file(to + ".part", to);
}

// The files (not folders) in a folder
std::vector<std::string> list_files(const std::string &dir) {
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAA d;
    HANDLE h = FindFirstFileA((dir + "/*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return out;
    do
        if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(d.cFileName);
    while (FindNextFileA(h, &d));
    FindClose(h);
#else
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) {
            struct stat st;
            if (stat((dir + "/" + e->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode)) out.push_back(e->d_name);
        }
        closedir(d);
    }
#endif
    return out;
}

uint64_t file_size(const std::string &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    return f ? (uint64_t)f.tellg() : UINT64_MAX;
}

// Windows' name for a path of this machine: Wine's Z: is the root
std::string win_path(std::string p) {
#ifndef _WIN32
    p = "Z:" + p;
#endif
    for (char &c : p)
        if (c == '/') c = '\\';
    return p;
}

void set_env(const char *k, const std::string &v) {
#ifdef _WIN32
    SetEnvironmentVariableA(k, v.c_str());
#else
    setenv(k, v.c_str(), 1);
#endif
}

class PcCore : public hk::Core {
public:
    bool option(const std::string &name, const std::function<std::string()> &value) override {
        if (name == "--wine") {
            wine_ = value();
            return true;
        }
        std::string error;
        if (ea_.option(name, value, &error)) {
            if (!error.empty()) {
                fprintf(stderr, "%s\n", error.c_str());
                exit(2);
            }
            return true;
        }
        return false;
    }
    std::string options_help() const override {
        return std::string("  --wine PROGRAM       Linux: the Wine that runs the game (default: wine, from the PATH)\n") +
               hk::EaOptions::help();
    }

    bool init(const Options &o) override {
        game_ = o.game;
        const std::string dumps = o.root + "/dumps/" + game_;
        auto m = read_machine(dumps + "/machine.txt");
        auto path_of = [&](const std::string &key) {
            const std::string p = m[key];
            return p.empty() || p[0] == '/' || (p.size() > 1 && p[1] == ':') ? p : dumps + "/" + p;
        };
        exe_ = m["exe"];
        program_ = path_of("program");
        const std::string data = path_of("data");
        const std::string drive = m.count("drive") ? m["drive"] : "D:\\" + game_;
        const std::string save_drive = m.count("save_drive") ? m["save_drive"] : "E:\\";
        if (m["core"] != "pc" || exe_.empty() || program_.empty() || data.empty()) {
            fprintf(stderr, "hiraku-pc: %s/machine.txt doesn't describe a PC game (core=pc, exe, program, data)\n", dumps.c_str());
            return false;
        }
        if (m.count("family")) family_ = m["family"];

        // The save folder: the program's copy (HIRAKU's DLLs for Konami's), the backup drive (e/)
        save_ = o.nvram_dir.empty() ? hk::user_data_dir(o.root) + "/" + game_ : o.nvram_dir;
        const std::string prog_name = program_.substr(program_.find_last_of("/\\") + 1);
        run_prog_ = save_ + "/prog/" + prog_name;
        run_e_ = save_ + "/e";
        mkdirs(run_prog_);
        mkdirs(run_e_);
        const std::string bin_dir = hk::sys::exe_path().substr(0, hk::sys::exe_path().rfind('/'));
        for (const std::string &f : list_files(program_)) {
            std::string lower = f;
            for (char &c : lower) c = (char)std::tolower((unsigned char)c);
            std::string from = program_ + "/" + f;
            if (lower == "ezusb.dll" || lower == "libacio.dll") from = bin_dir + "/pc/" + lower;
            const std::string to = run_prog_ + "/" + f;
            if (file_size(from) == UINT64_MAX) {
                fprintf(stderr, "hiraku-pc: no %s (HIRAKU's PC DLLs belong in a pc folder beside hiraku-pc)\n", from.c_str());
                return false;
            }
            // copied once; HIRAKU's DLLs each time (a new HIRAKU brings new ones)
            if (file_size(to) != file_size(from) || from.rfind(bin_dir, 0) == 0)
                if (!copy_file(from, to)) {
                    fprintf(stderr, "hiraku-pc: can't copy %s to %s\n", from.c_str(), to.c_str());
                    return false;
                }
        }
        std::string error;
        ea_.read_card_files(save_, &error);
        if (!error.empty()) fprintf(stderr, "%s\n", error.c_str());
        // a bare pop'n board's firmware: the game's own ezusb.dll, unless --popn-io-dll gave one
        hk::host::popnio_default_dll(program_ + "/ezusb.dll");

        // The link: a file both map. Linux: in /dev/shm (memory); Windows: in the save folder
#ifdef _WIN32
        link_path_ = save_ + "/link.bin";
        HANDLE lf = CreateFileA(link_path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        HANDLE lm = lf != INVALID_HANDLE_VALUE ? CreateFileMappingA(lf, nullptr, PAGE_READWRITE, 0, HK_PC_LINK_SIZE, nullptr) : nullptr;
        link_ = lm ? (hk_pc_link *)MapViewOfFile(lm, FILE_MAP_ALL_ACCESS, 0, 0, HK_PC_LINK_SIZE) : nullptr;
#else
        link_path_ = "/dev/shm/hiraku-pc-" + std::to_string(getpid());
        const int lfd = open(link_path_.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (lfd >= 0 && ftruncate(lfd, HK_PC_LINK_SIZE) == 0) {
            void *p = mmap(nullptr, HK_PC_LINK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
            link_ = p == MAP_FAILED ? nullptr : (hk_pc_link *)p;
        }
        if (lfd >= 0) close(lfd);
#endif
        if (!link_) {
            fprintf(stderr, "hiraku-pc: can't make the link file %s\n", link_path_.c_str());
            return false;
        }
        memset((void *)link_, 0, sizeof *link_);
        link_->host_shows = 1;
        link_->version = HK_PC_LINK_VERSION;
        link_->magic = HK_PC_LINK_MAGIC;
        black_.assign(640 * 480, 0);

        // What the DLLs are told (runtime/pc/cabinet.h)
        set_env("HK_PC_LINK", win_path(link_path_));
        set_env("HK_PC_MAP", drive + "\\data=" + win_path(data) + ";" + drive + "\\prog=" + win_path(save_ + "/prog") + ";" +
                                 save_drive + "=" + win_path(run_e_) + "\\");
        set_env("HK_PC_HZ", "0");   // the shell paces
        set_env("HK_PC_LOG", win_path(save_ + "/pc.log"));
        { std::ofstream(save_ + "/pc.log", std::ios::trunc); }
        printf("hiraku-pc: %s, %s from %s; saves in %s\n", game_.c_str(), exe_.c_str(), program_.c_str(), save_.c_str());
        fflush(stdout);
        return true;
    }

    const std::string &family() const override { return family_; }
    void set_frame_hook(FrameHook h) override { frame_hook_ = std::move(h); }
    void set_audio_sink(AudioSink s) override { audio_sink_ = std::move(s); }
    int audio_rate() const override { return HK_PC_AUDIO_RATE; }
    void set_on_fatal(std::function<void()> f) override { on_fatal_ = std::move(f); }
    double native_rate() const override { return HZ; }
    int monitor_khz() const override { return 31; }   // a PC's VGA monitor
    double seconds() const override { return (double)frame_ / HZ; }
    const uint8_t *mem(uint32_t, uint32_t) const override { return nullptr; }
    uint64_t frame_count() const override { return frame_; }
    void turntable_add(int, int) override {}
    void report() override {
        printf("hiraku-pc: %llu frames shown, %llu of them again while the game drew none\n", (unsigned long long)frame_,
               (unsigned long long)repeats_);
    }

    // The game runs; each picture it draws goes to the shell, which shows it and paces. While it
    // draws none (starting, loading), the last is shown again a 60th of a second on, so the window
    // keeps answering.
    [[noreturn]] void run() override {
        if (!start_game()) fatal();
        // A new save folder: the game makes its backup files and asks to be switched off and on
        // (BACKUP ERROR 5-1502); that is done for it, once, when they've been there a few seconds
        bool first_boot = list_files(run_e_ + "/settings").empty();
        auto backup_made = std::chrono::steady_clock::time_point{};
        auto last = std::chrono::steady_clock::now(), rate_from = last;
        uint32_t seen = link_->frame_seq, rate_pictures = seen;
        uint64_t rate_repeats = 0;
        for (;;) {
            if (!game_running()) {
                printf("hiraku-pc: the game ended\n");
                fatal();
            }
            if (first_boot && !list_files(run_e_ + "/settings").empty()) {
                const auto now = std::chrono::steady_clock::now();
                if (backup_made == std::chrono::steady_clock::time_point{}) backup_made = now;
                else if (now - backup_made > std::chrono::seconds(5)) {
                    printf("hiraku-pc: the game has set up its backup: starting it again, as it asks\n");
                    fflush(stdout);
                    first_boot = false;
                    stop_game();
                    link_->frame_seq = 0;
                    link_->frame_go = 0;
                    seen = 0;
                    if (!start_game()) fatal();
                }
            }
            const uint32_t seq = link_->frame_seq;
            const auto now = std::chrono::steady_clock::now();
            if (seq != seen) {
                std::atomic_thread_fence(std::memory_order_acquire);
                seen = seq;
                // a copy, and the game goes on to its next picture while this one is shown, as it did
                // on its own monitor (drawing the next frame while one is scanned out)
                const int w = (int)link_->frame_w, h = (int)link_->frame_h;
                const bool ok = w > 0 && h > 0 && (uint32_t)w <= HK_PC_MAX_W && (uint32_t)h <= HK_PC_MAX_H;
                if (ok) picture_.assign(link_pixels(), link_pixels() + (size_t)w * h);
                link_->frame_go = seq;
                if (ok) show(picture_.data(), w, h);
                else show(nullptr, 0, 0);
                // Without display sync the sound paces, as for the emulated machines; until the game's
                // sound starts, the game's 60 a second on a steady grid
                if (!hk::host::pacing::active() && !link_->audio_made) {
                    const auto period = std::chrono::nanoseconds((long long)(1e9 / HZ));
                    const auto t = std::chrono::steady_clock::now();
                    due_ = due_ == std::chrono::steady_clock::time_point{} || t - due_ > std::chrono::milliseconds(100) ? t : due_ + period;
                    if (due_ > t) std::this_thread::sleep_until(due_);
                }
                last = std::chrono::steady_clock::now();
            } else if (now - last > std::chrono::microseconds(1000000 / (int)HZ)) {
                show(last_rgb_ ? last_rgb_ : black_.data(), last_w_, last_h_);
                repeats_++;
                last = now;
            } else if (!take_sound()) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            if (getenv("HK_PC_RATE") && now - rate_from > std::chrono::seconds(10)) {   // how the game keeps up
                const double sec = std::chrono::duration<double>(now - rate_from).count();
                fprintf(stderr, "hiraku-pc: the game drew %.2f pictures a second, %llu shown again\n", (seq - rate_pictures) / sec,
                        (unsigned long long)(repeats_ - rate_repeats));
                rate_from = now, rate_pictures = seq, rate_repeats = repeats_;
            }
        }
    }

    void shutdown() override {
        stop_game();
#ifndef _WIN32
        if (!link_path_.empty()) unlink(link_path_.c_str());
#endif
    }

    bool set_input(const std::string &name, int value) override {
        if (!link_) return false;
        auto bit = [&](volatile uint32_t &w, uint32_t b) {
            if (value) w = w | b;
            else w = w & ~b;
        };
        if (name.rfind("P1 Button ", 0) == 0) {
            const int k = std::atoi(name.c_str() + 10);
            if (k < 1 || k > 9) return false;
            bit(link_->pad, HK_PC_PAD_BUTTON(k));
        } else if (name == "Test") {
            bit(link_->pad, HK_PC_PAD_TEST);
        } else if (name == "Service") {
            bit(link_->pad, HK_PC_PAD_SERVICE);
        } else if (name == "Coin 1") {
            if (value) link_->coins = link_->coins + 1;
        } else if (name == "Card") {
            if (value) present_card(ea_.card[0]);
        } else if (name == "Keypad 00" || name == "Keypad Enter") {
            bit(link_->keypad, HK_PC_KEY_00);
        } else if (name == "Keypad .") {
            bit(link_->keypad, HK_PC_KEY_DECIMAL);
        } else if (name.rfind("Keypad ", 0) == 0 && name.size() == 8 && name[7] >= '0' && name[7] <= '9') {
            bit(link_->keypad, 1u << (name[7] - '0'));
        } else {
            return false;
        }
        return true;
    }

    // A real reader's card: held to the game's reader
    bool card_read(int player, const std::string &number) override {
        if (player != 0) return false;
        present_card(number);
        return true;
    }

    // usbLamp's bits in the family's lamp order (shell/cabinet.cpp): the buttons' bits 23-31, the
    // top (HI-LAMP 1-5) bits 0-4, the sides (LEFT 1-2, RIGHT 1-2) bits 8-11, as spice2x has them
    const std::vector<uint8_t> &lamp_state() const override {
        const auto &l = hk::cab::lamps(family_);
        lamps_.resize(l.size());
        const uint32_t b = link_ ? link_->lamps : 0;
        for (size_t i = 0; i < l.size(); i++) {
            const std::string &n = l[i].name;
            const int k = std::atoi(n.c_str() + n.find_last_of(' ') + 1);
            int at = -1;
            if (n.rfind("Button Lamp", 0) == 0) at = 22 + k;
            else if (n.rfind("Top Lamp", 0) == 0) at = k - 1;
            else if (n.rfind("Side Lamp", 0) == 0) at = 7 + k;
            lamps_[i] = at >= 0 && (b >> at & 1);
        }
        if (b != last_lamps_) last_lamps_ = b, changes_++;
        return lamps_;
    }
    uint64_t lamp_changes() const override {
        lamp_state();
        return changes_;
    }

private:
    static constexpr double HZ = 60.0;   // the cabinet's VGA monitor

    const uint32_t *link_pixels() const { return (const uint32_t *)((const char *)link_ + HK_PC_PIXELS); }

    // A frame's sound is asked for as the frame's time passes (a 44.1th of a millisecond each), not
    // all at its start: what the game starts during the frame is mixed a millisecond later, and the
    // sound reaches the output smoothly. Over a frame, 735 frames: the sound stays with the pictures
    void ask_sound() {
        const auto now = std::chrono::steady_clock::now();
        const double into = std::chrono::duration<double>(now - frame_at_).count();
        const uint32_t step = HK_PC_AUDIO_RATE / 60, part = (uint32_t)std::min<double>(step, into * HK_PC_AUDIO_RATE);
        const uint32_t want = sound_frame_end_ - step + part;
        if ((int32_t)(want - link_->audio_want) > 0) link_->audio_want = want;
    }

    // The sound mixed so far, to the shell's output (HIRAKU's: device, latency, pacing). true if any
    bool take_sound() {
        ask_sound();
        const uint32_t made = link_->audio_made, taken = link_->audio_taken;
        if (made == taken || !audio_sink_) return false;
        std::atomic_thread_fence(std::memory_order_acquire);
        const float *ring = (const float *)((const char *)link_ + HK_PC_AUDIO);
        uint32_t at = taken;
        while (at != made) {
            const uint32_t i = at & (HK_PC_AUDIO_FRAMES - 1), n = std::min(made - at, HK_PC_AUDIO_FRAMES - i);
            audio_sink_(ring + 2 * i, (int)n);
            at += n;
            link_->audio_taken = at;
        }
        return true;
    }

    void show(const uint32_t *rgb, int w, int h) {
        // the frame before's sound all asked for; this frame's from now (ask_sound), unless the DLL has
        // fallen behind (no sound yet, the game restarting) by a fifth of a second
        if ((int32_t)(sound_frame_end_ - link_->audio_want) > 0) link_->audio_want = sound_frame_end_;
        if (link_->audio_want - link_->audio_made < HK_PC_AUDIO_RATE / 5) sound_frame_end_ = link_->audio_want + HK_PC_AUDIO_RATE / 60;
        frame_at_ = std::chrono::steady_clock::now();
        if (!link_->audio_made && audio_sink_) {   // no sound from the game yet (starting): a frame's silence
            silence_.assign(2 * (HK_PC_AUDIO_RATE / 60), 0.0f);
            audio_sink_(silence_.data(), HK_PC_AUDIO_RATE / 60);
        }
        take_sound();
        if (w <= 0 || h <= 0) rgb = black_.data(), w = 640, h = 480;
        last_rgb_ = rgb, last_w_ = w, last_h_ = h;
        ++frame_;
        if (frame_hook_) frame_hook_(frame_, rgb, w, h);
    }

    [[noreturn]] void fatal() {
        if (on_fatal_) on_fatal_();
        exit(0);
    }

    void present_card(const std::string &number) {
        if (number.size() != 16) {
            printf("e-AMUSEMENT: no card number (-card0, or card0.txt in the save folder)\n");
            return;
        }
        for (int i = 0; i < 8; i++) link_->card_uid[i] = (uint8_t)std::stoul(number.substr(2 * i, 2), nullptr, 16);
        link_->card_seq = link_->card_seq + 1;
        printf("e-AMUSEMENT: card %s held to the reader\n", number.c_str());
    }

#ifdef _WIN32
    PROCESS_INFORMATION pi_{};
    bool started_ = false;
    bool start_game() {
        STARTUPINFOA si{};
        si.cb = sizeof si;
        std::string cmd = "\"" + win_path(run_prog_ + "/" + exe_) + "\"";
        if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, win_path(run_prog_).c_str(), &si, &pi_))
            return started_ = true;
        fprintf(stderr, "hiraku-pc: can't start %s (error %lu)\n", cmd.c_str(), GetLastError());
        return false;
    }
    bool game_running() { return started_ && WaitForSingleObject(pi_.hProcess, 0) == WAIT_TIMEOUT; }
    void stop_game() {
        if (!started_) return;
        started_ = false;
        TerminateProcess(pi_.hProcess, 0);
        WaitForSingleObject(pi_.hProcess, 5000);
        CloseHandle(pi_.hProcess), CloseHandle(pi_.hThread);
    }
#else
    pid_t pid_ = 0;
    bool start_game() {
        // Wine's own folder for HIRAKU's PC games, made the first time without asking to install
        // Mono or Gecko (the games need neither)
        const std::string prefix = save_.substr(0, save_.rfind('/')) + "/pc-wine";
        setenv("WINEPREFIX", prefix.c_str(), 1);
        if (!getenv("WINEDEBUG")) setenv("WINEDEBUG", "-all", 1);
        if (!hk::sys::is_dir(prefix + "/drive_c")) {
            printf("hiraku-pc: preparing Wine's folder for the PC games (the first time only)\n");
            fflush(stdout);
            setenv("WINEDLLOVERRIDES", "mscoree=;mshtml=", 1);
            const int r = hk::sys::run_quiet({"/bin/sh", "-c", "exec \"$0\" wineboot -i", wine_});
            unsetenv("WINEDLLOVERRIDES");
            if (r != 0) fprintf(stderr, "hiraku-pc: wineboot said %d\n", r);
        }
        const std::string exe_path = run_prog_ + "/" + exe_;
        char *args[] = {(char *)wine_.c_str(), (char *)exe_path.c_str(), nullptr};
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addchdir_np(&fa, run_prog_.c_str());
        const int r = posix_spawnp(&pid_, wine_.c_str(), &fa, nullptr, args, environ);
        posix_spawn_file_actions_destroy(&fa);
        if (r == 0) return true;
        pid_ = 0;
        fprintf(stderr, "hiraku-pc: can't start %s (is Wine installed? --wine PROGRAM)\n", wine_.c_str());
        return false;
    }
    bool game_running() {
        int status;
        return pid_ > 0 && waitpid(pid_, &status, WNOHANG) == 0;
    }
    // The game and whatever else Wine runs for it in this folder
    void stop_game() {
        if (pid_ <= 0) return;
        const std::string ws = wine_.size() >= 4 && wine_.compare(wine_.size() - 4, 4, "wine") == 0
                                   ? wine_.substr(0, wine_.size() - 4) + "wineserver" : "wineserver";
        hk::sys::run_quiet({"/bin/sh", "-c", "exec \"$0\" -k", ws});
        kill(pid_, SIGTERM);
        int status;
        waitpid(pid_, &status, 0);
        pid_ = 0;
    }
#endif

    std::string game_, family_ = "pcpopn", exe_, program_, save_, run_prog_, run_e_, link_path_, wine_ = "wine";
    hk::EaOptions ea_;
    hk_pc_link *link_ = nullptr;
    FrameHook frame_hook_;
    AudioSink audio_sink_;
    uint32_t sound_frame_end_ = 0;   // the sound asked for by the end of the frame being shown
    std::chrono::steady_clock::time_point frame_at_{};
    std::function<void()> on_fatal_;
    uint64_t frame_ = 0, repeats_ = 0;
    std::vector<uint32_t> black_, picture_;
    std::vector<float> silence_;
    std::chrono::steady_clock::time_point due_{};
    const uint32_t *last_rgb_ = nullptr;
    int last_w_ = 640, last_h_ = 480;
    mutable std::vector<uint8_t> lamps_;
    mutable uint32_t last_lamps_ = 0;
    mutable uint64_t changes_ = 0;
};

PcCore g_core;

}  // namespace

hk::Core &hk::core() { return g_core; }
