#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A release's package, built from a checkout with nothing derived from a game in it (PLAN.md
# section 28, R-1): what .github/workflows/release.yml runs, and what can be run by hand (in an
# Ubuntu 22.04 container for Linux, so the package's glibc is old enough for most PCs).
#
#   tools/release.sh linux   OUT_DIR [DEPS]   (DEPS: tools/release-deps-linux.sh's folder)
#   tools/release.sh windows OUT_DIR          (with tools/win-deps.sh's out/win-deps)
#
# The build has HK_GAME_BINARIES off: one binary per core (hiraku-djmain, hiraku-sys573,
# hiraku-twinkle, told the game with --game), no Firebeat games, no recorded blocks or MEG
# programs, whatever build/ holds. OUT_DIR gets hiraku-linux-x86_64.tar.xz or
# hiraku-windows-x86_64.zip, both portable (their saves stay in their own user/ folder).
set -euo pipefail
target=${1:?linux or windows}
out=${2:?output folder}
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
jobs=${HK_JOBS:-$(nproc)}
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DHK_GAME_BINARIES=OFF -DHK_TESTS=OFF)

case $target in
linux)
  deps=$(cd "${3:-$root/out/rel-deps/linux}" && pwd)
  qt=$(echo "$deps"/qt/*/gcc_64)
  build=$root/build/release-linux
  # pkg-config sees only DEPS (SDL3, zstd, FFmpeg), not the build PC's own libraries; PC/SC is
  # loaded at run time (its headers only); the C++ runtime is linked in; a library is linked only
  # when used (FFmpeg's .pc names libatomic, which nothing needs); SDL3 is looked for in bin/lib
  # ($ORIGIN/lib). clang, as the main build (PLAN.md section 14).
  PKG_CONFIG_LIBDIR=$deps/lib/pkgconfig PKG_CONFIG_PATH= cmake -S "$root" -B "$build" "${common[@]}" \
    -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    -DCMAKE_PREFIX_PATH="$deps;$qt" -DHK_PCSC_DLOPEN=ON \
    '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc -Wl,--as-needed' '-DCMAKE_BUILD_RPATH=$ORIGIN/lib'
  cmake --build "$build" -j "$jobs"
  HK_QT_DIR=$qt HK_PACKAGE_BIN="$deps/bin/7zz" HK_PYTHON_DIR="$deps/python" \
    "$root/tools/package.sh" "$build" "$out/hiraku-linux-x86_64.tar.xz" portable
  ;;
windows)
  deps=$root/out/win-deps   # where cmake/x86_64-w64-mingw32.cmake looks (tools/win-deps.sh fills it)
  build=$root/build/release-windows
  cmake -S "$root" -B "$build" "${common[@]}" -DCMAKE_TOOLCHAIN_FILE="$root/cmake/x86_64-w64-mingw32.cmake"
  cmake --build "$build" -j "$jobs"
  HK_PACKAGE_BIN="$deps/bin/7zr.exe" "$root/tools/package-win.sh" "$build" "$out/hiraku-windows-x86_64.zip" portable
  ;;
*) echo "linux or windows" >&2; exit 2 ;;
esac
