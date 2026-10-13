// SPDX-License-Identifier: GPL-3.0-or-later
// Konami Python 1 (a PS2 with an i.LINK interface board): PCSX2 Reliquary's emulator
// hosted as a HIRAKU machine core (PLAN.md §21, PY-1). Reliquary is the reference for
// these boards, so this core is the reference by construction; later phases replace
// parts of it (the EE by static recompilation) and check them against it.
//
// The machine comes from dumps/<game>/machine.txt (tools/ingest.py): the interface
// board's files (boot ROM, config ROM, BBSRAM, dongles), the KN00002 memory-card
// dongle, the disk image (CHD), the COH-H31000 BIOS and the MagicGate key files.
//
// Each run works on its own copy of the files Reliquary writes back (BBSRAM, dongles,
// boot ROM, memory card; the disk's writes go to an overlay): in the NVRAM directory
// when there is one, else a fresh copy under the output directory, so scripted runs
// start from the dumps' state and repeat exactly.
//
// Threads: run() is PCSX2's CPU thread (EE, IOP, SPU2, the interface board). The GS
// runs on PCSX2's own thread. At each vsync the CPU thread hands the shell the picture
// the GS finished for the previous vsync (it waits for it): the GS keeps overlapping
// the EE, and pictures, digests and scripts stay deterministic.
//
// Linking Reliquary (GPL-3.0) makes the hiraku-<python game> binaries GPL-3.0 as a
// whole; this file is GPL-3.0-or-later like the rest of HIRAKU. The Host:: glue below
// follows pcsx2-gsrunner (GPL-3.0+).
#include <stdlib.h>   // on_exit (glibc)
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/SmallString.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Achievements.h"
#include "pcsx2/Config.h"
#include "pcsx2/FireWire/FireWire.h"
#include "pcsx2/GS.h"
#include "pcsx2/GameList.h"
#include "pcsx2/HirakuHooks.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/Memory.h"
#include "pcsx2/R5900.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/VMManager.h"

#include "shell/cabinet.h"
#include "shell/core.h"

namespace {

std::map<std::string, std::string> read_kv(const std::string &path) {
    std::map<std::string, std::string> kv;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos && line[0] != '#') kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

bool copy_file(const std::string &from, const std::string &to) {
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!in || !out) return false;
    out << in.rdbuf();
    return (bool)out;
}

bool exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

void mkdirs(const std::string &path) {
    for (size_t k = 1; k <= path.size(); k++)
        if (k == path.size() || path[k] == '/') mkdir(path.substr(0, k).c_str(), 0755);
}

std::string base_name(const std::string &p) { return p.substr(p.rfind('/') + 1); }

// pop'n buttons 1-9, left to right (MAME's names), on the interface board's JAMMA
// inputs as Reliquary's POPN profile reads them (KonamiPython1.cpp,
// BuildP1IOPopnInputSourceBits): SW1-SW9 are P1 Up/Down/Left/Right/Button1 and
// P2 Up/Down/Left/Right.
const std::map<std::string, std::string> POPN_INPUTS = {
    {"P1 Button 1", "P1Up"},   {"P1 Button 2", "P1Down"}, {"P1 Button 3", "P1Left"},
    {"P1 Button 4", "P1Right"}, {"P1 Button 5", "P1Button1"}, {"P1 Button 6", "P2Up"},
    {"P1 Button 7", "P2Down"}, {"P1 Button 8", "P2Left"}, {"P1 Button 9", "P2Right"},
    {"Coin 1", "Coin1"},       {"Coin 2", "Coin2"},       {"Test", "Test"},
    {"Service", "Service"},    {"1 Player Start", "P1Start"},
};

MemorySettingsInterface s_settings;

class PythonCore final : public hk::Core {
public:
    bool option(const std::string &name, const std::function<std::string()> &value) override {
        if (name == "--renderer") {
            const std::string v = value();
            renderer_ = v == "vk" ? GSRendererType::VK : v == "gl" ? GSRendererType::OGL : GSRendererType::SW;
            return true;
        }
        if (name == "--interp") { interp_ = true; return true; }
        if (name == "--pcsx2-log") { pcsx2_log_ = true; return true; }
        return false;
    }
    std::string options_help() const override {
        return "  --renderer sw|vk|gl  the GS renderer: software (default: what the oracle runs), Vulkan or OpenGL\n"
               "  --interp             every CPU interpreted (EE, IOP, VU0, VU1), as the oracle's --interp\n"
               "  --pcsx2-log          the emulator's own log on stderr\n";
    }

    bool init(const Options &o) override {
        opt_ = o;
        auto m = read_kv(o.root + "/dumps/" + o.game + "/machine.txt");
        if (m.empty() || m["core"] != "python") {
            fprintf(stderr, "no Python dumps/%s/machine.txt - run tools/ingest.py <the set's .py1> first\n", o.game.c_str());
            return false;
        }
        family_ = m.count("family") ? m["family"] : "popn";
        lamps_.assign(hk::cab::lamps(family_).size(), 0);

        // The run's own copy of what Reliquary writes back
        run_dir_ = o.nvram_dir.empty() ? o.out + "/python-run" : o.nvram_dir;
        if (run_dir_[0] != '/') {   // the descriptor's paths must not depend on the working directory
            char cwd[4096];
            if (getcwd(cwd, sizeof cwd)) run_dir_ = std::string(cwd) + "/" + run_dir_;
        }
        if (o.nvram_dir.empty() && exists(run_dir_)) FileSystem::RecursiveDeleteDirectory(run_dir_.c_str());
        mkdirs(run_dir_);
        std::string py1 = "[Game]\nName=" + m["name"] + "\nIoMode=" + m["io_mode"] + "\n";
        if (!m["game_id"].empty()) py1 += "GameId=" + m["game_id"] + "\n";
        struct F { const char *key, *ini; bool writable; };
        const F files[] = {
            {"hdd", "HddImagePath", false},           {"cf", "CfImagePath", false},
            {"bbsram", "BbsRamPath", true},           {"io_bootrom", "IoBootRomPath", true},
            {"io_config_rom", "IoConfigRomPath", false}, {"internal_dongle", "InternalDonglePath", true},
            {"external_dongle", "ExternalDonglePath", true}, {"memcard", "MemoryCardDonglePath", true},
            {"memcard_id", "MemoryCardIdPath", false},
        };
        for (const F &f : files) {
            if (m[f.key].empty()) continue;
            std::string src = o.data_path(m[f.key]);
            if (!exists(src)) { fprintf(stderr, "python: missing %s (%s)\n", src.c_str(), f.key); return false; }
            std::string path = src;
            if (f.writable) {
                path = run_dir_ + "/" + base_name(src);
                if (!exists(path) && !copy_file(src, path)) { fprintf(stderr, "python: cannot copy %s\n", src.c_str()); return false; }
            }
            py1 += std::string(f.ini) + "=" + path + "\n";
        }
        py1_path_ = run_dir_ + "/" + o.game + ".py1";
        if (!FileSystem::WriteStringToFile(py1_path_.c_str(), py1)) { fprintf(stderr, "python: cannot write %s\n", py1_path_.c_str()); return false; }

        const std::string bios = o.data_path(m["bios"]);
        const std::string keys = o.data_path(m["keys"]);
        if (!exists(bios)) { fprintf(stderr, "python: missing BIOS %s\n", bios.c_str()); return false; }
        return init_emulator(bios, keys);
    }
    const std::string &family() const override { return family_; }

    void set_frame_hook(FrameHook h) override { frame_hook_ = std::move(h); }
    void set_audio_sink(AudioSink s) override { audio_sink_ = std::move(s); }
    void set_on_fatal(std::function<void()> f) override { on_fatal_ = std::move(f); }

    [[noreturn]] void run() override {
        Hiraku::vsync_cpu = [] { self_->on_vsync(); };
        Hiraku::audio_sample = [](float l, float r) {
            self_->audio_.push_back(l);
            self_->audio_.push_back(r);
        };
        Hiraku::gs_frame = [](const u32 *rgba, u32 w, u32 h) { self_->on_gs_frame(rgba, w, h); };

        if (!VMManager::Internal::CPUThreadInitialize()) fatal("the emulator's CPU thread could not start");
        VMManager::ApplySettings();
        VMBootParameters params;
        params.filename = py1_path_;
        Error error;
        if (VMManager::Initialize(params, &error) != VMBootResult::StartupSuccess)
            fatal("the machine did not start: " + error.GetDescription());
        bind_inputs();
        VMManager::SetLimiterMode(LimiterModeType::Unlimited);   // the shell paces the machine
        VMManager::SetState(VMState::Running);
        while (VMManager::GetState() == VMState::Running) VMManager::Execute();
        fatal("the machine stopped");
    }
    void shutdown() override {
        // Reliquary writes the BBSRAM, dongles and disk overlay as the game goes
        fflush(nullptr);
    }
    void report() override {
        printf("python: %llu frames (%.1f s), renderer %s%s, run files in %s\n", (unsigned long long)frame_,
               seconds(), renderer_ == GSRendererType::SW ? "software" : renderer_ == GSRendererType::VK ? "Vulkan" : "OpenGL",
               interp_ ? ", interpreters" : "", run_dir_.c_str());
        if (gs_waits_)   // host timing: "profile:" lines are not compared by tools/regress.py
            printf("profile: python: waited for the GS on %llu frames, %.1f ms in all\n", (unsigned long long)gs_waits_, gs_wait_ms_);
    }
    void debug_line(uint64_t f) override {
        printf("frame %llu: EE pc %08x, cycle %u\n", (unsigned long long)f, cpuRegs.pc, cpuRegs.cycle);
    }

    double seconds() const override { return (double)frame_ / 59.94; }
    const uint8_t *mem(uint32_t addr, uint32_t len) const override {
        addr &= 0x1fffffff;
        if (!eeMem || (uint64_t)addr + len > Ps2MemSize::MainRam) return nullptr;
        return eeMem->Main + addr;
    }
    uint64_t frame_count() const override { return frame_; }

    bool set_input(const std::string &name, int value) override {
        auto it = binds_.find(name);
        if (it == binds_.end()) return false;
        FireWire::SetP1IOBindValue(it->second, value ? 1.0f : 0.0f);
        return true;
    }
    int audio_rate() const override { return 48000; }
    void turntable_add(int, int) override {}
    const std::vector<uint8_t> &lamp_state() const override { return lamps_; }
    uint64_t lamp_changes() const override { return 0; }

    static PythonCore *self_;

private:
    bool init_emulator(const std::string &bios, const std::string &keys) {
        EmuFolders::SetAppRoot();
        if (!EmuFolders::SetResourcesDirectory()) { fprintf(stderr, "python: no emulator resources next to the binary\n"); return false; }
        // not EmuFolders::SetDataDirectory(): it would create the user's own ~/.config/PCSX2
        EmuFolders::DataRoot = run_dir_ + "/pcsx2";   // settings, game settings, the disk overlay
        EmuFolders::Settings = EmuFolders::DataRoot + "/inis";   // never the user's own PCSX2 settings
        mkdirs(EmuFolders::Settings);
        const char *why;
        if (!VMManager::PerformEarlyHardwareChecks(&why)) { fprintf(stderr, "python: %s\n", why); return false; }

        const std::string font = EmuFolders::GetOverridableResourcePath("fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf");
        font_data_ = FileSystem::MapBinaryFileForRead(font.c_str());
        if (!font_data_.empty()) {
            std::vector<ImGuiManager::FontInfo> fonts;
            ImGuiManager::FontInfo fi{};
            fi.data = font_data_;
            fonts.push_back(fi);
            ImGuiManager::SetFonts(std::move(fonts));
        }

        SettingsInterface &si = s_settings;
        Host::Internal::SetBaseSettingsLayer(&s_settings);
        VMManager::SetDefaultSettings(si, true, true, true, true, true);

        // As the oracle runs (oracle/pcsx2/run.py): pinned, repeatable
        si.SetStringValue("Folders", "Bios", std::string(Path::GetDirectory(bios)).c_str());
        si.SetStringValue("Filenames", "BIOS", base_name(bios).c_str());
        si.SetStringValue("Filenames", "ArcadeBIOS", base_name(bios).c_str());
        const std::pair<const char *, const char *> key_files[] = {
            {"MgChallengeIvFile", "civ"}, {"MgCardKeyStoreFile", "cks"}, {"MgEncryptedKeyStoreFile", "eks"},
            {"MgKeyStoreKeyFile", "kek"}, {"ArcadeKelfOverrideKbitFile", "arcade_kelf_override_kbit"},
            {"ArcadeKelfOverrideKcFile", "arcade_kelf_override_kc"}};
        for (const auto &[k, f] : key_files) si.SetStringValue("Security", k, (keys + "/" + f + ".bin").c_str());
        si.SetBoolValue("EmuCore", "EnablePatches", false);
        si.SetBoolValue("EmuCore", "EnableCheats", false);
        si.SetBoolValue("EmuCore", "EnablePINE", false);
        si.SetBoolValue("EmuCore", "ManuallySetRealTimeClock", true);
        si.SetIntValue("EmuCore", "RtcYear", 3);
        si.SetIntValue("EmuCore", "RtcMonth", 6);
        si.SetIntValue("EmuCore", "RtcDay", 1);
        si.SetIntValue("EmuCore", "RtcHour", 12);
        si.SetIntValue("EmuCore", "RtcMinute", 0);
        si.SetIntValue("EmuCore", "RtcSecond", 0);
        si.SetBoolValue("EmuCore/Speedhacks", "vuThread", false);
        for (const char *cpu : {"EnableEE", "EnableIOP", "EnableVU0", "EnableVU1"})
            si.SetBoolValue("EmuCore/CPU/Recompiler", cpu, !interp_);
        si.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(renderer_));
        si.SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
        si.SetIntValue("EmuCore/GS", "VsyncEnable", 0);
        si.SetBoolValue("EmuCore/GS", "OsdShowMessages", false);
        si.SetStringValue("SPU2/Output", "Backend", "Null");
        si.SetBoolValue("InputSources", "SDL", false);
        si.SetBoolValue("InputSources", "XInput", false);
        Pad::ClearPortBindings(si, 0);
        si.ClearSection("Hotkeys");
        si.ClearSection("FireWire");   // no host bindings: the shell sets the inputs
        si.SetBoolValue("MemoryCards", "Slot2_Enable", false);
        si.SetBoolValue("Logging", "EnableSystemConsole", pcsx2_log_);
        si.SetBoolValue("Logging", "EnableFileLogging", false);
        si.SetBoolValue("Logging", "EnableEEConsole", pcsx2_log_);
        si.SetBoolValue("Logging", "EnableIOPConsole", pcsx2_log_);

        VMManager::Internal::LoadStartupSettings();
        // The shell ends a run with exit() from inside a vsync. PCSX2's static destructors
        // then assert (the VM is still mapped), so leave before them with the same status;
        // the shell has already saved and reported everything by then.
        on_exit([](int status, void *) { fflush(nullptr); _exit(status); }, nullptr);
        if (!EmuFolders::EnsureFoldersExist()) { fprintf(stderr, "python: cannot create %s\n", EmuFolders::DataRoot.c_str()); return false; }
        self_ = this;
        return true;
    }

    // MAME input names -> the interface board's bind indices (Reliquary's names)
    void bind_inputs() {
        std::map<std::string, u32> by_name;
        for (const InputBindingInfo &b : FireWire::GetP1IOBindings()) by_name[b.name] = b.bind_index;
        for (const auto &[mame, p1io] : POPN_INPUTS)
            if (auto it = by_name.find(p1io); it != by_name.end()) binds_[mame] = it->second;
    }

    // GS thread: the picture shown at this vsync, as 0x00RRGGBB
    void on_gs_frame(const u32 *rgba, u32 w, u32 h) {
        std::lock_guard lock(gs_mutex_);
        if (rgba && w && h) {
            gs_pic_.resize((size_t)w * h);
            for (size_t k = 0; k < gs_pic_.size(); k++) {
                const u32 p = rgba[k];
                gs_pic_[k] = ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
            }
            gs_w_ = (int)w, gs_h_ = (int)h;
        } else {
            std::fill(gs_pic_.begin(), gs_pic_.end(), 0u);   // a blank frame: black, the last size
        }
        gs_done_++;
        gs_cv_.notify_all();
    }

    // CPU thread, at each vsync: sound so far, the previous vsync's picture, the shell
    void on_vsync() {
        frame_++;
        if (audio_sink_ && !audio_.empty()) audio_sink_(audio_.data(), (int)(audio_.size() / 2));
        audio_.clear();
        {
            std::unique_lock lock(gs_mutex_);
            if (frame_ >= 2 && gs_done_ < frame_ - 1) {
                const auto t0 = std::chrono::steady_clock::now();
                if (!gs_cv_.wait_for(lock, std::chrono::seconds(10), [&] { return gs_done_ >= frame_ - 1; }))
                    fprintf(stderr, "python: frame %llu: no picture from the GS after 10 s\n", (unsigned long long)frame_);
                gs_waits_++;
                gs_wait_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            }
            if (gs_pic_.empty()) gs_pic_.assign((size_t)gs_w_ * gs_h_, 0u);
            pic_ = gs_pic_;
            pic_w_ = gs_w_, pic_h_ = gs_h_;
        }
        if (frame_hook_) frame_hook_(frame_, pic_.data(), pic_w_, pic_h_);
    }

    [[noreturn]] void fatal(const std::string &why) {
        fprintf(stderr, "python: %s\n", why.c_str());
        if (on_fatal_) on_fatal_();
        _exit(1);
    }

    Options opt_;
    std::string family_, run_dir_, py1_path_;
    GSRendererType renderer_ = GSRendererType::SW;
    bool interp_ = false, pcsx2_log_ = false;
    std::span<const u8> font_data_;   // mapped for the process's life
    std::map<std::string, u32> binds_;
    std::vector<uint8_t> lamps_;
    FrameHook frame_hook_;
    AudioSink audio_sink_;
    std::function<void()> on_fatal_;
    uint64_t frame_ = 0;
    std::vector<float> audio_;

    std::mutex gs_mutex_;
    std::condition_variable gs_cv_;
    uint64_t gs_done_ = 0;                    // GS vsyncs presented
    std::vector<uint32_t> gs_pic_, pic_;
    int gs_w_ = 640, gs_h_ = 448, pic_w_ = 640, pic_h_ = 448;
    uint64_t gs_waits_ = 0;
    double gs_wait_ms_ = 0;
};

PythonCore *PythonCore::self_ = nullptr;
PythonCore g_core;

// Host::RunOnCPUThread work, run at the next vsync's message pump (CPU thread)
std::mutex s_cpu_queue_mutex;
std::deque<std::function<void()>> s_cpu_queue;
std::thread::id s_cpu_thread;

}  // namespace

hk::Core &hk::core() { return g_core; }

//////////////////////////////////////////////////////////////////////////
// PCSX2's Host interface: what its core asks of a front end. Mostly nothing here:
// no UI, no window of its own (the GS runs surfaceless), no host input.
//////////////////////////////////////////////////////////////////////////

void Host::CommitBaseSettingChanges() {}
void Host::LoadSettings(SettingsInterface &, std::unique_lock<std::mutex> &) {}
void Host::CheckForSettingsChanges(const Pcsx2Config &) {}
bool Host::RequestResetSettings(bool, bool, bool, bool, bool) { return false; }
void Host::SetDefaultUISettings(SettingsInterface &) {}
bool Host::LocaleCircleConfirm() { return false; }
std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback() { return ProgressCallback::CreateNullProgressCallback(); }
void Host::ReportInfoAsync(const std::string_view title, const std::string_view message) {
    fprintf(stderr, "python: %.*s: %.*s\n", (int)title.size(), title.data(), (int)message.size(), message.data());
}
void Host::ReportErrorAsync(const std::string_view title, const std::string_view message) {
    fprintf(stderr, "python: error: %.*s: %.*s\n", (int)title.size(), title.data(), (int)message.size(), message.data());
}
void Host::OpenURL(const std::string_view) {}
bool Host::CopyTextToClipboard(const std::string_view) { return false; }
std::string Host::GetTextFromClipboard() { return {}; }
void Host::BeginTextInput() {}
void Host::EndTextInput() {}
std::optional<WindowInfo> Host::GetTopLevelWindowInfo() {
    WindowInfo wi;
    wi.type = WindowInfo::Type::Surfaceless;
    return wi;
}
void Host::OnInputDeviceConnected(const std::string_view, const std::string_view) {}
void Host::OnInputDeviceDisconnected(const InputBindingKey, const std::string_view) {}
void Host::SetMouseMode(bool, bool) {}
void Host::SetMouseLock(bool) {}
std::optional<WindowInfo> Host::AcquireRenderWindow(bool) { return GetTopLevelWindowInfo(); }
void Host::ReleaseRenderWindow() {}
void Host::BeginPresentFrame() {}
void Host::RequestResizeHostDisplay(s32, s32) {}
void Host::OnVMStarting() {}
void Host::OnVMStarted() {}
void Host::OnVMDestroyed() {}
void Host::OnVMPaused() {}
void Host::OnVMResumed() {}
void Host::OnGameChanged(const std::string &, const std::string &, const std::string &, const std::string &, u32, u32) {}
void Host::OnPerformanceMetricsUpdated() {}
void Host::OnSaveStateLoading(const std::string_view) {}
void Host::OnSaveStateLoaded(const std::string_view, bool) {}
void Host::OnSaveStateSaved(const std::string_view) {}
void Host::RunOnCPUThread(std::function<void()> function, bool block) {
    if (std::this_thread::get_id() == s_cpu_thread || s_cpu_thread == std::thread::id()) {
        function();
        return;
    }
    std::lock_guard lock(s_cpu_queue_mutex);
    s_cpu_queue.push_back(std::move(function));
    (void)block;   // only the CPU thread drains the queue; a blocking caller would deadlock it
}
void Host::RefreshGameListAsync(bool) {}
void Host::CancelGameListRefresh() {}
bool Host::IsFullscreen() { return false; }
void Host::SetFullscreen(bool) {}
void Host::OnCaptureStarted(const std::string &) {}
void Host::OnCaptureStopped() {}
void Host::RequestExitApplication(bool) {}
void Host::RequestExitBigPicture() {}
void Host::RequestVMShutdown(bool, bool, bool) { VMManager::SetState(VMState::Stopping); }
void Host::OnAchievementsLoginSuccess(const char *, u32, u32, u32) {}
void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason) {}
void Host::OnAchievementsHardcoreModeChanged(bool) {}
void Host::OnAchievementsRefreshed() {}
bool Host::InBatchMode() { return true; }
bool Host::InNoGUIMode() { return true; }
bool Host::ShouldPreferHostFileSelector() { return false; }
void Host::OpenHostFileSelectorAsync(std::string_view, bool, FileSelectorCallback callback, FileSelectorFilters, std::string_view) {
    callback(std::string());
}
int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs) { return lhs.compare(rhs); }
std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view) { return std::nullopt; }
std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32) { return std::nullopt; }
const char *InputManager::ConvertHostKeyboardCodeToIcon(u32) { return nullptr; }
BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()

void Host::PumpMessagesOnCPUThread() {
    s_cpu_thread = std::this_thread::get_id();
    std::deque<std::function<void()>> work;
    {
        std::lock_guard lock(s_cpu_queue_mutex);
        work.swap(s_cpu_queue);
    }
    for (auto &f : work) f();
}

s32 Host::Internal::GetTranslatedStringImpl(const std::string_view, const std::string_view msg, char *tbuf, size_t tbuf_space) {
    if (msg.size() > tbuf_space) return -1;
    std::memcpy(tbuf, msg.data(), msg.size());
    return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char *, const char *msg, const char *, int count) {
    std::string ret(msg);
    const std::string n = std::to_string(count);
    for (size_t pos; (pos = ret.find("%n")) != std::string::npos;) ret.replace(pos, 2, n);
    return ret;
}
