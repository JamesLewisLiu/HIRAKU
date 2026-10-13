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
# The same results on every host (the regress dumps are bit-exact): no fused multiply-adds,
# which compilers otherwise make wherever the host has them (every AArch64 CPU, x86-64 with
# -march), and a signed plain char, as on x86-64 (it is unsigned on AArch64).
add_compile_options(-ffp-contract=off -fsigned-char)
include_directories(${CMAKE_SOURCE_DIR}/include)

# Profile-guided optimisation (tools/pgo.py): HIRAKU's machines are interpreters, schedulers and
# renderers whose hot paths clang lays out better when it has counts from real runs. pgo.py
# builds instrumented (HK_PGO_GENERATE), runs the regress cases it has dumps for, and writes one
# profile per core to HK_PGO_DIR. Per core, because the cores compile the same sources into
# separate copies (Musashi as a 68000 and as a 68EC020, MAME's shim for the XT446 and for the
# 573): one profile would blend their hot paths. Measured (PLAN.md section 14), and so applied:
# - firebeat: the runtime, the SPU's 68000, the XT446 (kbm -18 %, popn7 -3 %, bm3final -1 %); not
#   the recompiled game code (no gain, and only the trained games would have a profile);
# - sys573: MAME's code (-28 %);
# - DJ Main: none (5 % slower with its profile).
# The profiles stay out of the repo: they come from running the games.
set(HK_PGO_DIR "" CACHE PATH "directory of per-core llvm-profdata profiles (tools/pgo.py)")
option(HK_PGO_GENERATE "build instrumented, to train the profiles (tools/pgo.py)" OFF)
set(HK_PGO_OK OFF)
if((HK_PGO_GENERATE OR HK_PGO_DIR) AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(WARNING "PGO needs clang (tools/pgo.py configures it): building without")
elseif(HK_PGO_GENERATE)
  add_compile_options(-fprofile-instr-generate)
  add_link_options(-fprofile-instr-generate)
elseif(HK_PGO_DIR)
  set(HK_PGO_OK ON)
  message(STATUS "profile-guided: ${HK_PGO_DIR}")
endif()
# hk_pgo(core target...): those targets are optimised with the core's profile, when there is one
function(hk_pgo core)
  set(prof "${HK_PGO_DIR}/${core}.profdata")
  if(HK_PGO_OK AND EXISTS "${prof}")
    foreach(t ${ARGN})
      target_compile_options(${t} PRIVATE "-fprofile-instr-use=${prof}" -Wno-profile-instr-unprofiled
                             -Wno-profile-instr-out-of-date -Wno-profile-instr-missing)
    endforeach()
  endif()
endfunction()

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
add_library(hiraku_shell STATIC runtime/host/script.cpp runtime/host/macro.cpp)
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
hk_pgo(firebeat hiraku_rt musashi hiraku_xt446)   # not the game code: see hk_pgo above

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
  add_library(hiraku_host STATIC runtime/host/window.cpp runtime/host/picture.cpp runtime/host/input.cpp runtime/host/lights.cpp
              runtime/host/pacing.cpp)
  target_include_directories(hiraku_host PUBLIC runtime)
  target_link_libraries(hiraku_host PUBLIC SDL3::SDL3 hiraku_shell hiraku_hidout)
  target_compile_definitions(hiraku_host PUBLIC HK_WITH_SDL=1)
  # Wayland: the compositor's presentation times and fifo, for display pacing (runtime/host/pacing.h).
  # Found without pkg-config (the laptop package turns it off); libwayland-client is on any system
  # with Mesa's EGL.
  find_path(WAYLAND_CLIENT_INCLUDE wayland-client.h)
  find_library(WAYLAND_CLIENT_LIB wayland-client)
  if(WAYLAND_CLIENT_INCLUDE AND WAYLAND_CLIENT_LIB)
    target_sources(hiraku_host PRIVATE third_party/wayland/presentation-time-protocol.c
                                       third_party/wayland/fifo-v1-protocol.c)
    target_include_directories(hiraku_host PRIVATE third_party/wayland ${WAYLAND_CLIENT_INCLUDE})
    target_link_libraries(hiraku_host PRIVATE ${WAYLAND_CLIENT_LIB})
    target_compile_definitions(hiraku_host PRIVATE HK_WITH_WAYLAND=1)
    message(STATUS "hiraku: Wayland presentation feedback and fifo enabled")
  endif()
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
    launcher/picturedialog.cpp
    launcher/unlocksdialog.cpp)
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
list(APPEND HK_573_MAME
  # the multi-session unit's TX3927 CPU (mips1), and the serial parts between it and the 573
  src/devices/cpu/mips/mips1.cpp src/devices/cpu/mips/mips1dsm.cpp src/emu/diserial.cpp src/devices/machine/ins8250.cpp
  src/devices/sound/tc9446f.cpp src/mame/konami/k573msu.cpp src/devices/cpu/psx/sio_uart.cpp)
list(TRANSFORM HK_573_MAME PREPEND ${HK_MAME}/)
# softfloat3 (Berkeley SoftFloat 3, BSD-3): the MIPS CPUs' FPU in mips1; MAME builds it as C++
file(GLOB_RECURSE HK_SF3_SRC ${HK_MAME}/src/3rdparty/softfloat3/source/*.c)
set_source_files_properties(${HK_SF3_SRC} PROPERTIES LANGUAGE CXX)
add_library(hk_softfloat3 STATIC ${HK_SF3_SRC})
target_include_directories(hk_softfloat3 PRIVATE ${HK_MAME}/src/3rdparty/softfloat3/build/MAME
  ${HK_MAME}/src/3rdparty/softfloat3/source ${HK_MAME}/src/3rdparty/softfloat3/source/8086)
target_include_directories(hk_softfloat3 PUBLIC ${HK_MAME}/src/3rdparty/softfloat3/source/include)
target_compile_definitions(hk_softfloat3 PUBLIC SOFTFLOAT_ROUND_ODD INLINE_LEVEL=5 SOFTFLOAT_FAST_DIV32TO16
  SOFTFLOAT_FAST_DIV64TO32 SOFTFLOAT_FAST_INT64 LITTLEENDIAN=1)
target_compile_options(hk_softfloat3 PRIVATE -w -O2)
set(HK_573_INC ${HK_MAME}/shim ${HK_MAME}/shim/stubs ${HK_MAME}/src/devices ${HK_MAME}/src/mame/konami
  ${HK_MAME}/src/emu ${HK_MAME}/src/3rdparty)
# The System 573's MAME code (CPU interpreter, GPU, SPU): mostly MAME's interpreter, which
# PGO (above) speeds up by ~30 % (PLAN.md section 19, 573-4). Safe with -march: contraction
# into fused multiply-adds is off for everything (above).
set(HK_573_OPT "-O3" CACHE STRING "compiler optimisation flags for the System 573's MAME code")
separate_arguments(HK_573_OPT_LIST UNIX_COMMAND "${HK_573_OPT}")
add_library(hiraku_mame573 STATIC ${HK_573_MAME})
target_include_directories(hiraku_mame573 PUBLIC ${HK_573_INC})
target_compile_options(hiraku_mame573 PRIVATE -w ${HK_573_OPT_LIST})
target_link_libraries(hiraku_mame573 PUBLIC hk_softfloat3)

add_library(hiraku_sys573 STATIC runtime/sys573/board.cpp runtime/sys573/core.cpp runtime/sys573/mips.cpp runtime/sys573/npu.cpp runtime/sys573/link.cpp)
target_include_directories(hiraku_sys573 PUBLIC runtime)
target_link_libraries(hiraku_sys573 PUBLIC hiraku_mame573 hiraku_cabinet)
target_compile_options(hiraku_sys573 PRIVATE -O2 -Wno-unused-parameter)
hk_pgo(sys573 hiraku_mame573)

# hiraku_sys573_game(<id>): a System 573 game. MAME's devices, and the game's recompiled R3000
# blocks when there is a recording of them, build/<id>/mips573.txt (runtime/sys573/mips.h,
# tools/mipsrec.py): tools/mipsgen.py turns it into C++. Without one, the CPU is all interpreted.
set(HK_573_BLOCK_FILES 8)
set(HK_573_MIN_RUNS 1 CACHE STRING "blocks run fewer times than this in the recording are left to the interpreter")
function(hiraku_sys573_game id)
  set(rec ${CMAKE_SOURCE_DIR}/build/${id}/mips573.txt)
  set(gen ${CMAKE_BINARY_DIR}/gen573/${id})
  set(outs ${gen}/table.cpp)
  math(EXPR last "${HK_573_BLOCK_FILES} - 1")
  foreach(k RANGE ${last})
    list(APPEND outs ${gen}/blocks_${k}.cpp)
  endforeach()
  # every build: mipsgen.py does nothing unless the recording (or the script) changed, and
  # writes empty tables while there is none, so a new recording needs no cmake run
  add_custom_target(hiraku_${id}_mipsgen
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/mipsgen.py ${rec} ${gen} --files ${HK_573_BLOCK_FILES} --min-runs ${HK_573_MIN_RUNS}
    BYPRODUCTS ${outs}
    COMMENT "Recompiling ${id}'s R3000 blocks, if its recording changed")
  add_library(hiraku_${id}_blocks STATIC ${outs})
  add_dependencies(hiraku_${id}_blocks hiraku_${id}_mipsgen)
  target_link_libraries(hiraku_${id}_blocks PRIVATE hiraku_mame573)
  target_include_directories(hiraku_${id}_blocks PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
  # generated code: unused variables and labels are normal; wrapping arithmetic is the guest's
  target_compile_options(hiraku_${id}_blocks PRIVATE -w -O2 -fwrapv)
  add_executable(hiraku-${id} runtime/main.cpp runtime/core/sampler.cpp)
  target_compile_definitions(hiraku-${id} PRIVATE HK_GAME_ID="${id}" HK_ROOT="${CMAKE_SOURCE_DIR}")
  if(TARGET hiraku_host)
    target_link_libraries(hiraku-${id} PRIVATE hiraku_host)
  endif()
  target_link_libraries(hiraku-${id} PRIVATE hiraku_shell hiraku_sys573 hiraku_${id}_blocks)
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
