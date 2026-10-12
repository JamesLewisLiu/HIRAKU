// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: MAME's logmacro.h interface (per-file VERBOSE masks, LOGMASKED/LOG),
// writing through device_t::logerror.
#pragma once

#define LOG_GENERAL (1U << 0)
#ifndef VERBOSE
#define VERBOSE 0
#endif
#ifndef LOG_OUTPUT_FUNC
#define LOG_OUTPUT_FUNC logerror
#endif
#define LOGMASKED(mask, ...) do { if (VERBOSE & (mask)) (LOG_OUTPUT_FUNC)(__VA_ARGS__); } while (false)
#define LOG(...) LOGMASKED(LOG_GENERAL, __VA_ARGS__)
