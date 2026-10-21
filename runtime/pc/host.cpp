// SPDX-License-Identifier: GPL-3.0-or-later
// hiraku-pc: a PC game (pop'n music 15 on) in HIRAKU's cabinet (PLAN.md section 30). The game is
// Konami's own Windows program, run as it is: on Linux under Wine, on Windows directly. HIRAKU's
// ezusb.dll and libacio.dll (runtime/pc/) take the place of Konami's in a copy of its program
// folder, and this program is the cabinet they talk to through a shared file (runtime/pc/link.h):
// the controls and their bindings (runtime/host/input.h), the lamps out to lights
// (runtime/host/lights.h), the card readers (runtime/host/readers.h).
//
//   hiraku-pc --game ID [options]
//     --window WxH     the game in a window of that size (default 1280x960)
//     --scale N        the same, N times the game's 640x480
//     --fullscreen     a window covering the screen, the picture at its height
//     --hz N           the most pictures a second (default 60, the game's monitor)
//   Escape in the game's window ends it, as in HIRAKU's other games.
//     --bindings FILE  controls (runtime/host/input.h)
//     --lights FILE, --lights-socket P   lamps out (runtime/host/lights.h)
//     --wine PROGRAM   Linux: the Wine to run it with (default: wine, from the PATH)
//     --reader PATH ... the e-AMUSEMENT card readers (runtime/host/readers.h)
//     --popn-io auto   a pop'n cabinet's I/O board (runtime/host/popnio.h); its firmware from the
//                      game's own ezusb.dll unless --popn-io-dll says otherwise
//     -card0 NUMBER    player 1's card (shell/eaoptions.h; card0.txt otherwise)
//
// dumps/<ID>/machine.txt says where the game is:
//   core=pc, family=pcpopn
//   exe=popn15.exe          the program
//   program=PATH            its folder (prog/<date>), copied into the save folder to run
//   data=PATH               the game's data, read where it is
//   drive=D:\popn15         where the game looks for its folders (program: <drive>\prog\<date>)
//   save_drive=E:\          its backup drive, a folder in the save folder
#include <SDL3/SDL.h>

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
#include <fcntl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include "host/input.h"
#include "host/lights.h"
#include "host/popnio.h"
#include "host/readers.h"
#include "pc/link.h"
#include "shell/cabinet.h"
#include "shell/core.h"
#include "shell/eaoptions.h"
#include "shell/sys.h"
#include "shell/userdir.h"

namespace {

hk_pc_link *g_link = nullptr;

// The cabinet as HIRAKU's shell sees a core: inputs by name go into the link, lamps come out of it
class PcCore : public hk::Core {
public:
    std::string family_ = "pcpopn";
    std::string card_uid_;   // player 1's card (16 hex digits), for the Card control

    bool init(const Options &) override { return true; }
    const std::string &family() const override { return family_; }
    void set_frame_hook(FrameHook) override {}
    void set_audio_sink(AudioSink) override {}
    void set_on_fatal(std::function<void()>) override {}
    [[noreturn]] void run() override { std::abort(); }
    void shutdown() override {}
    void report() override {}
    double seconds() const override { return 0; }
    const uint8_t *mem(uint32_t, uint32_t) const override { return nullptr; }
    uint64_t frame_count() const override { return g_link ? g_link->frames : 0; }
    void turntable_add(int, int) override {}

    bool set_input(const std::string &name, int value) override {
        if (!g_link) return false;
        auto bit = [&](volatile uint32_t &w, uint32_t b) {
            if (value) w = w | b;
            else w = w & ~b;
        };
        if (name.rfind("P1 Button ", 0) == 0) {
            const int k = std::atoi(name.c_str() + 10);
            if (k < 1 || k > 9) return false;
            bit(g_link->pad, HK_PC_PAD_BUTTON(k));
        } else if (name == "Test") {
            bit(g_link->pad, HK_PC_PAD_TEST);
        } else if (name == "Service") {
            bit(g_link->pad, HK_PC_PAD_SERVICE);
        } else if (name == "Coin 1") {
            if (value) g_link->coins = g_link->coins + 1;
        } else if (name == "Card") {
            if (value) present_card(card_uid_);
        } else if (name == "Keypad 00" || name == "Keypad Enter") {
            bit(g_link->keypad, HK_PC_KEY_00);
        } else if (name == "Keypad .") {
            bit(g_link->keypad, HK_PC_KEY_DECIMAL);
        } else if (name.rfind("Keypad ", 0) == 0 && name.size() == 8 && name[7] >= '0' && name[7] <= '9') {
            bit(g_link->keypad, 1u << (name[7] - '0'));
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

    void present_card(const std::string &number) {
        if (number.size() != 16) {
            printf("e-AMUSEMENT: no card number (-card0, or card0.txt in the save folder)\n");
            return;
        }
        for (int i = 0; i < 8; i++) g_link->card_uid[i] = (uint8_t)std::stoul(number.substr(2 * i, 2), nullptr, 16);
        g_link->card_seq = g_link->card_seq + 1;
        printf("e-AMUSEMENT: card %s held to the reader\n", number.c_str());
    }

    // usbLamp's bits in the family's lamp order (shell/cabinet.cpp): the buttons' bits 23-31, the
    // top (HI-LAMP 1-5) bits 0-4, the sides (LEFT 1-2, RIGHT 1-2) bits 8-11, as spice2x has them
    const std::vector<uint8_t> &lamp_state() const override {
        const auto &l = hk::cab::lamps(family_);
        lamps_.resize(l.size());
        const uint32_t b = g_link ? g_link->lamps : 0;
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
    mutable std::vector<uint8_t> lamps_;
    mutable uint32_t last_lamps_ = 0;
    mutable uint64_t changes_ = 0;
};

PcCore g_core;

// A key's Windows virtual-key code (the DLL reads the game window's keyboard itself): 0 if none
uint8_t vk_of(SDL_Keycode k) {
    if (k >= 'a' && k <= 'z') return (uint8_t)(k - 'a' + 'A');
    if (k >= '0' && k <= '9') return (uint8_t)k;
    if (k >= SDLK_F1 && k <= SDLK_F12) return (uint8_t)(0x70 + (k - SDLK_F1));
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return (uint8_t)(0x61 + (k - SDLK_KP_1));
    static const std::map<SDL_Keycode, uint8_t> other = {
        {SDLK_KP_0, 0x60},       {SDLK_KP_PERIOD, 0x6e},  {SDLK_KP_ENTER, 0x0d},   {SDLK_KP_PLUS, 0x6b},
        {SDLK_KP_MINUS, 0x6d},   {SDLK_KP_MULTIPLY, 0x6a}, {SDLK_KP_DIVIDE, 0x6f}, {SDLK_SPACE, 0x20},
        {SDLK_RETURN, 0x0d},     {SDLK_BACKSPACE, 0x08},  {SDLK_TAB, 0x09},        {SDLK_ESCAPE, 0x1b},
        {SDLK_LEFT, 0x25},       {SDLK_UP, 0x26},         {SDLK_RIGHT, 0x27},      {SDLK_DOWN, 0x28},
        {SDLK_INSERT, 0x2d},     {SDLK_DELETE, 0x2e},     {SDLK_HOME, 0x24},       {SDLK_END, 0x23},
        {SDLK_PAGEUP, 0x21},     {SDLK_PAGEDOWN, 0x22},   {SDLK_LSHIFT, 0xa0},     {SDLK_RSHIFT, 0xa1},
        {SDLK_LCTRL, 0xa2},      {SDLK_RCTRL, 0xa3},      {SDLK_LALT, 0xa4},       {SDLK_RALT, 0xa5},
        {SDLK_COMMA, 0xbc},      {SDLK_PERIOD, 0xbe},     {SDLK_SLASH, 0xbf},      {SDLK_SEMICOLON, 0xba},
        {SDLK_MINUS, 0xbd},      {SDLK_EQUALS, 0xbb},     {SDLK_LEFTBRACKET, 0xdb}, {SDLK_RIGHTBRACKET, 0xdd},
        {SDLK_BACKSLASH, 0xdc},  {SDLK_APOSTROPHE, 0xde}, {SDLK_GRAVE, 0xc0}};
    auto it = other.find(k);
    return it != other.end() ? it->second : 0;
}

// The keyboard keys bound to the cabinet's controls (the family's defaults, then the file's), into
// the link for the DLL; the rest (controllers) stays with input.cpp
void bind_keys(const std::string &bindings_file) {
    std::string text = hk::cab::default_bindings(g_core.family_);
    if (!bindings_file.empty()) {
        std::ifstream f(bindings_file);
        text += "\n" + std::string(std::istreambuf_iterator<char>(f), {});
    }
    std::map<std::string, std::vector<uint8_t>> keys;
    for (const auto &e : hk::cab::parse_list_file(text)) {
        std::vector<uint8_t> vks;
        for (const std::string &item : e.items) {
            if (item.rfind("key:", 0) != 0) continue;
            static const std::map<std::string, std::string> alias = {
                {"Comma", ","}, {"Period", "."}, {"Slash", "/"}, {"Semicolon", ";"}, {"Minus", "-"}, {"Equals", "="}};
            std::string k = item.substr(4);
            if (auto a = alias.find(k); a != alias.end()) k = a->second;
            if (const uint8_t vk = vk_of(SDL_GetKeyFromName(k.c_str()))) vks.push_back(vk);
        }
        keys[e.name] = vks;   // a later entry replaces an earlier one
    }
    auto put = [](uint8_t (&slot)[2], const std::vector<uint8_t> &vks) {
        slot[0] = vks.size() > 0 ? vks[0] : 0;
        slot[1] = vks.size() > 1 ? vks[1] : 0;
    };
    for (const auto &[name, vks] : keys) {
        if (name.rfind("P1 Button ", 0) == 0) {
            const int k = std::atoi(name.c_str() + 10);
            if (k >= 1 && k <= 9) put(g_link->keys[7 + k], vks);
        } else if (name == "Test") put(g_link->keys[7], vks);
        else if (name == "Service") put(g_link->keys[6], vks);
        else if (name == "Coin 1") put(g_link->coin_keys, vks);
        else if (name == "Card") put(g_link->card_keys, vks);
        else if (name == "Keypad 00" || name == "Keypad Enter") put(g_link->keypad_keys[10], vks);
        else if (name == "Keypad .") put(g_link->keypad_keys[11], vks);
        else if (name.rfind("Keypad ", 0) == 0 && name.size() == 8 && name[7] >= '0' && name[7] <= '9')
            put(g_link->keypad_keys[name[7] - '0'], vks);
    }
}

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

std::string find_root() {
    if (const char *r = getenv("HIRAKU_ROOT"); r && *r) return r;
    auto has_dumps = [](const std::string &d) { return hk::sys::is_dir(d + "/dumps"); };
    std::string dir, up;
    if (const std::string exe = hk::sys::exe_path(); !exe.empty()) {
        dir = exe.substr(0, exe.rfind('/'));
        up = dir.substr(0, dir.rfind('/'));
    }
    for (const std::string &d : {dir, up})
        if (!d.empty() && has_dumps(d) && hk::portable(d)) return d;
    if (has_dumps(HK_ROOT)) return HK_ROOT;
    for (const std::string &d : {dir, up})
        if (!d.empty() && has_dumps(d)) return d;
    return HK_ROOT;
}

std::atomic<bool> g_quit{false};

}  // namespace

hk::Core &hk::core() { return g_core; }

int main(int argc, char **argv) {
    std::string game, bindings, lights_file, lights_socket, wine = "wine", window = "1280x960";
    double hz = 60;
    hk::EaOptions ea;
    bool popnio = false, popnio_dll = false;
    for (int k = 1; k < argc; k++) {
        std::string a = argv[k];
        if (a.size() >= 2 && a[0] == '-' && a[1] != '-') a = "-" + a;   // spice2x's one dash
        auto val = [&]() -> std::string {
            if (k + 1 >= argc) {
                fprintf(stderr, "%s needs a value\n", a.c_str());
                exit(2);
            }
            return argv[++k];
        };
        std::string error;
        if (a == "--game") game = val();
        else if (a == "--window") window = val();
        else if (a == "--scale") {
            const int n = std::max(1, std::atoi(val().c_str()));
            window = std::to_string(640 * n) + "x" + std::to_string(480 * n);
        }
        else if (a == "--fullscreen") window = "full";
        else if (a == "--hz") hz = std::stod(val());
        else if (a == "--bindings") bindings = val();
        else if (a == "--lights") lights_file = val();
        else if (a == "--lights-socket") lights_socket = val();
        else if (a == "--wine") wine = val();
        else if (hk::host::popnio_option(a, val)) {
            popnio |= a == "--popn-io";
            popnio_dll |= a != "--popn-io";
        }
        else if (hk::host::readers_option(a, val, &error) || ea.option(a, val, &error)) {
            if (!error.empty()) {
                fprintf(stderr, "%s\n", error.c_str());
                return 2;
            }
        } else if (a == "--help" || a == "-h") {
            printf("hiraku-pc --game ID [--window WxH | --scale N | --fullscreen] [--hz N] [--bindings FILE]\n"
                   "          [--lights FILE] [--lights-socket P] [--wine PROGRAM]\n%s%s",
                   (hk::host::readers_help() + hk::host::popnio_help()).c_str(), hk::EaOptions::help());
            return 0;
        } else {
            fprintf(stderr, "hiraku-pc: unknown option %s (--help)\n", a.c_str());
            return 2;
        }
    }
    if (game.empty()) {
        fprintf(stderr, "hiraku-pc: --game ID\n");
        return 2;
    }
    const std::string root = find_root();
    const std::string dumps = root + "/dumps/" + game;
    auto m = read_machine(dumps + "/machine.txt");
    auto path_of = [&](const std::string &key) {
        const std::string p = m[key];
        return p.empty() || p[0] == '/' || (p.size() > 1 && p[1] == ':') ? p : dumps + "/" + p;
    };
    const std::string exe = m["exe"], program = path_of("program"), data = path_of("data");
    const std::string drive = m.count("drive") ? m["drive"] : "D:\\" + game;
    const std::string save_drive = m.count("save_drive") ? m["save_drive"] : "E:\\";
    if (m["core"] != "pc" || exe.empty() || program.empty() || data.empty()) {
        fprintf(stderr, "hiraku-pc: %s/machine.txt doesn't describe a PC game (core=pc, exe, program, data)\n", dumps.c_str());
        return 1;
    }
    if (m.count("family")) g_core.family_ = m["family"];

    // The save folder: the program's copy (with HIRAKU's DLLs for Konami's), the backup drive
    const std::string save = hk::user_data_dir(root) + "/" + game;
    const std::string prog_name = program.substr(program.find_last_of("/\\") + 1);
    const std::string run_prog = save + "/prog/" + prog_name, run_e = save + "/e";
    mkdirs(run_prog);
    mkdirs(run_e);
    const std::string bin_dir = hk::sys::exe_path().substr(0, hk::sys::exe_path().rfind('/'));
    const bool konami_reader = false;   // a real reader on a serial port keeps Konami's libacio.dll (later)
    for (const std::string &f : list_files(program)) {
        std::string lower = f;
        for (char &c : lower) c = (char)std::tolower((unsigned char)c);
        std::string from = program + "/" + f;
        if (lower == "ezusb.dll" || (lower == "libacio.dll" && !konami_reader)) from = bin_dir + "/pc/" + lower;
        const std::string to = run_prog + "/" + f;
        if (file_size(from) == UINT64_MAX) {
            fprintf(stderr, "hiraku-pc: no %s (HIRAKU's PC DLLs belong in a pc folder beside hiraku-pc)\n", from.c_str());
            return 1;
        }
        // copied once; HIRAKU's DLLs each time (a new HIRAKU brings new ones)
        if (file_size(to) != file_size(from) || from.rfind(bin_dir, 0) == 0)
            if (!copy_file(from, to)) {
                fprintf(stderr, "hiraku-pc: can't copy %s to %s\n", from.c_str(), to.c_str());
                return 1;
            }
    }

    std::string error;
    ea.read_card_files(save, &error);
    if (!error.empty()) fprintf(stderr, "%s\n", error.c_str());
    g_core.card_uid_ = ea.card[0];

    // The link: a file both map. Linux: in /dev/shm (memory); Windows: in the save folder
#ifdef _WIN32
    const std::string link_path = save + "/link.bin";
    HANDLE lf = CreateFileA(link_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    HANDLE lm = lf != INVALID_HANDLE_VALUE ? CreateFileMappingA(lf, nullptr, PAGE_READWRITE, 0, sizeof(hk_pc_link), nullptr) : nullptr;
    g_link = lm ? (hk_pc_link *)MapViewOfFile(lm, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(hk_pc_link)) : nullptr;
#else
    const std::string link_path = "/dev/shm/hiraku-pc-" + std::to_string(getpid());
    const int lfd = open(link_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (lfd >= 0 && ftruncate(lfd, sizeof(hk_pc_link)) == 0) {
        void *p = mmap(nullptr, sizeof(hk_pc_link), PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
        g_link = p == MAP_FAILED ? nullptr : (hk_pc_link *)p;
    }
    if (lfd >= 0) close(lfd);
#endif
    if (!g_link) {
        fprintf(stderr, "hiraku-pc: can't make the link file %s\n", link_path.c_str());
        return 1;
    }
    memset((void *)g_link, 0, sizeof *g_link);

    // Controls: controllers through input.cpp here, the keyboard through the DLL in the game
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS)) fprintf(stderr, "hiraku-pc: SDL: %s\n", SDL_GetError());
    hk::host::input_init(bindings);
    bind_keys(bindings);
    if (!lights_file.empty() || !lights_socket.empty()) hk::host::lights_init(game, g_core.family_, lights_file, lights_socket);
    hk::host::readers_start();
    if (popnio && !popnio_dll) {   // the firmware: the game's own ezusb.dll
        const std::string dll = program + "/ezusb.dll";
        hk::host::popnio_option("--popn-io-dll", [&] { return dll; });
    }
    hk::host::popnio_start();
    g_link->version = HK_PC_LINK_VERSION;
    g_link->magic = HK_PC_LINK_MAGIC;

    // What the DLLs are told (runtime/pc/cabinet.h)
    const std::string map = drive + "\\data=" + win_path(data) + ";" + drive + "\\prog=" + win_path(save + "/prog") + ";" +
                            save_drive + "=" + win_path(run_e) + "\\";
    auto setenv_ = [](const char *k, const std::string &v) {
#ifdef _WIN32
        SetEnvironmentVariableA(k, v.c_str());
#else
        setenv(k, v.c_str(), 1);
#endif
    };
    setenv_("HK_PC_LINK", win_path(link_path));
    setenv_("HK_PC_MAP", map);
    setenv_("HK_PC_WINDOW", window);
    setenv_("HK_PC_HZ", std::to_string(hz));
    setenv_("HK_PC_LOG", win_path(save + "/pc.log"));
    { std::ofstream(save + "/pc.log", std::ios::trunc); }
    printf("hiraku-pc: %s, %s from %s; saves in %s\n", game.c_str(), exe.c_str(), program.c_str(), save.c_str());
    fflush(stdout);

    // The game
#ifdef _WIN32
    PROCESS_INFORMATION pi{};
    auto start = [&]() -> bool {
        STARTUPINFOA si{};
        si.cb = sizeof si;
        std::string cmd = "\"" + win_path(run_prog + "/" + exe) + "\"";
        if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, win_path(run_prog).c_str(), &si, &pi)) return true;
        fprintf(stderr, "hiraku-pc: can't start %s (error %lu)\n", cmd.c_str(), GetLastError());
        return false;
    };
    auto running = [&] { return WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT; };
    auto stop = [&] {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess), CloseHandle(pi.hThread);
    };
#else
    // Wine's own folder for HIRAKU's PC games, made the first time without asking to install Mono
    // or Gecko (the games need neither)
    const std::string prefix = hk::user_data_dir(root) + "/pc-wine";
    setenv("WINEPREFIX", prefix.c_str(), 1);
    if (!getenv("WINEDEBUG")) setenv("WINEDEBUG", "-all", 1);
    if (!hk::sys::is_dir(prefix + "/drive_c")) {
        printf("hiraku-pc: preparing Wine's folder for the PC games (the first time only)\n");
        fflush(stdout);
        setenv("WINEDLLOVERRIDES", "mscoree=;mshtml=", 1);
        const int r = hk::sys::run_quiet({"/bin/sh", "-c", "exec \"$0\" wineboot -i", wine});
        unsetenv("WINEDLLOVERRIDES");
        if (r != 0) fprintf(stderr, "hiraku-pc: wineboot said %d\n", r);
    }
    if (chdir(run_prog.c_str()) != 0) {
        fprintf(stderr, "hiraku-pc: no %s\n", run_prog.c_str());
        return 1;
    }
    pid_t pid = 0;
    int status = 0;
    const std::string exe_path = run_prog + "/" + exe;
    auto start = [&]() -> bool {
        char *args[] = {(char *)wine.c_str(), (char *)exe_path.c_str(), nullptr};
        if (posix_spawnp(&pid, wine.c_str(), nullptr, nullptr, args, environ) == 0) return true;
        fprintf(stderr, "hiraku-pc: can't start %s (is Wine installed? --wine PROGRAM)\n", wine.c_str());
        return false;
    };
    auto running = [&] { return waitpid(pid, &status, WNOHANG) == 0; };
    auto stop = [&] {
        // the game and anything else Wine runs for it in this prefix
        const std::string ws = wine.size() >= 4 && wine.compare(wine.size() - 4, 4, "wine") == 0 ? wine.substr(0, wine.size() - 4) + "wineserver" : "wineserver";
        hk::sys::run_quiet({"/bin/sh", "-c", "exec \"$0\" -k", ws});
        kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
    };
    signal(SIGTERM, [](int) { g_quit = true; });
    signal(SIGINT, [](int) { g_quit = true; });
#endif
    // A new save folder: the game makes its backup files and asks to be switched off and on
    // (BACKUP ERROR 5-1502); that is done for it, once, when they've been there a few seconds
    bool first_boot = list_files(run_e + "/settings").empty();
    auto backup_made = std::chrono::steady_clock::time_point{};
    if (!start()) return 1;

    // The cabinet, a thousand times a second while the game runs
    auto last_frame = std::chrono::steady_clock::now(), rate_from = last_frame;
    uint32_t frames_seen = 0, rate_frames = 0, rate_polls = 0;
    while (!g_quit) {
        if (!running()) break;
        if (first_boot && !list_files(run_e + "/settings").empty()) {
            const auto now = std::chrono::steady_clock::now();
            if (backup_made == std::chrono::steady_clock::time_point{}) backup_made = now;
            else if (now - backup_made > std::chrono::seconds(5)) {
                printf("hiraku-pc: the game has set up its backup: starting it again, as it asks\n");
                fflush(stdout);
                first_boot = false;
                stop();
                if (!start()) break;
            }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_JOYSTICK_ADDED) hk::host::input_joystick_added(e.jdevice.which);
            else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) hk::host::input_joystick_removed(e.jdevice.which);
            else hk::host::input_event(e);
        }
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last_frame).count();
        if (g_link->frames != frames_seen || ms > 16.7) {   // the game's frame (or a 60th without one)
            frames_seen = g_link->frames;
            last_frame = now;
            hk::host::input_update(ms);
            hk::host::readers_frame([](int p, const std::string &n) { return g_core.card_read(p, n); },
                                    [](const std::string &n, int v) { return g_core.set_input(n, v); });
            hk::host::popnio_frame(g_core);
            hk::host::lights_frame();
            // the game's pictures a second, every half minute (it was made for its monitor's 60)
            if (now - rate_from > std::chrono::seconds(30)) {
                const double sec = std::chrono::duration<double>(now - rate_from).count();
                printf("hiraku-pc: %.2f pictures a second, the board read %.0f times a second\n",
                       (g_link->frames - rate_frames) / sec, (g_link->game_alive - rate_polls) / sec);
                rate_polls = g_link->game_alive;
                fflush(stdout);
                rate_from = now, rate_frames = g_link->frames;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (g_quit) stop();
#ifndef _WIN32
    else   // the game ended (Escape): Wine's helpers for it go too, instead of lingering
        hk::sys::run_quiet({"/bin/sh", "-c", "exec \"$0\" -k", wine.size() >= 4 && wine.compare(wine.size() - 4, 4, "wine") == 0 ? wine.substr(0, wine.size() - 4) + "wineserver" : "wineserver"});
#endif
    hk::host::popnio_stop();
    hk::host::readers_stop();
    hk::host::lights_close();
#ifndef _WIN32
    unlink(link_path.c_str());
#endif
    printf("hiraku-pc: the game ended\n");
    return 0;
}
