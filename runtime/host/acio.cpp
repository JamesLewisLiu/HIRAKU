// SPDX-License-Identifier: GPL-3.0-or-later
// After spice2x's reader/ (reader.cpp, message.cpp: GPLv3, the sp2xdev team and the SpiceTools
// contributors), on POSIX serial ports.
#include "acio.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace hk::host {

AcioPort::AcioPort(std::string path) : path_(std::move(path)), log_(getenv("HK_ACIO_LOG") != nullptr) {}

AcioPort::~AcioPort() { close(); }

#ifdef _WIN32
// Windows: a COM port ("COM3"; the \\.\ in front, which COM10 and up need, is added) through
// Win32's serial calls. Not yet
// tried on a real port (PLAN.md section 26).
namespace {
HANDLE handle(intptr_t fd) { return reinterpret_cast<HANDLE>(fd); }

// a read returns when a byte is there, or after ms with none
void read_wait(intptr_t fd, int ms) {
    COMMTIMEOUTS t{};
    t.ReadIntervalTimeout = MAXDWORD;
    t.ReadTotalTimeoutMultiplier = MAXDWORD;
    t.ReadTotalTimeoutConstant = (DWORD)std::max(ms, 1);
    t.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(handle(fd), &t);
}
}  // namespace

bool AcioPort::open() {
    close();
    const std::string name = path_.compare(0, 4, "\\\\.\\") == 0 ? path_ : "\\\\.\\" + path_;
    HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    id_ = 0;
    if (h == INVALID_HANDLE_VALUE) return false;
    fd_ = reinterpret_cast<intptr_t>(h);
    return true;
}

void AcioPort::close() {
    if (fd_ >= 0) CloseHandle(handle(fd_));
    fd_ = -1;
}

bool AcioPort::set_rate(int baud) {
    DCB d{};
    d.DCBlength = sizeof d;
    if (!GetCommState(handle(fd_), &d)) return false;
    d.BaudRate = (DWORD)baud;
    d.ByteSize = 8;   // 8N1, no flow control
    d.Parity = NOPARITY;
    d.StopBits = ONESTOPBIT;
    d.fBinary = TRUE;
    d.fParity = FALSE;
    d.fOutxCtsFlow = FALSE;
    d.fOutxDsrFlow = FALSE;
    d.fDtrControl = DTR_CONTROL_ENABLE;
    d.fRtsControl = RTS_CONTROL_ENABLE;
    d.fOutX = FALSE;
    d.fInX = FALSE;
    if (!SetCommState(handle(fd_), &d)) return false;
    PurgeComm(handle(fd_), PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

bool AcioPort::write_all(const uint8_t *d, size_t n) {
    read_wait(fd_, 1);
    while (n) {
        DWORD w = 0;
        if (!WriteFile(handle(fd_), d, (DWORD)n, &w, nullptr) || w == 0) return false;
        d += w;
        n -= w;
    }
    return true;
}

std::vector<uint8_t> AcioPort::read_some(int first_ms, size_t most) {
    std::vector<uint8_t> in;
    int wait = first_ms;
    while (in.size() < most) {
        read_wait(fd_, wait);
        uint8_t b[512];
        DWORD r = 0;
        if (!ReadFile(handle(fd_), b, (DWORD)std::min(sizeof b, most - in.size()), &r, nullptr) || r == 0) break;
        in.insert(in.end(), b, b + r);
        wait = 30;
    }
    return in;
}

#else

bool AcioPort::open() {
    close();
    fd_ = ::open(path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    id_ = 0;
    return fd_ >= 0;
}

void AcioPort::close() {
    if (fd_ >= 0) ::close((int)fd_);
    fd_ = -1;
}

bool AcioPort::set_rate(int baud) {
    speed_t rate;
    switch (baud) {
    case 9600: rate = B9600; break;
    case 19200: rate = B19200; break;
    case 38400: rate = B38400; break;
    case 57600: rate = B57600; break;
    case 115200: rate = B115200; break;
    default: return false;
    }
    termios t{};
    const int fd = (int)fd_;
    if (tcgetattr(fd, &t) != 0) return false;
    cfmakeraw(&t);
    cfsetispeed(&t, rate);
    cfsetospeed(&t, rate);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);   // 8N1, no flow control
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &t) != 0) return false;
    tcflush(fd, TCIOFLUSH);
    return true;
}

bool AcioPort::write_all(const uint8_t *d, size_t n) {
    while (n) {
        ssize_t w = write((int)fd_, d, n);
        if (w < 0 && errno == EAGAIN) {
            pollfd p{(int)fd_, POLLOUT, 0};
            if (poll(&p, 1, 1000) <= 0) return false;
            continue;
        }
        if (w <= 0) return false;
        d += w;
        n -= (size_t)w;
    }
    return true;
}

std::vector<uint8_t> AcioPort::read_some(int first_ms, size_t most) {
    std::vector<uint8_t> in;
    int wait = first_ms;
    while (in.size() < most) {
        pollfd p{(int)fd_, POLLIN, 0};
        if (poll(&p, 1, wait) <= 0) break;
        uint8_t b[512];
        ssize_t r = read((int)fd_, b, std::min(sizeof b, most - in.size()));
        if (r <= 0) break;
        in.insert(in.end(), b, b + r);
        wait = 30;
    }
    return in;
}

#endif

void AcioPort::dump(const char *dir, const std::vector<uint8_t> &bytes) const {
    if (!log_) return;
    fprintf(stderr, "acio %s %s", path_.c_str(), dir);
    for (uint8_t b : bytes) fprintf(stderr, " %02x", b);
    fprintf(stderr, "\n");
}

int AcioPort::handshake(const std::vector<int> &rates, const std::function<bool()> &keep_going) {
    std::vector<uint8_t> hello(525, 0x00);
    hello.insert(hello.end(), 40, 0xaa);
    for (int rate : rates) {
        if (!set_rate(rate)) return 0;
        for (int n = 0; n < 10 && keep_going(); n++) {
            if (!write_all(hello.data(), hello.size())) return 0;
            auto in = read_some(300, 565);
            if (!in.empty() && in.back() == 0xaa) return rate;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    return 0;
}

bool AcioPort::send(uint8_t node, uint16_t cmd, const std::vector<uint8_t> &payload) {
    std::vector<uint8_t> body = {node, uint8_t(cmd >> 8), uint8_t(cmd), ++id_, (uint8_t)payload.size()};
    body.insert(body.end(), payload.begin(), payload.end());
    uint8_t sum = 0;
    for (uint8_t b : body) sum += b;
    body.push_back(sum);
    dump(">", body);
    std::vector<uint8_t> out = {0xaa};
    for (uint8_t b : body) {
        if (b == 0xaa || b == 0xff) {
            out.push_back(0xff);
            out.push_back((uint8_t)~b);
        } else {
            out.push_back(b);
        }
    }
    return write_all(out.data(), out.size());
}

bool AcioPort::receive(Message &m, int wait_ms) {
    auto in = read_some(wait_ms, 4096);
    bool got = false, escape = false;
    std::vector<uint8_t> d;
    size_t want = 0;
    for (uint8_t b : in) {
        if (b == 0xaa && !escape) {
            d.clear();
            want = 6;
            continue;
        }
        if (!want) continue;
        if (b == 0xff && !escape) {
            escape = true;
            continue;
        }
        if (escape) b = (uint8_t)~b;
        escape = false;
        d.push_back(b);
        if (d.size() == 5) want = 6 + d[4];
        if (d.size() < want) continue;
        uint8_t sum = 0;
        for (size_t i = 0; i + 1 < d.size(); i++) sum += d[i];
        dump(sum == d.back() ? "<" : "< (bad sum)", d);
        if (sum == d.back()) {
            m.node = d[0];
            m.cmd = (uint16_t)(d[1] << 8 | d[2]);
            m.id = d[3];
            m.payload.assign(d.begin() + 5, d.end() - 1);
            got = true;
        }
        want = 0;
    }
    return got;
}

}  // namespace hk::host
