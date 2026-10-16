// SPDX-License-Identifier: GPL-3.0-or-later
// hiraku-chdcat: a CHD as HIRAKU reads it (runtime/dev/image.h), written out as chdman's
// extract would write it. For checking the reader against chdman, byte for byte, and for
// tools/chd.py where there's no chdman (a release: PLAN.md section 26).
//   hiraku-chdcat IMAGE.chd OUT        the image (a CD: the .bin; its cue sheet to OUT.cue);
//                                      progress on stderr as chdman's ("N% complete")
//   hiraku-chdcat --info IMAGE.chd     what it is, one fact a line:
//                                      kind cd|dvd|hd|raw, logical BYTES (the CHD's), size BYTES
//                                      (as extracted), sha1 HEX, track TYPE FRAMES (a CD's, as
//                                      chdman info names them), geometry C H S (a hard disk's)
#include <libchdr/chd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dev/image.h"

namespace {

int info(const std::string &in) {
    chd_file *c = nullptr;
    if (chd_open(in.c_str(), CHD_OPEN_READ, nullptr, &c) != CHDERR_NONE) {
        fprintf(stderr, "cannot open %s as a CHD\n", in.c_str());
        return 1;
    }
    const chd_header *h = chd_get_header(c);
    char meta[256];
    uint32_t len = 0;
    auto get = [&](uint32_t tag, uint32_t i) {
        memset(meta, 0, sizeof meta);
        return chd_get_metadata(c, tag, i, meta, sizeof meta - 1, &len, nullptr, nullptr) == CHDERR_NONE;
    };
    std::string kind = "raw";
    std::vector<std::pair<std::string, int>> tracks;
    for (uint32_t i = 0;; i++) {
        int n = 0, frames = 0, pregap = 0, postgap = 0;
        char type[32] = {}, sub[32] = {}, pgtype[32] = {}, pgsub[32] = {};
        if (get(CDROM_TRACK_METADATA2_TAG, i)) {
            if (sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &n, type, sub, &frames, &pregap, pgtype, pgsub, &postgap) != 8) break;
        } else if (get(CDROM_TRACK_METADATA_TAG, i)) {
            if (sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &n, type, sub, &frames) != 4) break;
        } else {
            break;
        }
        tracks.push_back({type, frames});
    }
    int cyls = 0, heads = 0, secs = 0, bps = 0;
    if (!tracks.empty()) kind = "cd";
    else if (get(CHD_MAKE_TAG('D', 'V', 'D', ' '), 0)) kind = "dvd";
    else if (get(HARD_DISK_METADATA_TAG, 0)) {
        kind = "hd";
        sscanf(meta, HARD_DISK_METADATA_FORMAT, &cyls, &heads, &secs, &bps);
    }
    printf("kind %s\nlogical %llu\n", kind.c_str(), (unsigned long long)h->logicalbytes);
    if (FILE *f = hk::image::fopen_image(in)) {
        printf("size %lld\n", (long long)hk::image::size(f));
        hk::image::close(f);
    }
    printf("sha1 ");
    for (int k = 0; k < 20; k++) printf("%02x", h->sha1[k]);
    printf("\n");
    for (auto &[type, frames] : tracks) printf("track %s %d\n", type.c_str(), frames);
    if (cyls) printf("geometry %d %d %d\n", cyls, heads, secs);
    chd_close(c);
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--info") return info(argv[2]);
    if (argc != 3) {
        fprintf(stderr, "usage: hiraku-chdcat IMAGE.chd OUT\n       hiraku-chdcat --info IMAGE.chd\n");
        return 2;
    }
    std::string in = argv[1], out = argv[2];
    FILE *f = hk::image::fopen_image(in);
    if (!f) {
        fprintf(stderr, "cannot open %s\n", in.c_str());
        return 1;
    }
    const int64_t total = hk::image::size(f);
    printf("%s: %s, %lld bytes\n", in.c_str(), hk::image::chd_kind(in).c_str(), (long long)total);
    fflush(stdout);
    FILE *o = fopen(out.c_str(), "wb");
    if (!o) {
        fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    std::vector<char> buf(1 << 20);
    uint64_t off = 0;
    int shown = -1;
    for (;;) {
        ssize_t n = hk::image::pread(f, buf.data(), buf.size(), off);
        if (n < 0) {
            fprintf(stderr, "%s: read error at %llu\n", in.c_str(), (unsigned long long)off);
            return 1;
        }
        if (n == 0) break;
        if (fwrite(buf.data(), 1, (size_t)n, o) != (size_t)n) {
            fprintf(stderr, "cannot write %s (disk full?)\n", out.c_str());
            return 1;
        }
        off += (uint64_t)n;
        const int pct = total > 0 ? (int)(off * 100 / (uint64_t)total) : 0;
        if (pct != shown) {
            fprintf(stderr, "Extracting, %d%% complete...\n", pct);
            shown = pct;
        }
    }
    if (fclose(o) != 0) return 1;
    hk::image::close(f);
    fprintf(stderr, "Extraction complete\n");
    std::string cue = hk::image::chd_cue_text(in);
    if (!cue.empty()) {
        FILE *c = fopen((out + ".cue").c_str(), "wb");
        fwrite(cue.data(), 1, cue.size(), c);
        fclose(c);
    }
    return 0;
}
