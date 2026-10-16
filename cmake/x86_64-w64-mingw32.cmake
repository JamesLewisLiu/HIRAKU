# SPDX-License-Identifier: GPL-3.0-or-later
# Cross-building for 64-bit Windows with clang, on Arch's mingw-w64-gcc (its headers, CRT,
# libstdc++ and winpthreads), as cmake/aarch64-linux-gnu.cmake does for ARM (PLAN.md section 26):
#   tools/win-deps.sh                       # SDL3, zlib and zstd for Windows, in out/win-deps
#   cmake -B build/win64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/x86_64-w64-mingw32.cmake
# (an absolute path: cmake resolves a relative one against the build folder). The binaries are
# linked statically, all but SDL3.dll, so Wine runs them here: the build's own generators
# (hiraku-recomp) through the emulator named below.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(HK_TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_RC_COMPILER ${HK_TRIPLE}-windres)
set(CMAKE_C_COMPILER_TARGET ${HK_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${HK_TRIPLE})
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static -pthread")
set(CMAKE_CROSSCOMPILING_EMULATOR wine)
set(HK_WIN_DEPS ${CMAKE_CURRENT_LIST_DIR}/../out/win-deps)
set(CMAKE_FIND_ROOT_PATH /usr/${HK_TRIPLE} ${HK_WIN_DEPS})
# pkg-config only sees the Windows libraries (zstd); not the host's, whose headers are glibc's
set(ENV{PKG_CONFIG_LIBDIR} ${HK_WIN_DEPS}/lib/pkgconfig)
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
