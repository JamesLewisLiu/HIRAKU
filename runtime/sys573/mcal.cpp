// SPDX-License-Identifier: GPL-3.0-or-later
// The 573 Master Calendar (mcal.h), after MAME's k573mcal.cpp (windyfairy, BSD-3-Clause).
#include "sys573/mcal.h"

#include "multibyte.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

DEFINE_DEVICE_TYPE(KONAMI_573_MASTER_CALENDAR_HLE, k573mcal_hle_device, "k573mcal_hle", "Konami 573 Master Calendar")

k573mcal_hle_device::k573mcal_hle_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
    : jvs_device(mconfig, KONAMI_573_MASTER_CALENDAR_HLE, tag, owner, clock) {}

void k573mcal_hle_device::device_start() { jvs_device::device_start(); }

void k573mcal_hle_device::device_reset() {
    m_seconds = 0;
    // MAME's: "randomly picked values" (shown in game as x41-6379)
    m_main_id = 41;
    m_sub_id = 6379;
    jvs_device::device_reset();
}

const char *k573mcal_hle_device::device_id() { return "KONAMI CO.,LTD.;Master Calendar;Ver1.0;"; }
uint8_t k573mcal_hle_device::command_format_version() { return 0x11; }
uint8_t k573mcal_hle_device::jvs_standard_version() { return 0x20; }
uint8_t k573mcal_hle_device::comm_method_version() { return 0x10; }

int k573mcal_hle_device::handle_message(const uint8_t *send_buffer, uint32_t send_size, uint8_t *&recv_buffer) {
    static const bool log = getenv("HK_573_MCAL_LOG") != nullptr;
    if (log) {
        fprintf(stderr, "mcal: %.6f", machine().time().as_double());
        for (uint32_t i = 0; i < send_size && i < 16; i++) fprintf(stderr, " %02x", send_buffer[i]);
        fprintf(stderr, "\n");
    }
    switch (send_buffer[0]) {
    case 0xf0:   // f0 d9: reset
        device_reset();
        break;

    case 0x70: {   // the calendar's time (MAME: the host's; here the machine's clock, as the timekeeper's)
        const system_time st((time_t)machine().m_base_time + (time_t)machine().time().seconds());
        *recv_buffer++ = 0x01;   // status
        *recv_buffer++ = uint8_t(st.local_time.year % 100);
        *recv_buffer++ = uint8_t(st.local_time.month + 1);
        *recv_buffer++ = st.local_time.mday;
        *recv_buffer++ = st.local_time.weekday;
        *recv_buffer++ = st.local_time.hour;
        *recv_buffer++ = st.local_time.minute;
        *recv_buffer++ = m_seconds;   // never the same twice in a row (MAME)
        m_seconds = (m_seconds + 1) % 60;
        return 1;
    }

    case 0x71:   // 71 ff ff 01: the area switch (the "Master Calendar ROM" otherwise)
        *recv_buffer++ = 0x01;
        *recv_buffer++ = uint8_t(m_area & 0x0f);
        return 4;

    case 0x7c: {   // 7c 7f 00 04: the main id; 7c 80 00 ..: the sub id
        const uint16_t val = get_u16be(&send_buffer[1]);
        if (val == 0x7f00) {
            *recv_buffer++ = 0x01;
            put_u32be(recv_buffer, m_main_id);
            recv_buffer += 4;
        } else if (val == 0x8000) {
            static const char init_complete[] = "<INIT COMPLETE!>";
            *recv_buffer++ = 0x01;
            memcpy(recv_buffer, init_complete, 16);
            recv_buffer += 16;
            put_u32be(recv_buffer, m_sub_id);
            recv_buffer += 4;
            put_u32be(recv_buffer, ~m_sub_id);
            recv_buffer += 4;
        }
        return 4;
    }

    case 0x7d: {   // 7d 80 10 08 ...: the next sub id
        if (get_u16be(&send_buffer[1]) == 0x8010) {
            m_sub_id = get_u32be(&send_buffer[4]);
            *recv_buffer++ = 0x01;
        }
        return 12;
    }

    case 0x7e:   // a byte of the machine's ids, sent on (status 1: next)
        *recv_buffer++ = 0x01;
        return 2;
    }
    return jvs_device::handle_message(send_buffer, send_size, recv_buffer);
}
