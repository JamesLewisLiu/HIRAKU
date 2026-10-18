// SPDX-License-Identifier: GPL-3.0-or-later
// Konami's 573 Master Calendar: a factory/development JVS device ("KONAMI CO.,LTD.;Master
// Calendar;Ver1.0;"). With it on the JVS bus the 573's boot program runs its factory set-up: it
// sets the clock and initialises the security cassette and, on an e-Amusement rental cabinet
// (Mambo a GoGo's rental version), the rental records in the timekeeper's RAM. MAME has the
// device (k573mcal.cpp, by windyfairy, BSD-3-Clause) but doesn't fit it (commented out in
// ksys573.cpp); this follows it, with the area switch set from here and the time from the
// machine's clock. --master-calendar fits it (rental.h has the rest of the story).
#pragma once

#include "emu.h"
#include "machine/jvsdev.h"

class k573mcal_hle_device : public jvs_device {
public:
    template <typename T>
    k573mcal_hle_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock, T &&jvs_host_tag)
        : k573mcal_hle_device(mconfig, tag, owner, clock) {
        host.set_tag(std::forward<T>(jvs_host_tag));
    }
    k573mcal_hle_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

    // the area switch (MAME's "Area" DIP: 0 JA, 1 UA, 2 EA, 4 AA, 5 KA, 7 JR, ...)
    void set_area(uint8_t area) { m_area = area; }

protected:
    void device_start() override;
    void device_reset() override;

    const char *device_id() override;
    uint8_t command_format_version() override;
    uint8_t jvs_standard_version() override;
    uint8_t comm_method_version() override;
    int handle_message(const uint8_t *send_buffer, uint32_t send_size, uint8_t *&recv_buffer) override;

private:
    uint8_t m_area = 0;
    uint8_t m_seconds = 0;
    uint32_t m_main_id = 0, m_sub_id = 0;
};

DECLARE_DEVICE_TYPE(KONAMI_573_MASTER_CALENDAR_HLE, k573mcal_hle_device)
