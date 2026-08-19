# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

function(build_baremetal ARCH TRIPLE START LINKER EMULATION)
  set(KERNEL_O "${BINARY_DIR}/baremetal-${ARCH}-kernel.o")
  set(START_O "${BINARY_DIR}/baremetal-${ARCH}-start.o")
  set(ELF "${BINARY_DIR}/baremetal-${ARCH}.elf")
  execute_process(COMMAND "${UINXC}" "${SOURCE_DIR}/kernel.ux" --target=${TRIPLE} --emit=obj -o "${KERNEL_O}"
    RESULT_VARIABLE RC OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR)
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "Uinx bare-metal compile ${ARCH} failed\n${OUT}\n${ERR}")
  endif()
  execute_process(COMMAND "${CLANG}" --target=${TRIPLE} -c "${SOURCE_DIR}/${START}" -o "${START_O}"
    RESULT_VARIABLE RC OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR)
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "startup assembly ${ARCH} failed\n${OUT}\n${ERR}")
  endif()
  execute_process(COMMAND "${LD_LLD}" -m ${EMULATION} -T "${SOURCE_DIR}/${LINKER}" "${START_O}" "${KERNEL_O}" -o "${ELF}"
    RESULT_VARIABLE RC OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR)
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "bare-metal link ${ARCH} failed\n${OUT}\n${ERR}")
  endif()
  if(NOT EXISTS "${ELF}")
    message(FATAL_ERROR "bare-metal ${ARCH} ELF not produced")
  endif()
endfunction()

build_baremetal(x86_64 x86_64-unknown-none start_x86_64.S x86_64.ld elf_x86_64)
build_baremetal(aarch64 aarch64-unknown-none start_aarch64.S aarch64.ld aarch64linux)
build_baremetal(riscv64 riscv64-unknown-none-elf start_riscv64.S riscv64.ld elf64lriscv)
