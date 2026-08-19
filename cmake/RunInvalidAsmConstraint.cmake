# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

execute_process(
  COMMAND "${UINXC}" "${SOURCE}" --target=aarch64-unknown-linux-gnu --emit=obj -o "${OUTPUT}"
  RESULT_VARIABLE RC OUTPUT_VARIABLE STDOUT ERROR_VARIABLE STDERR)
if(RC EQUAL 0)
  message(FATAL_ERROR "invalid AArch64 register constraint unexpectedly compiled")
endif()
set(ALL_OUTPUT "${STDOUT}\n${STDERR}")
if(NOT ALL_OUTPUT MATCHES "E0600")
  message(FATAL_ERROR "expected E0600 for invalid target register\n${ALL_OUTPUT}")
endif()
