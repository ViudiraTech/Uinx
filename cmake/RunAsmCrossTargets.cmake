# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

foreach(TRIPLE IN ITEMS aarch64-unknown-linux-gnu riscv64-unknown-linux-gnu)
  string(REPLACE "-" "_" SAFE_TRIPLE "${TRIPLE}")
  set(OUT "${BINARY_DIR}/asm-${SAFE_TRIPLE}.o")
  execute_process(
    COMMAND "${UINXC}" "${SOURCE}" --target=${TRIPLE} --emit=obj -o "${OUT}"
    RESULT_VARIABLE RC OUTPUT_VARIABLE STDOUT ERROR_VARIABLE STDERR)
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "asm cross target ${TRIPLE} failed (${RC})\n${STDOUT}\n${STDERR}")
  endif()
  if(NOT EXISTS "${OUT}")
    message(FATAL_ERROR "asm cross target ${TRIPLE} produced no object")
  endif()
endforeach()
