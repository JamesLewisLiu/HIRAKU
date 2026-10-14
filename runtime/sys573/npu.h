// SPDX-License-Identifier: GPL-3.0-or-later
// The network PCB unit (GUC07, "System 573 Hard Drive and Network Unit") of the e-AMUSEMENT
// GuitarFreaks and DrumMania (9th to 11th): a TX3927 box with an Ethernet port and a 10 GB IDE
// hard disk, cabled to a PC card in the 573's slot 2. MAME has only a stub (k573npu); its
// firmware is not needed here.
//
// High-level, after windyfairy's HLE (github.com/987123879113/mame, branch npu_hle, commit
// 45e5eaa1, BSD-3-Clause): the 573 game drives the unit through a 16-bit FIFO in the card's
// memory window, with remote calls (PLAN.md section 19, "e-AMUSEMENT"):
//   - 0x00/0x01/0x37/0x38, 0x80-0x82: the unit's start-up (its id, MAC address, code upload);
//   - 0x22-0x2e: BSD sockets (the game does HTTP and the e-AMUSEMENT protocol itself), on the
//     virtual LAN of runtime/eamuse/lan.h;
//   - 0x3a: the network settings (address, gateway, DNS, the local domain);
//   - 0x40-0x54: files on the hard disk ("atam:...") or a RAM disk ("ram:..."), much like the
//     PS2's iomanX.
// The hard disk is a folder (`hdd_dir`, in the save folder) rather than a disk image: the game
// only ever asks for files. A FORMAT empties it and writes `.format`, the partition figures
// the game reads back (devctl 2); without `.format` the disk doesn't mount, as an unformatted one.
// The RAM disk is a folder too, emptied at power-on.
//
// HK_573_NPU_LOG=1 prints each call to stderr; 2, the calls' and replies' words too; 3, each
// register access.
#pragma once

#include "emu.h"
#include "bus/pccard/pccard.h"
#include "eamuse/lan.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class k573npu_hle_device : public device_t, public device_pccard_interface {
public:
    k573npu_hle_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

    // where the hard disk and the RAM disk live ("" = no hard disk)
    void set_dirs(std::string hdd, std::string ram) { m_hdd_dir = std::move(hdd); m_ram_dir = std::move(ram); }
    // the network its Ethernet port is on (none: every socket call fails)
    void set_lan(hk::ea::Lan *lan) { m_lan = lan; }

    uint16_t read_memory(offs_t offset, uint16_t mem_mask = ~0) override;
    void write_memory(offs_t offset, uint16_t data, uint16_t mem_mask = ~0) override;

protected:
    void device_start() override;
    void device_reset() override;

private:
    // windyfairy's ring buffer of 16-bit words; lengths in bytes. wpos/rpos: where the words
    // pushed and popped are in the 64 KiB ring the 573's driver counts in (bytes): each side
    // publishes its own (the game: 0x34 in, 0x36 out; the unit: 0x34 out, 0x36 in)
    struct Fifo {
        static constexpr size_t COUNT = 0x8000;
        std::vector<uint16_t> buf = std::vector<uint16_t>(COUNT);
        size_t head = 0, tail = 0;
        uint16_t wpos = 0, rpos = 0;
        void clear() { std::fill(buf.begin(), buf.end(), 0); head = tail = 0; wpos = rpos = 0; }
        void push(uint16_t v) { buf[tail] = v; tail = (tail + 1) % COUNT; wpos += 2; }
        uint16_t pop() { uint16_t v = buf[head]; head = (head + 1) % COUNT; rpos += 2; return v; }
        uint16_t at(size_t i) const { return buf[(head + i) % COUNT]; }
        bool empty() const { return head == tail; }
        size_t length() const { return ((tail + COUNT - head) % COUNT) * 2; }
    };

    // a call's arguments, as the game packed them
    struct Args {
        std::vector<uint16_t> w;
        size_t pos = 0;
        uint16_t u16() { return pos < w.size() ? w[pos++] : 0; }
        uint32_t u32() { uint32_t lo = u16(); return lo | (uint32_t)u16() << 16; }
        std::string bytes(size_t n);       // n bytes, two to a word (low first); one a word if n is odd
        std::string str(size_t n) { std::string s = bytes(n); return s.substr(0, s.find('\0')); }
    };

    class Disk;   // npu.cpp

    void call();
    void log_call(const char *fmt, ...) __attribute__((format(printf, 2, 3)));   // HK_573_NPU_LOG
    void reply(uint16_t code, const std::vector<uint8_t> &payload);   // header and payload
    void reply32(int32_t v) { std::vector<uint8_t> p(4); for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); reply(0, p); }
    void socket_call(int op, Args &a);
    bool recv_reply(int fd, uint32_t max);   // false: the answer isn't there yet
    void poll_network(s32);
    Disk *disk_for_path(const std::string &path);
    Disk *disk_for_fd(int32_t fd);

    std::string m_hdd_dir, m_ram_dir;
    std::unique_ptr<Disk> m_hdd, m_ram;
    hk::ea::Lan *m_lan = nullptr;
    // recvs waiting for an online server's answer: the call's id, the socket, the most it takes.
    // The unit answers when the data comes, as its recv blocks; the 573 waits on the call's id.
    struct Waiting {
        uint16_t id;
        int fd;
        uint32_t max;
    };
    std::vector<Waiting> m_waiting;
    emu_timer *m_poll_timer = nullptr;
    bool m_log = false, m_log_packets = false, m_log_regs = false;

    // registers (windyfairy's names)
    uint16_t m_state_requested = 0, m_state_cur = 0, m_val_unk = 0;
    uint32_t m_init_cur_step = 0;
    int32_t m_buffer_write_length = 0, m_buffer_read_length = 0;
    uint16_t m_packet_id = 0;
    Fifo m_in, m_out;
};

DECLARE_DEVICE_TYPE(KONAMI_573_NPU_HLE, k573npu_hle_device)
