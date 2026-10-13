// SPDX-License-Identifier: GPL-3.0-or-later
// The session cable between two System 573 processes: a GuitarFreaks cabinet's SIO1 on one of
// the DrumMania cabinet's multi-session unit's ports (as windyfairy's fork joins two MAMEs with a
// null modem: msu_port2 to the GuitarFreaks' rs232_network). Each process is one cabinet, with
// its own game and recompiled code.
//
// The cable carries line levels (TXD, DTR, RTS each way), stamped with the sender's emulated
// time, so the far UART gets the bits as it would on a wire: TXD as the sending UART's bit clock
// drives it, once a bit (the fork's SIO1 counts calls, not time), DTR and RTS when they change.
// The two machines run in lockstep: every `quantum` of emulated time each side sends what changed
// and waits for the other to get there too, and a change takes effect on the far side one
// quantum after it happened (a cable one quantum long). So a session replays the same on any host, however its processes are
// scheduled, and either side may run faster than real time.
//
// Transport: a Unix socket (a path) or TCP (host:port). The host (the side with the unit)
// listens and waits; the other connects. If the far side quits, the cable goes idle.
#pragma once

#include "emu.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hk::k573 {

class SessionLink {
public:
    enum Line : uint8_t { TXD, DTR, RTS, LINES };

    // host: listen at `where` and wait for the other cabinet; else connect to it (trying for up
    // to ten minutes). false: a message was printed.
    bool open(const std::string &where, bool host, attotime quantum);
    bool is_open() const { return m_fd >= 0; }

    // once the machine exists: the quantum's timer and the delivery timer
    void attach(running_machine &m);

    // a line of ours changed (a devcb handler)
    void out(Line line, int state);
    // a line of the far side's, at its time plus a quantum
    std::function<void(Line line, int state)> in;

private:
    struct Event {
        int64_t seconds, attoseconds;   // the sender's emulated time
        uint8_t line, state;
    };
    void sync(s32);
    void deliver(s32);
    void arm_delivery();
    bool send_all(const void *p, size_t n);
    bool recv_all(void *p, size_t n);
    void lost(const char *why);

    int m_fd = -1;
    bool m_host = false;
    attotime m_quantum;
    running_machine *m_machine = nullptr;
    emu_timer *m_sync_timer = nullptr, *m_deliver_timer = nullptr;
    uint64_t m_step = 0;
    int m_last[LINES] = {-1, -1, -1};
    std::vector<Event> m_out;   // ours, not sent yet
    std::vector<Event> m_in;    // theirs, to deliver (in time order)
    size_t m_in_next = 0;
};

}  // namespace hk::k573
