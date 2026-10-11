// SPDX-License-Identifier: GPL-3.0-or-later
// USB HID report descriptors: just enough to find a device's outputs (the LEDs
// of lit arcade controllers) and to build the output reports that drive them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hk::hid {

// One variable output: a single LED or lamp, on or off (or a brightness).
struct OutputField {
    uint8_t report_id = 0;           // 0 = the device uses no report IDs
    uint32_t bit_offset = 0;         // in the report's data, after the ID byte
    uint32_t bit_size = 1;
    int32_t logical_min = 0, logical_max = 1;
    uint16_t usage_page = 0, usage = 0;
};

struct OutputLayout {
    std::vector<OutputField> fields;           // in descriptor order: a lights file names them by index
    std::map<uint8_t, uint32_t> report_bytes;  // output report ID -> data length in bytes (without the ID)
};

OutputLayout parse_outputs(const uint8_t *desc, size_t len);

// Could this output be a lamp? LED (other than a keyboard's lock LEDs), Button
// and Ordinal usages, and vendor-defined bits; not vendor byte arrays (data
// channels such as QMK's raw HID) or a vendor usage of 0.
bool light_like(const OutputLayout &l, size_t field);

// "LED: Generic Indicator", "Button 3", "Ordinal 5", "Usage 0xff00:0x0001"
std::string usage_name(uint16_t page, uint16_t usage);

// Put `value` into a field of a report's data (little-endian bit order, as HID)
void set_field(std::vector<uint8_t> &report_data, const OutputField &f, int32_t value);
inline int32_t field_on(const OutputField &f) { return f.logical_max > 0 ? f.logical_max : 1; }
inline int32_t field_off(const OutputField &f) { return f.logical_min <= 0 && f.logical_max >= 0 ? 0 : f.logical_min; }

}  // namespace hk::hid
