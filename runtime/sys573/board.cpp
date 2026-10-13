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
#include "sys573/link.h"
#include "sys573/npu.h"
#include "imagedev/cdromimg.h"
#include "k573cass.h"
#include "k573dio.h"
#include "k573mcr.h"
#include "k573msu.h"
#include "sound/tc9446f.h"
#include "cpu/psx/sio_uart.h"
#include "machine/ins8250.h"
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
SessionLink g_link;   // the session cable to another cabinet's process (link.h), when there is one

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
    // MAME's LAMPS port: GuitarFreaks' lamps (gtrfrks_lamps_b4..b7, active low); nothing else
    void lamps_w(offs_t, u32 data, u32 mem_mask) {
        if (cfg.family != "gtrf" || !(mem_mask & 0xf0)) return;
        for (int k = 0; k < 4; k++) set_lamp(k, !(data & (0x80u >> k)));   // spot L, spot R, start L, start R
    }
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
    if (c.msu) {
        // the multi-session unit on SIO1 (a UART then: hk_psx_sio1_uart), as the fork's msu_local()
        auto &sio1 = *m_maincpu->subdevice<psxsio1_uart_device>("sio1");
        m_cassette->dsr_handler().set(sio1, &psxsio1_uart_device::write_dsr);
        auto &msu = KONAMI_573_MULTI_SESSION_UNIT(config, "k573msu", 0);
        ns16550_device &uart = msu.link_uart();
        sio1.txd_handler().set(uart, &ins8250_uart_device::rx_w);
        sio1.dtr_handler().set(uart, &ins8250_uart_device::dsr_w);
        sio1.rts_handler().set(uart, &ins8250_uart_device::cts_w);
        uart.out_tx_callback().set(sio1, &psxsio1_uart_device::write_rxd);
        uart.out_rts_callback().set(sio1, &psxsio1_uart_device::write_cts);
        // not the unit's DTR: SIO1's DSR is the cassette's presence (the fork wires both to it, and
        // the unit's firmware drops DTR as it sets up its UARTs, so the game lost its cassette:
        // ERROR 120)
        if (!c.session_host.empty()) {
            // the other cabinet (GuitarFreaks) on the unit's second port, the fork's msu_port2,
            // wired as SIO1 is to the first
            auto &port2 = *msu.subdevice<ns16550_device>("duart_com_0:chan0");
            port2.out_tx_callback().set([](int s) { g_link.out(SessionLink::TXD, s); });
            port2.out_dtr_callback().set([](int s) { g_link.out(SessionLink::DTR, s); });
            port2.out_rts_callback().set([](int s) { g_link.out(SessionLink::RTS, s); });
            g_link.in = [&port2](SessionLink::Line l, int s) {
                if (l == SessionLink::TXD) port2.rx_w(s);
                else if (l == SessionLink::DTR) port2.dsr_w(s);
                else port2.cts_w(s);
            };
        }
    } else if (!c.session_join.empty()) {
        // a cabinet joining another's multi-session unit (the fork's msu_remote): SIO1 a UART on
        // the session cable; the unit's TXD and RTS come back, not its DTR (SIO1's DSR is the
        // cassette's, as above)
        auto &sio1 = *m_maincpu->subdevice<psxsio1_uart_device>("sio1");
        m_cassette->dsr_handler().set(sio1, &psxsio1_uart_device::write_dsr);
        sio1.txd_handler().set([](int s) { g_link.out(SessionLink::TXD, s); });
        sio1.dtr_handler().set([](int s) { g_link.out(SessionLink::DTR, s); });
        sio1.rts_handler().set([](int s) { g_link.out(SessionLink::RTS, s); });
        g_link.in = [&sio1](SessionLink::Line l, int s) {
            if (l == SessionLink::TXD) sio1.write_rxd(s);
            else if (l == SessionLink::RTS) sio1.write_cts(s);
        };
    } else {
        m_cassette->dsr_handler().set(*m_maincpu->subdevice<psxsio1_device>("sio1"), &psxsio1_device::write_dsr);
    }

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
    if (c.pccard1 == "32mb") {   // pccard1_32mb() (MAME also offers id245p01 there)
        m_pccard1->option_add("32mb", FUJITSU_32MB_FLASH_CARD);
        m_pccard1->set_default_option("32mb");
    }
    if (c.pccard2 == "32mb") {   // pccard2_32mb()
        m_pccard2->option_add("32mb", FUJITSU_32MB_FLASH_CARD);
        m_pccard2->set_default_option("32mb");
    } else if (c.pccard2 == "npu") {   // the network PCB unit (MAME: a stub, not fitted), high-level
        m_pccard2->option_add("npu", KONAMI_573_NPU_HLE);
        m_pccard2->set_default_option("npu");
        const std::string nv = c.nvram_dir;
        m_pccard2->set_option_machine_config("npu", [nv](device_t *d) {
            static_cast<k573npu_hle_device *>(d)->set_dirs(nv.empty() ? "" : nv + "/npu_hdd", nv.empty() ? "" : nv + "/npu_ram");
        });
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
    if (cfg.family == "gtrf") m_stage &= ~0x0303u;   // gtrfrks: the effect knobs (dials) at 0
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
    // the MP3 decoders: the digital board's MAS3507D and the multi-session unit's four TC9446Fs,
    // each at 44.1 kHz, resampled for the host when the MP3 is another rate (the emulation never
    // sees this), and where its resampler is between samples
    struct Decoder {
        sound_stream *stream;
        float gain;               // MAME's route
        bool msu;
        std::deque<float> q[2];
        double pos = 0;
        float peak = 0;           // its largest |sample|, and its rate (the report)
        u32 rate = 0;
    };
    std::vector<Decoder> decoders;
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
    if (cfg.msu) {
        cdrom_image_device::hk_set_path(":k573msu:ata_cdrom:0:cdrom:image", cfg.msu_cd);
        // the unit's TX3927 firmware: ROM_LOAD16_WORD_SWAP into a big-endian 32-bit region, so each
        // 32-bit word is the file's first 16-bit (little-endian) word above its second; held as host
        // (little-endian) words, the two halves of every 32-bit word swap
        std::vector<u8> fw = load_file(rom(cfg.msu_flash)), rtc = load_file(rom(cfg.msu_rtc)),
                        id = load_file(rom(cfg.digital_id));
        if (fw.size() != 0x80000 || rtc.empty() || id.empty()) {
            fprintf(stderr, "the multi-session unit needs %s, %s and %s in %s\n", cfg.msu_flash.c_str(), cfg.msu_rtc.c_str(),
                    cfg.digital_id.c_str(), cfg.rom_dir.c_str());
            return false;
        }
        for (size_t k = 0; k < fw.size(); k += 4) {
            std::swap(fw[k], fw[k + 2]);
            std::swap(fw[k + 1], fw[k + 3]);
        }
        m.add_region(":k573msu:tmpr3927", std::move(fw));
        m.add_region(":k573msu:m48t58y", std::move(rtc));
        m.add_region(":k573msu:digital_id", std::move(id));
    }
    hk_psx_sio1_uart = cfg.msu || !cfg.session_join.empty();
    if (!cfg.session_host.empty() && !cfg.msu) {
        fprintf(stderr, "--session-host: %s has no multi-session unit (start the DrumMania side as the host)\n", cfg.game.c_str());
        return false;
    }
    if (!cfg.session_host.empty() || !cfg.session_join.empty()) {
        const bool host = !cfg.session_host.empty();
        if (!g_link.open(host ? cfg.session_host : cfg.session_join, host, attotime::from_usec(cfg.session_quantum_us)))
            return false;
    }

    g_build = &g.cfg;
    sys573_state &st = sys573_state::SYS573_STATE(m.config(), "");
    g.state = &st;
    st.cfg = cfg;
    st.m_screen->m_hk_frame = [](screen_device &screen) {
        g.frames++;
        // HK_MSU_TRACE=1: where the multi-session unit's CPU is, every 300 frames
        static const bool msu_trace = getenv("HK_MSU_TRACE") != nullptr;
        if (msu_trace && g.frames % 300 == 0)
            if (auto *msu = g.state->subdevice<cpu_device>("k573msu:maincpu"))
                fprintf(stderr, "f%llu msu pc %08x cycles %llu\n", (unsigned long long)g.frames, msu->pc(), (unsigned long long)msu->total_cycles());
        // HK_MSU_HASH=file: every 60 frames, the unit's PC, cycle count and a hash of its RAM (to
        // check that a change meant to be invisible leaves the unit as it was: the 573's digest
        // doesn't see it)
        static FILE *msu_hash = getenv("HK_MSU_HASH") ? fopen(getenv("HK_MSU_HASH"), "w") : nullptr;
        if (msu_hash && g.frames % 60 == 0)
            if (auto *msu = g.state->subdevice<cpu_device>("k573msu:maincpu")) {
                ram_device *ram = g.state->subdevice<ram_device>("k573msu:ram");
                const u64 *w = (const u64 *)ram->pointer();
                u64 h = 0xcbf29ce484222325ull;
                for (size_t i = 0; i < ram->size() / 8; i++) h = (h ^ w[i]) * 0x100000001b3ull;
                fprintf(msu_hash, "%llu %08x %llu %016llx\n", (unsigned long long)g.frames, msu->pc(),
                        (unsigned long long)msu->total_cycles(), (unsigned long long)h);
                fflush(msu_hash);
            }
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
    g_link.attach(m);
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
        // at 1.0) and the multi-session unit's four (0.65 each, as windyfairy's fork routes them)
        if (g.audio_sink) {
            static sound_stream *spu = nullptr, *cd = nullptr;
            if (!spu)
                for (sound_stream *s : m.m_streams) {
                    if (s->m_device == g.state->m_spu.target()) spu = s;
                    else if (dynamic_cast<cdda_device *>(s->m_device) && s->sample_rate() == 44100) cd = s;
                    else if (dynamic_cast<mas3507d_device *>(s->m_device)) g.decoders.push_back({s, 1.0f, false});
                    else if (dynamic_cast<tc9446f_device *>(s->m_device)) g.decoders.push_back({s, 0.65f, true});
                }
            for (Board::Decoder &d : g.decoders) {
                // what its 50 Hz updates produced, never more: the decoders decode and ask for data
                // as their streams update, so updating one here would change the machine
                auto &ml = d.stream->m_out[0], &mr = d.stream->m_out[1];
                const size_t avail = std::min(ml.size(), mr.size());
                const u32 rate = d.stream->sample_rate();
                d.rate = rate;
                for (size_t i = 0; i < avail; i++) d.peak = std::max({d.peak, std::fabs(ml[i]), std::fabs(mr[i])});
                size_t used = avail;
                if (rate == 44100 || !rate) {
                    d.q[0].insert(d.q[0].end(), ml.begin(), ml.begin() + avail);
                    d.q[1].insert(d.q[1].end(), mr.begin(), mr.begin() + avail);
                } else {   // linear, from rate to 44.1 kHz
                    const double step = rate / 44100.0;
                    while (d.pos + 1 < (double)avail) {
                        const size_t i = (size_t)d.pos;
                        const float f = (float)(d.pos - (double)i);
                        d.q[0].push_back(ml[i] + (ml[i + 1] - ml[i]) * f);
                        d.q[1].push_back(mr[i] + (mr[i + 1] - mr[i]) * f);
                        d.pos += step;
                    }
                    used = std::min(avail, (size_t)d.pos);
                    d.pos -= (double)used;
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
                for (const Board::Decoder &d : g.decoders) n = std::min(n, d.q[0].size());
                // joined to another cabinet's unit: its music too (link.h), as the host waits for it
                if (!g.cfg.session_join.empty() && g_link.is_open()) n = std::min(n, g_link.audio_available());
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
                    // the multi-session unit's share, which the other cabinet in a session plays
                    // too (link.h); and theirs, when this cabinet joined another's unit
                    const bool host = g_link.is_open() && g.cfg.msu;
                    std::vector<float> unit(host ? n * 2 : 0);
                    for (Board::Decoder &d : g.decoders)
                        for (int ch = 0; ch < 2; ch++) {
                            auto &q = d.q[ch];
                            for (size_t i = 0; i < n; i++) lr[2 * i + ch] += q[i] * d.gain;
                            if (host && d.msu)
                                for (size_t i = 0; i < n; i++) unit[2 * i + ch] += q[i] * d.gain;
                            q.erase(q.begin(), q.begin() + n);
                        }
                    if (host) g_link.send_audio(unit.data(), n);
                    else if (!g.cfg.session_join.empty()) g_link.mix_audio(lr.data(), n);
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
        // GuitarFreaks (MAME's gtrfrks ports): the neck buttons on IN2's buttons, the pick and the
        // wailing on the stage bits; Service on IN3 (IN1's is unused)
        {"P1 Button R", {1, 0x1000}}, {"P1 Button G", {1, 0x2000}}, {"P1 Button B", {1, 0x4000}},
        {"P1 Pick", {0, 0x400}}, {"P1 Wailing", {0, 0x800}},
        {"P2 Button R", {1, 0x0010}}, {"P2 Button G", {1, 0x0020}}, {"P2 Button B", {1, 0x0040}},
        {"P2 Pick", {0, 0x004}}, {"P2 Wailing", {0, 0x008}},
        {"Service 1 (IN3)", {2, 0x100}},
    };
    // the effect knobs: MAME's dials, a 2-bit position (not active low) on the stage bits
    if (name == "P1 Effect Knob" || name == "P2 Effect Knob") {
        const int sh = name[1] == '1' ? 8 : 0;
        st.m_stage = (st.m_stage & ~(3u << sh)) | ((u32)(-value & 3) << sh);   // PORT_REVERSE
        return true;
    }
    // the shell's cabinet names for MAME's (runtime/shell/cabinet.cpp)
    const char *service = g.cfg.family == "gtrf" ? "Service 1 (IN3)" : "Service 1";
    auto it = names.find(name == "Test" ? "Service Mode" : name == "Service" ? service : name);
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
    std::string msu;
    u32 msu_rate = 0;
    for (const Board::Decoder &d : g.decoders) {
        if (d.msu) {
            snprintf(b, sizeof b, "%s%.2f", msu.empty() ? "" : "/", d.peak);
            msu += b;
            msu_rate = std::max(msu_rate, d.rate);
            continue;
        }
        snprintf(b, sizeof b, "; the MP3 peaks at %.2f, at %u Hz", d.peak, d.rate);
        out += b;
    }
    if (!msu.empty()) out += "; the unit's MP3s peak at " + msu + ", at " + std::to_string(msu_rate) + " Hz";
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
