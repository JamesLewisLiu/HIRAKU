// SPDX-License-Identifier: GPL-3.0-or-later
#include "shell/net.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/time.h>
#endif

namespace hk::net {

#ifdef _WIN32

bool start() {
    static const bool ok = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
}

int socket(int family, int type, int protocol) {
    start();
    SOCKET s = ::socket(family, type, protocol);
    return s == INVALID_SOCKET ? -1 : (int)s;
}

int accept(int fd) {
    SOCKET s = ::accept((SOCKET)fd, nullptr, nullptr);
    return s == INVALID_SOCKET ? -1 : (int)s;
}

int close(int fd) { return closesocket((SOCKET)fd); }

bool set_int(int fd, int level, int name, int value) {
    return setsockopt((SOCKET)fd, level, name, (const char *)&value, sizeof value) == 0;
}

bool set_nonblocking(int fd) {
    u_long on = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &on) == 0;
}

void set_timeouts(int fd, int ms) {
    const DWORD t = (DWORD)ms;
    setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
    setsockopt((SOCKET)fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t, sizeof t);
}

ssize_t send(int fd, const void *p, size_t n) { return ::send((SOCKET)fd, (const char *)p, (int)n, 0); }

// the lights' sockets are non-blocking already (set_nonblocking)
ssize_t send_now(int fd, const void *p, size_t n) { return send(fd, p, n); }

ssize_t recv(int fd, void *p, size_t n) { return ::recv((SOCKET)fd, (char *)p, (int)n, 0); }

bool interrupted() { return WSAGetLastError() == WSAEINTR; }

std::string error() {
    const int e = WSAGetLastError();
    char text[256] = {};
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, (DWORD)e, 0, text, sizeof text,
                        nullptr))
        snprintf(text, sizeof text, "socket error %d", e);
    std::string s = text;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s;
}

bool remove_path(const std::string &path) { return DeleteFileA(path.c_str()) != 0; }

#else

bool start() { return true; }

int socket(int family, int type, int protocol) { return ::socket(family, type, protocol); }

int accept(int fd) { return ::accept(fd, nullptr, nullptr); }

int close(int fd) { return ::close(fd); }

bool set_int(int fd, int level, int name, int value) { return setsockopt(fd, level, name, &value, sizeof value) == 0; }

bool set_nonblocking(int fd) {
    const int fl = fcntl(fd, F_GETFL);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

void set_timeouts(int fd, int ms) {
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

ssize_t send(int fd, const void *p, size_t n) { return ::send(fd, p, n, MSG_NOSIGNAL); }

ssize_t send_now(int fd, const void *p, size_t n) { return ::send(fd, p, n, MSG_NOSIGNAL | MSG_DONTWAIT); }

ssize_t recv(int fd, void *p, size_t n) { return ::recv(fd, p, n, 0); }

bool interrupted() { return errno == EINTR; }

std::string error() { return strerror(errno); }

bool remove_path(const std::string &path) { return unlink(path.c_str()) == 0; }

#endif

}  // namespace hk::net
