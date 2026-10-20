# SPDX-License-Identifier: GPL-3.0-or-later

# Instruction semantics vs QEMU. The reference cases are generated, not
# committed: tests/ppc/gen.py needs clang, ld.lld and qemu-ppc.
add_executable(test_ppc ppc/test_ppc.cpp)
target_link_libraries(test_ppc PRIVATE hiraku_rt)

set(HK_PPC_CASES ${CMAKE_BINARY_DIR}/tests/ppc-cases)
add_custom_command(
  OUTPUT ${HK_PPC_CASES}/cases.bin ${HK_PPC_CASES}/names.txt
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/ppc/gen.py ${HK_PPC_CASES}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ppc/gen.py
  COMMENT "Generating PowerPC reference cases with qemu-ppc")
add_custom_target(ppc_cases DEPENDS ${HK_PPC_CASES}/cases.bin)
add_dependencies(test_ppc ppc_cases)
add_test(NAME ppc_semantics COMMAND test_ppc ${HK_PPC_CASES})

# GCU renderer vs MAME: replay a MAME trace's GCU writes (oracle, not a ctest).
add_executable(gcu_replay gcu/gcu_replay.cpp)
target_link_libraries(gcu_replay PRIVATE hiraku_rt)

# HID output descriptors and reports (controller lights)
add_executable(test_hiddesc hid/test_hiddesc.cpp)
target_link_libraries(test_hiddesc PRIVATE hiraku_cabinet)
add_test(NAME hid_outputs COMMAND test_hiddesc)

# Konami ACIO devices on the PC (runtime/host/acio.h). Real card readers: the serial reader's dialogue against a pretend reader on a pseudo-terminal
# (Linux only: Windows has no pseudo-terminals to pretend with)
if(NOT WIN32)
  add_executable(test_readers acio/test_readers.cpp)
  target_link_libraries(test_readers PRIVATE hiraku_shell)
  add_test(NAME card_readers COMMAND test_readers)
  # beatmania IIDX's BIO2 I/O board against a pretend BI2A and a pretend machine
  add_executable(test_bio2 acio/test_bio2.cpp)
  target_link_libraries(test_bio2 PRIVATE hiraku_shell)
  add_test(NAME bio2_board COMMAND test_bio2)
endif()

# ATA security (SECURITY UNLOCK) on a hard disk, as MAME does it (DJ Main complete MIX on)
add_executable(test_ata_security ata/test_ata_security.cpp)
target_link_libraries(test_ata_security PRIVATE hiraku_ata)
target_include_directories(test_ata_security PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
add_test(NAME ata_security COMMAND test_ata_security)

# The picture's shader engine: FrameCount, frame history, feedback (needs a display and OpenGL
# 3.3; skipped without them)
if(TARGET hiraku_host)
  add_executable(test_picture picture/test_picture.cpp)
  target_link_libraries(test_picture PRIVATE hiraku_host)
  add_test(NAME picture_engine COMMAND test_picture)
  set_tests_properties(picture_engine PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Smooth motion's in-between pictures: the scroll guess (include/hiraku/scroll_guess.h)
add_executable(test_scroll_guess smooth/test_scroll_guess.cpp)
target_include_directories(test_scroll_guess PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME scroll_guess COMMAND test_scroll_guess)
