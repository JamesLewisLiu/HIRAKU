# SPDX-License-Identifier: GPL-3.0-or-later
# The Konami Python core (PLAN.md §23): PCSX2 Reliquary's emulator hosted by HIRAKU's shell.
#
# This file is not part of HIRAKU's own CMake build. PCSX2's CMake assumes it is the
# top-level project, so it is the other way round: Reliquary's build (with HIRAKU's
# patch, oracle/pcsx2/reliquary-oracle.patch) adds this directory when configured with
# -DHIRAKU_DIR=<this checkout>, as oracle/pcsx2/build-reliquary.sh does. The binaries
# land next to pcsx2-qt, in Reliquary's build/bin/, where its resources/ are.

set(HK ${HIRAKU_DIR})

# HIRAKU's shell: the same sources as HIRAKU's hiraku_shell, hiraku_cabinet and hiraku_host
add_library(hk_python_shell STATIC
	${HK}/runtime/shell/cabinet.cpp
	${HK}/runtime/shell/sys.cpp
	${HK}/runtime/shell/net.cpp
	${HK}/runtime/host/hiddesc.cpp
	${HK}/runtime/host/script.cpp
	${HK}/runtime/host/macro.cpp
	${HK}/runtime/host/acio.cpp
	${HK}/runtime/host/readers.cpp
	${HK}/runtime/host/bio2.cpp
	${HK}/runtime/host/hidout.cpp
	${HK}/runtime/host/window.cpp
	${HK}/runtime/host/picture.cpp
	${HK}/runtime/host/input.cpp
	${HK}/runtime/host/lights.cpp
	${HK}/runtime/host/pacing.cpp)
target_include_directories(hk_python_shell PUBLIC ${HK}/runtime ${HK}/include)
target_compile_definitions(hk_python_shell PUBLIC HK_WITH_SDL=1)
target_link_libraries(hk_python_shell PUBLIC SDL3::SDL3)
# NFC card readers through PC/SC (runtime/host/readers.h, --scard): optional, as in HIRAKU's build
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(HK_PCSC QUIET IMPORTED_TARGET libpcsclite)
endif()
if(HK_PCSC_FOUND)
	target_link_libraries(hk_python_shell PRIVATE PkgConfig::HK_PCSC)
	target_compile_definitions(hk_python_shell PRIVATE HK_WITH_PCSC=1)
endif()
find_path(HK_WAYLAND_CLIENT_INCLUDE wayland-client.h)
find_library(HK_WAYLAND_CLIENT_LIB wayland-client)
if(HK_WAYLAND_CLIENT_INCLUDE AND HK_WAYLAND_CLIENT_LIB)
	target_sources(hk_python_shell PRIVATE ${HK}/third_party/wayland/presentation-time-protocol.c
	                                       ${HK}/third_party/wayland/fifo-v1-protocol.c
	                                       ${HK}/third_party/wayland/content-type-v1-protocol.c)
	target_include_directories(hk_python_shell PRIVATE ${HK}/third_party/wayland ${HK_WAYLAND_CLIENT_INCLUDE})
	target_link_libraries(hk_python_shell PRIVATE ${HK_WAYLAND_CLIENT_LIB})
	target_compile_definitions(hk_python_shell PRIVATE HK_WITH_WAYLAND=1)
endif()
# HIRAKU's code, HIRAKU's warnings; PCSX2's global options still apply, except that
# HIRAKU's parsers use exceptions (PCSX2 builds with -fno-exceptions)
set(HK_PY_FLAGS -Wall -Wextra -fno-strict-aliasing -Wno-unused-parameter -fexceptions)
target_compile_options(hk_python_shell PRIVATE ${HK_PY_FLAGS})
set_target_properties(hk_python_shell PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# One binary per Python game, as for the other cores: hiraku-<id>
function(hiraku_python_game id)
	add_executable(hiraku-${id} ${HK}/runtime/main.cpp ${HK}/runtime/python/core.cpp ${HK}/runtime/python/cardunit.cpp ${HK}/runtime/python/icca.cpp ${HK}/runtime/python/eanet.cpp ${HK}/runtime/python/ethnet.cpp ${HK}/runtime/python/gfdmcard.cpp ${HK}/runtime/python/lancable.cpp ${HK}/runtime/python/netrelay.cpp ${HK}/runtime/python/lobby.cpp ${HK}/runtime/python/easerver.cpp ${HK}/runtime/python/eaclient.cpp)
	if(ARGV1 STREQUAL "CORE")   # hiraku-python: any Python game, told with --game (a release's)
		target_compile_definitions(hiraku-${id} PRIVATE HK_ROOT="${HK}")
	else()
		target_compile_definitions(hiraku-${id} PRIVATE HK_GAME_ID="${id}" HK_ROOT="${HK}")
	endif()
	target_include_directories(hiraku-${id} PRIVATE ${HK}/runtime "${CMAKE_BINARY_DIR}/common/include" "${CMAKE_SOURCE_DIR}/pcsx2")
	target_link_libraries(hiraku-${id} PRIVATE hk_python_shell PCSX2_FLAGS PCSX2)
	target_compile_options(hiraku-${id} PRIVATE ${HK_PY_FLAGS})
endfunction()

# hiraku-python: every Python game, as hiraku-djmain and the other cores' own binaries are
hiraku_python_game(python CORE)
hiraku_python_game(popn9)
hiraku_python_game(popn10)
hiraku_python_game(popn11)
hiraku_python_game(popn12)
hiraku_python_game(popn13)
hiraku_python_game(popn14)
# Konami Python 2 (PY-6)
hiraku_python_game(drmnvj)
hiraku_python_game(drmnv2j)
