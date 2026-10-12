// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: the bits of MAME's util/endianness.h these sources use (a little-endian host).
#pragma once
#define BYTE_XOR_LE(a) (a)
#define BYTE4_XOR_LE(a) (a)
#define WORD_XOR_LE(a) (a)
#define BYTE_XOR_BE(a) ((a) ^ 1)
#define BYTE4_XOR_BE(a) ((a) ^ 3)
#define WORD_XOR_BE(a) ((a) ^ 2)
