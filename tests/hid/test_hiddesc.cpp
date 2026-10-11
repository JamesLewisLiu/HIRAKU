// SPDX-License-Identifier: GPL-3.0-or-later
// HID output parsing and report building (runtime/host/hiddesc.cpp), on
// hand-written descriptors shaped like real devices.
#include <cstdio>
#include <vector>

#include "host/hiddesc.h"

using namespace hk::hid;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

int main() {
    // A lit 9-button controller: report 1 = buttons in; report 2 = nine lamps
    // (LED page, Ordinal 1-9, one bit each) + 7 bits padding + an RGB value (vendor, 3 bytes);
    // logical maximum 255 written as one byte (0xff), as many devices do.
    const std::vector<uint8_t> lit = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,                    // Generic Desktop, Game Pad, Collection (Application)
        0x85, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x09,        //   Report ID 1, Button 1-9
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x09, 0x81, 0x02,   // Input (Data, Var)
        0x95, 0x07, 0x81, 0x03,                                //   Input padding
        0x85, 0x02, 0x05, 0x08, 0x09, 0x4b, 0xa1, 0x02,        //   Report ID 2, LED page, Generic Indicator, Collection (Logical)
        0x05, 0x0a, 0x19, 0x01, 0x29, 0x09,                    //     Ordinal 1-9
        0x95, 0x09, 0x91, 0x02,                                //     Output (Data, Var): 9 x 1 bit
        0x95, 0x07, 0x91, 0x03,                                //     Output (Const): padding
        0x06, 0x00, 0xff, 0x09, 0x20, 0x15, 0x00, 0x25, 0xff,  //     Vendor 0xff00, usage 0x20, 0..255 (one byte!)
        0x75, 0x08, 0x95, 0x03, 0x91, 0x02,                    //     Output: 3 x 8 bits
        0xc0, 0xc0};
    OutputLayout l = parse_outputs(lit.data(), lit.size());
    CHECK(l.fields.size() == 12);
    CHECK(l.report_bytes.size() == 1 && l.report_bytes[2] == 5);
    if (l.fields.size() == 12) {
        CHECK(l.fields[0].report_id == 2 && l.fields[0].bit_offset == 0 && l.fields[0].bit_size == 1);
        CHECK(l.fields[0].usage_page == 0x0a && l.fields[0].usage == 1);
        CHECK(l.fields[8].usage == 9 && l.fields[8].bit_offset == 8);
        CHECK(l.fields[9].bit_offset == 16 && l.fields[9].bit_size == 8 && l.fields[9].usage_page == 0xff00);
        CHECK(l.fields[9].logical_min == 0 && l.fields[9].logical_max == 255);
        for (size_t k = 0; k < 12; k++) CHECK(light_like(l, k));

        std::vector<uint8_t> data(5, 0);
        set_field(data, l.fields[0], field_on(l.fields[0]));
        set_field(data, l.fields[8], field_on(l.fields[8]));
        set_field(data, l.fields[10], field_on(l.fields[10]));
        CHECK(data[0] == 0x01 && data[1] == 0x01 && data[2] == 0x00 && data[3] == 0xff && data[4] == 0x00);
        set_field(data, l.fields[8], field_off(l.fields[8]));
        CHECK(data[1] == 0x00);
        CHECK(usage_name(0x0a, 3) == "Ordinal 3");
    }

    // A boot keyboard: its lock LEDs are outputs, but not lamps
    const std::vector<uint8_t> kbd = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x95, 0x05, 0x91, 0x02, 0x95, 0x03, 0x91, 0x03, 0xc0};
    OutputLayout k = parse_outputs(kbd.data(), kbd.size());
    CHECK(k.fields.size() == 5 && k.report_bytes[0] == 1);
    for (size_t i = 0; i < k.fields.size(); i++) CHECK(!light_like(k, i));

    // A vendor data channel (QMK raw HID): 32 bytes of one usage
    const std::vector<uint8_t> raw = {
        0x06, 0x60, 0xff, 0x09, 0x61, 0xa1, 0x01, 0x09, 0x63, 0x15, 0x00, 0x26, 0xff, 0x00,
        0x95, 0x20, 0x75, 0x08, 0x91, 0x02, 0xc0};
    OutputLayout r = parse_outputs(raw.data(), raw.size());
    CHECK(r.fields.size() == 32 && r.report_bytes[0] == 32);
    CHECK(!light_like(r, 0));

    if (g_fail) return 1;
    printf("hiddesc: all checks passed\n");
    return 0;
}
