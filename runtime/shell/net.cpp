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

int accept(int fd, sockaddr_in *peer) {
    int len = sizeof *peer;
    SOCKET s = ::accept((SOCKET)fd, (sockaddr *)peer, peer ? &len : nullptr);
    return s == INVALID_SOCKET ? -1 : (int)s;
}

int close(int fd) { return closesocket((SOCKET)fd); }

void shutdown(int fd) { ::shutdown((SOCKET)fd, SD_BOTH); }

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

void set_recv_timeout(int fd, int ms) {
    const DWORD t = (DWORD)ms;
    setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
}

bool connect_within(int fd, const sockaddr *a, size_t len, int ms) {
    u_long on = 1, off = 0;
    ioctlsocket((SOCKET)fd, FIONBIO, &on);
    int r = ::connect((SOCKET)fd, a, (int)len);
    if (r != 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
        WSAPOLLFD p{(SOCKET)fd, POLLOUT, 0};
        int err = 0, el = sizeof err;
        r = WSAPoll(&p, 1, ms) == 1 && getsockopt((SOCKET)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &el) == 0 && !err ? 0 : -1;
        if (r != 0) WSASetLastError(err ? err : WSAETIMEDOUT);
    }
    ioctlsocket((SOCKET)fd, FIONBIO, &off);
    return r == 0;
}

pollfd watch(int fd, short events) {
    pollfd p{};
    p.fd = (SOCKET)fd;
    p.events = events;
    return p;
}

int fd_of(const pollfd &p) { return (int)p.fd; }

int poll(pollfd *fds, size_t n, int ms) { return WSAPoll(fds, (ULONG)n, ms); }

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

int accept(int fd, sockaddr_in *peer) {
    socklen_t len = sizeof *peer;
    return ::accept(fd, (sockaddr *)peer, peer ? &len : nullptr);
}

int close(int fd) { return ::close(fd); }

void shutdown(int fd) { ::shutdown(fd, SHUT_RDWR); }

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

void set_recv_timeout(int fd, int ms) {
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

bool connect_within(int fd, const sockaddr *a, size_t len, int ms) {
    const int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = ::connect(fd, a, (socklen_t)len);
    if (r < 0 && errno == EINPROGRESS) {
        pollfd p{fd, POLLOUT, 0};
        int err = 0;
        socklen_t el = sizeof err;
        r = ::poll(&p, 1, ms) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && !err ? 0 : -1;
        errno = err ? err : r ? ETIMEDOUT : 0;
    }
    fcntl(fd, F_SETFL, fl);
    return r == 0;
}

pollfd watch(int fd, short events) { return pollfd{fd, events, 0}; }

int fd_of(const pollfd &p) { return p.fd; }

int poll(pollfd *fds, size_t n, int ms) { return ::poll(fds, (nfds_t)n, ms); }

ssize_t send(int fd, const void *p, size_t n) { return ::send(fd, p, n, MSG_NOSIGNAL); }

ssize_t send_now(int fd, const void *p, size_t n) { return ::send(fd, p, n, MSG_NOSIGNAL | MSG_DONTWAIT); }

ssize_t recv(int fd, void *p, size_t n) { return ::recv(fd, p, n, 0); }

bool interrupted() { return errno == EINTR; }

std::string error() { return strerror(errno); }

bool remove_path(const std::string &path) { return unlink(path.c_str()) == 0; }

#endif

}  // namespace hk::net
