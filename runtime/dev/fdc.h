// SPDX-License-Identifier: GPL-3.0-or-later
// SMSC FDC37C665GT floppy controller (uPD765/82077-compatible core) with an
// empty 3.5" HD drive - what a Firebeat without a floppy inserted presents.
// Only the commands the games' self-tests issue are implemented; behaviour
// was matched against MAME's trace of beatmania III's FDD check.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace hk {

class Fdc37c665 {
public:
    uint8_t read(int port);              // port = ISA address 0x3f0..0x3f7
    void write(int port, uint8_t v);
    std::function<void(bool)> irq;

private:
    void command_complete();
    void raise_irq(uint8_t st0, uint8_t pcn);

    uint8_t dor_ = 0;
    std::vector<uint8_t> cmd_;
    std::deque<uint8_t> result_;
    uint8_t pcn_[4]{};
    uint8_t busy_ = 0;               // MSR drive-busy bits: set by seek/recalibrate until sensed
    int sensing_drive_ = -1;         // drive whose SENSE INTERRUPT result is being read
    // pending "seek end" interrupts, reported by SENSE INTERRUPT STATUS
    std::deque<std::pair<uint8_t, uint8_t>> irq_status_;
    bool config_mode_ = false;
    uint8_t config_index_ = 0;
    uint8_t config_regs_[64]{};
};

}  // namespace hk
