// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/png.h"

#include <zlib.h>

#include <cstdlib>
#include <cstring>

namespace hk::png {

bool decode(const std::vector<uint8_t> &f, int &width, int &height, std::vector<uint8_t> &rgba, std::string &err) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (f.size() < 8 || memcmp(f.data(), sig, 8)) return err = "not a PNG", false;
    auto be32 = [&](size_t at) { return (uint32_t)f[at] << 24 | (uint32_t)f[at + 1] << 16 | (uint32_t)f[at + 2] << 8 | f[at + 3]; };
    uint32_t w = 0, h = 0;
    int depth = 0, type = -1, interlace = 0;
    std::vector<uint8_t> idat, plte, trns;
    for (size_t at = 8; at + 12 <= f.size();) {
        const uint32_t len = be32(at);
        if (at + 12 + (size_t)len > f.size()) return err = "a PNG cut short", false;
        const std::string kind((const char *)&f[at + 4], 4);
        const uint8_t *d = &f[at + 8];
        if (kind == "IHDR" && len >= 13) {
            w = be32(at + 8), h = be32(at + 12), depth = d[8], type = d[9], interlace = d[12];
        } else if (kind == "PLTE") {
            plte.assign(d, d + len);
        } else if (kind == "tRNS") {
            trns.assign(d, d + len);
        } else if (kind == "IDAT") {
            idat.insert(idat.end(), d, d + len);
        } else if (kind == "IEND") {
            break;
        }
        at += 12 + len;
    }
    if (!w || !h || w > 8192 || h > 8192) return err = "a PNG with no picture (or one over 8192 pixels a side)", false;
    if (interlace) return err = "an interlaced PNG (save it without interlacing)", false;
    const int channels = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
    if (!channels || (depth != 8 && depth != 16) || (type == 3 && depth != 8))
        return err = "a PNG of a kind HIRAKU doesn't read (8 or 16 bits a channel, or 8-bit palette)", false;
    const size_t bpp = (size_t)channels * depth / 8, stride = (size_t)w * bpp;
    std::vector<uint8_t> raw(((size_t)stride + 1) * h);
    uLongf n = (uLongf)raw.size();
    if (uncompress(raw.data(), &n, idat.data(), (uLong)idat.size()) != Z_OK || n != raw.size()) return err = "a PNG whose data doesn't unpack", false;
    // the rows' filters undone in place
    std::vector<uint8_t> prev(stride, 0);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = &raw[(size_t)y * (stride + 1)];
        const int filter = row[0];
        uint8_t *p = row + 1;
        for (size_t x = 0; x < stride; x++) {
            const int a = x >= bpp ? p[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
            int v = p[x];
            switch (filter) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: {
                const int pa = abs(b - c), pb = abs(a - c), pc = abs(a + b - 2 * c);
                v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                break;
            }
            default: return err = "a PNG with an unknown row filter", false;
            }
            p[x] = (uint8_t)v;
        }
        memcpy(prev.data(), p, stride);
    }
    rgba.resize((size_t)w * h * 4);
    const int step = depth / 8;   // 16 bits: the high byte of each
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *p = &raw[(size_t)y * (stride + 1) + 1];
        for (uint32_t x = 0; x < w; x++, p += bpp) {
            uint8_t *o = &rgba[((size_t)y * w + x) * 4];
            switch (type) {
            case 0: o[0] = o[1] = o[2] = p[0], o[3] = 255; break;
            case 4: o[0] = o[1] = o[2] = p[0], o[3] = p[step]; break;
            case 2: o[0] = p[0], o[1] = p[step], o[2] = p[2 * step], o[3] = 255; break;
            case 6: o[0] = p[0], o[1] = p[step], o[2] = p[2 * step], o[3] = p[3 * step]; break;
            case 3:
                if ((size_t)p[0] * 3 + 2 >= plte.size()) return err = "a PNG whose palette is short", false;
                o[0] = plte[p[0] * 3], o[1] = plte[p[0] * 3 + 1], o[2] = plte[p[0] * 3 + 2];
                o[3] = p[0] < trns.size() ? trns[p[0]] : 255;
                break;
            }
        }
    }
    width = (int)w, height = (int)h;
    return true;
}

}  // namespace hk::png
