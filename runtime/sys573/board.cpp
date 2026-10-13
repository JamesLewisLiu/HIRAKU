// SPDX-License-Identifier: GPL-3.0-or-later
// The System 573 board: MAME's ksys573.cpp (BSD-3-Clause, R. Belmont, smf) as a
// device tree on the framework shim, with MAME's own devices. The driver class
// below follows ksys573_state and ddr_state; where it differs, a comment says so.
#include "sys573/board.h"
#include "sys573/mips.h"

#include "shell/cabinet.h"

#include "emu.h"
#include "screen.h"
#include "speaker.h"

#include "cpu/psx/psx.h"
#include "video/psx.h"
#include "sound/spu.h"
#include "sound/cdda.h"
#include "sound/mas3507d.h"
#include "machine/ram.h"
#include "machine/intelfsh.h"
#include "machine/timekpr.h"
#include "machine/adc083x.h"
#include "bus/ata/ataintf.h"
#include "bus/ata/cr589.h"
#include "bus/pccard/pccard.h"
#include "bus/pccard/linflash.h"
#include "imagedev/cdromimg.h"
#include "k573cass.h"
#include "k573dio.h"
#include "k573mcr.h"
#include "machine/jvshost.h"
#include "dirtc.h"
#include <numeric>

#include <sys/stat.h>

#include <cstdio>
#include <deque>
#include <map>

namespace hk::k573 {

namespace {

constexpr int ATAPI_CYCLES_PER_SECTOR = 30000;   // MAME: plenty of time to allow DMA setup etc.

std::vector<u8> load_file(const std::string &path) {
    std::vector<u8> d;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return d;
    fseek(f, 0, SEEK_END);
    d.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
    fclose(f);
    return d;
}

// ksys573.cpp's sys573_jvs_host: the JVS host as the 573 reads and writes it
class sys573_jvs_host : public jvs_host {
public:
    sys573_jvs_host(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
    void send_packet(u8 *data, int length) {
        while (length > 0) { push(*data); data++; length--; }
        commit_raw();
    }
    int received_packet(u8 *buffer) {
        if (jvs_sense_r()) return 0;   // the reset command is sent twice; no reply to it
        u32 length;
        const u8 *data;
        get_raw_reply(data, length);
        if (length > 0) {
            // MAME: the games don't unescape the data in memory; send it unescaped
            u8 checksum = std::accumulate(data, data + length, 0);
            buffer[0] = 0xe0;
            memcpy(buffer + 1, data, length);
            buffer[length + 1] = checksum;
            buffer[length + 2] = 0;
            length += 2;
            commit_encoded();
        }
        return (int)length;
    }
    int jvs_sense_r() { return !get_address_set_line(); }
};
const device_creator<sys573_jvs_host> SYS573_JVS_HOST("sys573_jvs_host", "JVS Host (System 573)");
sys573_jvs_host::sys573_jvs_host(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
    : jvs_host(mconfig, SYS573_JVS_HOST, tag, owner, clock) {}

// The game's configuration while the board is built: the shim runs device_add_mconfig as each
// device is constructed, before init() can hand the board its cfg.
const Config *g_build = nullptr;

// MAME's ioport output field: the write callback runs when its bits change
// (dynamic_field::write), with the active-low inversion of the field's default.
struct OutField {
    u32 mask;
    bool active_low;
    std::function<void(int)> write;
    u32 old = 0;
};

class sys573_state : public device_t {
public:
    sys573_state(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0)
        : device_t(mconfig, SYS573_STATE, tag, owner, clock),
          m_maincpu(*this, "maincpu"),
          m_ram(*this, "ram"),
          m_gpu(*this, "gpu"),
          m_gpu_ram(*this, "gpu_ram"),
          m_spu(*this, "spu"),
          m_spu_ram(*this, "spu_ram"),
          m_ata(*this, "ata"),
          m_cassette(*this, "cassette"),
          m_adc(*this, "adc0834"),
          m_m48t58(*this, "m48t58"),
          m_pccard1(*this, "pccard1"),
          m_pccard2(*this, "pccard2"),
          m_k573dio(*this, "k573dio"),
          m_sys573_jvs_host(*this, "sys573_jvs_host"),
          m_screen(*this, "screen") {}

    static const device_creator<sys573_state> SYS573_STATE;

    Config cfg;

    required_device<psxcpu_device> m_maincpu;
    required_device<ram_device> m_ram;
    required_device<psxgpu_device> m_gpu;
    required_device<ram_device> m_gpu_ram;
    required_device<spu_device> m_spu;
    required_device<ram_device> m_spu_ram;
    required_device<ata_interface_device> m_ata;
    required_device<konami573_cassette_slot_device> m_cassette;
    required_device<adc0834_device> m_adc;
    required_device<timekeeper_device> m_m48t58;
    required_device<pccard_slot_device> m_pccard1, m_pccard2;
    optional_device<k573dio_device> m_k573dio;
    required_device<sys573_jvs_host> m_sys573_jvs_host;
    required_device<screen_device> m_screen;
    fujitsu_29f016a_device *m_flash[8] = {};   // 31m 27m 31l 27l 31j 27j 31h 27h

    // ---- state (ksys573_state) ----
    u16 m_control = 0, m_n_security_control = 0;
    int m_pccard_cd[2] = {1, 1};   // MAME: card detect is active low (no card = 1)
    u32 *m_p_n_psxram = nullptr;
    u32 m_atapi_xferbase = 0;
    int m_atapi_xfersize = 0;
    emu_timer *m_atapi_timer = nullptr;
    const u8 *m_h8_response = nullptr;
    int m_h8_length = 0, m_h8_index = 0, m_h8_clk = 0;
    u32 m_out0 = 0, m_out1 = 0, m_out2 = 0;
    int m_jvs_input_idx_r = 0, m_jvs_input_idx_w = 0, m_jvs_output_idx_w = 0, m_jvs_output_len_w = 0;
    u8 m_jvs_input_buffer[512] = {}, m_jvs_output_buffer[512] = {};
    std::vector<OutField> m_out0_fields, m_out1_fields, m_out2_fields;
    u8 m_gx700pwbf_output_data[4] = {};   // the analog I/O board's output latches

    // ddr_state: the GN845-PWB(B) stage multiplexor
    struct stage_state { int DO = 0, clk = 0, shift = 0, state = 0, bit = 0; } m_stage_state[2];
    u32 m_stage_mask = 0xffffffff;
    u32 m_stage = 0x0f0f;     // STAGE port: panels, active low

    // inputs (active level as the port reads it)
    u32 m_in2_buttons = 0xf0f0;   // IN2 outside the stage bits: starts and buttons, active low
    u32 m_in3 = 0x0f000f00;       // IN3: P1/P2 buttons 4-6, service: active low
    u32 m_in1_coins = 0x13000000; // coins and service 1, active low
    u8 m_analog[4] = {0, 0, 0, 0};

    u32 m_outputs = 0;              // the I/O board's outputs, lit = 1, by output number
    std::vector<u8> m_lamps;        // in cab::lamps(family) order
    u64 m_lamp_changes = 0;

    void device_add_mconfig(machine_config &config) override;
    void device_start() override;   // MAME's driver start: after every other device
    void device_reset_after_children() override;   // machine_reset

    void konami573_map(address_map &map);
    void konami573a_map(address_map &map);
    void konami573d_map(address_map &map);

    // ---- handlers (ksys573.cpp) ----
    u16 flash_r(offs_t offset, u16 mem_mask);
    void flash_w(offs_t offset, u16 data, u16 mem_mask);
    u32 in0_r() { return 0xffffffff; }
    u32 in1_r();
    u16 port_in2_jvs_r(offs_t offset, u16 mem_mask);
    u32 in3_r() { return m_in3; }
    void out0_w(offs_t, u32 data, u32 mem_mask) { port_write(m_out0, m_out0_fields, data, mem_mask); }
    u16 control_r(offs_t, u16) { return m_control; }
    void control_w(offs_t, u16 data, u16 mem_mask);
    void atapi_reset_w(u16 data) { if (!(data & 1)) m_ata->reset(); }
    void lamps_w(offs_t, u32, u32) {}
    u16 jvs_input_r(offs_t, u16);
    void jvs_input_w(offs_t, u16 data, u16);
    bool jvs_is_valid_packet();
    int jvs_rx_r();
    u16 security_r(offs_t, u16) { return m_n_security_control; }
    void security_w(offs_t, u16 data, u16 mem_mask);

    void port_write(u32 &port, std::vector<OutField> &fields, u32 data, u32 mem_mask);
    void ata_interrupt(int state) { m_maincpu->subdevice<psxirq_device>("irq")->intin10(state); }
    void atapi_xfer_end(s32);
    void cdrom_dma_read(u32 *, u32 n_address, s32 n_size) { (void)n_address; (void)n_size; }
    void cdrom_dma_write(u32 *ram, u32 n_address, s32 n_size);
    void h8_clk_w(int state);
    int h8_d(int bit) { return (m_h8_response[m_h8_index] >> bit) & 1; }
    double analogue_inputs_callback(u8 input);

    // GX700-PWB(F), the analog I/O board
    u16 gx700pwbf_io_r(offs_t offset, u16 mem_mask);
    void gx700pwbf_io_w(offs_t offset, u16 data, u16 mem_mask);
    void gx700pwbf_output(int offset, u8 data);

    // ddr_state
    void gn845pwbb_do_w(int offset, int data) { m_stage_state[offset].DO = !data; }
    void gn845pwbb_clk_w(int offset, int data);
    void ddr_output_callback(offs_t offset, u8 data);
    void drmn_output_callback(offs_t offset, u8 data);
    void set_lamp(offs_t offset, bool lit);
};

const device_creator<sys573_state> sys573_state::SYS573_STATE("sys573", "Konami System 573");

// ---- configuration: konami573(), then k573d() + ddr3m() or k573a() + ddr() -------------------
void sys573_state::device_add_mconfig(machine_config &config) {
    const Config &c = *g_build;
    const bool digital = c.io_board == "digital";
    CXD8530CQ(config, m_maincpu, 67.7376_MHz_XTAL);
    m_maincpu->set_addrmap(AS_PROGRAM, digital ? address_map_constructor(&sys573_state::konami573d_map, this)
                                               : address_map_constructor(&sys573_state::konami573a_map, this));
    m_maincpu->set_ram(m_ram);
    m_maincpu->subdevice<psxdma_device>("dma")->install_read_handler(5, psxdma_device::read_delegate(&sys573_state::cdrom_dma_read, this));
    m_maincpu->subdevice<psxdma_device>("dma")->install_write_handler(5, psxdma_device::write_delegate(&sys573_state::cdrom_dma_write, this));

    RAM(config, m_ram).set_bits(32).set_default_size("4M").set_default_value(0);

    ATA_INTERFACE(config, m_ata);
    m_ata->irq_handler().set(*this, &sys573_state::ata_interrupt);
    m_ata->slot(0).option_add("cr589", CR589);
    m_ata->slot(0).set_option_machine_config("cr589", [](device_t *) {});   // MAME: cdda routes, disc regions
    m_ata->slot(0).set_default_option("cr589");

    KONAMI573_CASSETTE_SLOT(config, m_cassette);
    m_cassette->dsr_handler().set(*m_maincpu->subdevice<psxsio1_device>("sio1"), &psxsio1_device::write_dsr);

    static const char *const flash_tags[8] = {"29f016a.31m", "29f016a.27m", "29f016a.31l", "29f016a.27l",
                                              "29f016a.31j", "29f016a.27j", "29f016a.31h", "29f016a.27h"};
    for (int k = 0; k < 8; k++) m_flash[k] = &FUJITSU_29F016A(config, flash_tags[k]);

    PCCARD_SLOT(config, m_pccard1);
    m_pccard1->cd1().set([this](int state) { m_pccard_cd[0] = state; });
    PCCARD_SLOT(config, m_pccard2);
    m_pccard2->cd1().set([this](int state) { m_pccard_cd[1] = state; });

    CXD8561Q(config, m_gpu, 67.7376_MHz_XTAL / 2);
    m_gpu->set_cpu(m_maincpu);
    m_gpu->set_ram(m_gpu_ram);
    m_gpu->set_screen("screen");
    m_gpu->set_vclkn(53.693175_MHz_XTAL);
    RAM(config, m_gpu_ram).set_bits(16).set_default_size("2M").set_default_value(0);

    SCREEN(config, "screen");
    SPEAKER(config, "speaker", 2).front();

    SPU(config, m_spu, 67.7376_MHz_XTAL / 2);
    m_spu->set_cpu(m_maincpu);
    m_spu->set_ram(m_spu_ram);
    RAM(config, m_spu_ram).set_bits(16).set_default_size("512K").set_default_value(0);

    M48T58(config, "m48t58");
    ADC0834(config, "adc0834").set_input_callback(*this, &sys573_state::analogue_inputs_callback);
    SYS573_JVS_HOST(config, m_sys573_jvs_host);

    if (digital) {
        // k573d(): the digital I/O board; ddr3m(): DDR's outputs on it
        KONAMI_573_DIGITAL_IO_BOARD(config, m_k573dio, XTAL(19'660'800));
        // ddr3m(): DDR's outputs; drmn2m() to drmn10m(): DrumMania's
        if (c.family == "drmn") m_k573dio->output_callback().set(*this, &sys573_state::drmn_output_callback);
        else m_k573dio->output_callback().set(*this, &sys573_state::ddr_output_callback);
    }
    // k573a(): the analog board is the driver's own handlers (konami573a_map); init_ddr() sends
    // its outputs to ddr_output_callback (gx700pwbf_output)
    if (c.pccard2 == "32mb") {   // pccard2_32mb()
        m_pccard2->option_add("32mb", FUJITSU_32MB_FLASH_CARD);
        m_pccard2->set_default_option("32mb");
    }
    // the cassette: konami573() adds cassx(); ddr3m's cassyyi() replaces it
    // (cassyyi() and cassxxi() add the install cassette, of the game's kind)
    for (const char *slot : {"game", "install"}) {
        if (!strcmp(slot, "install") && c.install_eeprom.empty()) continue;
        if (c.cassette == "zi") m_cassette->option_add(slot, KONAMI573_CASSETTE_ZI);   // casszi(): ZS01 + DS2401
        else if (c.cassette == "yi") m_cassette->option_add(slot, KONAMI573_CASSETTE_YI);
        else if (c.cassette == "xi") m_cassette->option_add(slot, KONAMI573_CASSETTE_XI);
        else if (c.cassette == "y") m_cassette->option_add(slot, KONAMI573_CASSETTE_Y);
        else m_cassette->option_add(slot, KONAMI573_CASSETTE_X);
    }
    m_cassette->set_default_option(c.cassette_slot.c_str());
    if (c.memory_card_reader) KONAMI_573_MEMORY_CARD_READER(config, "k573mcr", m_sys573_jvs_host);

    // MAME's OUT0/OUT1/OUT2 port fields, in mask order (ioport fields are kept sorted)
    m_out0_fields = {
        {0x001, false, [this](int s) { m_adc->di_write(s); }},
        {0x002, false, [this](int s) { m_adc->cs_write(s); }},
        {0x004, false, [this](int s) { m_adc->clk_write(s); }},
        {0x100, false, [this](int s) { h8_clk_w(s); }},
    };
    m_out1_fields = {
        {0x01, false, [this](int s) { m_cassette->write_line_d0(s); }},
        {0x02, false, [this](int s) { m_cassette->write_line_d1(s); }},
        {0x04, false, [this](int s) { m_cassette->write_line_d2(s); }},
        {0x08, false, [this](int s) { m_cassette->write_line_d3(s); }},
        {0x10, false, [this](int s) { m_cassette->write_line_d4(s); }},
        {0x20, false, [this](int s) { m_cassette->write_line_d5(s); }},
        {0x40, false, [this](int s) { m_cassette->write_line_d6(s); }},
        {0x80, false, [this](int s) { m_cassette->write_line_d7(s); }},
    };
    m_out2_fields = {
        {0x40, true, [this](int s) { m_cassette->write_line_zs01_sda(s); }},
    };
    for (auto *f : {&m_out0_fields, &m_out1_fields, &m_out2_fields})
        for (OutField &o : *f) o.old = o.active_low ? 1 : 0;   // MAME: the field's default value
}

void sys573_state::konami573_map(address_map &map) {
    map(0x1f000000, 0x1f3fffff).rw(FUNC(sys573_state::flash_r), FUNC(sys573_state::flash_w));
    map(0x1f400000, 0x1f400003).r(FUNC(sys573_state::in0_r)).w(FUNC(sys573_state::out0_w));
    map(0x1f400004, 0x1f400007).r(FUNC(sys573_state::in1_r));
    map(0x1f400008, 0x1f40000b).r(FUNC(sys573_state::port_in2_jvs_r));
    map(0x1f40000c, 0x1f40000f).r(FUNC(sys573_state::in3_r));
    map(0x1f480000, 0x1f48000f).rw(m_ata, FUNC(ata_interface_device::cs0_r), FUNC(ata_interface_device::cs0_w));
    map(0x1f4c0000, 0x1f4c000f).rw(m_ata, FUNC(ata_interface_device::cs1_r), FUNC(ata_interface_device::cs1_w));
    map(0x1f500000, 0x1f500001).rw(FUNC(sys573_state::control_r), FUNC(sys573_state::control_w));
    map(0x1f560000, 0x1f560001).w(FUNC(sys573_state::atapi_reset_w));
    map(0x1f5c0000, 0x1f5c0003).nopw();   // watchdog?
    map(0x1f600000, 0x1f600003).w(FUNC(sys573_state::lamps_w));
    map(0x1f620000, 0x1f623fff).rw("m48t58", FUNC(timekeeper_device::read), FUNC(timekeeper_device::write)).umask32(0x00ff00ff);
    map(0x1f680000, 0x1f680001).rw(FUNC(sys573_state::jvs_input_r), FUNC(sys573_state::jvs_input_w));
    map(0x1f6a0000, 0x1f6a0001).rw(FUNC(sys573_state::security_r), FUNC(sys573_state::security_w));
}

void sys573_state::konami573d_map(address_map &map) {
    konami573_map(map);
    map(0x1f640000, 0x1f6400ff).m(m_k573dio, FUNC(k573dio_device::amap));
}

void sys573_state::konami573a_map(address_map &map) {
    konami573_map(map);
    map(0x1f640000, 0x1f6400ff).rw(FUNC(sys573_state::gx700pwbf_io_r), FUNC(sys573_state::gx700pwbf_io_w));
}

// MAME's driver_device starts last, once every other device has (driver_device::device_start)
void sys573_state::device_start() {
    m_atapi_timer = timer_alloc(&sys573_state::atapi_xfer_end, this);
    m_atapi_timer->adjust(attotime::never);
    memory_region *h8 = memregion(":h8_response");
    m_h8_response = h8->base();
    m_h8_length = (int)h8->bytes();
    m_h8_index = 0;
    m_stage_mask = 0xffffffff;   // ddr_state::machine_start
    m_lamps.assign(cab::lamps(cfg.family).size(), 0);
}

void sys573_state::device_reset_after_children() {
    m_n_security_control = 0;
    m_control = 0;
    m_h8_index = 0;
    m_h8_clk = 0;
    m_jvs_input_idx_r = m_jvs_input_idx_w = 0;
    m_jvs_output_idx_w = m_jvs_output_len_w = 0;
    std::fill_n(m_jvs_input_buffer, sizeof(m_jvs_input_buffer), 0);
    std::fill_n(m_jvs_output_buffer, sizeof(m_jvs_output_buffer), 0);
}

bool sys573_state::jvs_is_valid_packet() {
    if (m_jvs_input_idx_w < 5) return false;   // sync, node, size, a request byte, checksum
    if (m_jvs_input_buffer[0] != 0xe0 || m_jvs_input_buffer[1] == 0x00) return false;
    int command_size = m_jvs_input_buffer[2] + 3;
    if (m_jvs_input_idx_w < command_size) return false;
    u8 checksum = 0;
    for (int i = 1; i < command_size - 1; i++) checksum += m_jvs_input_buffer[i];
    return checksum == m_jvs_input_buffer[command_size - 1];
}

void sys573_state::jvs_input_w(offs_t, u16 data, u16) {
    m_jvs_input_buffer[m_jvs_input_idx_w++] = data & 0xff;
    m_jvs_input_buffer[m_jvs_input_idx_w++] = data >> 8;
    if (m_jvs_input_buffer[0] != 0xe0) m_jvs_input_idx_w = 0;
    if (jvs_is_valid_packet()) {
        int command_size = m_jvs_input_buffer[2] + 3;
        m_sys573_jvs_host->send_packet(m_jvs_input_buffer + 1, command_size - 2);   // no checksum
        m_jvs_input_idx_w = 0;
        m_jvs_input_idx_r = 0;
        m_jvs_input_buffer[0] = 0;
    }
}

u16 sys573_state::jvs_input_r(offs_t, u16) {
    u16 data = m_jvs_input_buffer[m_jvs_input_idx_r++];
    data |= m_jvs_input_buffer[m_jvs_input_idx_r++] << 8;
    return data;
}

int sys573_state::jvs_rx_r() {
    if (m_jvs_output_len_w <= 0) m_jvs_output_len_w = m_sys573_jvs_host->received_packet(m_jvs_output_buffer);
    return m_jvs_output_len_w > 0;
}

// ---- handlers -----------------------------------------------------------------------------
// The flash bank (MAME: an address_map_bank_device, 16-bit, banks of 4 MiB selected by
// control bits 0-5): four pairs of 8-bit 29F016As on the two byte lanes, then the cards.
u16 sys573_state::flash_r(offs_t offset, u16 mem_mask) {
    const u32 a = (u32)(m_control & 0x3f) * 0x400000 + offset * 2;
    if (a < 0x1000000) {
        const int pair = (int)(a >> 22) * 2;
        const offs_t o = (a & 0x3fffff) >> 1;
        u16 d = 0;
        if (mem_mask & 0x00ff) d |= m_flash[pair]->read(o);
        if (mem_mask & 0xff00) d |= (u16)(m_flash[pair + 1]->read(o) << 8);
        return d;
    }
    if (a >= 0x4000000 && a < 0x8000000) return m_pccard1->read_memory((a - 0x4000000) >> 1, mem_mask);
    if (a >= 0x8000000 && a < 0xc000000) return m_pccard2->read_memory((a - 0x8000000) >> 1, mem_mask);
    return 0;
}

void sys573_state::flash_w(offs_t offset, u16 data, u16 mem_mask) {
    const u32 a = (u32)(m_control & 0x3f) * 0x400000 + offset * 2;
    if (a < 0x1000000) {
        const int pair = (int)(a >> 22) * 2;
        const offs_t o = (a & 0x3fffff) >> 1;
        if (mem_mask & 0x00ff) m_flash[pair]->write(o, data & 0xff);
        if (mem_mask & 0xff00) m_flash[pair + 1]->write(o, data >> 8);
        return;
    }
    if (a >= 0x4000000 && a < 0x8000000) m_pccard1->write_memory((a - 0x4000000) >> 1, data, mem_mask);
    else if (a >= 0x8000000 && a < 0xc000000) m_pccard2->write_memory((a - 0x8000000) >> 1, data, mem_mask);
}

u32 sys573_state::in1_r() {
    // konami573 IN1: DIP switches (default: boot from flash), the H8 response, the
    // cassette's ADC and serial lines, the ADC0834, JVS sense (no devices: 1), coins
    u32 v = 0x00000007;                       // DIP 1-3 off; DIP 4 "Start Up Device" = Flash ROM
    v |= (u32)h8_d(0) << 4 | (u32)h8_d(1) << 5 | (u32)h8_d(2) << 6 | (u32)h8_d(3) << 7;
    v |= (u32)(m_cassette->read_line_adc083x_do() & 1) << 8;
    v |= (u32)(m_cassette->read_line_adc083x_sars() & 1) << 9;
    v |= 0x00001000;                          // "Network?" off
    v |= (u32)(m_cassette->read_line_ds2401() & 1) << 14;
    v |= (u32)(m_adc->do_read() & 1) << 16;
    v |= (u32)(m_cassette->read_line_secflash_sda() & 1) << 18;
    v |= (u32)(m_sys573_jvs_host->jvs_sense_r() & 1) << 19;
    v |= (u32)(jvs_rx_r() & 1) << 20;
    v |= m_in1_coins;
    v |= (u32)(m_pccard_cd[0] & 1) << 26;
    v |= (u32)(m_pccard_cd[1] & 1) << 27;
    return v;
}

u16 sys573_state::port_in2_jvs_r(offs_t offset, u16) {
    if (offset == 0) return (u16)((m_in2_buttons & ~0x0f0fu) | ((m_stage & m_stage_mask) & 0x0f0fu));
    // 0x1f40000a: the JVS reply, two bytes at a time
    if (m_jvs_output_len_w <= 0) return 0;
    u16 data = m_jvs_output_buffer[m_jvs_output_idx_w] | (m_jvs_output_buffer[m_jvs_output_idx_w + 1] << 8);
    m_jvs_output_idx_w += 2;
    if (m_jvs_output_idx_w >= m_jvs_output_len_w) {
        m_jvs_output_idx_w = 0;
        m_jvs_output_len_w = 0;
    }
    return data;
}

void sys573_state::port_write(u32 &port, std::vector<OutField> &fields, u32 data, u32 mem_mask) {
    COMBINE_DATA(&port);
    for (OutField &f : fields) {
        const u32 def = f.active_low ? f.mask : 0;
        u32 v = ((port ^ def) & f.mask) ? 1 : 0;
        if (v != f.old) {
            f.old = v;
            f.write((int)v);
        }
    }
}

void sys573_state::control_w(offs_t, u16 data, u16 mem_mask) {
    COMBINE_DATA(&m_control);
    port_write(m_out2, m_out2_fields, data, mem_mask);
}

void sys573_state::security_w(offs_t, u16 data, u16 mem_mask) {
    COMBINE_DATA(&m_n_security_control);
    port_write(m_out1, m_out1_fields, data, mem_mask);
}

void sys573_state::atapi_xfer_end(s32) {
    m_atapi_timer->adjust(attotime::never);
    for (int i = 0; i < m_atapi_xfersize; i++) {
        u32 d = m_ata->cs0_r(0) << 0;
        d |= m_ata->cs0_r(0) << 16;
        m_p_n_psxram[m_atapi_xferbase / 4] = d;
        m_atapi_xferbase += 4;
    }
    // MAME's HACK: konami80s only works if you dma more data than requested
    if ((m_ata->cs1_r(6) & 8) != 0)
        m_atapi_timer->adjust(m_maincpu->cycles_to_attotime(ATAPI_CYCLES_PER_SECTOR * (m_atapi_xfersize / 64)));
}

void sys573_state::cdrom_dma_write(u32 *ram, u32 n_address, s32 n_size) {
    m_p_n_psxram = ram;
    m_atapi_xferbase = n_address;
    m_atapi_xfersize = n_size;
    m_atapi_timer->adjust(m_maincpu->cycles_to_attotime(ATAPI_CYCLES_PER_SECTOR * (n_size / 512)));
}

void sys573_state::h8_clk_w(int state) {
    if (m_h8_clk != state) {
        if (state && m_h8_index < m_h8_length - 1) m_h8_index++;
        m_h8_clk = state;
    }
}

double sys573_state::analogue_inputs_callback(u8 input) {
    switch (input) {
    case ADC083X_CH0: return (double)(5 * m_analog[0]) / 255.0;
    case ADC083X_CH1: return (double)(5 * m_analog[1]) / 255.0;
    case ADC083X_CH2: return (double)(5 * m_analog[2]) / 255.0;
    case ADC083X_CH3: return (double)(5 * m_analog[3]) / 255.0;
    case ADC083X_AGND: return 0;
    case ADC083X_VREF: return 5;
    }
    return 0;
}

// GX700-PWB(F), the analog I/O board: four 8-bit output latches; reads return 0 (MAME)
u16 sys573_state::gx700pwbf_io_r(offs_t, u16) { return 0; }

void sys573_state::gx700pwbf_io_w(offs_t offset, u16 data, u16) {
    switch (offset) {
    case 0x40: gx700pwbf_output(0, data & 0xff); break;
    case 0x44: gx700pwbf_output(1, data & 0xff); break;
    case 0x48: gx700pwbf_output(2, data & 0xff); break;
    case 0x4c: gx700pwbf_output(3, data & 0xff); break;
    }
}

void sys573_state::gx700pwbf_output(int offset, u8 data) {
    static const int shift[] = {7, 6, 1, 0, 5, 4, 3, 2};
    for (int i = 0; i < 8; i++) {
        int oldbit = (m_gx700pwbf_output_data[offset] >> shift[i]) & 1;
        int newbit = (data >> shift[i]) & 1;
        if (oldbit != newbit) ddr_output_callback((offs_t)(offset * 8 + i), (u8)newbit);
    }
    m_gx700pwbf_output_data[offset] = data;
}

// ddr_state: GN845-PWB(B), the DDR stage multiplexor
void sys573_state::gn845pwbb_clk_w(int offset, int data) {
    static const int mask[] = {0, 6, 2, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 6};
    int clk = !data;
    stage_state &st = m_stage_state[offset];
    if (clk == st.clk) return;
    st.clk = clk;
    if (!clk) return;
    st.shift = (st.shift >> 1) | (st.DO << 12);
    switch (st.state) {
    case 0:   // DDR_STAGE_IDLE
        if (st.shift == 0xc90) {
            st.state = 1;
            st.bit = 0;
            m_stage_mask = 0xfffff9f9;
        }
        break;
    case 1:   // DDR_STAGE_INIT
        st.bit++;
        if (st.bit < 22) {
            int a = ((((~0x06) | mask[m_stage_state[0].bit]) & 0xff) << 8);
            int b = ((((~0x06) | mask[m_stage_state[1].bit]) & 0xff) << 0);
            m_stage_mask = 0xffff0000 | a | b;
        } else {
            st.bit = 0;
            st.state = 0;
            m_stage_mask = 0xffffffff;
        }
        break;
    }
}

void sys573_state::ddr_output_callback(offs_t offset, u8 data) {
    switch (offset) {
    case 4: gn845pwbb_do_w(0, !data); return;
    case 7: gn845pwbb_clk_w(0, !data); return;
    case 12: gn845pwbb_do_w(1, !data); return;
    case 15: gn845pwbb_clk_w(1, !data); return;
    }
    // the cabinet's lamps (MAME's outputs): lit = data 0; the digital board's speaker neon (28)
    // is the analog board's 30
    if (offset == 28) offset = 30;
    set_lamp(offset, !data);
}

// MAME's drmn_output_callback (ksys573.cpp), the digital board's numbering (DrumMania 2nd and
// later): the drum lamps and the spots are lit at data 0, the neons, Start and Select at data 1
void sys573_state::drmn_output_callback(offs_t offset, u8 data) {
    switch (offset) {
    case 0: case 1: case 2: case 3: case 8: case 12: set_lamp(offset, !data); break;
    case 9: case 11: case 13: case 14: set_lamp(offset, data != 0); break;
    default: break;
    }
}

void sys573_state::set_lamp(offs_t offset, bool lit) {
    if (offset >= 32) return;
    const u32 bit = 1u << offset, was = m_outputs;
    m_outputs = lit ? m_outputs | bit : m_outputs & ~bit;
    if (m_outputs == was) return;
    const auto &defs = cab::lamps(cfg.family);
    m_lamps.resize(defs.size());
    for (size_t k = 0; k < defs.size(); k++) m_lamps[k] = (u8)((m_outputs >> defs[k].bit) & 1);
    m_lamp_changes++;
}

// ---- the machine --------------------------------------------------------------------------
struct Board {
    std::unique_ptr<running_machine> machine;
    sys573_state *state = nullptr;
    Config cfg;
    FrameHook frame_hook;
    AudioSink audio_sink;
    // the digital board's MP3 (MAS3507D) at 44.1 kHz, resampled for the host when the MP3 is
    // another rate (the emulation never sees this), and where the resampler is between samples
    std::deque<float> mp3[2];
    double mp3_pos = 0;
    float mp3_peak = 0;   // the MP3's largest |sample|, and its rate (the report)
    u32 mp3_rate = 0;
    u64 frames = 0;
    float sound_peak = 0;     // the mix's largest |sample| (1.0 = full scale)
    u64 sound_over = 0;       // samples past full scale (clipped at the output)
    u64 sound_samples = 0;
    std::vector<u32> frame_rgb;
} g;

std::string nv(const char *name) { return g.cfg.nvram_dir.empty() ? std::string() : g.cfg.nvram_dir + "/" + name; }

}  // namespace

bool init(const Config &cfg) {
    g.cfg = cfg;
    auto rom = [&](const std::string &f) { return cfg.rom_dir + "/" + f; };
    g.machine = std::make_unique<running_machine>();
    running_machine &m = *g.machine;
    m.m_exact = true;
    m.m_base_time = cfg.rtc_time;
    if (!cfg.nvram_dir.empty()) { mkdir(cfg.nvram_dir.c_str(), 0755); m.m_nvram_dir = cfg.nvram_dir; }

    struct R { const char *tag; std::string file; } regions[] = {
        {":maincpu:rom", rom(cfg.bios)},
        {":h8_response", rom(cfg.h8)},
        {":cassette:game:eeprom", rom(cfg.cassette_eeprom)},
        {":cassette:game:id", cfg.cassette_id.empty() ? "" : rom(cfg.cassette_id)},           // the "i" cassettes
        {":cassette:install:eeprom", cfg.install_eeprom.empty() ? "" : rom(cfg.install_eeprom)},
        {":cassette:install:id", cfg.install_id.empty() ? "" : rom(cfg.install_id)},
        {":k573dio:digital_id", cfg.io_board == "digital" ? rom(cfg.digital_id) : ""},
    };
    for (auto &r : regions) {
        if (r.file.empty()) continue;
        std::vector<u8> d = load_file(r.file);
        if (d.empty()) { fprintf(stderr, "cannot read %s\n", r.file.c_str()); return false; }
        m.add_region(r.tag, std::move(d));
    }
    cdrom_image_device::hk_set_path(":ata:0:cr589:image", cfg.cd);

    g_build = &g.cfg;
    sys573_state &st = sys573_state::SYS573_STATE(m.config(), "");
    g.state = &st;
    st.cfg = cfg;
    st.m_screen->m_hk_frame = [](screen_device &screen) {
        g.frames++;
        if (!g.frame_hook) return;
        const rectangle &vis = screen.visible_area();
        const int w = vis.width(), h = vis.height();
        g.frame_rgb.resize((size_t)w * h);
        // raw rows: through pix() and the vectors, every store reloads the bitmap's pointers
        u32 *__restrict dst = g.frame_rgb.data();
        for (int y = 0; y < h; y++) {
            const u32 *__restrict src = &screen.m_bitmap.pix(vis.top() + y, vis.left());
            for (int x = 0; x < w; x++) dst[x] = src[x] | 0xff000000u;
            dst += w;
        }
        g.frame_hook(g.frames, g.frame_rgb.data(), w, h);
    };
    m.m_after_nvram_load = [&m] {
        // MAME's set_rtc_datetime: battery-backed RTCs get the power-on time
        for (device_rtc_interface *rtc : device_interface_enumerator<device_rtc_interface>(*m.m_devices.front()))
            if (rtc->has_battery()) rtc->set_current_time(system_time(m.m_base_time));
    };
    m.start();
    m.reset();

    // HK_573_ITRACE=file,from,to: every instruction of frames [from, to) as "totalcycles pc"
    // (to compare with MAME's debugger trace, oracle/lua/itrace573.lua)
    if (const char *t = getenv("HK_573_ITRACE")) {
        static FILE *tf = nullptr;
        static u64 from = 0, to = 0;
        char path[512];
        unsigned long long a = 0, b = 0;
        if (sscanf(t, "%511[^,],%llu,%llu", path, &a, &b) == 3) {
            tf = fopen(path, "w");
            from = a; to = b;
            cpu_device *cpu = st.m_maincpu.target();
            cpu->m_hk_trace = [cpu](offs_t pc) {
                if (g.frames >= from && g.frames < to && tf) fprintf(tf, "%llu %08x\n", (unsigned long long)cpu->total_cycles(), pc);
                else if (g.frames >= to && tf) { fclose(tf); tf = nullptr; }
            };
        }
    }
    mips::init(*st.m_maincpu);
    return true;
}

void set_frame_hook(FrameHook h) { g.frame_hook = std::move(h); }
void set_audio_sink(AudioSink s) { g.audio_sink = std::move(s); }

[[noreturn]] void run() {
    running_machine &m = *g.machine;
    for (;;) {
        m.timeslice();
        // sound: MAME's speaker, the SPU plus the CR-589's CD audio (both 44.1 kHz, routed at 1.0;
        // the CD at its MODE SELECT volume, set_output_gain), plus the digital board's MP3 (routed
        // at 1.0)
        if (g.audio_sink) {
            static sound_stream *spu = nullptr, *cd = nullptr, *mp3 = nullptr;
            if (!spu)
                for (sound_stream *s : m.m_streams) {
                    if (s->m_device == g.state->m_spu.target()) spu = s;
                    else if (dynamic_cast<cdda_device *>(s->m_device) && s->sample_rate() == 44100) cd = s;
                    else if (dynamic_cast<mas3507d_device *>(s->m_device)) mp3 = s;
                }
            if (mp3) {
                // what its 50 Hz updates produced, never more: the MAS3507D decodes and asks the
                // FPGA for data as its stream updates, so updating it here would change the machine
                auto &ml = mp3->m_out[0], &mr = mp3->m_out[1];
                const size_t avail = std::min(ml.size(), mr.size());
                const u32 rate = mp3->sample_rate();
                g.mp3_rate = rate;
                for (size_t i = 0; i < avail; i++) g.mp3_peak = std::max({g.mp3_peak, std::fabs(ml[i]), std::fabs(mr[i])});
                size_t used = avail;
                if (rate == 44100 || !rate) {
                    g.mp3[0].insert(g.mp3[0].end(), ml.begin(), ml.begin() + avail);
                    g.mp3[1].insert(g.mp3[1].end(), mr.begin(), mr.begin() + avail);
                } else {   // linear, from rate to 44.1 kHz
                    const double step = rate / 44100.0;
                    while (g.mp3_pos + 1 < (double)avail) {
                        const size_t i = (size_t)g.mp3_pos;
                        const float f = (float)(g.mp3_pos - (double)i);
                        g.mp3[0].push_back(ml[i] + (ml[i + 1] - ml[i]) * f);
                        g.mp3[1].push_back(mr[i] + (mr[i + 1] - mr[i]) * f);
                        g.mp3_pos += step;
                    }
                    used = std::min(avail, (size_t)g.mp3_pos);
                    g.mp3_pos -= (double)used;
                }
                ml.erase(ml.begin(), ml.begin() + used);
                mr.erase(mr.begin(), mr.begin() + used);
            }
            if (spu) {
                // the CD stream to now, so the SPU's samples don't wait for its 50 Hz update (the
                // game sees the same either way: get_audio_lba() updates the stream itself)
                if (cd) cd->update();
                auto &l = spu->m_out[0], &r = spu->m_out[1];
                size_t n = std::min(l.size(), r.size());
                if (cd) n = std::min({n, cd->m_out[0].size(), cd->m_out[1].size()});
                if (mp3) n = std::min(n, g.mp3[0].size());
                if (n) {
                    std::vector<float> lr(n * 2);
                    for (size_t i = 0; i < n; i++) { lr[2 * i] = l[i]; lr[2 * i + 1] = r[i]; }
                    l.erase(l.begin(), l.begin() + n);
                    r.erase(r.begin(), r.begin() + n);
                    if (cd) {
                        auto *cdd = static_cast<cdda_device *>(cd->m_device);
                        for (int ch = 0; ch < 2; ch++) {
                            auto &c = cd->m_out[ch];
                            const float gain = cdd->output_gain(ch);
                            for (size_t i = 0; i < n; i++) lr[2 * i + ch] += c[i] * gain;
                            c.erase(c.begin(), c.begin() + n);
                        }
                    }
                    if (mp3) {
                        for (int ch = 0; ch < 2; ch++) {
                            auto &q = g.mp3[ch];
                            for (size_t i = 0; i < n; i++) lr[2 * i + ch] += q[i];
                            q.erase(q.begin(), q.begin() + n);
                        }
                    }
                    for (float &v : lr) {
                        v *= g.cfg.mix_gain;
                        const float a = std::fabs(v);
                        if (a > g.sound_peak) g.sound_peak = a;
                        if (a > 1.0f) g.sound_over++;
                    }
                    g.sound_samples += lr.size();
                    g.audio_sink(lr.data(), (int)n);
                }
            }
        } else {
            for (sound_stream *s : m.m_streams) for (auto &o : s->m_out) o.clear();
        }
    }
}

void save_nvram() {
    if (g.machine) g.machine->nvram_save();
    mips::finish();
}

double seconds() { return g.machine ? g.machine->m_basetime.as_double() : 0.0; }
uint64_t frame_count() { return g.frames; }
const std::string &family() { return g.cfg.family; }

bool set_input(const std::string &name, int value) {
    sys573_state &st = *g.state;
    auto bit = [&](u32 &port, u32 mask) { if (value) port &= ~mask; else port |= mask; return true; };   // active low
    static const std::map<std::string, std::pair<int, u32>> names = {
        // STAGE (ddr): P1 0x100 left, 0x200 right, 0x400 up, 0x800 down; P2 0x1-0x8
        {"P1 Left", {0, 0x100}}, {"P1 Right", {0, 0x200}}, {"P1 Up", {0, 0x400}}, {"P1 Down", {0, 0x800}},
        {"P2 Left", {0, 0x001}}, {"P2 Right", {0, 0x002}}, {"P2 Up", {0, 0x004}}, {"P2 Down", {0, 0x008}},
        // IN2
        {"P1 Button 1", {1, 0x1000}}, {"P1 Button 2", {1, 0x2000}}, {"P1 Button 3", {1, 0x4000}}, {"1 Player Start", {1, 0x8000}},
        {"P2 Button 1", {1, 0x0010}}, {"P2 Button 2", {1, 0x0020}}, {"P2 Button 3", {1, 0x0040}}, {"2 Players Start", {1, 0x0080}},
        // IN3
        {"P1 Button 4", {2, 0x100}}, {"P1 Button 5", {2, 0x200}}, {"Service Mode", {2, 0x400}}, {"P1 Button 6", {2, 0x800}},
        {"P2 Button 4", {2, 0x1000000}}, {"P2 Button 5", {2, 0x2000000}}, {"P2 Button 6", {2, 0x8000000}},
        // IN1
        {"Coin 1", {3, 0x1000000}}, {"Coin 2", {3, 0x2000000}}, {"Service 1", {3, 0x10000000}},
        // DrumMania (MAME's drmn ports): the pads on the stage bits, the cymbal and the bass pedal
        // on IN2's buttons, the select buttons on player 2's left and right
        {"High Tom", {0, 0x100}}, {"Low Tom", {0, 0x200}}, {"Hi-Hat", {0, 0x400}}, {"Snare", {0, 0x800}},
        {"Select L", {0, 0x001}}, {"Select R", {0, 0x002}}, {"Cymbal", {1, 0x1000}}, {"Bass Drum", {1, 0x4000}},
    };
    // the shell's cabinet names for MAME's (runtime/shell/cabinet.cpp)
    auto it = names.find(name == "Test" ? "Service Mode" : name == "Service" ? "Service 1" : name);
    if (it == names.end()) return false;
    switch (it->second.first) {
    case 0: return bit(st.m_stage, it->second.second);
    case 1: return bit(st.m_in2_buttons, it->second.second);
    case 2: return bit(st.m_in3, it->second.second);
    default: return bit(st.m_in1_coins, it->second.second);
    }
}

const std::vector<uint8_t> &lamp_state() { return g.state->m_lamps; }
std::string sound_report() {
    if (!g.sound_samples) return {};
    char b[160];
    snprintf(b, sizeof b, "sound: mix peak %.2f of full scale, %.3f %% of samples over it", g.sound_peak,
             100.0 * (double)g.sound_over / (double)g.sound_samples);
    std::string out = b;
    if (g.mp3_rate) {
        snprintf(b, sizeof b, "; the MP3 peaks at %.2f, at %u Hz", g.mp3_peak, g.mp3_rate);
        out += b;
    }
    return out;
}

uint64_t lamp_changes() { return g.state->m_lamp_changes; }

const uint8_t *mem(uint32_t addr, uint32_t len) {
    // main RAM through any of its mirrors (KUSEG, KSEG0, KSEG1), little-endian as the guest sees it
    const u32 phys = addr & 0x1fffffff;
    ram_device &ram = *g.state->m_ram;
    if (phys + len > ram.size()) return nullptr;
    return ram.pointer() + phys;
}

uint32_t pc() { return g.state ? static_cast<cpu_device &>(*g.state->m_maincpu).pc() : 0; }

}  // namespace hk::k573
