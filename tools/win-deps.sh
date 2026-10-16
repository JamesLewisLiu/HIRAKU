#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The libraries a Windows build needs (cmake/x86_64-w64-mingw32.cmake, PLAN.md section 26), in
# out/win-deps: SDL3 from its official mingw development package, zlib and zstd cross-built from
# their sources with clang and mingw-w64.
#
#   tools/win-deps.sh SDL3_PACKAGE ZLIB_SOURCE ZSTD_SOURCE
#
# SDL3_PACKAGE: the unpacked SDL3-devel-<version>-mingw.tar.gz (the folder holding
# x86_64-w64-mingw32/), from https://github.com/libsdl-org/SDL/releases
# ZLIB_SOURCE: zlib's source folder (https://zlib.net); ZSTD_SOURCE: zstd's (its lib/ and build/cmake/)
set -euo pipefail
sdl=${1:?SDL3 mingw package folder}
zlib=${2:?zlib source folder}
zstd=${3:?zstd source folder}
root=$(cd "$(dirname "$0")/.." && pwd)
deps=$root/out/win-deps
work=$root/out/win-deps-build
tc=$root/cmake/x86_64-w64-mingw32.cmake
mkdir -p "$deps" "$work"

cp -r "$sdl/x86_64-w64-mingw32/." "$deps/"
# the package's cmake files and .pc point at the prefix it was built with; they find their
# folder by themselves (SDL3Config.cmake), the .pc by its own prefix line
sed -i "s|^prefix=.*|prefix=$deps|" "$deps/lib/pkgconfig/sdl3.pc"

cmake -S "$zlib" -B "$work/zlib" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$tc" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$deps" -DZLIB_BUILD_EXAMPLES=OFF
cmake --build "$work/zlib" && cmake --install "$work/zlib"
# static only: the games link statically (zlib's install also leaves a DLL import library, which
# find_package(ZLIB) would take first)
rm -f "$deps"/lib/libzlib.dll.a "$deps"/bin/libzlib*.dll
[ -f "$deps/lib/libzlibstatic.a" ] && cp "$deps/lib/libzlibstatic.a" "$deps/lib/libz.a"

cmake -S "$zstd/build/cmake" -B "$work/zstd" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$tc" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$deps" -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_PROGRAMS=OFF \
  -DZSTD_BUILD_TESTS=OFF -DZSTD_MULTITHREAD_SUPPORT=OFF
cmake --build "$work/zstd" && cmake --install "$work/zstd"
echo "win-deps: $deps"
