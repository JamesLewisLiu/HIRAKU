// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's util/multibyte.h (big- and little-endian loads and stores).
#pragma once
#include "emu.h"
constexpr u16 get_u16be(const u8 *b) { return u16((b[0] << 8) | b[1]); }
constexpr u32 get_u24be(const u8 *b) { return (u32(b[0]) << 16) | (u32(b[1]) << 8) | b[2]; }
constexpr u32 get_u32be(const u8 *b) { return (u32(b[0]) << 24) | (u32(b[1]) << 16) | (u32(b[2]) << 8) | b[3]; }
constexpr s16 get_s16be(const u8 *b) { return s16(get_u16be(b)); }
constexpr s32 get_s32be(const u8 *b) { return s32(get_u32be(b)); }
constexpr u16 get_u16le(const u8 *b) { return u16(b[0] | (b[1] << 8)); }
constexpr u32 get_u24le(const u8 *b) { return u32(b[0]) | (u32(b[1]) << 8) | (u32(b[2]) << 16); }
constexpr u32 get_u32le(const u8 *b) { return u32(b[0]) | (u32(b[1]) << 8) | (u32(b[2]) << 16) | (u32(b[3]) << 24); }
inline void put_u16be(u8 *b, u16 v) { b[0] = u8(v >> 8); b[1] = u8(v); }
inline void put_u24be(u8 *b, u32 v) { b[0] = u8(v >> 16); b[1] = u8(v >> 8); b[2] = u8(v); }
inline void put_u32be(u8 *b, u32 v) { b[0] = u8(v >> 24); b[1] = u8(v >> 16); b[2] = u8(v >> 8); b[3] = u8(v); }
inline void put_u16le(u8 *b, u16 v) { b[0] = u8(v); b[1] = u8(v >> 8); }
inline void put_u24le(u8 *b, u32 v) { b[0] = u8(v); b[1] = u8(v >> 8); b[2] = u8(v >> 16); }
inline void put_u32le(u8 *b, u32 v) { b[0] = u8(v); b[1] = u8(v >> 8); b[2] = u8(v >> 16); b[3] = u8(v >> 24); }
