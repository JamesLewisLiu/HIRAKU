# SPDX-License-Identifier: GPL-3.0-or-later
# Cross-building for 64-bit ARM Linux with clang, on Arch's aarch64-linux-gnu-gcc (its glibc
# and libstdc++):
#   cmake -B build/arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
# The binaries are linked statically, so qemu-aarch64 runs them here: the build's own
# generators (hiraku-recomp) through the emulator named below, and tools/regress.py with
# HK_REGRESS_BIN=build/arm64 through binfmt.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(HK_TRIPLE aarch64-linux-gnu)
set(CMAKE_SYSROOT /usr/${HK_TRIPLE})
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET ${HK_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${HK_TRIPLE})
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static")
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64)
# pkg-config only sees the target's libraries (none: FluidSynth, SDL3 and Qt are left out)
set(ENV{PKG_CONFIG_LIBDIR} ${CMAKE_SYSROOT}/lib/pkgconfig)
set(ENV{PKG_CONFIG_PATH} "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
