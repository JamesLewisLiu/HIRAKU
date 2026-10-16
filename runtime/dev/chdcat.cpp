// SPDX-License-Identifier: GPL-3.0-or-later
// hiraku-chdcat: a CHD as HIRAKU reads it (runtime/dev/image.h), written out as chdman's
// extract would write it. For checking the reader against chdman, byte for byte.
//   hiraku-chdcat IMAGE.chd OUT        the image (a CD: the .bin; its cue sheet to OUT.cue)
#include "dev/image.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: hiraku-chdcat IMAGE.chd OUT\n");
        return 2;
    }
    std::string in = argv[1], out = argv[2];
    FILE *f = hk::image::fopen_image(in);
    if (!f) {
        fprintf(stderr, "cannot open %s\n", in.c_str());
        return 1;
    }
    printf("%s: %s, %lld bytes\n", in.c_str(), hk::image::chd_kind(in).c_str(), (long long)hk::image::chd_size(f));
    FILE *o = fopen(out.c_str(), "wb");
    std::vector<char> buf(1 << 20);
    uint64_t off = 0;
    for (;;) {
        ssize_t n = hk::image::pread(f, buf.data(), buf.size(), off);
        if (n <= 0) break;
        fwrite(buf.data(), 1, (size_t)n, o);
        off += (uint64_t)n;
    }
    fclose(o);
    fclose(f);
    std::string cue = hk::image::chd_cue_text(in);
    if (!cue.empty()) {
        FILE *c = fopen((out + ".cue").c_str(), "w");
        fwrite(cue.data(), 1, cue.size(), c);
        fclose(c);
    }
    return 0;
}
