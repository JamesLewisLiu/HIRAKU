// SPDX-License-Identifier: GPL-3.0-or-later
// PC/SC (pcsc-lite) loaded when first used instead of linked (CMake's HK_PCSC_DLOPEN, Linux): a
// release's binaries then start on a PC without libpcsclite, and the NFC card readers
// (readers.cpp, --scard) work where it is installed. The functions readers.cpp calls, and the
// protocol control blocks pcsc-lite exports, each forwarding to libpcsclite.so.1; without it,
// SCARD_E_NO_SERVICE, as when its daemon isn't running.
#include <dlfcn.h>

#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>

extern "C" {
const SCARD_IO_REQUEST g_rgSCardT0Pci = {SCARD_PROTOCOL_T0, sizeof(SCARD_IO_REQUEST)};
const SCARD_IO_REQUEST g_rgSCardT1Pci = {SCARD_PROTOCOL_T1, sizeof(SCARD_IO_REQUEST)};
const SCARD_IO_REQUEST g_rgSCardRawPci = {SCARD_PROTOCOL_RAW, sizeof(SCARD_IO_REQUEST)};
}

namespace {

void *library() {
    static void *lib = [] {
        void *h = dlopen("libpcsclite.so.1", RTLD_NOW | RTLD_LOCAL);
        return h ? h : dlopen("libpcsclite.so", RTLD_NOW | RTLD_LOCAL);
    }();
    return lib;
}

template <typename F>
F *function(const char *name) {
    void *lib = library();
    return lib ? reinterpret_cast<F *>(dlsym(lib, name)) : nullptr;
}

}  // namespace

// each: the library's function, looked up once; SCARD_E_NO_SERVICE without it
#define HK_PCSC_FORWARD(name, params, args)                               \
    extern "C" LONG name params {                                         \
        static auto *f = function<LONG params>(#name);                    \
        return f ? f args : (LONG)SCARD_E_NO_SERVICE;                     \
    }

HK_PCSC_FORWARD(SCardEstablishContext, (DWORD scope, LPCVOID r1, LPCVOID r2, LPSCARDCONTEXT ctx),
                (scope, r1, r2, ctx))
HK_PCSC_FORWARD(SCardReleaseContext, (SCARDCONTEXT ctx), (ctx))
HK_PCSC_FORWARD(SCardListReaders, (SCARDCONTEXT ctx, LPCSTR groups, LPSTR readers, LPDWORD len),
                (ctx, groups, readers, len))
HK_PCSC_FORWARD(SCardGetStatusChange, (SCARDCONTEXT ctx, DWORD timeout, SCARD_READERSTATE *states, DWORD n),
                (ctx, timeout, states, n))
HK_PCSC_FORWARD(SCardConnect, (SCARDCONTEXT ctx, LPCSTR reader, DWORD share, DWORD protocols, LPSCARDHANDLE card,
                               LPDWORD active),
                (ctx, reader, share, protocols, card, active))
HK_PCSC_FORWARD(SCardDisconnect, (SCARDHANDLE card, DWORD disposition), (card, disposition))
HK_PCSC_FORWARD(SCardStatus, (SCARDHANDLE card, LPSTR name, LPDWORD name_len, LPDWORD state, LPDWORD protocol,
                              LPBYTE atr, LPDWORD atr_len),
                (card, name, name_len, state, protocol, atr, atr_len))
HK_PCSC_FORWARD(SCardTransmit, (SCARDHANDLE card, const SCARD_IO_REQUEST *send_pci, LPCBYTE send, DWORD send_len,
                                SCARD_IO_REQUEST *recv_pci, LPBYTE recv, LPDWORD recv_len),
                (card, send_pci, send, send_len, recv_pci, recv, recv_len))
