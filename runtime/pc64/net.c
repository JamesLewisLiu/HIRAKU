// SPDX-License-Identifier: GPL-3.0-or-later
// The cabinet's network at AVS2's imports (PLAN.md section 32), as runtime/pc/net.c gives pop'n
// 15-18's libavs:
//   - The network card (GetAdaptersInfo): 192.168.0.2/24 behind a router at 192.168.0.1, whatever
//     the PC has. Under Wine AVS2 read no address (avs_net_ctrl(7) failed), and the game's network
//     check saw it down (the boot check's NETWORK ERROR).
//   - e-AMUSEMENT's services name their modules' URLs without a port (http://127.0.0.1/): AVS2's
//     connections to port 80 of HK_PC64_EA's address go to its port.
//   - The keepalive's pings (ICMP echo through a raw socket, to the router, the server and the
//     center the services name): answered at once on a UDP socket of this machine, so they need
//     neither a raw socket's rights nor Wine's. ea3's status 0x11 reads online (3) once they come
//     back, and the game's own e-AMUSEMENT calls wait for that.
#include <winsock2.h>
#include "pc64.h"

#include <iphlpapi.h>
#include <stdlib.h>
#include <string.h>

static struct in_addr g_ea_addr;
static u_short g_ea_port;   // network order; 0: no server given

static void read_server(void) {
    const char *url = getenv("HK_PC64_EA"), *at = getenv("HK_PC_EA");   // a URL, or hiraku-pc's a.b.c.d:port
    unsigned a, b, c, d, port;
    if ((url && sscanf(url, "http://%u.%u.%u.%u:%u", &a, &b, &c, &d, &port) == 5) ||
        (!url && at && sscanf(at, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port) == 5)) {
        g_ea_addr.s_addr = htonl(a << 24 | b << 16 | c << 8 | d);
        g_ea_port = htons((u_short)port);
    }
}

// --- the card
static DWORD WINAPI GetAdaptersInfo_(PIP_ADAPTER_INFO info, PULONG size) {
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
static DWORD WINAPI AddIPAddress_(IPAddr a, IPMask m, DWORD i, PULONG context, PULONG instance) {
    (void)a, (void)m, (void)i;
    if (context) *context = 0;
    if (instance) *instance = 0;
    return NO_ERROR;
}
static DWORD WINAPI DeleteIPAddress_(ULONG context) {
    (void)context;
    return NO_ERROR;
}

// --- e-AMUSEMENT's port
static int(WSAAPI *r_connect)(SOCKET, const struct sockaddr *, int);
static int WSAAPI connect_(SOCKET s, const struct sockaddr *a, int n) {
    if (g_ea_port && a && a->sa_family == AF_INET && n >= (int)sizeof(struct sockaddr_in)) {
        struct sockaddr_in in = *(const struct sockaddr_in *)a;
        if (in.sin_port == htons(80) && in.sin_addr.s_addr == g_ea_addr.s_addr) {
            in.sin_port = g_ea_port;
            return r_connect(s, (struct sockaddr *)&in, sizeof in);
        }
    }
    return r_connect(s, a, n);
}

// --- the keepalive's pings: a UDP socket stands in for each raw ICMP one
#define MAX_PINGERS 8
static struct {
    SOCKET s;
    struct sockaddr_in self, last_to;   // where the answers come back; the last one pinged
} g_ping[MAX_PINGERS];
static int g_npings;

static int pinger(SOCKET s) {
    for (int i = 0; i < g_npings; i++)
        if (g_ping[i].s == s) return i;
    return -1;
}
static SOCKET WSAAPI socket_(int af, int type, int proto) {
    if (af == AF_INET && type == SOCK_RAW && proto == IPPROTO_ICMP && g_npings < MAX_PINGERS) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return s;
        memset(&g_ping[g_npings], 0, sizeof g_ping[g_npings]);
        g_ping[g_npings++].s = s;
        return s;
    }
    return socket(af, type, proto);
}
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
static int WSAAPI bind_(SOCKET s, const struct sockaddr *a, int len) {
    const int k = pinger(s);
    return k < 0 ? bind(s, a, len) : bind_pinger(k);
}
static int WSAAPI closesocket_(SOCKET s) {
    const int k = pinger(s);
    if (k >= 0) g_ping[k] = g_ping[--g_npings];
    return closesocket(s);
}
static u_short icmp_sum(const uint8_t *p, int n) {
    uint32_t sum = 0;
    for (int i = 0; i + 1 < n; i += 2) sum += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (n & 1) sum += (uint32_t)(p[n - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u_short)~sum;
}
// An echo request: its reply as a raw socket gives it (an IPv4 header first, from the one pinged),
// sent back to the stand-in for recvfrom and select to find
static int ping_send(int k, const char *buf, int len, const struct sockaddr *to, int tolen) {
    if (len < 8 || len > 1400 || !to || tolen < (int)sizeof(struct sockaddr_in) || bind_pinger(k) || (uint8_t)buf[0] != 8) return len;
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
    reply[20] = 0;   // an echo reply
    reply[22] = reply[23] = 0;
    const u_short cs = icmp_sum(reply + 20, len);
    reply[22] = (uint8_t)(cs >> 8), reply[23] = (uint8_t)cs;
    g_ping[k].last_to = *(const struct sockaddr_in *)to;
    sendto(g_ping[k].s, (const char *)reply, n, 0, (const struct sockaddr *)&g_ping[k].self, sizeof g_ping[k].self);
    return len;
}
static int ping_receive(int k, char *buf, int len, int flags, struct sockaddr *from, int *fromlen) {
    struct sockaddr_in got;
    int gotlen = sizeof got;
    const int r = recvfrom(g_ping[k].s, buf, len, flags, (struct sockaddr *)&got, &gotlen);
    if (r >= 0 && from && fromlen && *fromlen >= (int)sizeof(struct sockaddr_in)) {
        memcpy(from, &g_ping[k].last_to, sizeof(struct sockaddr_in));
        if (r >= 16) memcpy(&((struct sockaddr_in *)from)->sin_addr, buf + 12, 4);   // the reply's sender
        *fromlen = sizeof(struct sockaddr_in);
    }
    return r;
}
// AVS2 pings through WSASendTo and WSARecvFrom (synchronously: no overlapped I/O)
static int WSAAPI WSASendTo_(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD sent, DWORD flags, const struct sockaddr *to, int tolen,
                             LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE done) {
    const int k = pinger(s);
    if (k < 0 || ov || done) return WSASendTo(s, bufs, n, sent, flags, to, tolen, ov, done);
    char one[1500];
    int len = 0;
    for (DWORD i = 0; i < n && len + (int)bufs[i].len <= (int)sizeof one; i++) {
        memcpy(one + len, bufs[i].buf, bufs[i].len);
        len += (int)bufs[i].len;
    }
    const int r = ping_send(k, one, len, to, tolen);
    if (sent) *sent = (DWORD)r;
    return 0;
}
static int WSAAPI WSARecvFrom_(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD got, LPDWORD flags, struct sockaddr *from, LPINT fromlen,
                               LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE done) {
    const int k = pinger(s);
    if (k < 0 || ov || done) return WSARecvFrom(s, bufs, n, got, flags, from, fromlen, ov, done);
    char one[1500];
    const int r = ping_receive(k, one, sizeof one, flags ? (int)*flags : 0, from, fromlen);
    if (r < 0) return SOCKET_ERROR;
    int at = 0;
    for (DWORD i = 0; i < n && at < r; i++) {
        const int c = r - at < (int)bufs[i].len ? r - at : (int)bufs[i].len;
        memcpy(bufs[i].buf, one + at, (size_t)c);
        at += c;
    }
    if (got) *got = (DWORD)at;
    if (flags) *flags = 0;
    return 0;
}

void pc64_net_install(HMODULE avs) {
    read_server();
    // AVS2 imports Winsock by ordinal: 2 bind, 3 closesocket, 4 connect, 23 socket
    r_connect = pc64_import_ordinal(avs, "ws2_32.dll", 4, (void *)connect_);
    pc64_import_ordinal(avs, "ws2_32.dll", 2, (void *)bind_);
    pc64_import_ordinal(avs, "ws2_32.dll", 3, (void *)closesocket_);
    pc64_import_ordinal(avs, "ws2_32.dll", 23, (void *)socket_);
    pc64_import(avs, "ws2_32.dll", "WSASendTo", (void *)WSASendTo_);
    pc64_import(avs, "ws2_32.dll", "WSARecvFrom", (void *)WSARecvFrom_);
    pc64_import(avs, "iphlpapi.dll", "GetAdaptersInfo", (void *)GetAdaptersInfo_);
    pc64_import(avs, "iphlpapi.dll", "AddIPAddress", (void *)AddIPAddress_);
    pc64_import(avs, "iphlpapi.dll", "DeleteIPAddress", (void *)DeleteIPAddress_);
    if (!r_connect) pc64_log("AVS2 doesn't import connect by ordinal 4: its network as it is");
}
