// SPDX-License-Identifier: GPL-3.0-or-later
// The e-Amusement rental unit (rental.h).
#include "sys573/rental.h"

#include <cstdio>
#include <cstdlib>

namespace hk::k573 {

namespace {
constexpr int BAUD = 19200;
}

void RentalUnit::attach(running_machine &m, std::function<void(int)> rxd) {
    machine_ = &m;
    rxd_ = std::move(rxd);
    log_ = getenv("HK_573_RENTAL_LOG") != nullptr;
    send_timer_ = m.alloc_timer([this](s32 p) { send_tick(p); });
}

void RentalUnit::reset() {
    rx_bits_ = -1;
    in_.clear();
    out_.clear();
    sending_ = false;
    if (send_timer_) send_timer_->adjust(attotime::never);
}

void RentalUnit::txd(int bit) {
    if (!machine_) return;
    if (rx_bits_ < 0) {
        if (bit == 0) { rx_bits_ = 0; rx_byte_ = 0; }
        return;
    }
    if (rx_bits_ < 8) {
        rx_byte_ |= uint8_t(bit ? 1 : 0) << rx_bits_;
        rx_bits_++;
        return;
    }
    rx_bits_ = -1;   // the stop bit
    if (log_) fprintf(stderr, "rental: %.6f in %02x\n", machine_->time().as_double(), rx_byte_);
    take(rx_byte_);
}

void RentalUnit::take(uint8_t b) {
    in_.push_back(b);
    // the version query (the game's driver at 0x803169c4 in the boot program): a5 c0; the answer
    // echoes it and gives three characters, shown as "x.yz" (config.dat's eamuse.ver)
    while (in_.size() >= 2) {
        if (in_[0] == 0xa5 && in_[1] == 0xc0) {
            in_.pop_front();
            in_.pop_front();
            send({0xa5, 0xc0, '1', '0', '0'});
            continue;
        }
        break;
    }
}

void RentalUnit::send(std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) out_.push_back(b);
    if (!sending_) {
        sending_ = true;
        send_timer_->adjust(attotime::from_hz(BAUD) * 10);
    }
}

void RentalUnit::send_tick(s32) {
    if (out_.empty()) {
        sending_ = false;
        return;
    }
    const uint8_t b = out_.front();
    out_.pop_front();
    if (log_) fprintf(stderr, "rental: %.6f out %02x\n", machine_->time().as_double(), b);
    rxd_(0);
    for (int i = 0; i < 8; i++) rxd_((b >> i) & 1);
    rxd_(1);
    send_timer_->adjust(attotime::from_hz(BAUD) * 10);
}

}  // namespace hk::k573
