// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: the subset of MAME's emulator framework (emu.h) that the H8 CPU
// core and the SWP30 need, so their MAME sources compile nearly unchanged
// (third_party/mame/README.md lists every edit). Not MAME code: a small
// reimplementation of the same interfaces.
//
// - devices: a tree built at construction time (device_add_mconfig runs as each
//   device is created), started and reset in tree order;
// - finders resolve to the device created with them (or to the device given to
//   set_tag), not by tag lookup;
// - address spaces are flat page tables (direct pages for RAM/ROM, per-byte
//   handler tables for I/O), compiled from address_map entries;
// - running_machine is a small scheduler: CPUs in timeslices up to the next
//   timer, timers fired in time order; time() is the executing CPU's local time.
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// ---- basic types ----------------------------------------------------------------------
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using offs_t = u32;
using attoseconds_t = s64;

#define ATTR_COLD
#define FUNC(x) &x
#define NAME(x) #x
#define STRUCT_MEMBER(s, m) #s "." #m
#define ROM_NAME(name) nullptr
// ROM declarations (the XT446 glue loads the files itself)
#define ROM_START(name) [[maybe_unused]] static const tiny_rom_entry *rom_##name##_unused = nullptr; namespace {
#define ROM_END }
#define ROM_REGION16_LE(...)
#define ROM_REGION32_LE(...)
#define ROM_REGION(...)
#define ROM_LOAD(...)
#define ROM_LOAD16_WORD_SWAP(...)
#define ROM_LOAD32_WORD(...)
#define ACCESSING_BITS_0_7 ((mem_mask & 0x000000ffU) != 0)
#define ACCESSING_BITS_8_15 ((mem_mask & 0x0000ff00U) != 0)
#define ACCESSING_BITS_0_15 ((mem_mask & 0x0000ffffU) != 0)
#define ACCESSING_BITS_16_31 ((mem_mask & 0xffff0000U) != 0)
#define COMBINE_DATA(varptr) (*(varptr) = (*(varptr) & ~mem_mask) | (data & mem_mask))
#define DEVICE_SELF ""
#define DEVICE_SELF_OWNER "^"
#define TIMER_CALLBACK_MEMBER(name) void name(s32 param)

constexpr int ASSERT_LINE = 1;
constexpr int CLEAR_LINE = 0;
constexpr int HOLD_LINE = 2;
constexpr int INPUT_LINE_NMI = 32;
constexpr int INPUT_LINE_IRQ0 = 0, INPUT_LINE_IRQ1 = 1, INPUT_LINE_IRQ2 = 2, INPUT_LINE_IRQ3 = 3, INPUT_LINE_IRQ4 = 4,
              INPUT_LINE_IRQ5 = 5, INPUT_LINE_IRQ6 = 6, INPUT_LINE_IRQ7 = 7, INPUT_LINE_IRQ8 = 8, INPUT_LINE_IRQ9 = 9,
              INPUT_LINE_IRQ10 = 10, INPUT_LINE_IRQ11 = 11, INPUT_LINE_IRQ12 = 12, INPUT_LINE_IRQ13 = 13,
              INPUT_LINE_IRQ14 = 14, INPUT_LINE_IRQ15 = 15;

enum { STATE_GENPC = -1, STATE_GENPCBASE = -2, STATE_GENSP = -3, STATE_GENFLAGS = -4 };
enum { AS_PROGRAM = 0, AS_DATA = 1, AS_IO = 2, AS_OPCODES = 3 };
enum endianness_t { ENDIANNESS_LITTLE, ENDIANNESS_BIG };
enum { SUSPEND_REASON_HALT = 1, SUSPEND_REASON_RESET = 2, SUSPEND_REASON_SPIN = 4, SUSPEND_REASON_TRIGGER = 8,
       SUSPEND_REASON_DISABLE = 16, SUSPEND_REASON_TIMESLICE = 32, SUSPEND_REASON_CLOCK = 64 };

template <typename T> constexpr T BIT(T x, int n) noexcept { return (x >> n) & T(1); }
template <typename T> constexpr T BIT(T x, int n, int w) noexcept { return (x >> n) & ((T(1) << w) - 1); }

template <typename T, typename U> constexpr T downcast(U *p) { return static_cast<T>(p); }
template <typename T, typename U> constexpr T downcast(U &r) { return static_cast<T>(r); }
template <typename T, typename... B> constexpr T bitswap(T val, B... bits) {
    T r = 0;
    ((r = (r << 1) | ((val >> bits) & 1)), ...);
    return r;
}
template <int N, typename T, typename... B> constexpr T bitswap(T val, B... bits) { return bitswap<T>(val, bits...); }

namespace util {
template <typename T> constexpr T make_bitmask(unsigned n) { return n >= sizeof(T) * 8 ? ~T(0) : T((T(1) << n) - 1); }
template <typename T> constexpr std::make_signed_t<T> sext(T value, unsigned width) noexcept {
    using S = std::make_signed_t<T>;
    unsigned sh = sizeof(T) * 8 - width;
    return S(value << sh) >> sh;
}
}  // namespace util

// ---- printf-style formatting that accepts std::string and any integer width (MAME's strformat)
namespace hk_mame {
std::string vformat(const char *fmt, const std::vector<std::string> &conv);
struct fmt_arg {
    enum kind_t { INT, UINT, DBL, STR, PTR } kind;
    long long i = 0;
    unsigned long long u = 0;
    double d = 0;
    std::string s;
    const void *p = nullptr;
};
template <typename T> fmt_arg make_arg(const T &v) {
    fmt_arg a;
    if constexpr (std::is_same_v<T, bool>) { a.kind = fmt_arg::UINT; a.u = v; a.i = v; }
    else if constexpr (std::is_enum_v<T>) { a.kind = fmt_arg::INT; a.i = (long long)v; a.u = (unsigned long long)v; }
    else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) { a.kind = fmt_arg::INT; a.i = v; a.u = (unsigned long long)(std::make_unsigned_t<T>)v; }
    else if constexpr (std::is_integral_v<T>) { a.kind = fmt_arg::UINT; a.u = v; a.i = (long long)v; }
    else if constexpr (std::is_floating_point_v<T>) { a.kind = fmt_arg::DBL; a.d = v; }
    else if constexpr (std::is_convertible_v<T, std::string_view>) { a.kind = fmt_arg::STR; a.s = std::string(std::string_view(v)); }
    else if constexpr (std::is_pointer_v<T>) { a.kind = fmt_arg::PTR; a.p = (const void *)v; }
    else { a.kind = fmt_arg::STR; a.s = "?"; }
    return a;
}
std::string format_args(const char *fmt, const std::vector<fmt_arg> &args);
}  // namespace hk_mame

namespace util {
template <typename... A> std::string string_format(const char *fmt, A &&...a) {
    return hk_mame::format_args(fmt, {hk_mame::make_arg(a)...});
}
template <typename... A> std::string string_format(const std::string &fmt, A &&...a) {
    return hk_mame::format_args(fmt.c_str(), {hk_mame::make_arg(a)...});
}
template <typename... A> void stream_format(std::ostream &o, const char *fmt, A &&...a) {
    o << hk_mame::format_args(fmt, {hk_mame::make_arg(a)...});
}
}  // namespace util
using util::string_format;

[[noreturn]] void fatalerror(const char *fmt, ...);
class emu_fatalerror {
public:
    template <typename... A> emu_fatalerror(const char *fmt, A &&...a) : m_text(util::string_format(fmt, std::forward<A>(a)...)) {}
    const char *what() const { return m_text.c_str(); }
private:
    std::string m_text;
};

// ---- attotime -------------------------------------------------------------------------
constexpr attoseconds_t ATTOSECONDS_PER_SECOND = 1'000'000'000'000'000'000LL;
constexpr attoseconds_t HZ_TO_ATTOSECONDS(u64 hz) { return ATTOSECONDS_PER_SECOND / (attoseconds_t)hz; }

class attotime {
public:
    using i128 = __int128;
    constexpr attotime() : m_v(0) {}
    constexpr attotime(s64 seconds, attoseconds_t attos) : m_v(i128(seconds) * ATTOSECONDS_PER_SECOND + attos) {}
    static constexpr attotime raw(i128 v) { attotime t; t.m_v = v; return t; }
    static const attotime zero;
    static const attotime never;

    bool is_never() const { return m_v >= NEVER; }
    bool is_zero() const { return m_v == 0; }
    s64 seconds() const { return is_never() ? 1000000000 : (s64)(m_v / ATTOSECONDS_PER_SECOND); }
    attoseconds_t attoseconds() const { return is_never() ? 0 : (attoseconds_t)(m_v % ATTOSECONDS_PER_SECOND); }
    double as_double() const { return is_never() ? 1e9 : (double)m_v / 1e18; }
    u64 as_ticks(u32 frequency) const { return is_never() ? ~u64(0) : (u64)(m_v * frequency / ATTOSECONDS_PER_SECOND); }
    u64 as_ticks(u64 frequency) const { return is_never() ? ~u64(0) : (u64)(m_v * (i128)frequency / ATTOSECONDS_PER_SECOND); }
    static attotime from_ticks(u64 ticks, u32 frequency) {
        if (!frequency) return never;
        return raw(i128(ticks) * HZ_TO_ATTOSECONDS(frequency));
    }
    static attotime from_ticks(u64 ticks, u64 frequency) { return from_ticks(ticks, (u32)frequency); }
    static attotime from_hz(double hz) { return raw((i128)(1e18 / hz)); }
    static attotime from_usec(s64 us) { return raw(i128(us) * 1'000'000'000'000LL); }
    static attotime from_nsec(s64 ns) { return raw(i128(ns) * 1'000'000'000LL); }
    static attotime from_double(double s) { return raw((i128)(s * 1e18)); }
    i128 raw_value() const { return m_v; }
    std::string as_string(int precision = 9) const;
    std::string to_string() const { return as_string(); }

    friend attotime operator+(attotime a, attotime b) {
        if (a.is_never() || b.is_never()) return never;
        return raw(a.m_v + b.m_v);
    }
    friend attotime operator-(attotime a, attotime b) {
        if (a.is_never()) return never;
        return raw(a.m_v - b.m_v);
    }
    attotime &operator+=(attotime b) { return *this = *this + b; }
    attotime &operator-=(attotime b) { return *this = *this - b; }
    friend attotime operator*(attotime a, u32 f) { return a.is_never() ? never : raw(a.m_v * f); }
    friend attotime operator/(attotime a, u32 f) { return a.is_never() ? never : raw(a.m_v / f); }
    friend bool operator<(attotime a, attotime b) { return a.m_v < b.m_v; }
    friend bool operator>(attotime a, attotime b) { return a.m_v > b.m_v; }
    friend bool operator<=(attotime a, attotime b) { return a.m_v <= b.m_v; }
    friend bool operator>=(attotime a, attotime b) { return a.m_v >= b.m_v; }
    friend bool operator==(attotime a, attotime b) { return a.m_v == b.m_v; }
    friend bool operator!=(attotime a, attotime b) { return a.m_v != b.m_v; }

private:
    static constexpr i128 NEVER = i128(1000000000) * ATTOSECONDS_PER_SECOND;
    i128 m_v;
};
inline const attotime attotime::zero = attotime();
inline const attotime attotime::never = attotime::raw(attotime::NEVER);

// ---- XTAL -----------------------------------------------------------------------------
class XTAL {
public:
    constexpr explicit XTAL(double hz) : m_hz(hz) {}
    constexpr u32 value() const { return (u32)m_hz; }
    constexpr operator u32() const { return value(); }
private:
    double m_hz;
};
constexpr XTAL operator""_MHz_XTAL(long double v) { return XTAL(double(v * 1e6)); }
constexpr XTAL operator""_MHz_XTAL(unsigned long long v) { return XTAL(double(v) * 1e6); }

// ---- forward declarations -------------------------------------------------------------
class device_t;
class cpu_device;
class running_machine;
class address_map;
class address_space;
class emu_timer;
class memory_region;
struct tiny_rom_entry {};
struct device_state_entry {
    int m_index;
    int index() const { return m_index; }
};

// ---- device types: TYPE(config, finder, args...) creates the device ----------------------
class machine_config {
public:
    device_t *m_owner = nullptr;              // device whose device_add_mconfig is running
    running_machine *m_machine = nullptr;
    struct opts_t {
        bool drc_rwx() const { return false; }
    };
    opts_t options() const { return {}; }
};

class device_type_base {
public:
    constexpr device_type_base(const char *shortname, const char *fullname) : m_shortname(shortname), m_fullname(fullname) {}
    const char *shortname() const { return m_shortname; }
    const char *fullname() const { return m_fullname; }
private:
    const char *m_shortname, *m_fullname;
};
using device_type = const device_type_base &;

class finder_base {
public:
    finder_base() = default;
    finder_base(device_t *owner, const char *tag) : m_owner(owner), m_tag(tag ? tag : "") {}
    virtual ~finder_base() = default;
    virtual device_t *device_ptr() const = 0;
    static constexpr const char *DUMMY_TAG = "finder_dummy_tag";
    const char *finder_tag() const { return m_tag.c_str(); }
    device_t *m_owner = nullptr;
    std::string m_tag;
};

void hk_register_device(machine_config &config, device_t *dev);   // tree order bookkeeping
device_t *hk_owner_of(device_t *dev);
device_t *hk_find_device(device_t *owner, const char *tag);   // by tag relative to owner

template <class D> class device_creator : public device_type_base {
public:
    using device_type_base::device_type_base;
    // TYPE(config, finder, args...) and TYPE(config, "tag", args...)
    template <typename F, typename... A> D &operator()(machine_config &config, F &&finder, A &&...args) const;
};

#define DECLARE_DEVICE_TYPE(Type, Class) extern const device_creator<Class> Type;
#define DEFINE_DEVICE_TYPE(Type, Class, Short, Full) const device_creator<Class> Type(Short, Full);

// ---- devcb ----------------------------------------------------------------------------
template <typename T> class devcb_read_base {
public:
    devcb_read_base() = default;
    devcb_read_base(device_t &, T defval = 0) : m_def(defval) {}
    // bind() hands out a proxy: MAME's `auto read_portN() { return ...bind(); }` copies it
    struct binder {
        devcb_read_base *p;
        template <typename L> binder &set(L &&l) { p->set(std::forward<L>(l)); return *this; }
        binder &set_constant(T v) { p->set_constant(v); return *this; }
    };
    binder bind() { return {this}; }
    template <typename L> void set(L &&l) {
        if constexpr (std::is_invocable_v<L, offs_t, T>) m_f = [l = std::forward<L>(l)](offs_t o, T m) mutable { return T(l(o, m)); };
        else if constexpr (std::is_invocable_v<L, offs_t>) m_f = [l = std::forward<L>(l)](offs_t o, T) mutable { return T(l(o)); };
        else m_f = [l = std::forward<L>(l)](offs_t, T) mutable { return T(l()); };
    }
    void set_constant(T v) { m_f = [v](offs_t, T) { return v; }; }
    bool isunset() const { return !m_f; }
    T operator()(offs_t o = 0, T mask = ~T(0)) { return m_f ? m_f(o, mask) : m_def; }
    template <unsigned N> class array : public std::array<devcb_read_base, N> {
    public:
        array(device_t &d, T defval = 0) { for (auto &e : *this) e = devcb_read_base(d, defval); }
    };
private:
    std::function<T(offs_t, T)> m_f;
    T m_def = 0;
};

template <typename T> class devcb_write_base {
public:
    devcb_write_base() = default;
    devcb_write_base(device_t &) {}
    struct binder {
        devcb_write_base *p;
        template <typename L> binder &set(L &&l) { p->set(std::forward<L>(l)); return *this; }
        binder &set_nop() { p->set_nop(); return *this; }
    };
    binder bind() { return {this}; }
    template <typename L> void set(L &&l) {
        if constexpr (std::is_invocable_v<L, offs_t, T, T>) m_f = [l = std::forward<L>(l)](offs_t o, T d, T m) mutable { l(o, d, m); };
        else if constexpr (std::is_invocable_v<L, offs_t, T>) m_f = [l = std::forward<L>(l)](offs_t o, T d, T) mutable { l(o, d); };
        else m_f = [l = std::forward<L>(l)](offs_t, T d, T) mutable { l(d); };
    }
    void set_nop() { m_f = [](offs_t, T, T) {}; }
    bool isunset() const { return !m_f; }
    void operator()(T data) { if (m_f) m_f(0, data, ~T(0)); }
    void operator()(offs_t o, T data, T mask = ~T(0)) { if (m_f) m_f(o, data, mask); }
    template <unsigned N> class array : public std::array<devcb_write_base, N> {
    public:
        array(device_t &d) { for (auto &e : *this) e = devcb_write_base(d); }
    };
private:
    std::function<void(offs_t, T, T)> m_f;
};

using devcb_read8 = devcb_read_base<u8>;
using devcb_read16 = devcb_read_base<u16>;
using devcb_read32 = devcb_read_base<u32>;
using devcb_write8 = devcb_write_base<u8>;
using devcb_write16 = devcb_write_base<u16>;
using devcb_write32 = devcb_write_base<u32>;

class devcb_write_line {
public:
    devcb_write_line() = default;
    devcb_write_line(device_t &) {}
    struct binder {
        devcb_write_line *p;
        template <typename L> binder &set(L &&l) { p->set(std::forward<L>(l)); return *this; }
        binder &set_nop() { p->set_nop(); return *this; }
    };
    binder bind() { return {this}; }
    template <typename L> void set(L &&l) { m_f = std::forward<L>(l); }
    void set_nop() { m_f = [](int) {}; }
    bool isunset() const { return !m_f; }
    void operator()(int state) { if (m_f) m_f(state); }
    template <unsigned N> class array : public std::array<devcb_write_line, N> {
    public:
        array(device_t &d) { for (auto &e : *this) e = devcb_write_line(d); }
    };
private:
    std::function<void(int)> m_f;
};

// ---- util streams / disassembler interface ----------------------------------------------
namespace util {
class read_stream {};
class write_stream {};
}  // namespace util
inline std::pair<std::error_condition, size_t> read(util::read_stream &, void *, size_t) { return {std::make_error_condition(std::errc::not_supported), 0}; }
inline std::pair<std::error_condition, size_t> write(util::write_stream &, const void *, size_t) { return {std::make_error_condition(std::errc::not_supported), 0}; }
namespace util {
class disasm_interface {
public:
    enum : u32 { SUPPORTED = 0x80000000, STEP_OVER = 0x20000000, STEP_OUT = 0x40000000, STEP_COND = 0x10000000,
                 LENGTHMASK = 0x0000ffff, OVERINSTMASK = 0x0f000000, OVERINSTSHIFT = 24 };
    class data_buffer {
    public:
        std::function<u8(offs_t)> m_r8;
        u8 r8(offs_t pc) const { return m_r8(pc); }
        u16 r16(offs_t pc) const { return (u16(r8(pc)) << 8) | r8(pc + 1); }
        u32 r32(offs_t pc) const { return (u32(r16(pc)) << 16) | r16(pc + 2); }
        u64 r64(offs_t pc) const { return (u64(r32(pc)) << 32) | r32(pc + 4); }
    };
    virtual ~disasm_interface() = default;
    virtual u32 opcode_alignment() const = 0;
    virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) = 0;
};
}  // namespace util

// ---- memory ---------------------------------------------------------------------------
class memory_region {
public:
    std::string m_name;
    std::vector<u8> m_data;   // the bus-width words, in host order
    u32 bytes() const { return (u32)m_data.size(); }
    u8 *base() { return m_data.data(); }
    const char *name() const { return m_name.c_str(); }
    u16 &as_u16(offs_t i = 0) { return reinterpret_cast<u16 *>(m_data.data())[i]; }
};

class address_map_constructor {
public:
    address_map_constructor() = default;
    template <class C> address_map_constructor(void (C::*m)(address_map &), C *obj)
        : m_f([m, obj](address_map &map) { (obj->*m)(map); }) {}
    template <class C> address_map_constructor(void (C::*m)(address_map &) const, const C *obj)
        : m_f([m, obj](address_map &map) { (obj->*m)(map); }) {}
    explicit operator bool() const { return bool(m_f); }
    void operator()(address_map &map) const { m_f(map); }
private:
    std::function<void(address_map &)> m_f;
};

class address_space_config {
public:
    address_space_config() = default;
    address_space_config(const char *name, endianness_t endian, u8 datawidth, u8 addrwidth, s8 addrshift = 0,
                         address_map_constructor internal = address_map_constructor())
        : m_name(name), m_endianness(endian), m_data_width(datawidth), m_addr_width(addrwidth),
          m_logaddr_width(addrwidth), m_addr_shift(addrshift), m_internal_map(internal) {}
    address_space_config(const char *name, endianness_t endian, u8 datawidth, u8 addrwidth, s8 addrshift, u8 /*logwidth*/, u8 /*pageshift*/,
                         address_map_constructor internal = address_map_constructor())
        : address_space_config(name, endian, datawidth, addrwidth, addrshift, internal) {}
    const char *m_name = "";
    endianness_t m_endianness = ENDIANNESS_LITTLE;
    u8 m_data_width = 8, m_addr_width = 0, m_logaddr_width = 0;
    s8 m_addr_shift = 0;
    address_map_constructor m_internal_map;
};
using space_config_vector = std::vector<std::pair<int, const address_space_config *>>;
struct device_memory_interface {
    using space_config_vector = ::space_config_vector;
};

// One map entry. Handlers are normalised to (offset, data, mem_mask) at their own width.
struct address_map_entry {
    enum kind_t { NONE, MEM, HANDLER, NOP, SUBMAP };
    address_map &m_map;
    offs_t m_start, m_end, m_mirror = 0, m_select = 0;
    u64 m_umask = ~u64(0);
    kind_t m_rkind = NONE, m_wkind = NONE;
    bool m_rom = false;
    std::string m_region;
    offs_t m_region_offset = 0;
    bool m_has_region = false;
    int m_rwidth = 0, m_wwidth = 0;     // handler widths in bits
    std::function<u64(offs_t, u64)> m_rh;
    std::function<void(offs_t, u64, u64)> m_wh;
    address_map_constructor m_submap;
    device_t *m_subdev = nullptr;

    address_map_entry(address_map &map, offs_t s, offs_t e) : m_map(map), m_start(s), m_end(e) {}

    address_map_entry &mirror(offs_t m) { m_mirror = m; return *this; }
    address_map_entry &select(offs_t s) { m_select = s; return *this; }
    address_map_entry &umask16(u16 m) { m_umask = m; return *this; }
    address_map_entry &umask32(u32 m) { m_umask = m; return *this; }
    address_map_entry &umask64(u64 m) { m_umask = m; return *this; }
    address_map_entry &ram() { m_rkind = m_wkind = MEM; return *this; }
    address_map_entry &rom() { m_rkind = MEM; m_wkind = NOP; m_rom = true; return *this; }
    address_map_entry &nopw() { m_wkind = NOP; return *this; }
    address_map_entry &nopr() { m_rkind = NOP; return *this; }
    address_map_entry &noprw() { m_rkind = m_wkind = NOP; return *this; }
    address_map_entry &unmaprw() { m_rkind = m_wkind = NONE; return *this; }
    address_map_entry &region(const char *tag, offs_t off) { m_region = tag; m_region_offset = off; m_has_region = true; return *this; }

    // handlers of the map's own device ...
    template <typename RF> address_map_entry &r(RF rf);
    template <typename WF> address_map_entry &w(WF wf);
    template <typename RF, typename WF> address_map_entry &rw(RF rf, WF wf) { r(rf); return w(wf); }
    // ... or of another device
    template <typename D, typename RF> address_map_entry &r(D &&dev, RF rf);
    template <typename D, typename WF> address_map_entry &w(D &&dev, WF wf);
    template <typename D, typename RF, typename WF> address_map_entry &rw(D &&dev, RF rf, WF wf) { r(dev, rf); return w(dev, wf); }
    // submaps
    template <typename D, class C> address_map_entry &m(D &&dev, void (C::*f)(address_map &));

    template <class C, typename F> void set_r(C *obj, F f);
    template <class C, typename F> void set_w(C *obj, F f);
};

class address_map {
public:
    explicit address_map(device_t &dev) : m_device(&dev) {}
    address_map_entry &operator()(offs_t start, offs_t end) {
        m_entries.push_back(std::make_unique<address_map_entry>(*this, start, end));
        return *m_entries.back();
    }
    device_t *m_device;
    std::vector<std::unique_ptr<address_map_entry>> m_entries;
};

// A flat address space: 4 KiB pages, either direct (one memory block covers the
// page) or per-byte entry tables. Addresses in the API are in the space's units
// (bytes when addr_shift is 0, bus words when it is negative), as in MAME.
class address_space {
public:
    address_space(device_t &dev, const address_space_config &cfg, int spacenum);
    void install(std::unique_ptr<address_map> map);    // later entries win
    void finalize();

    u8 read_byte(offs_t a) { return (u8)read_sized(unit_to_byte(a), 1); }
    u16 read_word(offs_t a) { return (u16)read_sized(unit_to_byte(a), 2); }
    u32 read_dword(offs_t a) { return (u32)read_sized(unit_to_byte(a), 4); }
    u64 read_qword(offs_t a) { return read_sized(unit_to_byte(a), 8); }
    void write_byte(offs_t a, u8 d) { write_sized(unit_to_byte(a), 1, d); }
    void write_word(offs_t a, u16 d) { write_sized(unit_to_byte(a), 2, d); }
    void write_dword(offs_t a, u32 d) { write_sized(unit_to_byte(a), 4, d); }
    void write_qword(offs_t a, u64 d) { write_sized(unit_to_byte(a), 8, d); }

    template <class C> void cache(C &c) { c.m_space = this; }
    template <class C> void specific(C &c) { c.m_space = this; }

    const address_space_config &m_config;
    device_t &m_device;
    int m_spacenum;
    // HIRAKU debugging: observes writes that take the slow path (I/O), as MAME's install_write_tap
    std::function<void(offs_t byte_addr, u64 data, u64 mask)> m_write_tap, m_read_tap;

private:
    struct entry_rt {
        address_map_entry *e;
        u8 *mem;       // MEM: the bus words backing [start, end] (byte address start -> mem)
        offs_t base;   // byte address that maps to mem[0]
    };
    struct page_t {
        u8 *direct_r = nullptr, *direct_w = nullptr;   // one memory entry covers the page: host pointer for its base
        u16 did_r = 0, did_w = 0;                       // ... and that entry's index + 1
        std::unique_ptr<u16[]> ridx, widx;              // otherwise per byte: entry index + 1 (0 = unmapped)
    };

    u64 read_sized(u64 ba, int size);
    void write_sized(u64 ba, int size, u64 data);
    u64 read_slow(u64 ba, int size);
    void write_slow(u64 ba, int size, u64 data);
    u64 read_bus(u64 bus_ba, u64 mask);   // one bus word at aligned byte address, lanes in mask
    void write_bus(u64 bus_ba, u64 data, u64 mask);
    u64 unit_to_byte(offs_t a) const { return m_shift < 0 ? (u64)a << (-m_shift) : (u64)a; }
    void collect(address_map &map, offs_t base, offs_t mirror);
    u64 lane_shift(u64 ba, int size) const;   // shift of the size-byte lane at ba inside its bus word

    int m_bus_bytes;
    int m_pbits;
    int m_shift;
    bool m_big;
    u64 m_byte_mask;
    std::vector<page_t> m_pages;
    std::vector<entry_rt> m_rt;
    std::vector<address_map_entry *> m_all;
    std::vector<std::unique_ptr<address_map>> m_maps;
    std::vector<std::unique_ptr<std::vector<u8>>> m_owned;
};

inline u64 address_space::lane_shift(u64 ba, int size) const {
    int off = (int)(ba & (m_bus_bytes - 1));
    return m_big ? (u64)(m_bus_bytes - off - size) * 8 : (u64)off * 8;
}

inline u64 address_space::read_sized(u64 ba, int size) {
    ba &= m_byte_mask;
    page_t &p = m_pages[ba >> m_pbits];
    if (p.direct_r) {
        u64 bus = ba & ~u64(m_bus_bytes - 1);
        const u8 *w = p.direct_r + (bus & ((u64(1) << m_pbits) - 1));
        u64 v;
        switch (m_bus_bytes) {
        case 1: v = *w; break;
        case 2: v = *(const u16 *)w; break;
        case 4: v = *(const u32 *)w; break;
        default: v = *(const u64 *)w; break;
        }
        if (size == m_bus_bytes) return v;
        return (v >> lane_shift(ba, size)) & ((u64(1) << (size * 8)) - 1);
    }
    return read_slow(ba, size);
}

inline void address_space::write_sized(u64 ba, int size, u64 data) {
    ba &= m_byte_mask;
    page_t &p = m_pages[ba >> m_pbits];
    if (p.direct_w && size == m_bus_bytes) {
        u8 *w = p.direct_w + (ba & ((u64(1) << m_pbits) - 1));
        switch (m_bus_bytes) {
        case 1: *w = (u8)data; break;
        case 2: *(u16 *)w = (u16)data; break;
        case 4: *(u32 *)w = (u32)data; break;
        default: *(u64 *)w = data; break;
        }
        return;
    }
    write_slow(ba, size, data);
}

template <int A, int D, int S, endianness_t E> struct memory_access {
    class cache {
    public:
        address_space *m_space = nullptr;
        u8 read_byte(offs_t a) { return m_space->read_byte(a); }
        u16 read_word(offs_t a) { return m_space->read_word(a); }
        u32 read_dword(offs_t a) { return m_space->read_dword(a); }
        u64 read_qword(offs_t a) { return m_space->read_qword(a); }
        void write_byte(offs_t a, u8 d) { m_space->write_byte(a, d); }
        void write_word(offs_t a, u16 d) { m_space->write_word(a, d); }
        void write_dword(offs_t a, u32 d) { m_space->write_dword(a, d); }
        void write_qword(offs_t a, u64 d) { m_space->write_qword(a, d); }
    };
    using specific = cache;
};

// ---- shared pointers / region pointers ---------------------------------------------------
template <typename T> class optional_shared_ptr {
public:
    optional_shared_ptr(device_t &, const char *) {}
    explicit operator bool() const { return false; }
    T &operator[](size_t i) { return m_dummy; }
    u32 bytes() const { return 0; }
    u32 length() const { return 0; }
private:
    T m_dummy{};
};

memory_region *hk_find_region(device_t &dev, const char *tag);
template <typename T> class required_region_ptr {
public:
    required_region_ptr(device_t &dev, const char *tag) : m_dev(&dev), m_tag(tag) {}
    T &operator[](size_t i) { return target()[i]; }
    T *target() {
        if (!m_ptr) {
            memory_region *r = hk_find_region(*m_dev, m_tag.c_str());
            if (!r) fatalerror("required region %s missing", m_tag.c_str());
            m_ptr = reinterpret_cast<T *>(r->base());
        }
        return m_ptr;
    }
private:
    device_t *m_dev;
    std::string m_tag;
    T *m_ptr = nullptr;
};

// ---- state interface (debugger registers: accepted, not used) ------------------------------
class device_state_entry_stub {
public:
    device_state_entry_stub &mask(u64) { return *this; }
    device_state_entry_stub &noshow() { return *this; }
    device_state_entry_stub &formatstr(const char *) { return *this; }
    device_state_entry_stub &callimport() { return *this; }
    device_state_entry_stub &callexport() { return *this; }
};

// ---- device_t ---------------------------------------------------------------------------
class device_t {
public:
    device_t(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock);
    virtual ~device_t() = default;

    const char *tag() const { return m_tag.c_str(); }
    const char *basetag() const { return m_basetag.c_str(); }
    const char *shortname() const { return m_type.shortname(); }
    device_t *owner() const { return m_owner; }
    u32 clock() const { return m_clock; }
    void set_clock(u32 c) { m_clock = c; }
    running_machine &machine() const { return *m_machine; }
    const machine_config &mconfig() const { return *m_mconfig; }
    std::string subtag(const char *tag) const { return m_tag + ":" + tag; }

    template <typename... A> void save_item(A &&...) {}
    void logerror(const char *fmt) const { hk_log(fmt); }
    void logerror(const std::string &text) const { hk_log(text.c_str()); }
    template <typename... A> void logerror(const char *fmt, A &&...a) const {
        if (s_log) hk_log(util::string_format(fmt, std::forward<A>(a)...).c_str());
    }
    template <typename C> emu_timer *timer_alloc(void (C::*f)(s32), C *obj);
    memory_region *memregion(const char *tag) { return hk_find_region(*this, tag); }

    // lifecycle (MAME's device_t interface)
    virtual void device_config_complete() {}
    virtual void device_start() {}
    virtual void device_reset() {}
    virtual void device_reset_after_children() {}
    void reset();   // this device and its children, as MAME's device_t::reset
    virtual void device_stop() {}
    virtual void device_post_load() {}
    virtual void device_clock_changed() {}
    virtual void device_add_mconfig(machine_config &) {}
    virtual const tiny_rom_entry *device_rom_region() const { return nullptr; }

    // device_memory_interface (folded in)
    virtual space_config_vector memory_space_config() const { return {}; }
    address_space &space(int spacenum = 0);
    bool has_space(int spacenum = 0) const;
    void set_addrmap(int spacenum, address_map_constructor map);

    std::vector<device_t *> m_children;
    static bool s_log;

protected:
    void hk_log(const char *s) const;

private:
    friend class running_machine;
    friend void hk_register_device(machine_config &, device_t *);
    device_type m_type;
    std::string m_tag, m_basetag;
    device_t *m_owner;
    u32 m_clock;
    running_machine *m_machine;
    const machine_config *m_mconfig;
    std::vector<std::pair<int, address_map_constructor>> m_external_maps;
    std::vector<std::unique_ptr<address_space>> m_spaces;
};

// ---- finders ----------------------------------------------------------------------------
template <class T> class device_finder : public finder_base {
public:
    device_finder() = default;
    device_finder(device_t &owner, const char *tag) : finder_base(&owner, tag) {}

    void set_tag(device_t &dev) { m_target = dynamic_cast<T *>(&dev); m_link = nullptr; }
    template <class U> void set_tag(device_finder<U> &f) { m_link = [p = &f] { return p->device_ptr(); }; }
    template <class U> void set_tag(const device_finder<U> &f) { m_link = [p = &f] { return p->device_ptr(); }; }
    void set_target(T *t) { m_target = t; }

    // a finder told to follow another finder resolves to that one's device, lazily
    T *target() const {
        if (!m_target && m_link) m_target = dynamic_cast<T *>(m_link());
        if (!m_target && m_tag == DEVICE_SELF_OWNER && m_owner) m_target = dynamic_cast<T *>(hk_owner_of(m_owner));
        if (!m_target && m_owner && !m_tag.empty() && m_tag != DUMMY_TAG) m_target = dynamic_cast<T *>(hk_find_device(m_owner, m_tag.c_str()));
        return m_target;
    }
    device_t *device_ptr() const override { return dynamic_cast<device_t *>(target()); }
    T *operator->() const { return target(); }
    T &operator*() const { return *target(); }
    operator T *() const { return target(); }
    explicit operator bool() const { return target() != nullptr; }
    T *lookup() const { return target(); }

private:
    mutable T *m_target = nullptr;
    std::function<device_t *()> m_link;
};

template <class T> using required_device = device_finder<T>;
template <class T> using optional_device = device_finder<T>;

template <class T, unsigned N> class device_array_finder : public std::array<device_finder<T>, N> {
public:
    device_array_finder(device_t &owner, const char *fmt, unsigned start) {
        for (unsigned i = 0; i < N; i++) {
            char buf[64];
            snprintf(buf, sizeof buf, fmt, start + i);
            (*this)[i] = device_finder<T>(owner, buf);
        }
    }
};
template <class T, unsigned N> using required_device_array = device_array_finder<T, N>;
template <class T, unsigned N> using optional_device_array = device_array_finder<T, N>;

template <class T> struct device_type_enumerator {
    device_type_enumerator(device_t &) {}
    T *begin() { return nullptr; }
    T *end() { return nullptr; }
};

template <class D>
template <typename F, typename... A>
D &device_creator<D>::operator()(machine_config &config, F &&finder, A &&...args) const {
    device_t *owner = config.m_owner;
    D *dev;
    if constexpr (std::is_convertible_v<F, const char *>) {
        dev = new D(config, finder, owner, std::forward<A>(args)...);
    } else {
        dev = new D(config, finder.m_tag.c_str(), owner, std::forward<A>(args)...);
        finder.set_target(dev);
    }
    hk_register_device(config, dev);
    return *dev;
}

// ---- emu_timer --------------------------------------------------------------------------
class emu_timer {
public:
    void adjust(attotime duration, s32 param = 0, attotime period = attotime::never);
    void enable(bool on = true) { m_enabled = on; }
    bool enabled() const { return m_enabled; }
    attotime remaining() const;
    attotime expire() const { return m_expire; }
    s32 param() const { return m_param; }

    running_machine *m_machine = nullptr;
    std::function<void(s32)> m_cb;
    attotime m_expire = attotime::never, m_period = attotime::never;
    s32 m_param = 0;
    bool m_enabled = false;
};

// ---- device interfaces ----------------------------------------------------------------------
class device_nvram_interface {
public:
    device_nvram_interface(const machine_config &, device_t &) {}
    virtual ~device_nvram_interface() = default;
    void nvram_enable_backup(bool on) { m_backup = on; }
    bool nvram_backup_enabled() const { return m_backup; }
    virtual void nvram_default() {}
    virtual bool nvram_read(util::read_stream &) { return true; }
    virtual bool nvram_write(util::write_stream &) { return true; }

private:
    bool m_backup = false;
};

class sound_stream {
public:
    void put_int_clamp(int, int, s32, s32) {}
    float get(int, int) const { return 0.0f; }
};
enum { STREAM_SYNCHRONOUS = 1, STREAM_DEFAULT_FLAGS = 0 };
class device_sound_interface {
public:
    device_sound_interface(const machine_config &, device_t &) {}
    virtual ~device_sound_interface() = default;
    virtual void sound_stream_update(sound_stream &) {}
    sound_stream *stream_alloc(int, int, u32, int = 0) { return &m_stream; }
    device_sound_interface &add_route(int, device_t &, double, int = 0) { return *this; }
private:
    sound_stream m_stream;
};
class device_mixer_interface : public device_sound_interface {
public:
    using device_sound_interface::device_sound_interface;
};

class cpu_device : public device_t {
public:
    cpu_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
        : device_t(mconfig, type, tag, owner, clock) {}

    // device_execute_interface
    virtual bool cpu_is_interruptible() const { return false; }
    virtual u32 execute_min_cycles() const noexcept { return 1; }
    virtual u32 execute_max_cycles() const noexcept { return 1; }
    virtual u32 execute_input_lines() const noexcept { return 0; }
    virtual bool execute_input_edge_triggered(int) const noexcept { return false; }
    virtual void execute_run() = 0;
    virtual void execute_set_input(int, int) {}
    virtual u64 execute_clocks_to_cycles(u64 clocks) const noexcept { return clocks; }
    virtual u64 execute_cycles_to_clocks(u64 cycles) const noexcept { return cycles; }

    void set_icountptr(int &ic) { m_icountptr = &ic; }
    u64 total_cycles() const { return m_executing ? m_totalcycles + (u64)(m_cycles_running - *m_icountptr) : m_totalcycles; }
    attotime local_time() const;
    u64 cycles_per_second() const { return execute_clocks_to_cycles(clock()); }
    attotime cycles_to_attotime(u64 cycles) const { return attotime::raw((attotime::i128)cycles * attoseconds_per_cycle()); }
    u64 attotime_to_cycles(attotime t) const { return (u64)(t.raw_value() / attoseconds_per_cycle()); }
    attoseconds_t attoseconds_per_cycle() const { return HZ_TO_ATTOSECONDS(cycles_per_second()); }

    void set_input_line(int line, int state) { execute_set_input(line, state); }
    bool suspended(u32 reason = ~0u) const { return (m_suspend & reason) != 0; }
    void suspend(u32 reason, bool /*eatcycles*/) { m_suspend |= reason; }
    void resume(u32 reason) { m_suspend &= ~reason; }
    void abort_timeslice() {}
    bool access_to_be_redone() noexcept { return false; }        // MAME's memory wait states: none here
    bool access_to_be_redone_noeat() noexcept { return false; }
    int standard_irq_callback(int, offs_t) { return 0; }
    bool debugger_enabled() const { return false; }
    void debugger_instruction_hook(offs_t) {}
    void debugger_exception_hook(int) {}
    void debugger_interrupt_hook(int, offs_t = 0) {}
    bool allow_drc() const { return false; }
    offs_t pc() const { return m_pc_for_log ? m_pc_for_log() : 0; }

    template <typename T> device_state_entry_stub state_add(int, const char *, T &) { return {}; }
    template <typename T, typename G, typename S> device_state_entry_stub state_add(int, const char *, G &&, S &&) { return {}; }
    virtual void state_import(const device_state_entry &) {}
    virtual void state_export(const device_state_entry &) {}
    virtual void state_string_export(const device_state_entry &, std::string &) const {}
    virtual std::unique_ptr<util::disasm_interface> create_disassembler() { return nullptr; }

    // scheduler state
    int *m_icountptr = nullptr;
    u64 m_totalcycles = 0;
    int m_cycles_running = 0;
    bool m_executing = false;
    attotime m_localtime;
    u32 m_suspend = 0;
    std::function<offs_t()> m_pc_for_log;
};

// ---- the machine: device tree, regions, scheduler ---------------------------------------------
class running_machine {
public:
    running_machine();
    ~running_machine();
    machine_config &config() { return m_config; }

    // build: devices created with TYPE(config(), ...) at the root, then start()
    void start();
    void reset();
    void run_until(attotime target);   // all CPUs and timers up to target

    attotime time() const;
    bool side_effects_disabled() const { return false; }
    u32 rand() {
        m_rand_seed = 1664525 * m_rand_seed + 1013904223;
        return (m_rand_seed >> 16) | (m_rand_seed << 16);
    }
    memory_region *add_region(const std::string &tag, std::vector<u8> data);
    memory_region *find_region(const std::string &tag);

    emu_timer *alloc_timer(std::function<void(s32)> cb);

    std::vector<device_t *> m_devices;     // creation order (a pre-order walk of the tree)
    std::vector<cpu_device *> m_cpus;
    std::vector<std::unique_ptr<emu_timer>> m_timers;
    std::vector<std::unique_ptr<memory_region>> m_regions;
    attotime m_basetime;
    cpu_device *m_executing = nullptr;
    const emu_timer *m_firing = nullptr;
    attotime m_fire_time;

private:
    machine_config m_config;
    u32 m_rand_seed = 0x9d14abd7;
};

template <typename C> emu_timer *device_t::timer_alloc(void (C::*f)(s32), C *obj) {
    return machine().alloc_timer([f, obj](s32 p) { (obj->*f)(p); });
}

// ---- map entry handler plumbing -------------------------------------------------------------
namespace hk_mame {
template <typename T> struct fn_traits;
#define HK_FN_TRAITS(...)                                                                              \
    template <class C, typename R, typename... A> struct fn_traits<R (C::*)(A...) __VA_ARGS__> {          \
        using cls = C;                                                                                  \
        using ret = R;                                                                                  \
        static constexpr int nargs = sizeof...(A);                                                      \
        using args = std::tuple<A...>;                                                                  \
    };
HK_FN_TRAITS()
HK_FN_TRAITS(const)
HK_FN_TRAITS(noexcept)
HK_FN_TRAITS(const noexcept)
#undef HK_FN_TRAITS

template <typename D> device_t *as_device(D &&d) {
    using T = std::remove_cv_t<std::remove_reference_t<D>>;
    if constexpr (std::is_base_of_v<finder_base, T>) return d.device_ptr();
    else if constexpr (std::is_pointer_v<T>) return d;
    else return &d;
}
}  // namespace hk_mame

template <class C, typename F> void address_map_entry::set_r(C *obj, F f) {
    using tr = hk_mame::fn_traits<F>;
    using R = typename tr::ret;
    m_rkind = HANDLER;
    m_rwidth = sizeof(R) * 8;
    if constexpr (tr::nargs == 0) m_rh = [obj, f](offs_t, u64) { return u64((obj->*f)()); };
    else if constexpr (tr::nargs == 1) m_rh = [obj, f](offs_t o, u64) { return u64((obj->*f)(o)); };
    else m_rh = [obj, f](offs_t o, u64 m) { return u64((obj->*f)(o, R(m))); };
}

template <class C, typename F> void address_map_entry::set_w(C *obj, F f) {
    using tr = hk_mame::fn_traits<F>;
    m_wkind = HANDLER;
    if constexpr (tr::nargs == 1) {
        using V = std::tuple_element_t<0, typename tr::args>;
        m_wwidth = sizeof(V) * 8;
        m_wh = [obj, f](offs_t, u64 d, u64) { (obj->*f)(V(d)); };
    } else if constexpr (tr::nargs == 2) {
        using V = std::tuple_element_t<1, typename tr::args>;
        m_wwidth = sizeof(V) * 8;
        m_wh = [obj, f](offs_t o, u64 d, u64) { (obj->*f)(o, V(d)); };
    } else {
        using V = std::tuple_element_t<1, typename tr::args>;
        m_wwidth = sizeof(V) * 8;
        m_wh = [obj, f](offs_t o, u64 d, u64 m) { (obj->*f)(o, V(d), V(m)); };
    }
}

template <typename RF> address_map_entry &address_map_entry::r(RF rf) {
    using C = typename hk_mame::fn_traits<RF>::cls;
    set_r(dynamic_cast<C *>(m_map.m_device), rf);
    return *this;
}
template <typename WF> address_map_entry &address_map_entry::w(WF wf) {
    using C = typename hk_mame::fn_traits<WF>::cls;
    set_w(dynamic_cast<C *>(m_map.m_device), wf);
    return *this;
}
template <typename D, typename RF> address_map_entry &address_map_entry::r(D &&dev, RF rf) {
    using C = typename hk_mame::fn_traits<RF>::cls;
    set_r(dynamic_cast<C *>(hk_mame::as_device(dev)), rf);
    return *this;
}
template <typename D, typename WF> address_map_entry &address_map_entry::w(D &&dev, WF wf) {
    using C = typename hk_mame::fn_traits<WF>::cls;
    set_w(dynamic_cast<C *>(hk_mame::as_device(dev)), wf);
    return *this;
}
template <typename D, class C> address_map_entry &address_map_entry::m(D &&dev, void (C::*f)(address_map &)) {
    C *obj = dynamic_cast<C *>(hk_mame::as_device(dev));
    m_rkind = m_wkind = SUBMAP;
    m_subdev = obj;
    m_submap = address_map_constructor(f, obj);
    return *this;
}

#include "logmacro.h"
