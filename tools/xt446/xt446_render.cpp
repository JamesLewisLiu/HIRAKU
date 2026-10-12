// SPDX-License-Identifier: GPL-3.0-or-later
// hiraku-xt446: run the XT446 synth card on its own and record what it plays.
//
//   hiraku-xt446 ROMDIR OUT.wav [--seconds S] [--midi LOG] [--demo]
//
// ROMDIR holds the MAME set's XT446 files (e.g. dumps/kbm/rom). --midi plays a
// timed byte log, one "<seconds> <hex byte>" per line (as HK_MIDI_LOG=time
// writes it); --demo plays a short scale on channel 1 after an XG System On.
// Prints how fast the card runs (emulated seconds per host second).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "dev/xt446.h"

namespace {

void put16(std::FILE *f, uint16_t v) { std::fputc(v & 0xff, f); std::fputc(v >> 8, f); }
void put32(std::FILE *f, uint32_t v) { put16(f, v & 0xffff); put16(f, v >> 16); }

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s ROMDIR OUT.wav [--seconds S] [--midi LOG] [--demo]\n", argv[0]);
        return 2;
    }
    std::string romdir = argv[1], wav = argv[2], midi_log;
    double seconds = 10;
    bool demo = false;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--midi") && i + 1 < argc) midi_log = argv[++i];
        else if (!std::strcmp(argv[i], "--demo")) demo = true;
    }

    // the byte schedule
    std::vector<std::pair<double, uint8_t>> events;
    if (!midi_log.empty()) {
        std::ifstream f(midi_log);
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ls(line);
            double t;
            unsigned b;
            if (ls >> t >> std::hex >> b) events.emplace_back(t, (uint8_t)b);
        }
    }
    if (demo) {
        double t = 3.0;
        for (uint8_t b : {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7}) events.emplace_back(t, b);   // XG System On
        t += 0.5;
        events.emplace_back(t, 0xc0), events.emplace_back(t, 0x00);   // piano
        for (int k = 0; k < 8; k++, t += 0.4) {
            static const int scale[8] = {60, 62, 64, 65, 67, 69, 71, 72};
            events.emplace_back(t, 0x90), events.emplace_back(t, scale[k]), events.emplace_back(t, 100);
            events.emplace_back(t + 0.35, 0x80), events.emplace_back(t + 0.35, scale[k]), events.emplace_back(t + 0.35, 0);
        }
        std::stable_sort(events.begin(), events.end(), [](auto &a, auto &b) { return a.first < b.first; });
    }

    hk::Xt446 card;
    std::string err;
    if (!card.load(romdir, err)) {
        std::fprintf(stderr, "xt446: %s\n", err.c_str());
        return 1;
    }

    const int rate = 44100;
    const long total = (long)(seconds * rate);
    std::vector<int16_t> pcm;
    pcm.reserve((size_t)total * 2);
    std::vector<float> buf;
    size_t ev = 0;
    double peak = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (long n = 0; n < total;) {
        while (ev < events.size() && events[ev].first * rate <= n) card.midi(events[ev++].second);
        long chunk = 64;
        if (ev < events.size()) chunk = std::min<long>(chunk, std::max<long>(1, (long)(events[ev].first * rate) - n));
        chunk = std::min(chunk, total - n);
        buf.assign((size_t)chunk * 2, 0.0f);
        card.render(buf.data(), (int)chunk, 1.0f);
        for (float v : buf) {
            peak = std::max(peak, (double)std::abs(v));
            pcm.push_back((int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
        }
        n += chunk;
        if (n % (rate * 5) < chunk) {
            double host = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::fprintf(stderr, "  %5.1f s emulated, %5.2fx real time, peak %.3f\n", (double)n / rate, (double)n / rate / host, peak);
        }
    }
    double host = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "xt446: %.1f s in %.2f s (%.2fx real time), %llu MIDI bytes, peak %.3f\n", seconds, host, seconds / host,
                 (unsigned long long)card.midi_bytes(), peak);

    std::FILE *f = std::fopen(wav.c_str(), "wb");
    if (!f) { std::perror(wav.c_str()); return 1; }
    uint32_t data = (uint32_t)(pcm.size() * 2);
    std::fwrite("RIFF", 1, 4, f); put32(f, 36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, rate); put32(f, rate * 4); put16(f, 4); put16(f, 16);
    std::fwrite("data", 1, 4, f); put32(f, data);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    return 0;
}
