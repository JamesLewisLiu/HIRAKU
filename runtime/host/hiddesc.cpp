// SPDX-License-Identifier: GPL-3.0-or-later
#include "hiddesc.h"

#include <algorithm>
#include <cstdio>

namespace hk::hid {

namespace {

struct Globals {
    uint16_t usage_page = 0;
    int32_t logical_min = 0, logical_max = 0;
    uint32_t report_size = 0, report_count = 0;
    uint8_t report_id = 0;
};

int32_t sign_extend(uint32_t v, int bytes) {
    if (bytes == 1) return (int8_t)v;
    if (bytes == 2) return (int16_t)v;
    return (int32_t)v;
}

}  // namespace

OutputLayout parse_outputs(const uint8_t *d, size_t len) {
    OutputLayout out;
    Globals g;
    std::vector<Globals> stack;
    std::vector<uint32_t> usages;            // local: Usage items (with the page in bits 16-31 when 4 bytes)
    uint32_t usage_min = 0, usage_max = 0;
    bool have_range = false;
    std::map<uint8_t, uint32_t> bits;        // output report ID -> bits so far
    uint32_t raw_min = 0, raw_max = 0;

    size_t i = 0;
    while (i < len) {
        uint8_t prefix = d[i];
        if (prefix == 0xfe) {                // long item: skip
            if (i + 2 >= len) break;
            i += 3 + d[i + 1];
            continue;
        }
        int size = prefix & 3;
        if (size == 3) size = 4;
        int type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (i + 1 + size > len) break;
        uint32_t v = 0;
        for (int k = 0; k < size; k++) v |= (uint32_t)d[i + 1 + k] << (8 * k);
        i += 1 + size;

        if (type == 1) {                     // global
            switch (tag) {
            case 0: g.usage_page = (uint16_t)v; break;
            case 1: raw_min = v; g.logical_min = sign_extend(v, size); break;
            case 2: raw_max = v; g.logical_max = sign_extend(v, size); break;
            case 7: g.report_size = v; break;
            case 8: g.report_id = (uint8_t)v; break;
            case 9: g.report_count = v; break;
            case 10: stack.push_back(g); break;
            case 11: if (!stack.empty()) { g = stack.back(); stack.pop_back(); } break;
            default: break;
            }
            // A common descriptor bug: a logical maximum of 255 written as one byte (0xff = -1).
            if ((tag == 1 || tag == 2) && g.logical_max < g.logical_min) {
                g.logical_min = (int32_t)raw_min;
                g.logical_max = (int32_t)raw_max;
            }
        } else if (type == 2) {              // local
            uint32_t full = size == 4 ? v : ((uint32_t)g.usage_page << 16) | v;
            switch (tag) {
            case 0: usages.push_back(full); break;
            case 1: usage_min = full; have_range = true; break;
            case 2: usage_max = full; have_range = true; break;
            default: break;
            }
        } else if (type == 0) {              // main
            if (tag == 9) {                  // Output
                uint32_t &at = bits[g.report_id];
                bool constant = v & 1, variable = v & 2;
                for (uint32_t k = 0; k < g.report_count; k++) {
                    if (!constant && variable) {
                        uint32_t u = 0;
                        if (!usages.empty()) u = usages[std::min<size_t>(k, usages.size() - 1)];
                        else if (have_range) u = std::min(usage_min + k, usage_max);
                        OutputField f;
                        f.report_id = g.report_id;
                        f.bit_offset = at;
                        f.bit_size = g.report_size;
                        f.logical_min = g.logical_min;
                        f.logical_max = g.logical_max;
                        f.usage_page = (uint16_t)(u >> 16);
                        f.usage = (uint16_t)u;
                        if (f.bit_size > 0 && f.bit_size <= 32) out.fields.push_back(f);
                    }
                    at += g.report_size;
                }
            }
            usages.clear();                  // every main item ends the local state
            have_range = false;
            usage_min = usage_max = 0;
        }
    }
    for (auto &[id, n] : bits) out.report_bytes[id] = (n + 7) / 8;
    return out;
}

bool light_like(const OutputLayout &l, size_t k) {
    const OutputField &f = l.fields[k];
    if (f.usage_page == 0x08) return f.usage > 0x05;
    if (f.usage_page == 0x09 || f.usage_page == 0x0a) return true;
    if (f.usage_page < 0xff00 || f.usage == 0) return false;
    if (f.bit_size < 8) return true;
    int same = 0;   // a run of bytes with one usage is a data channel
    for (auto &g : l.fields)
        if (g.report_id == f.report_id && g.usage_page == f.usage_page && g.usage == f.usage && g.bit_size == f.bit_size) same++;
    return same < 8;
}

std::string usage_name(uint16_t page, uint16_t usage) {
    char b[64];
    if (page == 0x08) {
        static const struct { uint16_t u; const char *n; } led[] = {
            {0x01, "Num Lock"}, {0x02, "Caps Lock"}, {0x03, "Scroll Lock"}, {0x04, "Compose"}, {0x05, "Kana"},
            {0x4b, "Generic Indicator"}, {0x3c, "Usage Indicator Color"}, {0x47, "Red"}, {0x48, "Green"}, {0x49, "Amber"}};
        for (auto &e : led)
            if (e.u == usage) return std::string("LED: ") + e.n;
        snprintf(b, sizeof b, "LED 0x%02x", usage);
        return b;
    }
    if (page == 0x09) { snprintf(b, sizeof b, "Button %u", usage); return b; }
    if (page == 0x0a) { snprintf(b, sizeof b, "Ordinal %u", usage); return b; }
    snprintf(b, sizeof b, "Usage 0x%04x:0x%04x", page, usage);
    return b;
}

void set_field(std::vector<uint8_t> &data, const OutputField &f, int32_t value) {
    uint32_t v = (uint32_t)value;
    for (uint32_t k = 0; k < f.bit_size; k++) {
        uint32_t bit = f.bit_offset + k, byte = bit / 8;
        if (byte >= data.size()) return;
        uint8_t m = (uint8_t)(1u << (bit % 8));
        if ((v >> k) & 1) data[byte] |= m;
        else data[byte] &= (uint8_t)~m;
    }
}

}  // namespace hk::hid
