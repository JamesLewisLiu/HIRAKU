#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds build/libchdr_mini.so (for tools/chdread.py) from libchdr and LZMA sources.
# They are vendored in PCSX2 Reliquary's tree (BSD-3 / public domain); point HK_RELIQUARY_SRC at it.
set -e
R=${HK_RELIQUARY_SRC:-$(dirname "$(readlink -f "$0")")/../../Python-work/pcsx2-reliquary}/3rdparty   # default: beside the checkout
OUT=$(dirname "$(readlink -f "$0")")/../build
mkdir -p "$OUT"
clang -O2 -shared -fPIC -w -o "$OUT/libchdr_mini.so" \
	-I"$R/libchdr/include" -I"$R/lzma/include" -D_7ZIP_ST -DZSTD_DISABLE_ASM \
	"$R"/libchdr/src/*.c "$R"/lzma/src/*.c -lz -lzstd
echo "$OUT/libchdr_mini.so"
