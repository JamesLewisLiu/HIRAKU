// SPDX-License-Identifier: GPL-3.0-or-later
// HID devices with outputs (lit arcade controllers), through SDL's hidapi.
// A lights file names an output as
//     hid:<vid>:<pid>[:<interface>]/<n>
// with the IDs in hex and n the device's n-th output field (hiddesc.h), e.g.
// hid:1ccf:8048:0/3. Shared by the runtime and the launcher.
//
// On Linux the device's /dev/hidraw* node must be writable by the user; see
// docs/LIGHTS.md for a udev rule.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "hiddesc.h"

struct SDL_hid_device;

namespace hk::hidout {

struct Device {
    std::string path;              // the OS's path (/dev/hidrawN)
    uint16_t vid = 0, pid = 0;
    int interface = -1;
    std::string name;              // manufacturer and product strings
    std::string id;                // "1ccf:8048:0": what lights files use
    std::string error;             // why it could not be opened ("" = fine)
    hid::OutputLayout layout;
};

// Every HID device with outputs that look like lamps (hid::light_like), whether or
// not this user may open it (Device::error says).
std::vector<Device> enumerate();

struct Spec {
    uint16_t vid = 0, pid = 0;
    int interface = -1;            // -1 = any
    int field = 0;
};
bool parse_spec(const std::string &s, Spec &out);
std::string format_spec(const Device &d, int field);
std::string field_label(const Device &d, int field);   // "3: LED: Generic Indicator (report 2)"

// Drives outputs, opening devices on first use. Not thread-safe: one owner thread.
class Writer {
public:
    ~Writer();
    // Set one output; false when its device or field is missing (reported once on stderr)
    bool set(const Spec &s, bool on);
    void flush();                  // send the reports that changed
    void close();
private:
    struct Open {
        SDL_hid_device *dev = nullptr;
        hid::OutputLayout layout;
        std::map<uint8_t, std::vector<uint8_t>> reports;
        std::map<uint8_t, bool> dirty;
        bool failed = false;
    };
    Open *find(const Spec &s);
    std::map<std::string, Open> m_open;   // key: "vid:pid:interface"
    std::vector<Device> m_devices;
    bool m_enumerated = false;
};

}  // namespace hk::hidout
