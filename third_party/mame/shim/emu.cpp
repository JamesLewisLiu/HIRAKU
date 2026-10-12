// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: the MAME framework subset declared in emu.h.
#include "emu.h"

bool device_t::s_log = getenv("HK_XT446_LOG") != nullptr;

// ---- formatting -----------------------------------------------------------------------
namespace hk_mame {

std::string format_args(const char *fmt, const std::vector<fmt_arg> &args) {
    std::string out;
    size_t ai = 0;
    char buf[512];
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { out += *p; continue; }
        if (p[1] == '%') { out += '%'; p++; continue; }
        // %[flags][width][.prec][length]conv
        std::string spec = "%";
        p++;
        while (*p && strchr("-+ #0", *p)) spec += *p++;
        while (*p && (isdigit((unsigned char)*p) || *p == '*')) spec += *p++;
        if (*p == '.') { spec += *p++; while (*p && isdigit((unsigned char)*p)) spec += *p++; }
        while (*p && strchr("hlLqjzt", *p)) p++;   // lengths are implied by the argument
        char conv = *p;
        if (!conv) break;
        if (ai >= args.size()) { out += "<?>"; continue; }
        const fmt_arg &a = args[ai++];
        switch (conv) {
        case 'd': case 'i':
            snprintf(buf, sizeof buf, (spec + "lld").c_str(), a.kind == fmt_arg::DBL ? (long long)a.d : a.i);
            break;
        case 'u': case 'x': case 'X': case 'o':
            snprintf(buf, sizeof buf, (spec + "ll" + conv).c_str(), a.kind == fmt_arg::DBL ? (unsigned long long)a.d : a.u);
            break;
        case 'c':
            snprintf(buf, sizeof buf, (spec + "c").c_str(), (int)a.i);
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            snprintf(buf, sizeof buf, (spec + conv).c_str(), a.kind == fmt_arg::DBL ? a.d : (double)a.i);
            break;
        case 's':
            if (a.kind == fmt_arg::STR) snprintf(buf, sizeof buf, (spec + "s").c_str(), a.s.c_str());
            else snprintf(buf, sizeof buf, (spec + "lld").c_str(), a.i);
            break;
        case 'p':
            snprintf(buf, sizeof buf, "%p", a.p);
            break;
        default:
            snprintf(buf, sizeof buf, "<%%%c?>", conv);
            break;
        }
        out += buf;
    }
    return out;
}

}  // namespace hk_mame

void fatalerror(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "xt446: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    abort();
}

std::string attotime::as_string(int precision) const {
    if (is_never()) return "(never)";
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", precision, as_double());
    return buf;
}

// ---- devices ----------------------------------------------------------------------------
device_t::device_t(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
    : m_type(type), m_basetag(tag ? tag : ""), m_owner(owner), m_clock(clock), m_machine(mconfig.m_machine), m_mconfig(&mconfig) {
    m_tag = owner ? std::string(owner->tag()) + ":" + m_basetag : m_basetag;
}

void device_t::hk_log(const char *s) const {
    if (!s_log) return;
    fprintf(stderr, "[%s] %s %s", machine().time().as_string(6).c_str(), tag(), s);
}

void hk_register_device(machine_config &config, device_t *dev) {
    running_machine &m = *config.m_machine;
    m.m_devices.push_back(dev);
    if (dev->owner()) dev->owner()->m_children.push_back(dev);
    if (auto *cpu = dynamic_cast<cpu_device *>(dev)) m.m_cpus.push_back(cpu);
    device_t *saved = config.m_owner;
    config.m_owner = dev;
    dev->device_add_mconfig(config);
    config.m_owner = saved;
}

device_t *hk_owner_of(device_t *dev) { return dev->owner(); }

device_t *hk_find_device(device_t *owner, const char *tag) {
    std::string full = owner->subtag(tag);
    for (device_t *d : owner->machine().m_devices)
        if (full == d->tag()) return d;
    return nullptr;
}

void device_t::reset() {
    device_reset();
    for (device_t *c : m_children) c->reset();
    device_reset_after_children();
}

memory_region *hk_find_region(device_t &dev, const char *tag) {
    // regions by base tag (the XT446 has one of each name)
    return dev.machine().find_region(tag);
}

void device_t::set_addrmap(int spacenum, address_map_constructor map) {
    m_external_maps.emplace_back(spacenum, map);
}

bool device_t::has_space(int spacenum) const {
    for (auto &[n, c] : memory_space_config())
        if (n == spacenum && c) return true;
    return false;
}

address_space &device_t::space(int spacenum) {
    for (auto &s : m_spaces)
        if (s->m_spacenum == spacenum) return *s;
    const address_space_config *cfg = nullptr;
    for (auto &[n, c] : memory_space_config())
        if (n == spacenum) cfg = c;
    if (!cfg) fatalerror("%s: no address space %d", tag(), spacenum);
    auto sp = std::make_unique<address_space>(*this, *cfg, spacenum);
    for (auto &[n, ctor] : m_external_maps) {
        if (n != spacenum) continue;
        auto map = std::make_unique<address_map>(*this);
        ctor(*map);
        sp->install(std::move(map));
    }
    if (cfg->m_internal_map) {
        auto map = std::make_unique<address_map>(*this);
        cfg->m_internal_map(*map);
        sp->install(std::move(map));
    }
    sp->finalize();
    m_spaces.push_back(std::move(sp));
    return *m_spaces.back();
}

// ---- address spaces ----------------------------------------------------------------------
address_space::address_space(device_t &dev, const address_space_config &cfg, int spacenum)
    : m_config(cfg), m_device(dev), m_spacenum(spacenum) {
    m_bus_bytes = cfg.m_data_width / 8;
    m_shift = cfg.m_addr_shift;
    m_big = cfg.m_endianness == ENDIANNESS_BIG;
    int bits = cfg.m_addr_width + (m_shift < 0 ? -m_shift : 0);
    m_byte_mask = bits >= 64 ? ~u64(0) : (u64(1) << bits) - 1;
    m_pbits = std::max(8, bits - 16);
    m_pages.resize(size_t(1) << std::max(0, bits - m_pbits));
}

void address_space::collect(address_map &map, offs_t base, offs_t mirror) {
    for (auto &e : map.m_entries) {
        if (e->m_rkind == address_map_entry::SUBMAP) {
            auto sub = std::make_unique<address_map>(*e->m_subdev);
            e->m_submap(*sub);
            collect(*sub, base + e->m_start, mirror | e->m_mirror);
            m_maps.push_back(std::move(sub));
            continue;
        }
        // map addresses are in the space's units (bus words when the shift is negative): to bytes
        if (m_shift < 0) {
            int sh = -m_shift;
            e->m_start <<= sh;
            e->m_end = ((e->m_end + 1) << sh) - 1;
            e->m_mirror <<= sh;
            e->m_select <<= sh;
        }
        e->m_start += base;
        e->m_end += base;
        e->m_mirror |= mirror;
        m_all.push_back(e.get());
    }
}

void address_space::install(std::unique_ptr<address_map> map) {
    collect(*map, 0, 0);
    m_maps.push_back(std::move(map));   // the entries live as long as the space
}

void address_space::finalize() {
    const u64 psize = u64(1) << m_pbits;
    for (address_map_entry *e : m_all) {
        // backing memory
        u8 *mem = nullptr;
        if (e->m_rkind == address_map_entry::MEM || e->m_wkind == address_map_entry::MEM) {
            u64 len = (u64)e->m_end - e->m_start + 1;
            if (e->m_has_region) {
                memory_region *r = m_device.machine().find_region(e->m_region);
                if (!r) fatalerror("%s: region %s missing", m_device.tag(), e->m_region.c_str());
                if (e->m_region_offset + len > r->bytes()) fatalerror("%s: region %s too small (%x-%x + %x, %x bytes)", m_device.tag(), e->m_region.c_str(), e->m_start, e->m_end, e->m_region_offset, r->bytes());
                mem = r->base() + e->m_region_offset;
            } else {
                m_owned.push_back(std::make_unique<std::vector<u8>>(len + 8, 0));
                mem = m_owned.back()->data();
            }
        }
        m_rt.push_back({e, mem, e->m_start});
        const u16 id = (u16)m_rt.size();
        const offs_t extra = e->m_mirror | e->m_select;
        // every combination of the mirror and select bits
        offs_t sub = 0;
        do {
            u64 lo = (u64)(e->m_start | sub) & m_byte_mask, hi = (u64)(e->m_end | sub) & m_byte_mask;
            for (int side = 0; side < 2; side++) {
                address_map_entry::kind_t k = side ? e->m_wkind : e->m_rkind;
                if (k == address_map_entry::NONE) continue;
                u64 a = lo;
                while (a <= hi) {
                    page_t &p = m_pages[a >> m_pbits];
                    u64 pbase = a & ~(psize - 1);
                    bool whole = a == pbase && hi >= pbase + psize - 1;
                    if (whole && k == address_map_entry::MEM && !(extra & (psize - 1)) && e->m_umask == ~u64(0)) {
                        u8 *host = mem + ((pbase & ~(u64)extra) - e->m_start);
                        (side ? p.direct_w : p.direct_r) = host;
                        (side ? p.did_w : p.did_r) = id;
                        (side ? p.widx : p.ridx).reset();
                        a += psize;
                        continue;
                    }
                    auto &idx = side ? p.widx : p.ridx;
                    u8 *&direct = side ? p.direct_w : p.direct_r;
                    u16 &did = side ? p.did_w : p.did_r;
                    if (!idx) {
                        idx.reset(new u16[psize]());
                        if (direct) std::fill_n(idx.get(), psize, did);   // the page was one entry's: per byte now
                        direct = nullptr;
                        did = 0;
                    }
                    u64 end = std::min(hi, pbase + psize - 1);
                    for (; a <= end; a++) {
                        if (e->m_umask != ~u64(0)) {
                            u64 lm = u64(0xff) << lane_shift(a, 1);
                            if (!(e->m_umask & lm)) continue;
                        }
                        idx[a - pbase] = id;
                    }
                }
            }
            sub = (sub - extra) & extra;
        } while (sub);
    }
}

// one bus word at bus_ba (aligned), the lanes in mask
u64 address_space::read_bus(u64 bus_ba, u64 mask) {
    page_t &p = m_pages[bus_ba >> m_pbits];
    const u64 psize = u64(1) << m_pbits;
    u64 result = 0;
    // distinct entries among the requested lanes
    u16 ids[8];
    u64 masks[8];
    int n = 0;
    for (int b = 0; b < m_bus_bytes; b++) {
        u64 lm = u64(0xff) << lane_shift(bus_ba + b, 1);
        if (!(mask & lm)) continue;
        u16 id = p.ridx ? p.ridx[(bus_ba + b) & (psize - 1)] : 0;
        int j = 0;
        while (j < n && ids[j] != id) j++;
        if (j == n) { ids[n] = id; masks[n] = 0; n++; }
        masks[j] |= lm;
    }
    for (int j = 0; j < n; j++) {
        if (!ids[j]) continue;
        const entry_rt &r = m_rt[ids[j] - 1];
        const address_map_entry *e = r.e;
        const u64 em = masks[j];
        if (e->m_rkind == address_map_entry::MEM) {
            const u8 *w = r.mem + ((bus_ba & ~(u64)(e->m_mirror | e->m_select)) - e->m_start);
            u64 v;
            switch (m_bus_bytes) {
            case 1: v = *w; break;
            case 2: v = *(const u16 *)w; break;
            case 4: v = *(const u32 *)w; break;
            default: v = *(const u64 *)w; break;
            }
            result |= v & em;
        } else if (e->m_rkind == address_map_entry::HANDLER) {
            u64 offb = (bus_ba & ~(u64)e->m_mirror) - e->m_start;
            int hw = e->m_rwidth / 8;
            if (hw >= m_bus_bytes) {
                result |= e->m_rh((offs_t)(offb / m_bus_bytes), em & e->m_umask) & em;
            } else {
                u64 smask = (u64(1) << e->m_rwidth) - 1;
                int units = 0;
                for (int k = 0; k < m_bus_bytes / hw; k++) {
                    u64 lm = smask << lane_shift(bus_ba + k * hw, hw);
                    if ((e->m_umask & lm) == lm) units++;
                }
                int u = 0;
                for (int k = 0; k < m_bus_bytes / hw; k++) {
                    u64 sh = lane_shift(bus_ba + k * hw, hw);
                    u64 lm = smask << sh;
                    if ((e->m_umask & lm) != lm) continue;
                    if (em & lm) result |= (e->m_rh((offs_t)((offb / m_bus_bytes) * units + u), smask) & smask) << sh;
                    u++;
                }
            }
        }
    }
    return result;
}

void address_space::write_bus(u64 bus_ba, u64 data, u64 mask) {
    page_t &p = m_pages[bus_ba >> m_pbits];
    const u64 psize = u64(1) << m_pbits;
    if (p.direct_w) {
        u8 *w = p.direct_w + (bus_ba & (psize - 1));
        switch (m_bus_bytes) {
        case 1: *w = (u8)((*w & ~mask) | (data & mask)); break;
        case 2: *(u16 *)w = (u16)((*(u16 *)w & ~mask) | (data & mask)); break;
        case 4: *(u32 *)w = (u32)((*(u32 *)w & ~mask) | (data & mask)); break;
        default: *(u64 *)w = (*(u64 *)w & ~mask) | (data & mask); break;
        }
        return;
    }
    if (!p.widx) return;
    u16 ids[8];
    u64 masks[8];
    int n = 0;
    for (int b = 0; b < m_bus_bytes; b++) {
        u64 lm = u64(0xff) << lane_shift(bus_ba + b, 1);
        if (!(mask & lm)) continue;
        u16 id = p.widx[(bus_ba + b) & (psize - 1)];
        int j = 0;
        while (j < n && ids[j] != id) j++;
        if (j == n) { ids[n] = id; masks[n] = 0; n++; }
        masks[j] |= lm;
    }
    for (int j = 0; j < n; j++) {
        if (!ids[j]) continue;
        const entry_rt &r = m_rt[ids[j] - 1];
        const address_map_entry *e = r.e;
        const u64 em = masks[j];
        if (e->m_wkind == address_map_entry::MEM) {
            u8 *w = r.mem + ((bus_ba & ~(u64)(e->m_mirror | e->m_select)) - e->m_start);
            switch (m_bus_bytes) {
            case 1: *w = (u8)((*w & ~em) | (data & em)); break;
            case 2: *(u16 *)w = (u16)((*(u16 *)w & ~em) | (data & em)); break;
            case 4: *(u32 *)w = (u32)((*(u32 *)w & ~em) | (data & em)); break;
            default: *(u64 *)w = (*(u64 *)w & ~em) | (data & em); break;
            }
        } else if (e->m_wkind == address_map_entry::HANDLER) {
            u64 offb = (bus_ba & ~(u64)e->m_mirror) - e->m_start;
            int hw = e->m_wwidth / 8;
            if (hw >= m_bus_bytes) {
                e->m_wh((offs_t)(offb / m_bus_bytes), data & em, em & e->m_umask);
            } else {
                u64 smask = (u64(1) << e->m_wwidth) - 1;
                int units = 0;
                for (int k = 0; k < m_bus_bytes / hw; k++) {
                    u64 lm = smask << lane_shift(bus_ba + k * hw, hw);
                    if ((e->m_umask & lm) == lm) units++;
                }
                int u = 0;
                for (int k = 0; k < m_bus_bytes / hw; k++) {
                    u64 sh = lane_shift(bus_ba + k * hw, hw);
                    u64 lm = smask << sh;
                    if ((e->m_umask & lm) != lm) continue;
                    if (em & lm) e->m_wh((offs_t)((offb / m_bus_bytes) * units + u), (data >> sh) & smask, smask);
                    u++;
                }
            }
        }
    }
}

u64 address_space::read_slow(u64 ba, int size) {
    u64 bus = ba & ~u64(m_bus_bytes - 1);
    u64 sh = lane_shift(ba, size);
    u64 smask = size == 8 ? ~u64(0) : (u64(1) << (size * 8)) - 1;
    u64 v = read_bus(bus, smask << sh);
    if (m_read_tap) m_read_tap((offs_t)bus, v & (smask << sh), smask << sh);
    return (v >> sh) & smask;
}

void address_space::write_slow(u64 ba, int size, u64 data) {
    u64 bus = ba & ~u64(m_bus_bytes - 1);
    u64 sh = lane_shift(ba, size);
    u64 smask = size == 8 ? ~u64(0) : (u64(1) << (size * 8)) - 1;
    if (m_write_tap) m_write_tap((offs_t)bus, (data & smask) << sh, smask << sh);
    write_bus(bus, (data & smask) << sh, smask << sh);
}

// ---- timers ------------------------------------------------------------------------------
void emu_timer::adjust(attotime duration, s32 param, attotime period) {
    if (duration < attotime::zero) duration = attotime::zero;
    m_expire = m_machine->time() + duration;
    m_param = param;
    m_period = period;
    m_enabled = true;
}

attotime emu_timer::remaining() const {
    attotime now = m_machine->time();
    if (!m_enabled || m_expire.is_never()) return attotime::never;
    return m_expire <= now ? attotime::zero : m_expire - now;
}

// ---- CPUs and the machine ------------------------------------------------------------------
attotime cpu_device::local_time() const {
    if (!m_executing) return m_localtime;
    return m_localtime + cycles_to_attotime((u64)(m_cycles_running - *m_icountptr));
}

running_machine::running_machine() { m_config.m_machine = this; }

running_machine::~running_machine() {
    for (auto it = m_devices.rbegin(); it != m_devices.rend(); ++it) delete *it;
}

memory_region *running_machine::add_region(const std::string &tag, std::vector<u8> data) {
    auto r = std::make_unique<memory_region>();
    r->m_name = tag;
    r->m_data = std::move(data);
    m_regions.push_back(std::move(r));
    return m_regions.back().get();
}

memory_region *running_machine::find_region(const std::string &tag) {
    for (auto &r : m_regions)
        if (r->m_name == tag) return r.get();
    return nullptr;
}

emu_timer *running_machine::alloc_timer(std::function<void(s32)> cb) {
    auto t = std::make_unique<emu_timer>();
    t->m_machine = this;
    t->m_cb = std::move(cb);
    m_timers.push_back(std::move(t));
    return m_timers.back().get();
}

attotime running_machine::time() const {
    if (m_firing) return m_fire_time;
    if (m_executing) return m_executing->local_time();
    return m_basetime;
}

void running_machine::start() {
    for (device_t *d : m_devices) d->device_config_complete();
    for (device_t *d : m_devices) d->device_start();
}

void running_machine::reset() {
    for (device_t *d : m_devices)
        if (!d->owner()) d->reset();
}

void running_machine::run_until(attotime target) {
    while (m_basetime < target) {
        attotime next = target;
        for (auto &t : m_timers)
            if (t->m_enabled && t->m_expire < next) next = t->m_expire;
        if (next < m_basetime) next = m_basetime;

        for (cpu_device *cpu : m_cpus) {
            if (cpu->m_suspend) {
                if (cpu->m_localtime < next) cpu->m_localtime = next;
                continue;
            }
            if (!(next > cpu->m_localtime)) continue;
            u64 cycles = cpu->attotime_to_cycles(next - cpu->m_localtime);
            if (!cycles) continue;
            if (cycles > 0x3fffffff) cycles = 0x3fffffff;
            cpu->m_cycles_running = (int)cycles;
            *cpu->m_icountptr = (int)cycles;
            cpu->m_executing = true;
            m_executing = cpu;
            cpu->execute_run();
            m_executing = nullptr;
            cpu->m_executing = false;
            u64 ran = (u64)(cpu->m_cycles_running - *cpu->m_icountptr);
            cpu->m_totalcycles += ran;
            cpu->m_localtime += cpu->cycles_to_attotime(ran);
        }
        m_basetime = next;

        // timers due, in time order
        for (;;) {
            emu_timer *due = nullptr;
            for (auto &t : m_timers)
                if (t->m_enabled && t->m_expire <= m_basetime && (!due || t->m_expire < due->m_expire)) due = t.get();
            if (!due) break;
            m_fire_time = due->m_expire;
            if (due->m_period.is_never() || due->m_period.is_zero()) due->m_enabled = false;
            else due->m_expire += due->m_period;
            m_firing = due;
            due->m_cb(due->m_param);
            m_firing = nullptr;
        }
    }
}
