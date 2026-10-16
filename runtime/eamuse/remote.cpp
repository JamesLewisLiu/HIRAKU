// SPDX-License-Identifier: GPL-3.0-or-later
// e-AMUSEMENT online: the game's HTTP requests forwarded to a real server (remote.h).
#include "eamuse/remote.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "shell/net.h"

namespace hk::ea {

Remote::Remote(const std::string &url) {
    std::string u = url;
    if (u.compare(0, 7, "http://") != 0) return;   // HTTP only: the game's own protocol is plain HTTP/1.0
    u = u.substr(7);
    const size_t slash = u.find('/');
    std::string hostport = u.substr(0, slash);
    path_ = slash == std::string::npos ? "" : u.substr(slash);
    while (!path_.empty() && path_.back() == '/') path_.pop_back();
    if (const size_t colon = hostport.rfind(':'); colon != std::string::npos) {
        port_ = (uint16_t)std::strtoul(hostport.c_str() + colon + 1, nullptr, 10);
        hostport.resize(colon);
    }
    if (port_ == 0) return;
    host_ = hostport;
}

std::string Remote::exchange(const Lan::Target &t, const std::string &request, std::string *error) const {
    const std::string host = t.services ? host_ : t.host;
    const uint16_t port = t.services ? port_ : t.port;
    const std::string host_header = port == 80 ? host : host + ":" + std::to_string(port);

    // the request line's path, with the server URL's path in front; the Host header
    std::string req = request;
    const size_t line_end = req.find("\r\n");
    if (line_end == std::string::npos) { *error = "not an HTTP request"; return ""; }
    if (t.services && !path_.empty()) {
        const size_t sp = req.find(' ');
        if (sp != std::string::npos && sp < line_end) req.insert(sp + 1, path_);
    }
    const size_t head_end = req.find("\r\n\r\n");
    for (size_t at = req.find("\r\n"); at != std::string::npos && at < head_end; at = req.find("\r\n", at + 2)) {
        if (strncasecmp(req.c_str() + at + 2, "Host:", 5) != 0) continue;
        const size_t e = req.find("\r\n", at + 2);
        req.replace(at + 2, e - at - 2, "Host: " + host_header);
        break;
    }

    net::start();
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (int e = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res); e != 0) {
        *error = host + ": " + gai_strerror(e);
        return "";
    }
    int fd = -1;
    for (addrinfo *a = res; a; a = a->ai_next) {
        fd = net::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        net::set_timeouts(fd, 15000);   // the game gives up after about as long
        if (connect(fd, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        net::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { *error = host_header + ": " + net::error(); return ""; }

    for (size_t sent = 0; sent < req.size();) {
        const ssize_t n = net::send(fd, req.data() + sent, req.size() - sent);
        if (n <= 0) { *error = host_header + ": send: " + net::error(); net::close(fd); return ""; }
        sent += (size_t)n;
    }
    std::string answer;
    char buf[4096];
    for (;;) {   // HTTP/1.0: the answer ends where the server closes (or its Content-Length)
        const ssize_t n = net::recv(fd, buf, sizeof buf);
        if (n < 0) { *error = host_header + ": recv: " + net::error(); net::close(fd); return ""; }
        if (n == 0) break;
        answer.append(buf, (size_t)n);
        const size_t he = answer.find("\r\n\r\n");
        if (he == std::string::npos) continue;
        std::string head = answer.substr(0, he);
        for (char &c : head) c = (char)std::tolower((unsigned char)c);
        if (const size_t cl = head.find("\r\ncontent-length:"); cl != std::string::npos &&
            answer.size() >= he + 4 + std::strtoul(head.c_str() + cl + 17, nullptr, 10))
            break;
    }
    net::close(fd);
    if (answer.empty()) *error = host_header + ": no answer";
    return answer;
}

}  // namespace hk::ea
