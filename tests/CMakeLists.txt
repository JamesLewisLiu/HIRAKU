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
add_executable(test_readers acio/test_readers.cpp)
target_link_libraries(test_readers PRIVATE hiraku_shell)
add_test(NAME card_readers COMMAND test_readers)
# beatmania IIDX's BIO2 I/O board against a pretend BI2A and a pretend machine
add_executable(test_bio2 acio/test_bio2.cpp)
target_link_libraries(test_bio2 PRIVATE hiraku_shell)
add_test(NAME bio2_board COMMAND test_bio2)

# ATA security (SECURITY UNLOCK) on a hard disk, as MAME does it (DJ Main complete MIX on)
add_executable(test_ata_security ata/test_ata_security.cpp)
target_link_libraries(test_ata_security PRIVATE hiraku_ata)
target_include_directories(test_ata_security PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
add_test(NAME ata_security COMMAND test_ata_security)
