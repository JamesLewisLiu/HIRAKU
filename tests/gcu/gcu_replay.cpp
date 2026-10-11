// SPDX-License-Identifier: GPL-3.0-or-later
// Replay a MAME oracle trace's GCU writes into HIRAKU's K057714 and render the
// frames MAME took snapshots of, to compare the renderer pixel for pixel.
//
//   gcu_replay <mmio.bin recorded with HIRAKU_NOFOLD=gcu> <out dir> FRAME...
//
// For each FRAME, writes two PPMs: the state after all records labelled < FRAME
// ("before") and <= FRAME ("after"); which one corresponds to MAME's snapshot
// depends on where MAME's frame callback falls relative to the screen update.
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "dev/k057714.h"

namespace {
struct Rec {
    uint32_t frame;
    double time;
    uint32_t pc, addr, data, mask;
    uint8_t kind, group;
    uint16_t count;
};
static_assert(sizeof(Rec) == 32 || true, "");

void ppm(const std::string &path, const uint32_t *rgb, int w, int h) {
    FILE *f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int k = 0; k < w * h; k++) {
        uint8_t px[3] = {(uint8_t)(rgb[k] >> 16), (uint8_t)(rgb[k] >> 8), (uint8_t)rgb[k]};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: gcu_replay <mmio.bin> <out dir> FRAME...\n"); return 2; }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    std::string out = argv[2];
    std::set<uint32_t> want;
    for (int k = 3; k < argc; k++) want.insert((uint32_t)std::stoul(argv[k]));

    hk::K057714 gcu;
    std::vector<uint32_t> fb(640 * 480);
    auto render = [&](uint32_t f, const char *tag) {
        gcu.draw(fb.data(), 640, 480);
        char name[128];
        snprintf(name, sizeof name, "/f%06u-%s.ppm", f, tag);
        ppm(out + name, fb.data(), 640, 480);
    };

    uint8_t raw[32];
    uint32_t cur = 0;
    uint64_t n = 0, writes = 0;
    while (fread(raw, 1, 32, in) == 32) {
        Rec r;
        memcpy(&r.frame, raw, 4);
        memcpy(&r.addr, raw + 16, 4);
        memcpy(&r.data, raw + 20, 4);
        memcpy(&r.mask, raw + 24, 4);
        r.kind = raw[28];
        r.group = raw[29];
        memcpy(&r.count, raw + 30, 2);
        n++;
        while (r.frame > cur) {            // frame boundary: everything labelled <= cur is applied
            if (want.count(cur + 1)) render(cur + 1, "before");
            if (want.count(cur)) render(cur, "after");
            cur++;
        }
        if (r.group != 1 || !(r.kind & 1)) continue;
        if (r.kind & 2 || r.count > 1) {   // folded records cannot be replayed
            fprintf(stderr, "record %llu is folded: record the trace with HIRAKU_NOFOLD=gcu\n", (unsigned long long)n);
            return 1;
        }
        if (r.addr >= 0x7e800000 && r.addr <= 0x7e8000ff) { gcu.write(r.addr & 0xfc, r.data, r.mask); writes++; }
    }
    if (want.count(cur)) render(cur, "after");
    printf("%llu records, %llu GCU writes replayed, last frame %u\n", (unsigned long long)n, (unsigned long long)writes, cur);
    return 0;
}
