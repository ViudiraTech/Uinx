# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

# Uinx Language — by JiTianYu391
foreach(TRIPLE x86_64-unknown-linux-gnu aarch64-unknown-linux-gnu riscv64-unknown-linux-gnu)
  string(REPLACE "-" "_" STEM "${TRIPLE}")
  set(OUT "${BINARY_DIR}/nostd-${STEM}.o")
  execute_process(COMMAND "${UINXC}" "${SOURCE}" --emit=obj --target=${TRIPLE} -o "${OUT}"
                  RESULT_VARIABLE R OUTPUT_VARIABLE O ERROR_VARIABLE E)
  if(NOT R EQUAL 0)
    message(FATAL_ERROR "cross-target ${TRIPLE} failed:\n${O}\n${E}")
  endif()
  if(NOT EXISTS "${OUT}")
    message(FATAL_ERROR "cross-target ${TRIPLE} did not create ${OUT}")
  endif()
endforeach()
