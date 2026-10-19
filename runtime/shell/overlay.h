// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Mods' media layer (docs/MODS.md): byte ranges of a disc or disk image replaced as it's read.
// Registered by the image's path before the core opens it; the image layer (runtime/dev/image.h)
// and the Python core's disk reader apply them to every read. Nothing registered: nothing done.
#include <cstdint>
#include <string>
#include <vector>

namespace hk::overlay {

// From now on, reads of the image at `path` see `data` at `offset` (later ranges win). label
// names it in the log, which says once per label when it's first read.
void add(const std::string &path, uint64_t offset, std::vector<uint8_t> data, const std::string &label = "");

// The ranges for this image: an id for apply(), 0 when it has none
int find(const std::string &path);

// Lay the ranges of id over n bytes read at off into buf
void apply(int id, uint64_t off, void *buf, size_t n);

// The size an image appears to have once ranges past its end are laid on: max(size, the end of
// its last range)
uint64_t extent(int id, uint64_t size);

// Reads n bytes at off of the image at path as it is, without the ranges (for a mod finding a
// file on a disc): through the reader the image layer registers (runtime/dev/image.cpp: CHDs
// too), else as a plain file. false if it can't.
using Reader = bool (*)(const std::string &path, uint64_t off, void *buf, size_t n);
void set_reader(Reader r);
bool read(const std::string &path, uint64_t off, void *buf, size_t n);

}  // namespace hk::overlay
