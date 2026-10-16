// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Sockets on Linux and Windows (PLAN.md section 26): the system's own headers, and the calls whose
// shape differs (Winsock's sockets are handles closed with closesocket, take char * and DWORD
// options, need WSAStartup, and have no SIGPIPE to keep away). A socket is an int here as on Linux
// (a Winsock handle fits one); socket(), bind(), connect(), listen() and getaddrinfo() are the
// system's.
#include <sys/types.h>

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace hk::net {

bool start();   // once before the first socket (Winsock's WSAStartup); true if sockets work
int socket(int family, int type, int protocol);   // -1 if not
int accept(int fd, sockaddr_in *peer = nullptr);  // -1 if none
int close(int fd);
void shutdown(int fd);   // both ways: a thread blocked reading it returns
bool set_int(int fd, int level, int name, int value);   // setsockopt with an int
bool set_nonblocking(int fd);
void set_timeouts(int fd, int ms);   // send and receive
void set_recv_timeout(int fd, int ms);   // 0: none
// connect, giving up after ms; false with error() telling why
bool connect_within(int fd, const sockaddr *a, size_t len, int ms);
pollfd watch(int fd, short events);   // a pollfd for poll() (Winsock's holds a SOCKET)
int fd_of(const pollfd &p);
int poll(pollfd *fds, size_t n, int ms);
ssize_t send(int fd, const void *p, size_t n);   // never raises SIGPIPE
ssize_t send_now(int fd, const void *p, size_t n);   // ... and never waits (a non-blocking send)
ssize_t recv(int fd, void *p, size_t n);
bool interrupted();   // the last call failed only because a signal came (try again)
std::string error();  // what the last call's failure was
bool remove_path(const std::string &path);   // a Unix socket's file

}  // namespace hk::net
