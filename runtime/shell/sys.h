// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The few things HIRAKU asks of the system that Linux and Windows do differently (PLAN.md section
// 26): page memory and its protection, reading a file at an offset, folders, the calendar. On
// Linux each is what the code called before; on Windows, Win32's equivalent. Kept out of the
// header: <windows.h>, whose macros (IN, OUT, ERROR, min, max...) collide with HIRAKU's names.
#include <fcntl.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

#ifndef O_BINARY
#define O_BINARY 0   // Windows' open() translates line ends without it
#endif

namespace hk::sys {

size_t page_size();
void *alloc_pages(size_t n);   // page-aligned, read-write; nullptr if there's no memory
void free_pages(void *p);
bool protect_pages(void *p, size_t n, bool writable);

// A write to a page protect_pages made read-only calls handler(the address written). It returns
// true when it has made the write possible (the write then runs again), false to let the fault
// go on as if nobody had caught it.
void catch_write_faults(bool (*handler)(void *address));

const void *map_file(int fd, uint64_t size);   // read-only; nullptr if it can't be mapped
void unmap_file(const void *p, uint64_t size);
ssize_t pread(int fd, void *buf, size_t n, uint64_t off);
void will_need(int fd, uint64_t off, uint64_t n);   // a hint: read that ahead

bool make_dir(const std::string &path);   // true if it is there now
bool is_dir(const std::string &path);
std::string exe_path();   // this program's file, with / between folders; empty if unknown
double thread_cpu_ms();   // the calling thread's CPU time
long major_faults();      // the process's page faults that read the disk (0 where unknown)
std::tm utc(std::time_t t);
std::tm local(std::time_t t);
std::time_t from_utc(const std::tm &t);   // utc() backwards (timegm)

#ifdef _WIN32
// Win32 fibers, for the Firebeat core's guest threads (runtime/core/glue.cpp): Windows checks
// the stack a thread runs on against what the thread says its stack is, which a hand-made switch
// wouldn't update.
void *fiber_this_thread();                        // the calling thread as a fiber
void *fiber_new(size_t stack, void (*entry)());
void fiber_switch(void *to);
void fiber_delete(void *f);
#endif

}  // namespace hk::sys
