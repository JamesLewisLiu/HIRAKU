# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(hiraku LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -fno-strict-aliasing)
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- decoder (shared) --------------------------------------------------------
add_library(hiraku_ppc STATIC src/ppc/decode.cpp)
target_include_directories(hiraku_ppc PUBLIC src)

# --- the recompiler ----------------------------------------------------------
add_executable(hiraku-recomp recomp/main.cpp)
target_link_libraries(hiraku-recomp PRIVATE hiraku_ppc)

# --- Musashi 68000 core (MIT licence), configured as a plain 68000 ---------------
add_library(musashi STATIC
  third_party/musashi/m68kcpu.c
  third_party/musashi/m68kops.c
  third_party/musashi/m68kdasm.c
  third_party/musashi/softfloat/softfloat.c)
target_include_directories(musashi PUBLIC third_party/musashi runtime/spu)
target_compile_definitions(musashi PUBLIC MUSASHI_CNF="hk_m68kconf.h")
target_compile_options(musashi PRIVATE -w)

# ... and again as a 68EC020 with interrupt acknowledge (the DJ Main main CPU)
add_library(musashi_020 STATIC
  third_party/musashi/m68kcpu.c
  third_party/musashi/m68kops.c
  third_party/musashi/m68kdasm.c
  third_party/musashi/softfloat/softfloat.c)
target_include_directories(musashi_020 PUBLIC third_party/musashi runtime/djmain)
target_compile_definitions(musashi_020 PUBLIC MUSASHI_CNF="hk_m68kconf_dj.h")
target_compile_options(musashi_020 PRIVATE -w)

# --- Yamaha XT446 synth card (Keyboardmania): MAME's H8S/2655 and SWP30 (BSD-3) ----
# on a small framework shim (third_party/mame/README.md). h8make.py generates the
# H8 opcode dispatch (and disassembler tables) from h8.lst at build time.
set(HK_MAME ${CMAKE_SOURCE_DIR}/third_party/mame)
set(HK_MAME_GEN ${CMAKE_BINARY_DIR}/mame-gen)
set(HK_H8_GEN)
foreach(pair "o;h8" "h;h8h" "s20;h8s2000" "s26;h8s2600")
  list(GET pair 0 t)
  list(GET pair 1 n)
  foreach(mode s d)
    if(mode STREQUAL "s")
      set(out ${HK_MAME_GEN}/cpu/h8/${n}.hxx)
    else()
      set(out ${HK_MAME_GEN}/cpu/h8/${n}d.hxx)
    endif()
    add_custom_command(
      OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${HK_MAME_GEN}/cpu/h8
      COMMAND python3 ${HK_MAME}/src/devices/cpu/h8/h8make.py ${HK_MAME}/src/devices/cpu/h8/h8.lst ${mode} ${t} ${out}
      DEPENDS ${HK_MAME}/src/devices/cpu/h8/h8make.py ${HK_MAME}/src/devices/cpu/h8/h8.lst
      COMMENT "h8make.py ${mode} ${t}")
    list(APPEND HK_H8_GEN ${out})
  endforeach()
endforeach()
set(HK_H8_SRC h8 h8h h8s2000 h8s2600 h8s2655 h8d h8hd h8s2000d h8s2600d
  h8_adc h8_dma h8_dtc h8_intc h8_port h8_sci h8_timer16 h8_timer8 h8_watchdog)
list(TRANSFORM HK_H8_SRC PREPEND ${HK_MAME}/src/devices/cpu/h8/)
list(TRANSFORM HK_H8_SRC APPEND .cpp)
add_library(hiraku_xt446 STATIC
  ${HK_MAME}/shim/emu.cpp
  ${HK_H8_SRC}
  ${HK_MAME}/src/devices/sound/swp30.cpp
  runtime/dev/xt446.cpp
  ${HK_H8_GEN})
target_include_directories(hiraku_xt446 PRIVATE ${HK_MAME}/shim ${HK_MAME}/src/devices ${HK_MAME_GEN})
target_include_directories(hiraku_xt446 PUBLIC runtime)
# MAME sources: their own warning set; optimised even in debug builds (the card is real-time)
target_compile_options(hiraku_xt446 PRIVATE -w -O3)

add_executable(hiraku-xt446 tools/xt446/xt446_render.cpp runtime/core/sampler.cpp)
target_link_libraries(hiraku-xt446 PRIVATE hiraku_xt446)

# --- cabinet tables, lights file formats (shared with the launcher) -------------
add_library(hiraku_cabinet STATIC runtime/shell/cabinet.cpp runtime/host/hiddesc.cpp)
target_include_directories(hiraku_cabinet PUBLIC runtime)

# --- the shell: what every machine core shares (runtime/shell/core.h) --------------
# Input scripts; the host front end below. Cores provide hk::core().
add_library(hiraku_shell STATIC runtime/host/script.cpp)
target_include_directories(hiraku_shell PUBLIC runtime)
target_link_libraries(hiraku_shell PUBLIC hiraku_cabinet)

# ATA/ATAPI bus and disc/disk images (both cores)
add_library(hiraku_ata STATIC runtime/dev/ata.cpp)
target_include_directories(hiraku_ata PUBLIC runtime)

# --- the Firebeat core: PPC403 runtime and the Firebeat board ------------------------
add_library(hiraku_rt STATIC
  runtime/core/state.cpp
  runtime/core/interp.cpp
  runtime/core/dispatch.cpp
  runtime/core/sched.cpp
  runtime/core/cpu403.cpp
  runtime/core/glue.cpp
  runtime/dev/k057714.cpp
  runtime/dev/rtc65271.cpp
  runtime/dev/flash29f016a.cpp
  runtime/dev/fdc.cpp
  runtime/dev/rf5c400.cpp
  runtime/dev/ymz280b.cpp
  runtime/spu/spu.cpp
  runtime/firebeat/board.cpp
  runtime/firebeat/spectrum.cpp
  runtime/firebeat/synth.cpp)
target_include_directories(hiraku_rt PUBLIC runtime)
target_link_libraries(hiraku_rt PUBLIC hiraku_ppc musashi hiraku_ata hiraku_cabinet hiraku_xt446)

# --- the DJ Main core: an interpreted 68EC020 and the DJ Main board -----------------
add_library(hiraku_djmain STATIC
  runtime/djmain/board.cpp
  runtime/djmain/k056832.cpp
  runtime/djmain/k054539.cpp
  runtime/djmain/core.cpp)
target_include_directories(hiraku_djmain PUBLIC runtime)
target_link_libraries(hiraku_djmain PUBLIC musashi_020 hiraku_ata hiraku_cabinet)
target_compile_options(hiraku_djmain PRIVATE -O2)

# Keyboardmania's synth stand-in (optional: FluidSynth, LGPL, linked dynamically)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FLUIDSYNTH QUIET IMPORTED_TARGET fluidsynth)
endif()
if(FLUIDSYNTH_FOUND)
  target_link_libraries(hiraku_rt PUBLIC PkgConfig::FLUIDSYNTH)
  target_compile_definitions(hiraku_rt PRIVATE HK_WITH_FLUIDSYNTH=1)
  message(STATUS "hiraku: FluidSynth ${FLUIDSYNTH_VERSION}: Keyboardmania synth stand-in enabled")
endif()

# --- host front end (optional: SDL3) ---------------------------------------------
find_package(SDL3 CONFIG QUIET)
if(SDL3_FOUND)
  # HID outputs (controller LEDs), shared with the launcher
  add_library(hiraku_hidout STATIC runtime/host/hidout.cpp)
  target_link_libraries(hiraku_hidout PUBLIC SDL3::SDL3 hiraku_cabinet)
  add_library(hiraku_host STATIC runtime/host/window.cpp runtime/host/picture.cpp runtime/host/input.cpp runtime/host/lights.cpp)
  target_include_directories(hiraku_host PUBLIC runtime)
  target_link_libraries(hiraku_host PUBLIC SDL3::SDL3 hiraku_shell hiraku_hidout)
  target_compile_definitions(hiraku_host PUBLIC HK_WITH_SDL=1)
  message(STATUS "hiraku: SDL3 front end enabled")
endif()

# --- the launcher (optional: Qt 6 Widgets + SDL3) ------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(Qt6_FOUND AND TARGET hiraku_hidout)
  set(CMAKE_AUTOMOC ON)
  add_executable(hiraku
    launcher/main.cpp
    launcher/config.cpp
    launcher/games.cpp
    launcher/inputs.cpp
    launcher/mainwindow.cpp
    launcher/controlsdialog.cpp
    launcher/lightsdialog.cpp
    launcher/settingsdialog.cpp
    launcher/picturedialog.cpp)
  target_compile_definitions(hiraku PRIVATE HK_ROOT="${CMAKE_SOURCE_DIR}")
  target_link_libraries(hiraku PRIVATE Qt6::Widgets Qt6::Network hiraku_cabinet hiraku_hidout)
  set(CMAKE_AUTOMOC OFF)
  message(STATUS "hiraku: Qt ${Qt6_VERSION} launcher enabled")
endif()

# --- tests -------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- games -------------------------------------------------------------------
# A game is built when analyze/prep.py has produced build/<id>/recomp.txt
# (which needs the user's own dump: nothing game-derived is in the repo).
set(HK_RECOMP_FILES 32)
# hiraku_game(<id> <entry> [<load address, default 0x80000000>])
function(hiraku_game id entry)
  set(load 0x80000000)
  if(ARGC GREATER 2)
    set(load ${ARGV2})
  endif()
  set(spec ${CMAKE_SOURCE_DIR}/build/${id}/recomp.txt)
  if(NOT EXISTS ${spec})
    message(STATUS "hiraku: ${id}: no ${spec}, skipped (run tools/ingest.py, analyze/ghidra.sh, analyze/prep.py)")
    return()
  endif()
  set(gen ${CMAKE_BINARY_DIR}/gen/${id})
  set(outs ${gen}/funcs.h ${gen}/table.c)
  math(EXPR last "${HK_RECOMP_FILES} - 1")
  foreach(k RANGE ${last})
    string(LENGTH "${k}" len)
    if(len EQUAL 1)
      set(k "00${k}")
    elseif(len EQUAL 2)
      set(k "0${k}")
    endif()
    list(APPEND outs ${gen}/code_${k}.c)
  endforeach()
  add_custom_command(
    OUTPUT ${outs}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${gen}
    COMMAND hiraku-recomp ${spec} ${gen} --files ${HK_RECOMP_FILES}
    DEPENDS hiraku-recomp ${spec}
    COMMENT "Recompiling ${id}")
  add_library(hiraku_${id}_code STATIC ${outs})
  target_include_directories(hiraku_${id}_code PRIVATE ${gen})
  # generated code: unused labels and parameters are normal
  target_compile_options(hiraku_${id}_code PRIVATE -w -O2 -fwrapv)
  add_executable(hiraku-${id} runtime/main.cpp runtime/firebeat/core.cpp runtime/core/sampler.cpp)
  target_compile_definitions(hiraku-${id} PRIVATE
    HK_GAME_ID="${id}" HK_ENTRY=${entry}u HK_LOAD=${load}u HK_ROOT="${CMAKE_SOURCE_DIR}")
  if(TARGET hiraku_host)
    target_link_libraries(hiraku-${id} PRIVATE hiraku_host)
  endif()
  target_link_libraries(hiraku-${id} PRIVATE hiraku_shell hiraku_rt hiraku_${id}_code hiraku_rt)
endfunction()

# --- the System 573 core (PLAN.md section 19): MAME's PS1 and System 573 devices
# (BSD-3) on the framework shim, run by MAME's own scheduler (exact mode) --------
set(HK_573_MAME
  shim/emu.cpp shim/screen.cpp shim/machine/ram.cpp shim/imagedev/cdromimg.cpp shim/stubs/ata_stubs.cpp
  src/emu/dirtc.cpp
  src/devices/cpu/psx/psx.cpp src/devices/cpu/psx/gte.cpp src/devices/cpu/psx/dma.cpp src/devices/cpu/psx/irq.cpp
  src/devices/cpu/psx/rcnt.cpp src/devices/cpu/psx/sio.cpp src/devices/cpu/psx/mdec.cpp src/devices/cpu/psx/psxdasm.cpp
  src/devices/video/psx.cpp
  src/devices/sound/spu.cpp src/devices/sound/spureverb.cpp src/devices/sound/spu_tables.cpp src/devices/sound/cdda.cpp
  src/devices/sound/mas3507d.cpp src/devices/sound/mp3_audio.cpp
  src/devices/machine/intelfsh.cpp src/devices/machine/timekpr.cpp src/devices/machine/adc083x.cpp
  src/devices/machine/ds2401.cpp src/devices/machine/x76f041.cpp src/devices/machine/x76f100.cpp
  src/devices/machine/atahle.cpp src/devices/machine/t10mmc.cpp src/devices/machine/t10spc.cpp
  src/devices/bus/ata/ataintf.cpp src/devices/bus/ata/atadev.cpp src/devices/bus/ata/atapihle.cpp
  src/devices/bus/ata/atapicdr.cpp src/devices/bus/ata/cr589.cpp
  src/devices/bus/pccard/pccard.cpp src/devices/bus/pccard/linflash.cpp
  src/devices/machine/jvshost.cpp src/devices/machine/jvsdev.cpp src/devices/bus/psx/ctlrport.cpp src/devices/bus/psx/memcard.cpp
  src/mame/konami/k573cass.cpp src/mame/konami/zs01.cpp src/mame/konami/k573dio.cpp src/mame/konami/k573fpga.cpp src/mame/konami/k573mcr.cpp)
list(TRANSFORM HK_573_MAME PREPEND ${HK_MAME}/)
set(HK_573_INC ${HK_MAME}/shim ${HK_MAME}/shim/stubs ${HK_MAME}/src/devices ${HK_MAME}/src/mame/konami
  ${HK_MAME}/src/emu ${HK_MAME}/src/3rdparty)
add_library(hiraku_mame573 STATIC ${HK_573_MAME})
target_include_directories(hiraku_mame573 PUBLIC ${HK_573_INC})
target_compile_options(hiraku_mame573 PRIVATE -w -O2)

add_library(hiraku_sys573 STATIC runtime/sys573/board.cpp runtime/sys573/core.cpp)
target_include_directories(hiraku_sys573 PUBLIC runtime)
target_link_libraries(hiraku_sys573 PUBLIC hiraku_mame573 hiraku_cabinet)
target_compile_options(hiraku_sys573 PRIVATE -O2 -Wno-unused-parameter)

function(hiraku_sys573_game id)
  add_executable(hiraku-${id} runtime/main.cpp runtime/core/sampler.cpp)
  target_compile_definitions(hiraku-${id} PRIVATE HK_GAME_ID="${id}" HK_ROOT="${CMAKE_SOURCE_DIR}")
  if(TARGET hiraku_host)
    target_link_libraries(hiraku-${id} PRIVATE hiraku_host)
  endif()
  target_link_libraries(hiraku-${id} PRIVATE hiraku_shell hiraku_sys573)
endfunction()

# hiraku_djmain_game(<id>): a DJ Main game. No generated code: the program is
# interpreted, so the binary is the same for every game (the data comes from
# dumps/<id>/ at run time, written by tools/ingest.py).
function(hiraku_djmain_game id)
  add_executable(hiraku-${id} runtime/main.cpp runtime/core/sampler.cpp)
  target_compile_definitions(hiraku-${id} PRIVATE HK_GAME_ID="${id}" HK_ROOT="${CMAKE_SOURCE_DIR}")
  if(TARGET hiraku_host)
    target_link_libraries(hiraku-${id} PRIVATE hiraku_host)
  endif()
  target_link_libraries(hiraku-${id} PRIVATE hiraku_shell hiraku_djmain)
endfunction()

# every games/<id>/game.toml with core = "djmain" (MAME's DJ Main sets)
file(GLOB HK_GAME_TOMLS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/games/*/game.toml)
foreach(toml ${HK_GAME_TOMLS})
  file(STRINGS ${toml} djmain_core REGEX "^core *= *\"djmain\"")
  if(djmain_core)
    get_filename_component(dir ${toml} DIRECTORY)
    get_filename_component(id ${dir} NAME)
    hiraku_djmain_game(${id})
  endif()
  file(STRINGS ${toml} sys573_core REGEX "^core *= *\"sys573\"")
  if(sys573_core)
    get_filename_component(dir ${toml} DIRECTORY)
    get_filename_component(id ${dir} NAME)
    hiraku_sys573_game(${id})
  endif()
endforeach()

hiraku_game(bm3final 0x80003000)
hiraku_game(popn4 0x80003000)
hiraku_game(popn5 0x80003000)
hiraku_game(popn6 0x80002018)
hiraku_game(popn7 0x80002018)
hiraku_game(ppp1mp 0x80002018)
hiraku_game(ppp 0x80002018)
hiraku_game(ppp11 0x80002018)
hiraku_game(ppd 0x80002018)
hiraku_game(popnanm 0x80003000)
hiraku_game(popnanm2 0x80003000)
hiraku_game(popn8 0x80002018)
hiraku_game(kbm 0x80083000 0x80080000)
hiraku_game(kbh 0x80083000 0x80080000)
hiraku_game(kbm2nd 0x80002018)
hiraku_game(kbm3rd 0x80002018)
