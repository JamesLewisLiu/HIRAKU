// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Disc and disk images, read straight from MAME's CHDs (libchdr, third_party/libchdr).
//
// A CHD opens as a stdio stream that reads as the file `chdman extract*` would have written:
//   - a hard disk or DVD: its logical bytes (extracthd, extractdvd, extractraw);
//   - a CD: the .bin of `chdman extractcd` (each track's frames at its own data size, audio
//     little-endian, the 4-frame padding between tracks left out), and fopen_cue() gives the
//     cue sheet extractcd writes (its FILE names the CHD itself).
// So every reader of extracted images reads a CHD by opening it here instead of with fopen(),
// and sees the same bytes. A plain file opens as a plain FILE.
//
// Reads go through a cache of decompressed hunks (32 MiB), which a worker thread fills ahead of
// a run of reads (4 MiB past the last), with its own handle on the CHD; a stream may be read from
// any one thread at a time. pread() reads a plain file with pread(2) and a CHD from that cache.
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/types.h>

namespace hk::image {

bool is_chd(const std::string &path);

// "cd", "dvd", "hd" (a hard disk, or any other logical-bytes CHD), or "" for a plain file or a
// CHD that cannot be read
std::string chd_kind(const std::string &path);

// The image as a read-only stream ("rb"): a plain file, or a CHD as chdman would extract it.
// nullptr if it cannot be opened. Close with fclose().
FILE *fopen_image(const std::string &path);

// A CD's cue sheet: the .cue file itself, or for a CD CHD the sheet `chdman extractcd` writes
// (FILE "<the CHD's name>" BINARY, its tracks, INDEX 01 / PREGAP / POSTGAP), as a stream.
FILE *fopen_cue(const std::string &path);

// ... and as text (empty if path isn't a CD CHD)
std::string chd_cue_text(const std::string &path);

// n bytes at off: pread(2) for a plain file's stream, the CHD's cache for a CHD's.
ssize_t pread(FILE *f, void *buf, size_t n, uint64_t off);

// a CHD stream's size (as extracted); -1 for a plain file's (use its fd)
int64_t chd_size(FILE *f);

}  // namespace hk::image
