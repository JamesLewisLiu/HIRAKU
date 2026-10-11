// SPDX-License-Identifier: GPL-3.0-or-later
#include "fdc.h"

#include <cstdio>

#include "../core/sched.h"

namespace hk {

namespace {
// parameter bytes following each command byte (low 5 bits of the opcode)
int params_for(uint8_t c) {
    switch (c & 0x1f) {
    case 0x03: return 2;   // SPECIFY
    case 0x04: return 1;   // SENSE DRIVE STATUS
    case 0x07: return 1;   // RECALIBRATE
    case 0x08: return 0;   // SENSE INTERRUPT STATUS
    case 0x0f: return 2;   // SEEK
    case 0x10: return 0;   // VERSION
    case 0x12: return 1;   // PERPENDICULAR MODE
    case 0x13: return 3;   // CONFIGURE
    case 0x14: return 0;   // LOCK
    default: return -1;
    }
}

Fdc37c665 *g_fdc = nullptr;
std::function<void()> g_fdc_done;
Timer g_fdc_timer("fdc", [] { if (g_fdc_done) g_fdc_done(); });
}  // namespace

void Fdc37c665::raise_irq(uint8_t st0, uint8_t pcn) {
    irq_status_.emplace_back(st0, pcn);
    if (irq) irq(true);
}

uint8_t Fdc37c665::read(int port) {
    switch (port & 7) {
    case 0:
    case 1:
        if (config_mode_ && (port & 7) == 1) return config_regs_[config_index_ & 63];
        return 0xff;
    case 2: return dor_;
    case 4: {                                            // main status register
        uint8_t msr = 0x80 | busy_;                      // RQM + drives still seeking/unsensed
        if (!result_.empty()) msr |= 0x40 | 0x10;        // DIO + busy: result to read
        else if (!cmd_.empty()) msr |= 0x10;
        return msr;
    }
    case 5: {                                            // FIFO
        if (result_.empty()) return 0xff;
        uint8_t v = result_.front();
        result_.pop_front();
        if (sensing_drive_ >= 0 && result_.size() == 1) {   // ST0 of a seek-end status: drive no longer busy
            busy_ &= ~(1u << sensing_drive_);
            sensing_drive_ = -1;
        }
        return v;
    }
    case 7: return 0x80;                                 // DIR: disk changed / no disk
    default: return 0xff;
    }
}

void Fdc37c665::write(int port, uint8_t v) {
    switch (port & 7) {
    case 0:                                              // config port: 0x55 0x55 enters, 0xaa leaves
        if (v == 0x55) config_mode_ = true;
        else if (v == 0xaa) config_mode_ = false;
        else if (config_mode_) config_index_ = v;
        break;
    case 1:
        if (config_mode_) config_regs_[config_index_ & 63] = v;
        break;
    case 2: {                                            // digital output register
        uint8_t old = dor_;
        dor_ = v;
        if (!(old & 0x04) && (v & 0x04)) {               // leaving reset: polling interrupt x4
            cmd_.clear();
            result_.clear();
            irq_status_.clear();
            for (int d = 0; d < 4; d++) irq_status_.emplace_back(0xc0 | d, pcn_[d]);
            if (irq) irq(true);
        }
        break;
    }
    case 4: break;                                       // data rate select
    case 5:                                              // FIFO: command and parameters
        cmd_.push_back(v);
        if (params_for(cmd_[0]) < 0) {
            fprintf(stderr, "fdc: unsupported command %02x\n", cmd_[0]);
            cmd_.clear();
            result_ = {0x80};                            // invalid command
        } else if ((int)cmd_.size() == params_for(cmd_[0]) + 1) {
            command_complete();
        }
        break;
    case 7: break;                                       // configuration control
    default: break;
    }
}

void Fdc37c665::command_complete() {
    std::vector<uint8_t> c = cmd_;
    cmd_.clear();
    switch (c[0] & 0x1f) {
    case 0x03: case 0x12: break;                         // SPECIFY, PERPENDICULAR: no result
    case 0x13:                                           // CONFIGURE: POLL=1 cancels reset polling interrupts
        if (c[2] & 0x10) {
            while (!irq_status_.empty() && (irq_status_.front().first & 0xc0) == 0xc0) irq_status_.pop_front();
            if (irq_status_.empty() && irq) irq(false);
        }
        break;
    case 0x14: result_ = {(uint8_t)((c[0] & 0x80) ? 0x10 : 0)}; break;
    case 0x10: result_ = {0x90}; break;                  // VERSION: enhanced controller
    case 0x04: result_ = {(uint8_t)(0x28 | (c[1] & 3) | (pcn_[c[1] & 3] == 0 ? 0x10 : 0))}; break;
    case 0x08:                                           // SENSE INTERRUPT STATUS
        if (irq_status_.empty()) {
            result_ = {0x80};
        } else {
            auto [st0, pcn] = irq_status_.front();
            irq_status_.pop_front();
            result_ = {st0, pcn};
            if ((st0 & 0xe0) == 0x20) sensing_drive_ = st0 & 3;
        }
        if (irq_status_.empty() && irq) irq(false);
        break;
    case 0x07:                                           // RECALIBRATE: seek end after the steps
    case 0x0f: {                                         // SEEK
        int d = c[1] & 3;
        uint8_t target = (c[0] & 0x1f) == 0x07 ? 0 : c[2];
        busy_ |= 1u << d;
        g_fdc = this;
        g_fdc_done = [this, d, target] {
            pcn_[d] = target;
            raise_irq((uint8_t)(0x20 | d), target);
        };
        g_fdc_timer.in(ticks_from_us(3000));             // a few step times
        break;
    }
    }
}

}  // namespace hk
