// SPDX-License-Identifier: GPL-3.0-or-later
#include "hidout.h"

#include <SDL3/SDL.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

namespace hk::hidout {

namespace {

std::string narrow(const wchar_t *w) {
    std::string s;
    if (!w) return s;
    for (; *w; w++) s += *w < 0x80 ? (char)*w : '?';
    return s;
}

std::string make_id(uint16_t vid, uint16_t pid, int iface) {
    char b[32];
    if (iface >= 0) snprintf(b, sizeof b, "%04x:%04x:%d", vid, pid, iface);
    else snprintf(b, sizeof b, "%04x:%04x", vid, pid);
    return b;
}

bool read_layout(SDL_hid_device *dev, hid::OutputLayout &out) {
    unsigned char desc[4096];
    int n = SDL_hid_get_report_descriptor(dev, desc, sizeof desc);
    if (n <= 0) return false;
    out = hid::parse_outputs(desc, (size_t)n);
    return true;
}

// Linux: the descriptor is readable in sysfs without opening (or being allowed to open) the device
bool read_layout_sysfs(const std::string &path, hid::OutputLayout &out) {
    if (path.rfind("/dev/", 0) != 0) return false;
    std::ifstream in("/sys/class/hidraw/" + path.substr(5) + "/device/report_descriptor", std::ios::binary);
    if (!in) return false;
    std::string d((std::istreambuf_iterator<char>(in)), {});
    if (d.empty()) return false;
    out = hid::parse_outputs((const uint8_t *)d.data(), d.size());
    return true;
}

bool has_lights(const hid::OutputLayout &l) {
    for (size_t k = 0; k < l.fields.size(); k++)
        if (hid::light_like(l, k)) return true;
    return false;
}

}  // namespace

std::vector<Device> enumerate() {
    std::vector<Device> out;
    SDL_SetHint(SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS, "0");   // LED boards are not game controllers
    if (SDL_hid_init() != 0) return out;
    std::set<std::string> seen;
    SDL_hid_device_info *list = SDL_hid_enumerate(0, 0);
    for (auto *e = list; e; e = e->next) {
        if (!e->path || !seen.insert(e->path).second) continue;   // one entry per top-level collection
        Device d;
        d.path = e->path;
        d.vid = e->vendor_id;
        d.pid = e->product_id;
        d.interface = e->interface_number;
        d.id = make_id(d.vid, d.pid, d.interface);
        std::string man = narrow(e->manufacturer_string), prod = narrow(e->product_string);
        d.name = man.empty() ? prod : prod.empty() ? man : (prod.rfind(man, 0) == 0 ? prod : man + " " + prod);
        if (d.name.empty()) d.name = "HID device " + d.id;
        if (read_layout_sysfs(d.path, d.layout)) {
#ifndef _WIN32   // (sysfs: Linux's; MSVC has no unistd.h)
            if (access(d.path.c_str(), R_OK | W_OK) != 0) d.error = "no permission to open " + d.path;
#endif
        } else if (SDL_hid_device *dev = SDL_hid_open_path(e->path)) {
            if (!read_layout(dev, d.layout)) d.error = "cannot read its report descriptor";
            SDL_hid_close(dev);
        } else {
            continue;   // cannot tell whether it has outputs
        }
        if (!has_lights(d.layout)) continue;   // keyboards' lock LEDs, data channels: nothing to light
        out.push_back(std::move(d));
    }
    SDL_hid_free_enumeration(list);
    return out;
}

bool parse_spec(const std::string &s, Spec &out) {
    unsigned vid, pid;
    int iface, field;
    if (sscanf(s.c_str(), "hid:%x:%x:%d/%d", &vid, &pid, &iface, &field) == 4) {
        out = {(uint16_t)vid, (uint16_t)pid, iface, field};
        return true;
    }
    if (sscanf(s.c_str(), "hid:%x:%x/%d", &vid, &pid, &field) == 3) {
        out = {(uint16_t)vid, (uint16_t)pid, -1, field};
        return true;
    }
    return false;
}

std::string format_spec(const Device &d, int field) { return "hid:" + d.id + "/" + std::to_string(field); }

std::string field_label(const Device &d, int field) {
    if (field < 0 || field >= (int)d.layout.fields.size()) return std::to_string(field) + ": (missing)";
    auto &f = d.layout.fields[field];
    std::string s = std::to_string(field) + ": " + hid::usage_name(f.usage_page, f.usage);
    if (f.report_id) s += " (report " + std::to_string(f.report_id) + ")";
    if (f.bit_size > 1) s += " [" + std::to_string(f.logical_min) + ".." + std::to_string(f.logical_max) + "]";
    return s;
}

Writer::~Writer() { close(); }

Writer::Open *Writer::find(const Spec &s) {
    std::string key = make_id(s.vid, s.pid, s.interface);
    if (auto it = m_open.find(key); it != m_open.end()) return it->second.failed ? nullptr : &it->second;
    Open &o = m_open[key];
    o.failed = true;
    if (!m_enumerated) {
        m_devices = enumerate();
        m_enumerated = true;
    }
    for (auto &d : m_devices) {
        if (d.vid != s.vid || d.pid != s.pid || (s.interface >= 0 && d.interface != s.interface)) continue;
        if (!d.error.empty()) {
            fprintf(stderr, "lights: %s: %s\n", key.c_str(), d.error.c_str());
            return nullptr;
        }
        o.dev = SDL_hid_open_path(d.path.c_str());
        if (!o.dev) break;
        o.layout = d.layout;
        for (auto &[id, n] : o.layout.report_bytes) {
            o.reports[id].assign(n, 0);
            o.dirty[id] = true;   // the first flush sends every report: no stale LEDs from before
        }
        o.failed = false;
        fprintf(stderr, "lights: %s = %s (%zu outputs)\n", key.c_str(), d.name.c_str(), o.layout.fields.size());
        return &o;
    }
    fprintf(stderr, "lights: no HID device %s with outputs\n", key.c_str());
    return nullptr;
}

bool Writer::set(const Spec &s, bool on) {
    Open *o = find(s);
    if (!o) return false;
    if (s.field < 0 || s.field >= (int)o->layout.fields.size()) return false;
    auto &f = o->layout.fields[s.field];
    auto &data = o->reports[f.report_id];
    std::vector<uint8_t> before = data;
    hid::set_field(data, f, on ? hid::field_on(f) : hid::field_off(f));
    if (data != before) o->dirty[f.report_id] = true;
    return true;
}

void Writer::flush() {
    for (auto &[key, o] : m_open) {
        if (o.failed || !o.dev) continue;
        for (auto &[id, data] : o.reports) {
            if (!o.dirty[id]) continue;
            o.dirty[id] = false;
            std::vector<uint8_t> buf(1 + data.size());
            buf[0] = id;   // report ID, or 0 for devices without them
            memcpy(buf.data() + 1, data.data(), data.size());
            if (SDL_hid_write(o.dev, buf.data(), buf.size()) < 0) {
                fprintf(stderr, "lights: writing to %s failed: %s\n", key.c_str(), SDL_GetError());
                SDL_hid_close(o.dev);
                o.dev = nullptr;
                o.failed = true;
                break;
            }
        }
    }
}

void Writer::close() {
    for (auto &[key, o] : m_open)
        if (o.dev) SDL_hid_close(o.dev);
    m_open.clear();
}

}  // namespace hk::hidout
