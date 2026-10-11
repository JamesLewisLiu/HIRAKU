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

# --- runtime core ------------------------------------------------------------
add_library(hiraku_rt STATIC
  runtime/core/state.cpp
  runtime/core/interp.cpp
  runtime/core/dispatch.cpp
  runtime/core/sched.cpp
  runtime/core/cpu403.cpp
  runtime/core/glue.cpp
  runtime/dev/k057714.cpp
  runtime/dev/ata.cpp
  runtime/dev/rtc65271.cpp
  runtime/dev/flash29f016a.cpp
  runtime/dev/fdc.cpp
  runtime/dev/rf5c400.cpp
  runtime/dev/ymz280b.cpp
  runtime/spu/spu.cpp
  runtime/host/script.cpp
  runtime/firebeat/board.cpp
  runtime/firebeat/spectrum.cpp)
target_include_directories(hiraku_rt PUBLIC runtime)
target_link_libraries(hiraku_rt PUBLIC hiraku_ppc musashi)

# --- host front end (optional: SDL3) ---------------------------------------------
find_package(SDL3 CONFIG QUIET)
if(SDL3_FOUND)
  add_library(hiraku_host STATIC runtime/host/window.cpp runtime/host/input.cpp)
  target_include_directories(hiraku_host PUBLIC runtime)
  target_link_libraries(hiraku_host PUBLIC SDL3::SDL3 hiraku_rt)
  target_compile_definitions(hiraku_host PUBLIC HK_WITH_SDL=1)
  message(STATUS "hiraku: SDL3 front end enabled")
endif()

# --- tests -------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- games -------------------------------------------------------------------
# A game is built when analyze/prep.py has produced build/<id>/recomp.txt
# (which needs the user's own dump: nothing game-derived is in the repo).
set(HK_RECOMP_FILES 32)
function(hiraku_game id entry)
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
  add_executable(hiraku-${id} runtime/main.cpp runtime/core/sampler.cpp)
  target_compile_definitions(hiraku-${id} PRIVATE
    HK_GAME_ID="${id}" HK_ENTRY=${entry}u HK_ROOT="${CMAKE_SOURCE_DIR}")
  target_link_libraries(hiraku-${id} PRIVATE hiraku_rt hiraku_${id}_code hiraku_rt)
  if(TARGET hiraku_host)
    target_link_libraries(hiraku-${id} PRIVATE hiraku_host)
  endif()
endfunction()

hiraku_game(bm3final 0x80003000)
hiraku_game(popn4 0x80003000)
hiraku_game(popn5 0x80003000)
hiraku_game(popn6 0x80002018)
hiraku_game(popn7 0x80002018)
hiraku_game(ppp1mp 0x80002018)
