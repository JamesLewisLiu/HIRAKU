// SPDX-License-Identifier: GPL-3.0-or-later
// The loader's log, and the C++ exceptions the game throws (with where: they name what it lacked,
// as std::out_of_range named pcb.boot's region, PLAN.md section 32), and its faults (an access
// violation, ...: under Wine an unhandled one waits in its debugger, the game stopped and silent)
#include "pc64.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

FILE *pc64_log_file(void) {
    static FILE *f;
    if (!f) {
        const char *path = getenv("HK_PC64_LOG");
        f = path ? fopen(path, "a") : NULL;
        if (!f) f = stderr;
    }
    return f;
}

void pc64_log(const char *fmt, ...) {
    FILE *f = pc64_log_file();
    va_list a;
    va_start(a, fmt);
    fputs("hiraku-pc64: ", f);
    vfprintf(f, fmt, a);
    fputc('\n', f);
    va_end(a);
    fflush(f);
}

// An address as module+offset
static void where(const char *what, const void *at) {
    HMODULE m = NULL;
    char path[MAX_PATH] = "?";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, at, &m);
    if (m) GetModuleFileNameA(m, path, sizeof path);
    const char *b = strrchr(path, '\\');
    pc64_log("  %s%s+%llx", what, b ? b + 1 : path, (unsigned long long)((const char *)at - (const char *)m));
}
static void stack(void) {
    void *frames[16];
    USHORT n = CaptureStackBackTrace(1, 16, frames, NULL);
    for (USHORT i = 0; i < n; i++) where("", frames[i]);
}

// A fault (the first few): where, and for an access violation the address read or written
static void on_fault(PEXCEPTION_POINTERS ep) {
    static volatile LONG said;
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    if (InterlockedIncrement(&said) > 8) return;
    pc64_log("the game faulted: %08lx (thread %lu)", (unsigned long)r->ExceptionCode, GetCurrentThreadId());
    where("at ", r->ExceptionAddress);
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        pc64_log("  %s %p", r->ExceptionInformation[0] == 1 ? "writing" : r->ExceptionInformation[0] == 8 ? "running" : "reading",
                 (void *)r->ExceptionInformation[1]);
    pc64_log("  rsp %p", (void *)ep->ContextRecord->Rsp);
    const ULONG_PTR *sp = (const ULONG_PTR *)ep->ContextRecord->Rsp;   // return addresses on the stack, as found
    for (int i = 0, n = 0; i < 64 && n < 12; i++) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void *)sp[i], &mi, sizeof mi) || !(mi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE | PAGE_EXECUTE_READWRITE))) continue;
        where("  ", (void *)sp[i]);
        n++;
    }
}

// MSVC's C++ throw (0xe06d7363): the thrown type is in its ThrowInfo's first CatchableType's
// TypeDescriptor; std::exception's what() is its vtable's second slot. The stack as module offsets.
static LONG CALLBACK on_throw(PEXCEPTION_POINTERS ep) {
    EXCEPTION_RECORD *r = ep->ExceptionRecord;
    switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_PRIV_INSTRUCTION:
    case 0xc0000409:   // a fail-fast (the stack's cookie, an invalid parameter)
        on_fault(ep);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (r->ExceptionCode != 0xe06d7363 || r->NumberParameters < 4) return EXCEPTION_CONTINUE_SEARCH;
    ULONG_PTR obj = r->ExceptionInformation[1], info = r->ExceptionInformation[2], base = r->ExceptionInformation[3];
    const char *name = "?";
    if (info && base) {
        const int *types = (const int *)(base + *(const int *)(info + 12));
        if (types[0] > 0) name = (const char *)(base + *(const int *)(base + types[1] + 4) + 16);
    }
    pc64_log("the game threw %s", name);
    if (obj && strstr(name, "exception")) {
        const char *(*what)(void *) = (const char *(*)(void *))(*(void ***)obj)[1];
        pc64_log("  what(): %s", what((void *)obj));
    }
    stack();
    return EXCEPTION_CONTINUE_SEARCH;
}

void pc64_faults_install(void) {
    AddVectoredExceptionHandler(1, on_throw);
}

// HK_PC64_TRACE_STRUCTS (tests): the game's searches for answer nodes (avs2-core's XCgsqzn00000a1,
// ordinal 162: a child by name) and its struct reads (XCgsqzn00000b2, ordinal 179: a node into a
// struct by a descriptor in popn.dll), each with what came of it: which part of an answer it
// doesn't take (PLAN.md section 32, the player's record)
static void *(*r_search)(void *, void *, const char *);
static char (*r_struct)(void *, void *, void *, const void *);
static void *search_(void *ctx, void *node, const char *name) {
    void *r = r_search(ctx, node, name);
    pc64_log("answer: node %s%s", name ? name : "?", r ? "" : ": none");
    return r;
}
static char struct_(void *ctx, void *node, void *dst, const void *desc) {
    const char r = r_struct(ctx, node, dst, desc);
    HMODULE m = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, desc, &m);
    pc64_log("answer: struct %llx: %s", (unsigned long long)((const char *)desc - (const char *)m + 0x180000000ull), r ? "read" : "FAILED");
    return r;
}
void pc64_trace_install(HMODULE game) {
    if (!getenv("HK_PC64_TRACE_STRUCTS")) return;
    r_search = pc64_import_ordinal(game, "avs2-core.dll", 162, (void *)search_);
    r_struct = pc64_import_ordinal(game, "avs2-core.dll", 179, (void *)struct_);
    pc64_log("tracing the answers' nodes and structs");
}
