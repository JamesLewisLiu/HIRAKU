// SPDX-License-Identifier: GPL-3.0-or-later
// The PC games' way to e-AMUSEMENT (PLAN.md section 30): what the game's network library
// (libavs's Winsock calls, by ordinal) is given in place of the real network, when hiraku-pc says
// where its e-AMUSEMENT server is (HK_PC_EA="a.b.c.d:port": HIRAKU's own, or -url's).
//   - The services' host by name (pop'n 15: services.eamuse.konami.fun): that server's address,
//     and a connection to it on port 80 goes to the server's port.
//   - The keepalive's pings (ICMP echo through a raw socket, to the router, server and center
//     that the services name): answered at once, on an ordinary UDP socket of this machine, so
//     they need neither the rights a raw socket wants on Windows nor anything from Wine.
//   - The cabinet's network card (GetAdaptersInfo): one of its own, 192.168.0.2/24 behind a router
//     at 192.168.0.1, whatever the PC has (under Wine the library read nonsense, and kept its
//     network off); and the addresses the cabinet would set on its card (AddIPAddress) left alone.
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cabinet.h"

static struct in_addr g_ea_addr;
static u_short g_ea_port;   // network order
static int g_ea = -1;       // -1: not read yet, 0: no server given

static int ea(void) {
    if (g_ea < 0) {
        g_ea = 0;
        const char *s = getenv("HK_PC_EA");
        unsigned a, b, c, d, port;
        if (s && sscanf(s, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port) == 5) {
            g_ea_addr.s_addr = htonl(a << 24 | b << 16 | c << 8 | d);
            g_ea_port = htons((u_short)port);
            g_ea = 1;
            hk_log("hiraku-pc: e-AMUSEMENT at %s", s);
        }
    }
    return g_ea;
}

static int net_log(void) {
    static int on = -1;
    if (on < 0) on = getenv("HK_PC_NET_LOG") != NULL;
    return on;
}

// --- the services' host

static struct hostent *WSAAPI my_gethostbyname(const char *name) {
    if (ea() && name && strstr(name, "konami")) {
        static struct in_addr addr;
        static char *list[2];
        static struct hostent h;
        static char hname[256];
        snprintf(hname, sizeof hname, "%s", name);
        addr = g_ea_addr;
        list[0] = (char *)&addr, list[1] = NULL;
        h.h_name = hname, h.h_aliases = NULL, h.h_addrtype = AF_INET, h.h_length = 4, h.h_addr_list = list;
        return &h;
    }
    return gethostbyname(name);
}

static int WSAAPI my_connect_(SOCKET s, const struct sockaddr *to, int len);
static int WSAAPI my_connect(SOCKET s, const struct sockaddr *to, int len) {
    const int r = my_connect_(s, to, len);
    if (net_log()) hk_log("net: connect(%d) = %d (%d)", (int)s, r, r ? WSAGetLastError() : 0);
    return r;
}

static int WSAAPI my_connect_(SOCKET s, const struct sockaddr *to, int len) {
    if (ea() && to && to->sa_family == AF_INET && len >= (int)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)to;
        if (in->sin_addr.s_addr == g_ea_addr.s_addr && in->sin_port == htons(80)) {
            struct sockaddr_in there = *in;
            there.sin_port = g_ea_port;
            return connect(s, (const struct sockaddr *)&there, sizeof there);
        }
    }
    return connect(s, to, len);
}

// --- the pings

#define MAX_PINGERS 8
static struct {
    SOCKET s;
    struct sockaddr_in self;     // where its answers come back
    struct sockaddr_in last_to;  // whom it last pinged: the answer's sender
} g_ping[MAX_PINGERS];
static int g_npings;

static int pinger(SOCKET s) {
    for (int i = 0; i < g_npings; i++)
        if (g_ping[i].s == s) return i;
    return -1;
}

static SOCKET WSAAPI my_socket_(int af, int type, int proto);
static SOCKET WSAAPI my_socket(int af, int type, int proto) {
    const SOCKET s = my_socket_(af, type, proto);
    if (net_log()) hk_log("net: socket(%d, %d, %d) = %d (%d)", af, type, proto, (int)s, s == INVALID_SOCKET ? WSAGetLastError() : 0);
    return s;
}

static SOCKET WSAAPI my_socket_(int af, int type, int proto) {
    if (ea() && af == AF_INET && type == SOCK_RAW && proto == IPPROTO_ICMP && g_npings < MAX_PINGERS) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return s;
        memset(&g_ping[g_npings], 0, sizeof g_ping[g_npings]);
        g_ping[g_npings].s = s;
        g_npings++;
        hk_log("hiraku-pc: the game's pings are answered by HIRAKU");
        return s;
    }
    return socket(af, type, proto);
}

// The stand-in's own address on this machine, where its answers come back: bound when the game binds
// it (a raw socket's bind, port and all, means nothing) or first sends
static int bind_pinger(int k) {
    if (g_ping[k].self.sin_port) return 0;
    struct sockaddr_in self;
    memset(&self, 0, sizeof self);
    self.sin_family = AF_INET;
    self.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int n = sizeof self;
    if (bind(g_ping[k].s, (struct sockaddr *)&self, sizeof self) || getsockname(g_ping[k].s, (struct sockaddr *)&self, &n)) return -1;
    g_ping[k].self = self;
    return 0;
}

static int WSAAPI my_bind(SOCKET s, const struct sockaddr *a, int len) {
    const int k = pinger(s);
    return k < 0 ? bind(s, a, len) : bind_pinger(k);
}

static u_short icmp_sum(const uint8_t *p, int n) {
    uint32_t sum = 0;
    for (int i = 0; i + 1 < n; i += 2) sum += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (n & 1) sum += (uint32_t)(p[n - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u_short)~sum;
}

// An echo request (ICMP type 8) to anyone: its reply (type 0) as a raw socket gives it, an IPv4
// header first, from the one pinged, sent back to this socket for recvfrom and select to find
static int WSAAPI my_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen) {
    const int k = pinger(s);
    if (k < 0) return sendto(s, buf, len, flags, to, tolen);
    if (len < 8 || len > 1400 || !to || tolen < (int)sizeof(struct sockaddr_in) || bind_pinger(k)) return len;
    uint8_t reply[1500];
    const int n = 20 + len;
    memset(reply, 0, 20);
    reply[0] = 0x45;
    reply[2] = (uint8_t)(n >> 8), reply[3] = (uint8_t)n;
    reply[8] = 64, reply[9] = IPPROTO_ICMP;
    memcpy(reply + 12, &((const struct sockaddr_in *)to)->sin_addr, 4);
    memcpy(reply + 16, &g_ping[k].self.sin_addr, 4);
    const u_short hs = icmp_sum(reply, 20);
    reply[10] = (uint8_t)(hs >> 8), reply[11] = (uint8_t)hs;
    memcpy(reply + 20, buf, (size_t)len);
    if (reply[20] != 8) return len;   // only echo requests are answered
    reply[20] = 0;                    // echo reply
    reply[22] = reply[23] = 0;
    const u_short cs = icmp_sum(reply + 20, len);
    reply[22] = (uint8_t)(cs >> 8), reply[23] = (uint8_t)cs;
    g_ping[k].last_to = *(const struct sockaddr_in *)to;
    sendto(s, (const char *)reply, n, 0, (const struct sockaddr *)&g_ping[k].self, sizeof g_ping[k].self);
    return len;
}

static int WSAAPI my_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen) {
    const int k = pinger(s);
    if (k < 0) return recvfrom(s, buf, len, flags, from, fromlen);
    struct sockaddr_in got;
    int gotlen = sizeof got;
    const int r = recvfrom(s, buf, len, flags, (struct sockaddr *)&got, &gotlen);
    if (r >= 0 && from && fromlen && *fromlen >= (int)sizeof(struct sockaddr_in)) {
        memcpy(from, &g_ping[k].last_to, sizeof(struct sockaddr_in));
        if (r >= 16) memcpy(&((struct sockaddr_in *)from)->sin_addr, buf + 12, 4);   // the reply's sender
        *fromlen = sizeof(struct sockaddr_in);
    }
    return r;
}

static int WSAAPI my_closesocket(SOCKET s) {
    const int k = pinger(s);
    if (k >= 0) g_ping[k] = g_ping[--g_npings];
    return closesocket(s);
}

// --- the network card

static DWORD WINAPI my_GetAdaptersInfo(PIP_ADAPTER_INFO info, PULONG size) {
    if (!size) return ERROR_INVALID_PARAMETER;
    if (!info || *size < sizeof(IP_ADAPTER_INFO)) {
        *size = sizeof(IP_ADAPTER_INFO);
        return ERROR_BUFFER_OVERFLOW;
    }
    memset(info, 0, sizeof *info);
    strcpy(info->AdapterName, "{48495241-4b55-0000-0000-000000000001}");
    strcpy(info->Description, "HIRAKU");
    info->AddressLength = 6;
    static const BYTE mac[6] = {0x02, 0x48, 0x4b, 0x00, 0x00, 0x01};   // locally administered
    memcpy(info->Address, mac, 6);
    info->Index = 1;
    info->Type = MIB_IF_TYPE_ETHERNET;
    strcpy(info->IpAddressList.IpAddress.String, "192.168.0.2");
    strcpy(info->IpAddressList.IpMask.String, "255.255.255.0");
    strcpy(info->GatewayList.IpAddress.String, "192.168.0.1");
    strcpy(info->GatewayList.IpMask.String, "255.255.255.255");
    return NO_ERROR;
}

static DWORD WINAPI my_AddIPAddress(IPAddr a, IPMask m, DWORD index, PULONG context, PULONG instance) {
    (void)a, (void)m, (void)index;
    if (context) *context = 1;
    if (instance) *instance = 1;
    return NO_ERROR;
}

static DWORD WINAPI my_DeleteIPAddress(ULONG context) {
    (void)context;
    return NO_ERROR;
}

// The hook for one of iphlpapi's calls, or NULL
void *hk_net_card_hook(const char *fn) {
    if (!ea()) return NULL;
    if (!strcmp(fn, "GetAdaptersInfo")) return (void *)my_GetAdaptersInfo;
    if (!strcmp(fn, "AddIPAddress")) return (void *)my_AddIPAddress;
    if (!strcmp(fn, "DeleteIPAddress")) return (void *)my_DeleteIPAddress;
    return NULL;
}

// The hook for one of ws2_32's calls (by name: cabinet.c turns ordinals into names), or NULL
void *hk_net_hook(const char *fn) {
    if (!ea()) return NULL;
    if (!strcmp(fn, "gethostbyname")) return (void *)my_gethostbyname;
    if (!strcmp(fn, "connect")) return (void *)my_connect;
    if (!strcmp(fn, "socket")) return (void *)my_socket;
    if (!strcmp(fn, "bind")) return (void *)my_bind;
    if (!strcmp(fn, "sendto")) return (void *)my_sendto;
    if (!strcmp(fn, "recvfrom")) return (void *)my_recvfrom;
    if (!strcmp(fn, "closesocket")) return (void *)my_closesocket;
    return NULL;
}
